<?php
/**
 * Plugin settings (one option array).
 *
 * @package DubEcho
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

class Dub_Echo_Settings {

	const OPTION = 'dub_echo_settings';

	/**
	 * Factory presets of the Dubasco Tape Echo (same list as in the plugin and in dub-echo-engine.js).
	 */
	public static function presets() {
		return array(
			'Init - Classic Space',
			'Kingston Skank Echo',
			'Roots Rockers Quarter',
			'Steppers Ping-Pong',
			'Spiral Runaway',
			'Hazy Ark Session',
			'UK Sound System',
			'Sci-Fi Space Sweep',
			'Throw The Snare',
			'Vocal Dub Tail',
			'Melodica Chamber',
			'Bass Heavy Echo',
			'Worn Out Tape',
			'Clean Modern Tape',
			'Slapback 70s',
			'Spring Tank Only',
		);
	}

	public static function defaults() {
		return array(
			'title'          => 'DUB ECHO',
			'subtitle'       => 'TAPE ECHO  RE-201 STYLE',
			'default_preset' => 'Init - Classic Space',
			'default_bpm'    => 120,
			'likes'          => 1,
			'show_counts'    => 1,
			'count_plays'    => 1,
			'show_presets'   => 1,
			'auto_next'      => 1,
			'loop_track'     => 0,
			'shuffle'        => 0,
			'max_width'      => 1100,
			'note'           => '',
			'delete_data'    => 0,
		);
	}

	public static function get( $key = null ) {
		$opts = get_option( self::OPTION, array() );
		$opts = wp_parse_args( is_array( $opts ) ? $opts : array(), self::defaults() );
		if ( null === $key ) {
			return $opts;
		}
		return isset( $opts[ $key ] ) ? $opts[ $key ] : null;
	}

	public static function sanitize( $in ) {
		$d   = self::defaults();
		$in  = is_array( $in ) ? $in : array();
		$out = array();

		$out['title']          = isset( $in['title'] ) ? sanitize_text_field( $in['title'] ) : $d['title'];
		$out['subtitle']       = isset( $in['subtitle'] ) ? sanitize_text_field( $in['subtitle'] ) : $d['subtitle'];
		$out['default_preset'] = isset( $in['default_preset'] ) && in_array( $in['default_preset'], self::presets(), true ) ? $in['default_preset'] : $d['default_preset'];
		$bpm                   = isset( $in['default_bpm'] ) ? (float) $in['default_bpm'] : $d['default_bpm'];
		$out['default_bpm']    = max( 30, min( 300, $bpm ) );
		foreach ( array( 'likes', 'show_counts', 'count_plays', 'show_presets', 'auto_next', 'loop_track', 'shuffle', 'delete_data' ) as $flag ) {
			$out[ $flag ] = empty( $in[ $flag ] ) ? 0 : 1;
		}
		$w                = isset( $in['max_width'] ) ? absint( $in['max_width'] ) : $d['max_width'];
		$out['max_width'] = $w ? max( 320, min( 3000, $w ) ) : 0;
		$out['note']      = isset( $in['note'] ) ? sanitize_textarea_field( $in['note'] ) : '';
		return $out;
	}
}
