<?php
/**
 * Removes the plugin data on uninstall, if the admin asked for it in the settings.
 * The MP3 files in the media library are never touched.
 *
 * @package DubEcho
 */

if ( ! defined( 'WP_UNINSTALL_PLUGIN' ) ) {
	exit;
}

$dub_echo_settings = get_option( 'dub_echo_settings', array() );
if ( empty( $dub_echo_settings['delete_data'] ) ) {
	return;
}

global $wpdb;
$dub_echo_ids = $wpdb->get_col( $wpdb->prepare( "SELECT ID FROM {$wpdb->posts} WHERE post_type = %s", 'dub_echo_track' ) );
foreach ( $dub_echo_ids as $dub_echo_id ) {
	wp_delete_post( (int) $dub_echo_id, true );
}
$wpdb->query( "DROP TABLE IF EXISTS {$wpdb->prefix}dub_echo_likes" ); // phpcs:ignore WordPress.DB.PreparedSQL.InterpolatedNotPrepared
delete_option( 'dub_echo_settings' );
delete_option( 'dub_echo_db_version' );
