<?php
/**
 * Tracks: a private post type, one post per MP3 in the player.
 *
 * post_title  = title shown in the dropdown
 * post_status = publish (visible) or draft (hidden)
 * menu_order  = position in the dropdown
 * meta        = attachment id, artist, BPM, preset, likes, plays
 *
 * @package DubEcho
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

class Dub_Echo_Tracks {

	const POST_TYPE = 'dub_echo_track';

	public static function register_post_type() {
		register_post_type(
			self::POST_TYPE,
			array(
				'labels'          => array(
					'name'          => __( 'Dub Echo Titel', 'dub-echo' ),
					'singular_name' => __( 'Dub Echo Titel', 'dub-echo' ),
				),
				'public'          => false,
				'show_ui'         => false,
				'show_in_rest'    => false,
				'rewrite'         => false,
				'query_var'       => false,
				'supports'        => array( 'title' ),
				'capability_type' => 'post',
			)
		);
	}

	/**
	 * @param array $args { status: 'publish'|'any', ids: int[], orderby: 'menu_order'|'likes'|'plays' }
	 * @return WP_Post[]
	 */
	public static function query( $args = array() ) {
		$args = wp_parse_args(
			$args,
			array(
				'status'  => 'publish',
				'ids'     => array(),
				'orderby' => 'menu_order',
			)
		);
		$q    = array(
			'post_type'        => self::POST_TYPE,
			'post_status'      => 'any' === $args['status'] ? array( 'publish', 'draft' ) : $args['status'],
			'posts_per_page'   => 500,
			'no_found_rows'    => true,
			'suppress_filters' => true,
			'orderby'          => array(
				'menu_order' => 'ASC',
				'ID'         => 'ASC',
			),
		);
		if ( ! empty( $args['ids'] ) ) {
			$q['post__in'] = array_map( 'absint', (array) $args['ids'] );
			$q['orderby']  = 'post__in';
		}
		if ( 'likes' === $args['orderby'] || 'plays' === $args['orderby'] ) {
			$q['meta_key'] = '_dub_echo_' . $args['orderby'];
			$q['orderby']  = array(
				'meta_value_num' => 'DESC',
				'menu_order'     => 'ASC',
			);
		}
		return get_posts( $q );
	}

	public static function get( $id ) {
		$p = get_post( absint( $id ) );
		return ( $p && self::POST_TYPE === $p->post_type ) ? $p : null;
	}

	/**
	 * Creates a track from a media library attachment. Title and artist come from the ID3 tags.
	 */
	public static function create_from_attachment( $attachment_id ) {
		$attachment_id = absint( $attachment_id );
		$att           = get_post( $attachment_id );
		if ( ! $att || 'attachment' !== $att->post_type || ! self::is_audio( $attachment_id ) ) {
			return new WP_Error( 'dub_echo_not_audio', __( 'Die Datei ist keine Audiodatei.', 'dub-echo' ) );
		}
		$meta   = wp_get_attachment_metadata( $attachment_id );
		$title  = $att->post_title ? $att->post_title : basename( get_attached_file( $attachment_id ) );
		$artist = is_array( $meta ) && ! empty( $meta['artist'] ) ? $meta['artist'] : '';
		if ( false === strpos( $title, ' ' ) ) {
			// no ID3 title, WordPress used the file name: "my_dub-track" -> "My Dub Track"
			$title = ucwords( str_replace( array( '_', '-' ), ' ', $title ) );
		}

		global $wpdb;
		$max_order = (int) $wpdb->get_var( $wpdb->prepare( "SELECT MAX(menu_order) FROM {$wpdb->posts} WHERE post_type = %s", self::POST_TYPE ) );

		$id = wp_insert_post(
			array(
				'post_type'   => self::POST_TYPE,
				'post_status' => 'publish',
				'post_title'  => wp_strip_all_tags( $title ),
				'menu_order'  => $max_order + 1,
			),
			true
		);
		if ( is_wp_error( $id ) ) {
			return $id;
		}
		update_post_meta( $id, '_dub_echo_attachment', $attachment_id );
		update_post_meta( $id, '_dub_echo_artist', sanitize_text_field( $artist ) );
		update_post_meta( $id, '_dub_echo_bpm', '' );
		update_post_meta( $id, '_dub_echo_preset', '' );
		update_post_meta( $id, '_dub_echo_likes', 0 );
		update_post_meta( $id, '_dub_echo_plays', 0 );
		return $id;
	}

	public static function is_audio( $attachment_id ) {
		$mime = get_post_mime_type( $attachment_id );
		return $mime && 0 === strpos( $mime, 'audio/' );
	}

	public static function delete( $id ) {
		$p = self::get( $id );
		if ( ! $p ) {
			return false;
		}
		Dub_Echo_Likes::delete_for_track( $p->ID );
		return (bool) wp_delete_post( $p->ID, true );
	}

	public static function likes( $id ) {
		return (int) get_post_meta( $id, '_dub_echo_likes', true );
	}

	public static function plays( $id ) {
		return (int) get_post_meta( $id, '_dub_echo_plays', true );
	}

	public static function add_play( $id ) {
		global $wpdb;
		// atomic increment, so parallel plays do not get lost
		$updated = $wpdb->query(
			$wpdb->prepare(
				"UPDATE {$wpdb->postmeta} SET meta_value = CAST(meta_value AS UNSIGNED) + 1 WHERE post_id = %d AND meta_key = '_dub_echo_plays'",
				$id
			)
		);
		if ( ! $updated ) {
			update_post_meta( $id, '_dub_echo_plays', 1 );
		}
		wp_cache_delete( $id, 'post_meta' );
		return self::plays( $id );
	}

	/**
	 * Data for the front end.
	 */
	public static function to_public_array( WP_Post $p ) {
		$att  = (int) get_post_meta( $p->ID, '_dub_echo_attachment', true );
		$url  = $att ? wp_get_attachment_url( $att ) : '';
		$meta = $att ? wp_get_attachment_metadata( $att ) : array();
		$bpm  = get_post_meta( $p->ID, '_dub_echo_bpm', true );
		return array(
			'id'     => $p->ID,
			'title'  => html_entity_decode( get_the_title( $p ), ENT_QUOTES, 'UTF-8' ),
			'artist' => (string) get_post_meta( $p->ID, '_dub_echo_artist', true ),
			'url'    => $url ? $url : '',
			'length' => is_array( $meta ) && ! empty( $meta['length'] ) ? (int) $meta['length'] : 0,
			'bpm'    => '' === $bpm ? null : (float) $bpm,
			'preset' => (string) get_post_meta( $p->ID, '_dub_echo_preset', true ),
			'likes'  => self::likes( $p->ID ),
		);
	}
}
