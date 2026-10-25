<?php
/**
 * Public REST endpoints for the player: fresh counters, likes, plays.
 *
 * They work without a login and without a nonce on purpose, so they also work on cached
 * pages. Abuse is limited by visitor id + IP checks and a rate limit.
 *
 * @package DubEcho
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

class Dub_Echo_Rest {

	const NS = 'dub-echo/v1';

	public static function register_routes() {
		$visitor = array(
			'type'              => 'string',
			'required'          => true,
			'validate_callback' => array( __CLASS__, 'valid_visitor' ),
		);
		$track   = array(
			'type'              => 'integer',
			'required'          => true,
			'sanitize_callback' => 'absint',
		);

		register_rest_route(
			self::NS,
			'/tracks',
			array(
				'methods'             => WP_REST_Server::READABLE,
				'callback'            => array( __CLASS__, 'tracks' ),
				'permission_callback' => '__return_true',
				'args'                => array(
					'ids'     => array(
						'type'     => 'string',
						'required' => false,
					),
					'visitor' => array_merge( $visitor, array( 'required' => false ) ),
				),
			)
		);
		register_rest_route(
			self::NS,
			'/like',
			array(
				'methods'             => WP_REST_Server::CREATABLE,
				'callback'            => array( __CLASS__, 'like' ),
				'permission_callback' => '__return_true',
				'args'                => array(
					'track'   => $track,
					'visitor' => $visitor,
					'like'    => array(
						'type'    => 'boolean',
						'default' => true,
					),
				),
			)
		);
		register_rest_route(
			self::NS,
			'/play',
			array(
				'methods'             => WP_REST_Server::CREATABLE,
				'callback'            => array( __CLASS__, 'play' ),
				'permission_callback' => '__return_true',
				'args'                => array(
					'track'   => $track,
					'visitor' => $visitor,
				),
			)
		);
	}

	public static function valid_visitor( $v ) {
		return is_string( $v ) && preg_match( '/^[a-f0-9]{32}$/', $v );
	}

	private static function public_track( $id ) {
		$p = Dub_Echo_Tracks::get( $id );
		return ( $p && 'publish' === $p->post_status ) ? $p : null;
	}

	/**
	 * Simple fixed window rate limit per IP.
	 */
	private static function rate_limited( $bucket, $max, $window ) {
		$key = 'dub_echo_rl_' . $bucket . '_' . substr( Dub_Echo_Likes::ip_hash(), 0, 20 );
		$n   = (int) get_transient( $key );
		if ( $n >= $max ) {
			return true;
		}
		set_transient( $key, $n + 1, $window );
		return false;
	}

	public static function tracks( WP_REST_Request $req ) {
		$ids = array_filter( array_map( 'absint', explode( ',', (string) $req->get_param( 'ids' ) ) ) );
		$out = array();
		foreach ( Dub_Echo_Tracks::query( array( 'ids' => array_slice( $ids, 0, 200 ) ) ) as $p ) {
			$row = array(
				'id'    => $p->ID,
				'likes' => Dub_Echo_Tracks::likes( $p->ID ),
			);
			if ( $req->get_param( 'visitor' ) ) {
				$row['liked'] = Dub_Echo_Likes::has_liked( $p->ID, Dub_Echo_Likes::visitor_hash( $req->get_param( 'visitor' ) ) );
			}
			$out[] = $row;
		}
		$res = rest_ensure_response( array( 'tracks' => $out ) );
		$res->header( 'Cache-Control', 'no-store' );
		return $res;
	}

	public static function like( WP_REST_Request $req ) {
		if ( ! Dub_Echo_Settings::get( 'likes' ) ) {
			return new WP_Error( 'dub_echo_disabled', __( 'Likes sind ausgeschaltet.', 'dub-echo' ), array( 'status' => 403 ) );
		}
		$p = self::public_track( $req->get_param( 'track' ) );
		if ( ! $p ) {
			return new WP_Error( 'dub_echo_no_track', __( 'Titel nicht gefunden.', 'dub-echo' ), array( 'status' => 404 ) );
		}
		if ( self::rate_limited( 'like', 60, 10 * MINUTE_IN_SECONDS ) ) {
			return new WP_Error( 'dub_echo_slow_down', __( 'Bitte etwas langsamer.', 'dub-echo' ), array( 'status' => 429 ) );
		}
		$visitor = Dub_Echo_Likes::visitor_hash( $req->get_param( 'visitor' ) );
		if ( $req->get_param( 'like' ) ) {
			$r = Dub_Echo_Likes::like( $p->ID, $visitor );
			if ( is_wp_error( $r ) ) {
				return $r;
			}
		} else {
			Dub_Echo_Likes::unlike( $p->ID, $visitor );
		}
		return rest_ensure_response(
			array(
				'likes' => Dub_Echo_Tracks::likes( $p->ID ),
				'liked' => Dub_Echo_Likes::has_liked( $p->ID, $visitor ),
			)
		);
	}

	public static function play( WP_REST_Request $req ) {
		if ( ! Dub_Echo_Settings::get( 'count_plays' ) ) {
			return rest_ensure_response( array( 'counted' => false ) );
		}
		$p = self::public_track( $req->get_param( 'track' ) );
		if ( ! $p ) {
			return new WP_Error( 'dub_echo_no_track', __( 'Titel nicht gefunden.', 'dub-echo' ), array( 'status' => 404 ) );
		}
		// one play per visitor and track every 30 minutes
		$key = 'dub_echo_play_' . substr( Dub_Echo_Likes::visitor_hash( $req->get_param( 'visitor' ) . '|' . $p->ID ), 0, 32 );
		if ( get_transient( $key ) || self::rate_limited( 'play', 120, HOUR_IN_SECONDS ) ) {
			return rest_ensure_response( array( 'counted' => false ) );
		}
		set_transient( $key, 1, 30 * MINUTE_IN_SECONDS );
		Dub_Echo_Tracks::add_play( $p->ID );
		return rest_ensure_response( array( 'counted' => true ) );
	}
}
