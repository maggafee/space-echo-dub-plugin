<?php
/**
 * Plugin Name:       Dub Echo Plugin
 * Plugin URI:        https://github.com/maggafee/space-echo-dub-plugin
 * Description:       Das Dubasco Tape Echo (RE-201 Style) im Browser: Besucher wählen deine MP3s aus einem Dropdown, spielen sie durch das Bandecho und experimentieren mit allen Reglern. Mit Likes und Statistik. Einbinden per Shortcode [dub_echo] oder Block.
 * Version:           1.0.0
 * Requires at least: 5.8
 * Requires PHP:      7.4
 * Author:            Dubasco
 * License:           GPL-2.0-or-later
 * License URI:       https://www.gnu.org/licenses/gpl-2.0.html
 * Text Domain:       dub-echo
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

define( 'DUB_ECHO_VERSION', '1.0.0' );
define( 'DUB_ECHO_DB_VERSION', '1' );
define( 'DUB_ECHO_FILE', __FILE__ );
define( 'DUB_ECHO_DIR', plugin_dir_path( __FILE__ ) );
define( 'DUB_ECHO_URL', plugin_dir_url( __FILE__ ) );

require_once DUB_ECHO_DIR . 'includes/class-dub-echo-settings.php';
require_once DUB_ECHO_DIR . 'includes/class-dub-echo-tracks.php';
require_once DUB_ECHO_DIR . 'includes/class-dub-echo-likes.php';
require_once DUB_ECHO_DIR . 'includes/class-dub-echo-rest.php';
require_once DUB_ECHO_DIR . 'includes/class-dub-echo-frontend.php';

if ( is_admin() ) {
	require_once DUB_ECHO_DIR . 'includes/class-dub-echo-admin.php';
}

/**
 * Activation: create the likes table and the default settings.
 */
function dub_echo_activate() {
	Dub_Echo_Tracks::register_post_type();
	Dub_Echo_Likes::install();
	if ( false === get_option( Dub_Echo_Settings::OPTION ) ) {
		add_option( Dub_Echo_Settings::OPTION, Dub_Echo_Settings::defaults() );
	}
}
register_activation_hook( __FILE__, 'dub_echo_activate' );

add_action( 'init', array( 'Dub_Echo_Tracks', 'register_post_type' ) );
add_action( 'plugins_loaded', array( 'Dub_Echo_Likes', 'maybe_upgrade' ) );
add_action( 'rest_api_init', array( 'Dub_Echo_Rest', 'register_routes' ) );
Dub_Echo_Frontend::init();

if ( is_admin() ) {
	Dub_Echo_Admin::init();
}
