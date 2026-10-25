<?php
/**
 * Front end: shortcode [dub_echo], the block and the player assets.
 *
 * @package DubEcho
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

class Dub_Echo_Frontend {

	public static function init() {
		add_shortcode( 'dub_echo', array( __CLASS__, 'shortcode' ) );
		add_action( 'init', array( __CLASS__, 'register_block' ) );
		add_action( 'wp_enqueue_scripts', array( __CLASS__, 'register_assets' ) );
		add_action( 'enqueue_block_editor_assets', array( __CLASS__, 'block_editor_data' ) );
	}

	public static function block_editor_data() {
		$tracks = array();
		foreach ( Dub_Echo_Tracks::query() as $p ) {
			$tracks[] = array(
				'id'    => $p->ID,
				'title' => html_entity_decode( get_the_title( $p ), ENT_QUOTES, 'UTF-8' ),
			);
		}
		wp_localize_script(
			'dub-echo-block',
			'dubEchoBlock',
			array(
				'preview' => DUB_ECHO_URL . 'assets/img/background.jpg',
				'tracks'  => $tracks,
				'presets' => Dub_Echo_Settings::presets(),
			)
		);
	}

	public static function register_assets() {
		wp_register_style( 'dub-echo', DUB_ECHO_URL . 'assets/css/dub-echo.css', array(), DUB_ECHO_VERSION );
		wp_register_script( 'dub-echo-engine', DUB_ECHO_URL . 'assets/js/dub-echo-engine.js', array(), DUB_ECHO_VERSION, true );
		wp_register_script( 'dub-echo-player', DUB_ECHO_URL . 'assets/js/dub-echo-player.js', array( 'dub-echo-engine' ), DUB_ECHO_VERSION, true );
	}

	public static function register_block() {
		if ( ! function_exists( 'register_block_type' ) ) {
			return;
		}
		wp_register_script(
			'dub-echo-block',
			DUB_ECHO_URL . 'assets/js/block.js',
			array( 'wp-blocks', 'wp-element', 'wp-components', 'wp-block-editor', 'wp-i18n' ),
			DUB_ECHO_VERSION,
			true
		);
		register_block_type(
			'dub-echo/player',
			array(
				'editor_script'   => 'dub-echo-block',
				'render_callback' => array( __CLASS__, 'render_block' ),
				'attributes'      => array(
					'track'  => array(
						'type'    => 'string',
						'default' => '',
					),
					'preset' => array(
						'type'    => 'string',
						'default' => '',
					),
				),
			)
		);
	}

	public static function render_block( $attrs ) {
		return self::shortcode(
			array(
				'track'  => isset( $attrs['track'] ) ? $attrs['track'] : '',
				'preset' => isset( $attrs['preset'] ) ? $attrs['preset'] : '',
			)
		);
	}

	/**
	 * [dub_echo tracks="12,15" track="15" preset="Spiral Runaway" title="..."]
	 */
	public static function shortcode( $atts ) {
		$atts = shortcode_atts(
			array(
				'tracks' => '',
				'track'  => '',
				'preset' => '',
				'title'  => '',
			),
			$atts,
			'dub_echo'
		);
		$s    = Dub_Echo_Settings::get();
		$ids  = array_filter( array_map( 'absint', explode( ',', $atts['tracks'] ) ) );

		$tracks = array();
		foreach ( Dub_Echo_Tracks::query( array( 'ids' => $ids ) ) as $p ) {
			$t = Dub_Echo_Tracks::to_public_array( $p );
			if ( $t['url'] ) {
				$tracks[] = $t;
			}
		}
		if ( $s['shuffle'] ) {
			shuffle( $tracks );
		}

		$preset = in_array( $atts['preset'], Dub_Echo_Settings::presets(), true ) ? $atts['preset'] : $s['default_preset'];
		$config = array(
			'assetsUrl'  => DUB_ECHO_URL . 'assets/',
			'workletUrl' => DUB_ECHO_URL . 'assets/js/dub-echo-engine.js?ver=' . DUB_ECHO_VERSION,
			'restUrl'    => esc_url_raw( rest_url( Dub_Echo_Rest::NS . '/' ) ),
			'tracks'     => $tracks,
			'startTrack' => $atts['track'] ? absint( $atts['track'] ) : null,
			'settings'   => array(
				'title'         => $atts['title'] ? $atts['title'] : $s['title'],
				'subtitle'      => $s['subtitle'],
				'defaultPreset' => $preset,
				'defaultBpm'    => (float) $s['default_bpm'],
				'likes'         => (bool) $s['likes'],
				'showCounts'    => (bool) $s['show_counts'],
				'countPlays'    => (bool) $s['count_plays'],
				'showPresets'   => (bool) $s['show_presets'],
				'autoNext'      => (bool) $s['auto_next'],
				'loopTrack'     => (bool) $s['loop_track'],
				'maxWidth'      => (int) $s['max_width'],
				'note'          => $s['note'],
			),
		);

		wp_enqueue_style( 'dub-echo' );
		wp_enqueue_script( 'dub-echo-player' );

		// Without JavaScript the visitor still gets plain audio players.
		$fallback = '';
		foreach ( $tracks as $t ) {
			$fallback .= '<p>' . esc_html( ( $t['artist'] ? $t['artist'] . ' – ' : '' ) . $t['title'] ) . '<br><audio controls preload="none" src="' . esc_url( $t['url'] ) . '"></audio></p>';
		}

		return sprintf(
			'<div class="dubecho" data-config="%1$s"><noscript><div class="dubecho-fallback">%2$s%3$s</div></noscript></div>',
			esc_attr( wp_json_encode( $config ) ),
			esc_html__( 'Für das Dub Echo bitte JavaScript einschalten.', 'dub-echo' ),
			$fallback
		);
	}
}
