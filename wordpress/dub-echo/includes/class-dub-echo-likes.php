<?php
/**
 * Likes: one row per (track, visitor). Visitors are identified by a random id stored in
 * their browser; only salted hashes of that id and of the IP address are stored.
 *
 * @package DubEcho
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

class Dub_Echo_Likes {

	public static function table() {
		global $wpdb;
		return $wpdb->prefix . 'dub_echo_likes';
	}

	public static function install() {
		global $wpdb;
		require_once ABSPATH . 'wp-admin/includes/upgrade.php';
		$table   = self::table();
		$charset = $wpdb->get_charset_collate();
		dbDelta(
			"CREATE TABLE {$table} (
				id bigint(20) unsigned NOT NULL AUTO_INCREMENT,
				track_id bigint(20) unsigned NOT NULL,
				visitor char(64) NOT NULL,
				ip_hash char(64) NOT NULL DEFAULT '',
				created datetime NOT NULL,
				PRIMARY KEY  (id),
				UNIQUE KEY track_visitor (track_id,visitor),
				KEY track_ip (track_id,ip_hash),
				KEY created (created)
			) {$charset};"
		);
		update_option( 'dub_echo_db_version', DUB_ECHO_DB_VERSION );
	}

	public static function maybe_upgrade() {
		if ( get_option( 'dub_echo_db_version' ) !== DUB_ECHO_DB_VERSION ) {
			self::install();
		}
	}

	public static function hash( $value ) {
		return hash_hmac( 'sha256', (string) $value, wp_salt( 'auth' ) );
	}

	public static function visitor_hash( $token ) {
		return self::hash( 'v|' . $token );
	}

	public static function ip_hash() {
		$ip = isset( $_SERVER['REMOTE_ADDR'] ) ? sanitize_text_field( wp_unslash( $_SERVER['REMOTE_ADDR'] ) ) : '';
		return self::hash( 'ip|' . $ip );
	}

	public static function has_liked( $track_id, $visitor ) {
		global $wpdb;
		$table = self::table();
		return (bool) $wpdb->get_var( $wpdb->prepare( "SELECT id FROM {$table} WHERE track_id = %d AND visitor = %s", $track_id, $visitor ) );
	}

	/**
	 * @return true|WP_Error
	 */
	public static function like( $track_id, $visitor ) {
		global $wpdb;
		$table = self::table();
		if ( self::has_liked( $track_id, $visitor ) ) {
			return true;
		}
		$ip = self::ip_hash();
		/**
		 * Number of likes one IP address may give a track within 24 hours (several people can
		 * share an address, so this is more than 1). Filter `dub_echo_likes_per_ip`.
		 */
		$per_ip = (int) apply_filters( 'dub_echo_likes_per_ip', 3 );
		if ( $per_ip > 0 ) {
			$recent = (int) $wpdb->get_var(
				$wpdb->prepare(
					"SELECT COUNT(*) FROM {$table} WHERE track_id = %d AND ip_hash = %s AND created > %s",
					$track_id,
					$ip,
					gmdate( 'Y-m-d H:i:s', time() - DAY_IN_SECONDS )
				)
			);
			if ( $recent >= $per_ip ) {
				return new WP_Error( 'dub_echo_too_many', __( 'Von deinem Anschluss kamen heute schon genug Likes für diesen Titel.', 'dub-echo' ), array( 'status' => 429 ) );
			}
		}
		$wpdb->query(
			$wpdb->prepare(
				"INSERT IGNORE INTO {$table} (track_id, visitor, ip_hash, created) VALUES (%d, %s, %s, %s)",
				$track_id,
				$visitor,
				$ip,
				current_time( 'mysql', true )
			)
		);
		self::recount( $track_id );
		return true;
	}

	public static function unlike( $track_id, $visitor ) {
		global $wpdb;
		$wpdb->delete(
			self::table(),
			array(
				'track_id' => $track_id,
				'visitor'  => $visitor,
			),
			array( '%d', '%s' )
		);
		self::recount( $track_id );
	}

	public static function recount( $track_id ) {
		global $wpdb;
		$table = self::table();
		$n     = (int) $wpdb->get_var( $wpdb->prepare( "SELECT COUNT(*) FROM {$table} WHERE track_id = %d", $track_id ) );
		update_post_meta( $track_id, '_dub_echo_likes', $n );
		return $n;
	}

	public static function delete_for_track( $track_id ) {
		global $wpdb;
		$wpdb->delete( self::table(), array( 'track_id' => $track_id ), array( '%d' ) );
	}

	public static function reset_all() {
		global $wpdb;
		$table = self::table();
		$wpdb->query( "TRUNCATE TABLE {$table}" ); // phpcs:ignore WordPress.DB.PreparedSQL.InterpolatedNotPrepared
		foreach ( Dub_Echo_Tracks::query( array( 'status' => 'any' ) ) as $p ) {
			update_post_meta( $p->ID, '_dub_echo_likes', 0 );
			update_post_meta( $p->ID, '_dub_echo_plays', 0 );
		}
	}

	/**
	 * Likes per track since a given time.
	 *
	 * @return array track_id => count
	 */
	public static function counts_since( $seconds ) {
		global $wpdb;
		$table = self::table();
		$rows  = $wpdb->get_results(
			$wpdb->prepare(
				"SELECT track_id, COUNT(*) AS n FROM {$table} WHERE created > %s GROUP BY track_id",
				gmdate( 'Y-m-d H:i:s', time() - (int) $seconds )
			)
		);
		$out   = array();
		foreach ( (array) $rows as $r ) {
			$out[ (int) $r->track_id ] = (int) $r->n;
		}
		return $out;
	}

	/**
	 * Likes per day for the last $days days (all tracks).
	 *
	 * @return array 'Y-m-d' => count
	 */
	public static function per_day( $days = 30 ) {
		global $wpdb;
		$table = self::table();
		$rows  = $wpdb->get_results(
			$wpdb->prepare(
				"SELECT DATE(created) AS d, COUNT(*) AS n FROM {$table} WHERE created > %s GROUP BY DATE(created)",
				gmdate( 'Y-m-d 00:00:00', time() - ( $days - 1 ) * DAY_IN_SECONDS )
			)
		);
		$map   = array();
		foreach ( (array) $rows as $r ) {
			$map[ $r->d ] = (int) $r->n;
		}
		$out = array();
		for ( $i = $days - 1; $i >= 0; --$i ) {
			$d         = gmdate( 'Y-m-d', time() - $i * DAY_IN_SECONDS );
			$out[ $d ] = isset( $map[ $d ] ) ? $map[ $d ] : 0;
		}
		return $out;
	}
}
