<?php
/**
 * Admin: menu "Dub Echo Plugin" with playlist, statistics, settings and help.
 *
 * @package DubEcho
 */

if ( ! defined( 'ABSPATH' ) ) {
	exit;
}

class Dub_Echo_Admin {

	const CAP = 'manage_options';

	public static function init() {
		add_action( 'admin_menu', array( __CLASS__, 'menu' ) );
		add_action( 'admin_init', array( __CLASS__, 'register_settings' ) );
		add_action( 'admin_enqueue_scripts', array( __CLASS__, 'assets' ) );
		add_action( 'wp_dashboard_setup', array( __CLASS__, 'dashboard_widget' ) );
		add_filter( 'plugin_action_links_' . plugin_basename( DUB_ECHO_FILE ), array( __CLASS__, 'action_links' ) );
		foreach ( array( 'add_tracks', 'upload_tracks', 'save_tracks', 'delete_track', 'reset_stats', 'reset_track', 'export_csv' ) as $a ) {
			add_action( 'admin_post_dub_echo_' . $a, array( __CLASS__, 'handle_' . $a ) );
		}
	}

	public static function menu() {
		add_menu_page(
			__( 'Dub Echo Plugin', 'dub-echo' ),
			__( 'Dub Echo Plugin', 'dub-echo' ),
			self::CAP,
			'dub-echo',
			array( __CLASS__, 'page_tracks' ),
			'dashicons-format-audio',
			26
		);
		add_submenu_page( 'dub-echo', __( 'Titel & Playlist', 'dub-echo' ), __( 'Titel & Playlist', 'dub-echo' ), self::CAP, 'dub-echo', array( __CLASS__, 'page_tracks' ) );
		add_submenu_page( 'dub-echo', __( 'Statistik & Likes', 'dub-echo' ), __( 'Statistik & Likes', 'dub-echo' ), self::CAP, 'dub-echo-stats', array( __CLASS__, 'page_stats' ) );
		add_submenu_page( 'dub-echo', __( 'Einstellungen', 'dub-echo' ), __( 'Einstellungen', 'dub-echo' ), self::CAP, 'dub-echo-settings', array( __CLASS__, 'page_settings' ) );
		add_submenu_page( 'dub-echo', __( 'Einbinden & Hilfe', 'dub-echo' ), __( 'Einbinden & Hilfe', 'dub-echo' ), self::CAP, 'dub-echo-help', array( __CLASS__, 'page_help' ) );
	}

	public static function action_links( $links ) {
		array_unshift( $links, '<a href="' . esc_url( admin_url( 'admin.php?page=dub-echo' ) ) . '">' . esc_html__( 'Titel verwalten', 'dub-echo' ) . '</a>' );
		return $links;
	}

	public static function assets( $hook ) {
		$page = isset( $_GET['page'] ) ? sanitize_key( wp_unslash( $_GET['page'] ) ) : ''; // phpcs:ignore WordPress.Security.NonceVerification.Recommended
		if ( 0 !== strpos( $page, 'dub-echo' ) ) {
			return;
		}
		wp_enqueue_style( 'dub-echo-admin', DUB_ECHO_URL . 'admin/admin.css', array(), DUB_ECHO_VERSION );
		if ( 'dub-echo' === $page ) {
			wp_enqueue_media();
			wp_enqueue_script( 'dub-echo-admin', DUB_ECHO_URL . 'admin/admin.js', array( 'jquery', 'jquery-ui-sortable' ), DUB_ECHO_VERSION, true );
			wp_localize_script(
				'dub-echo-admin',
				'dubEchoAdmin',
				array(
					'frameTitle'  => __( 'MP3-Dateien für den Dub Echo Player auswählen', 'dub-echo' ),
					'frameButton' => __( 'Zur Playlist hinzufügen', 'dub-echo' ),
					'confirmDel'  => __( 'Diesen Titel aus der Playlist entfernen? Die MP3 bleibt in der Mediathek, die Likes des Titels werden gelöscht.', 'dub-echo' ),
					'confirmLeave' => __( 'Es gibt ungespeicherte Änderungen.', 'dub-echo' ),
				)
			);
		}
		if ( 'dub-echo-stats' === $page ) {
			wp_enqueue_script( 'dub-echo-admin-stats', DUB_ECHO_URL . 'admin/admin.js', array( 'jquery' ), DUB_ECHO_VERSION, true );
			wp_localize_script(
				'dub-echo-admin-stats',
				'dubEchoAdmin',
				array(
					'confirmReset'      => __( 'Wirklich alle Likes und Wiedergaben auf 0 setzen? Das lässt sich nicht rückgängig machen.', 'dub-echo' ),
					'confirmResetTrack' => __( 'Likes und Wiedergaben dieses Titels auf 0 setzen?', 'dub-echo' ),
				)
			);
		}
	}

	// ------------------------------------------------------------------ helpers
	private static function check( $action ) {
		if ( ! current_user_can( self::CAP ) ) {
			wp_die( esc_html__( 'Keine Berechtigung.', 'dub-echo' ), 403 );
		}
		check_admin_referer( $action );
	}

	private static function back( $page, $msg, $extra = array() ) {
		wp_safe_redirect( add_query_arg( array_merge( array( 'page' => $page, 'dub_echo_msg' => $msg ), $extra ), admin_url( 'admin.php' ) ) );
		exit;
	}

	private static function notice() {
		if ( empty( $_GET['dub_echo_msg'] ) ) { // phpcs:ignore WordPress.Security.NonceVerification.Recommended
			return;
		}
		$msg   = sanitize_key( wp_unslash( $_GET['dub_echo_msg'] ) ); // phpcs:ignore WordPress.Security.NonceVerification.Recommended
		$count = isset( $_GET['n'] ) ? absint( $_GET['n'] ) : 0; // phpcs:ignore WordPress.Security.NonceVerification.Recommended
		$texts = array(
			/* translators: %d: number of tracks */
			'added'      => sprintf( _n( '%d Titel hinzugefügt.', '%d Titel hinzugefügt.', $count, 'dub-echo' ), $count ),
			'saved'      => __( 'Playlist gespeichert.', 'dub-echo' ),
			'deleted'    => __( 'Titel entfernt.', 'dub-echo' ),
			'reset'      => __( 'Alle Likes und Wiedergaben wurden zurückgesetzt.', 'dub-echo' ),
			'resettrack' => __( 'Zähler des Titels zurückgesetzt.', 'dub-echo' ),
			'noaudio'    => __( 'Es wurde keine Audiodatei hinzugefügt. Bitte MP3-Dateien wählen.', 'dub-echo' ),
			'uploaderr'  => __( 'Beim Hochladen ist ein Fehler aufgetreten.', 'dub-echo' ),
		);
		if ( isset( $texts[ $msg ] ) ) {
			$type = in_array( $msg, array( 'noaudio', 'uploaderr' ), true ) ? 'error' : 'success';
			printf( '<div class="notice notice-%s is-dismissible"><p>%s</p></div>', esc_attr( $type ), esc_html( $texts[ $msg ] ) );
		}
		if ( ! empty( $_GET['err'] ) ) { // phpcs:ignore WordPress.Security.NonceVerification.Recommended
			printf( '<div class="notice notice-error is-dismissible"><p>%s</p></div>', esc_html( sanitize_text_field( wp_unslash( $_GET['err'] ) ) ) ); // phpcs:ignore WordPress.Security.NonceVerification.Recommended
		}
	}

	private static function header( $title ) {
		echo '<div class="wrap dub-echo-admin">';
		echo '<h1 class="dub-echo-title"><span class="dashicons dashicons-format-audio"></span> ' . esc_html( $title ) . '</h1>';
		self::notice();
	}

	private static function preset_select( $name, $current, $empty_label ) {
		$html = '<select name="' . esc_attr( $name ) . '"><option value="">' . esc_html( $empty_label ) . '</option>';
		foreach ( Dub_Echo_Settings::presets() as $p ) {
			$html .= '<option value="' . esc_attr( $p ) . '"' . selected( $current, $p, false ) . '>' . esc_html( $p ) . '</option>';
		}
		return $html . '</select>';
	}

	private static function format_length( $sec ) {
		$sec = (int) $sec;
		return $sec > 0 ? sprintf( '%d:%02d', floor( $sec / 60 ), $sec % 60 ) : '';
	}

	// ------------------------------------------------------------------ page: tracks
	public static function page_tracks() {
		$tracks = Dub_Echo_Tracks::query( array( 'status' => 'any' ) );
		self::header( __( 'Dub Echo Plugin – Titel & Playlist', 'dub-echo' ) );
		?>
		<p class="dub-echo-intro"><?php esc_html_e( 'Hier legst du fest, welche MP3s die Besucher im Dropdown des Players auswählen können. Die Reihenfolge änderst du per Drag & Drop.', 'dub-echo' ); ?></p>

		<div class="dub-echo-addbox">
			<form id="dub-echo-add-form" method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>">
				<?php wp_nonce_field( 'dub_echo_add_tracks' ); ?>
				<input type="hidden" name="action" value="dub_echo_add_tracks">
				<input type="hidden" name="attachments" id="dub-echo-attachments" value="">
				<button type="button" class="button button-primary button-hero" id="dub-echo-add"><span class="dashicons dashicons-upload"></span> <?php esc_html_e( 'MP3s hochladen oder aus der Mediathek wählen', 'dub-echo' ); ?></button>
			</form>
			<form class="dub-echo-upload" method="post" enctype="multipart/form-data" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>">
				<?php wp_nonce_field( 'dub_echo_upload_tracks' ); ?>
				<input type="hidden" name="action" value="dub_echo_upload_tracks">
				<label><?php esc_html_e( 'Oder direkt hochladen:', 'dub-echo' ); ?>
					<input type="file" name="dub_echo_files[]" accept=".mp3,.m4a,.ogg,.wav,audio/*" multiple required>
				</label>
				<button type="submit" class="button"><?php esc_html_e( 'Hochladen', 'dub-echo' ); ?></button>
				<span class="description">
					<?php
					/* translators: %s: max upload size */
					printf( esc_html__( 'Maximale Dateigröße: %s', 'dub-echo' ), esc_html( size_format( wp_max_upload_size() ) ) );
					?>
				</span>
			</form>
		</div>

		<?php if ( ! $tracks ) : ?>
			<div class="dub-echo-empty">
				<p><strong><?php esc_html_e( 'Noch keine Titel in der Playlist.', 'dub-echo' ); ?></strong></p>
				<p><?php esc_html_e( 'Lade oben deine ersten MP3s hoch. Danach den Shortcode [dub_echo] auf einer Seite einfügen (oder den Block „Dub Echo Player“).', 'dub-echo' ); ?></p>
			</div>
		<?php else : ?>
			<form method="post" action="<?php echo esc_url( admin_url( 'admin-post.php' ) ); ?>" id="dub-echo-tracks-form">
				<?php wp_nonce_field( 'dub_echo_save_tracks' ); ?>
				<input type="hidden" name="action" value="dub_echo_save_tracks">
				<table class="widefat striped dub-echo-tracks">
					<thead>
						<tr>
							<th class="col-handle" aria-label="<?php esc_attr_e( 'Reihenfolge', 'dub-echo' ); ?>"></th>
							<th class="col-play" aria-label="<?php esc_attr_e( 'Vorhören', 'dub-echo' ); ?>"></th>
							<th class="col-title"><?php esc_html_e( 'Titel', 'dub-echo' ); ?></th>
							<th><?php esc_html_e( 'Künstler', 'dub-echo' ); ?></th>
							<th class="col-bpm" title="<?php esc_attr_e( 'Tempo für TIME SYNC (leer = Standard aus den Einstellungen)', 'dub-echo' ); ?>"><?php esc_html_e( 'BPM', 'dub-echo' ); ?></th>
							<th><?php esc_html_e( 'Start-Preset', 'dub-echo' ); ?></th>
							<th class="col-num"><?php esc_html_e( 'Sichtbar', 'dub-echo' ); ?></th>
							<th class="col-num" title="<?php esc_attr_e( 'Likes', 'dub-echo' ); ?>">&hearts;</th>
							<th class="col-num" title="<?php esc_attr_e( 'Wiedergaben', 'dub-echo' ); ?>">&#9654;</th>
							<th class="col-file"><?php esc_html_e( 'Datei', 'dub-echo' ); ?></th>
							<th class="col-actions"></th>
						</tr>
					</thead>
					<tbody>
					<?php
					foreach ( $tracks as $p ) :
						$att  = (int) get_post_meta( $p->ID, '_dub_echo_attachment', true );
						$url  = $att ? wp_get_attachment_url( $att ) : '';
						$meta = $att ? wp_get_attachment_metadata( $att ) : array();
						$file = $att ? basename( (string) get_attached_file( $att ) ) : '';
						$name = 'tracks[' . $p->ID . ']';
						$del  = wp_nonce_url( admin_url( 'admin-post.php?action=dub_echo_delete_track&track=' . $p->ID ), 'dub_echo_delete_track' );
						?>
						<tr data-id="<?php echo esc_attr( $p->ID ); ?>" class="<?php echo 'publish' === $p->post_status ? '' : 'is-hidden'; ?>">
							<td class="col-handle"><span class="dashicons dashicons-menu" title="<?php esc_attr_e( 'Ziehen zum Sortieren', 'dub-echo' ); ?>"></span><input type="hidden" class="order" name="<?php echo esc_attr( $name ); ?>[order]" value="<?php echo esc_attr( $p->menu_order ); ?>"></td>
							<td class="col-play"><?php if ( $url ) : ?><button type="button" class="button-link dub-echo-preview" data-src="<?php echo esc_url( $url ); ?>" aria-label="<?php esc_attr_e( 'Vorhören', 'dub-echo' ); ?>"><span class="dashicons dashicons-controls-play"></span></button><?php endif; ?></td>
							<td class="col-title"><input type="text" class="regular-text" name="<?php echo esc_attr( $name ); ?>[title]" value="<?php echo esc_attr( $p->post_title ); ?>" required></td>
							<td><input type="text" name="<?php echo esc_attr( $name ); ?>[artist]" value="<?php echo esc_attr( get_post_meta( $p->ID, '_dub_echo_artist', true ) ); ?>"></td>
							<td class="col-bpm"><input type="number" min="30" max="300" step="0.1" name="<?php echo esc_attr( $name ); ?>[bpm]" value="<?php echo esc_attr( get_post_meta( $p->ID, '_dub_echo_bpm', true ) ); ?>" placeholder="<?php echo esc_attr( Dub_Echo_Settings::get( 'default_bpm' ) ); ?>"></td>
							<td><?php echo self::preset_select( $name . '[preset]', get_post_meta( $p->ID, '_dub_echo_preset', true ), __( '– Standard –', 'dub-echo' ) ); // phpcs:ignore WordPress.Security.EscapeOutput.OutputNotEscaped ?></td>
							<td class="col-num"><input type="checkbox" name="<?php echo esc_attr( $name ); ?>[visible]" value="1" <?php checked( 'publish', $p->post_status ); ?>></td>
							<td class="col-num"><strong><?php echo esc_html( number_format_i18n( Dub_Echo_Tracks::likes( $p->ID ) ) ); ?></strong></td>
							<td class="col-num"><?php echo esc_html( number_format_i18n( Dub_Echo_Tracks::plays( $p->ID ) ) ); ?></td>
							<td class="col-file">
								<?php if ( $url ) : ?>
									<a href="<?php echo esc_url( get_edit_post_link( $att ) ); ?>"><?php echo esc_html( $file ); ?></a>
									<span class="description"><?php echo esc_html( self::format_length( is_array( $meta ) && isset( $meta['length'] ) ? $meta['length'] : 0 ) ); ?></span>
								<?php else : ?>
									<span class="dub-echo-missing"><?php esc_html_e( 'Datei fehlt', 'dub-echo' ); ?></span>
								<?php endif; ?>
							</td>
							<td class="col-actions"><a href="<?php echo esc_url( $del ); ?>" class="dub-echo-delete" aria-label="<?php esc_attr_e( 'Entfernen', 'dub-echo' ); ?>"><span class="dashicons dashicons-trash"></span></a></td>
						</tr>
					<?php endforeach; ?>
					</tbody>
				</table>
				<p class="submit"><button type="submit" class="button button-primary"><?php esc_html_e( 'Änderungen speichern', 'dub-echo' ); ?></button></p>
			</form>
		<?php endif; ?>

		<div class="dub-echo-shortcode-hint">
			<?php esc_html_e( 'Einbinden:', 'dub-echo' ); ?> <code>[dub_echo]</code>
			<?php esc_html_e( 'auf einer Seite oder in einem Beitrag, oder den Block „Dub Echo Player“ verwenden.', 'dub-echo' ); ?>
			<a href="<?php echo esc_url( admin_url( 'admin.php?page=dub-echo-help' ) ); ?>"><?php esc_html_e( 'Mehr Möglichkeiten', 'dub-echo' ); ?></a>
		</div>
		</div>
		<?php
	}

	public static function handle_add_tracks() {
		self::check( 'dub_echo_add_tracks' );
		$ids = isset( $_POST['attachments'] ) ? array_filter( array_map( 'absint', explode( ',', sanitize_text_field( wp_unslash( $_POST['attachments'] ) ) ) ) ) : array();
		$n   = 0;
		foreach ( $ids as $id ) {
			if ( ! is_wp_error( Dub_Echo_Tracks::create_from_attachment( $id ) ) ) {
				++$n;
			}
		}
		self::back( 'dub-echo', $n ? 'added' : 'noaudio', array( 'n' => $n ) );
	}

	public static function handle_upload_tracks() {
		self::check( 'dub_echo_upload_tracks' );
		if ( ! current_user_can( 'upload_files' ) ) {
			wp_die( esc_html__( 'Keine Berechtigung zum Hochladen.', 'dub-echo' ), 403 );
		}
		require_once ABSPATH . 'wp-admin/includes/file.php';
		require_once ABSPATH . 'wp-admin/includes/media.php';
		require_once ABSPATH . 'wp-admin/includes/image.php';

		// phpcs:ignore WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- files are validated by media_handle_upload
		$files = isset( $_FILES['dub_echo_files'] ) ? $_FILES['dub_echo_files'] : null;
		$n     = 0;
		$err   = '';
		if ( $files && is_array( $files['name'] ) ) {
			$count = count( $files['name'] );
			for ( $i = 0; $i < $count; ++$i ) {
				if ( UPLOAD_ERR_NO_FILE === $files['error'][ $i ] ) {
					continue;
				}
				$_FILES['dub_echo_single'] = array(
					'name'     => $files['name'][ $i ],
					'type'     => $files['type'][ $i ],
					'tmp_name' => $files['tmp_name'][ $i ],
					'error'    => $files['error'][ $i ],
					'size'     => $files['size'][ $i ],
				);
				$att = media_handle_upload( 'dub_echo_single', 0 );
				if ( is_wp_error( $att ) ) {
					$err = $files['name'][ $i ] . ': ' . $att->get_error_message();
					continue;
				}
				$r = Dub_Echo_Tracks::create_from_attachment( $att );
				if ( is_wp_error( $r ) ) {
					$err = $files['name'][ $i ] . ': ' . $r->get_error_message();
				} else {
					++$n;
				}
			}
		}
		$extra = array( 'n' => $n );
		if ( $err ) {
			$extra['err'] = rawurlencode( $err );
		}
		self::back( 'dub-echo', $n ? 'added' : 'uploaderr', $extra );
	}

	public static function handle_save_tracks() {
		self::check( 'dub_echo_save_tracks' );
		$rows    = isset( $_POST['tracks'] ) && is_array( $_POST['tracks'] ) ? wp_unslash( $_POST['tracks'] ) : array(); // phpcs:ignore WordPress.Security.ValidatedSanitizedInput.InputNotSanitized -- sanitized per field below
		$presets = Dub_Echo_Settings::presets();
		foreach ( $rows as $id => $row ) {
			$p = Dub_Echo_Tracks::get( $id );
			if ( ! $p || ! is_array( $row ) ) {
				continue;
			}
			$title = isset( $row['title'] ) ? sanitize_text_field( $row['title'] ) : $p->post_title;
			wp_update_post(
				array(
					'ID'          => $p->ID,
					'post_title'  => '' !== $title ? $title : $p->post_title,
					'post_status' => empty( $row['visible'] ) ? 'draft' : 'publish',
					'menu_order'  => isset( $row['order'] ) ? (int) $row['order'] : $p->menu_order,
				)
			);
			update_post_meta( $p->ID, '_dub_echo_artist', isset( $row['artist'] ) ? sanitize_text_field( $row['artist'] ) : '' );
			$bpm = isset( $row['bpm'] ) && '' !== trim( $row['bpm'] ) ? max( 30, min( 300, (float) str_replace( ',', '.', $row['bpm'] ) ) ) : '';
			update_post_meta( $p->ID, '_dub_echo_bpm', $bpm );
			$preset = isset( $row['preset'] ) && in_array( $row['preset'], $presets, true ) ? $row['preset'] : '';
			update_post_meta( $p->ID, '_dub_echo_preset', $preset );
		}
		self::back( 'dub-echo', 'saved' );
	}

	public static function handle_delete_track() {
		self::check( 'dub_echo_delete_track' );
		Dub_Echo_Tracks::delete( isset( $_GET['track'] ) ? absint( $_GET['track'] ) : 0 );
		self::back( 'dub-echo', 'deleted' );
	}

	// ------------------------------------------------------------------ page: statistics
	public static function page_stats() {
		$tracks = Dub_Echo_Tracks::query(
			array(
				'status'  => 'any',
				'orderby' => 'likes',
			)
		);
		$week   = Dub_Echo_Likes::counts_since( 7 * DAY_IN_SECONDS );
		$days   = Dub_Echo_Likes::per_day( 30 );
		$likes  = 0;
		$plays  = 0;
		$max    = 1;
		foreach ( $tracks as $p ) {
			$l      = Dub_Echo_Tracks::likes( $p->ID );
			$likes += $l;
			$plays += Dub_Echo_Tracks::plays( $p->ID );
			$max    = max( $max, $l );
		}
		self::header( __( 'Dub Echo Plugin – Statistik & Likes', 'dub-echo' ) );
		?>
		<div class="dub-echo-cards">
			<div class="dub-echo-card"><span class="num"><?php echo esc_html( number_format_i18n( count( $tracks ) ) ); ?></span><span class="lbl"><?php esc_html_e( 'Titel', 'dub-echo' ); ?></span></div>
			<div class="dub-echo-card"><span class="num">&hearts; <?php echo esc_html( number_format_i18n( $likes ) ); ?></span><span class="lbl"><?php esc_html_e( 'Likes gesamt', 'dub-echo' ); ?></span></div>
			<div class="dub-echo-card"><span class="num"><?php echo esc_html( number_format_i18n( array_sum( $week ) ) ); ?></span><span class="lbl"><?php esc_html_e( 'Likes in den letzten 7 Tagen', 'dub-echo' ); ?></span></div>
			<div class="dub-echo-card"><span class="num">&#9654; <?php echo esc_html( number_format_i18n( $plays ) ); ?></span><span class="lbl"><?php esc_html_e( 'Wiedergaben gesamt', 'dub-echo' ); ?></span></div>
		</div>

		<h2><?php esc_html_e( 'Welcher Titel kommt am besten an?', 'dub-echo' ); ?></h2>
		<?php if ( ! $tracks ) : ?>
			<p><?php esc_html_e( 'Noch keine Titel vorhanden.', 'dub-echo' ); ?></p>
		<?php else : ?>
		<table class="widefat striped dub-echo-ranking">
			<thead><tr>
				<th class="col-rank">#</th>
				<th><?php esc_html_e( 'Titel', 'dub-echo' ); ?></th>
				<th class="col-bar"><?php esc_html_e( 'Likes', 'dub-echo' ); ?></th>
				<th class="col-num"><?php esc_html_e( '7 Tage', 'dub-echo' ); ?></th>
				<th class="col-num"><?php esc_html_e( 'Wiedergaben', 'dub-echo' ); ?></th>
				<th class="col-num" title="<?php esc_attr_e( 'Likes pro 100 Wiedergaben', 'dub-echo' ); ?>"><?php esc_html_e( 'Like-Quote', 'dub-echo' ); ?></th>
				<th class="col-actions"></th>
			</tr></thead>
			<tbody>
			<?php
			$rank = 0;
			foreach ( $tracks as $p ) :
				++$rank;
				$l      = Dub_Echo_Tracks::likes( $p->ID );
				$pl     = Dub_Echo_Tracks::plays( $p->ID );
				$artist = get_post_meta( $p->ID, '_dub_echo_artist', true );
				$reset  = wp_nonce_url( admin_url( 'admin-post.php?action=dub_echo_reset_track&track=' . $p->ID ), 'dub_echo_reset_track' );
				?>
				<tr class="<?php echo 'publish' === $p->post_status ? '' : 'is-hidden'; ?>">
					<td class="col-rank"><?php echo $rank <= 3 && $l > 0 ? '<span class="medal medal-' . (int) $rank . '">' . (int) $rank . '</span>' : (int) $rank; ?></td>
					<td><strong><?php echo esc_html( $p->post_title ); ?></strong><?php echo $artist ? ' <span class="description">– ' . esc_html( $artist ) . '</span>' : ''; ?><?php echo 'publish' === $p->post_status ? '' : ' <span class="description">(' . esc_html__( 'ausgeblendet', 'dub-echo' ) . ')</span>'; ?></td>
					<td class="col-bar"><div class="bar"><span style="width:<?php echo esc_attr( round( $l / $max * 100, 1 ) ); ?>%"></span></div><strong>&hearts; <?php echo esc_html( number_format_i18n( $l ) ); ?></strong></td>
					<td class="col-num"><?php echo esc_html( number_format_i18n( isset( $week[ $p->ID ] ) ? $week[ $p->ID ] : 0 ) ); ?></td>
					<td class="col-num"><?php echo esc_html( number_format_i18n( $pl ) ); ?></td>
					<td class="col-num"><?php echo $pl > 0 ? esc_html( number_format_i18n( $l / $pl * 100, 1 ) ) . ' %' : '–'; ?></td>
					<td class="col-actions"><a href="<?php echo esc_url( $reset ); ?>" class="dub-echo-reset-track" title="<?php esc_attr_e( 'Zähler zurücksetzen', 'dub-echo' ); ?>"><span class="dashicons dashicons-image-rotate"></span></a></td>
				</tr>
			<?php endforeach; ?>
			</tbody>
		</table>
		<?php endif; ?>

		<h2><?php esc_html_e( 'Likes der letzten 30 Tage', 'dub-echo' ); ?></h2>
		<div class="dub-echo-chart" role="img" aria-label="<?php esc_attr_e( 'Likes pro Tag', 'dub-echo' ); ?>">
			<?php
			$dmax = max( 1, max( $days ) );
			foreach ( $days as $d => $n ) {
				printf(
					'<div class="day" title="%1$s: %2$d"><span style="height:%3$s%%"></span><em>%4$s</em></div>',
					esc_attr( date_i18n( 'D, j. M', strtotime( $d ) ) ),
					(int) $n,
					esc_attr( round( $n / $dmax * 100, 1 ) ),
					esc_html( $n ? $n : '' )
				);
			}
			?>
		</div>

		<p class="dub-echo-stat-actions">
			<a class="button" href="<?php echo esc_url( wp_nonce_url( admin_url( 'admin-post.php?action=dub_echo_export_csv' ), 'dub_echo_export_csv' ) ); ?>"><span class="dashicons dashicons-download"></span> <?php esc_html_e( 'Als CSV exportieren', 'dub-echo' ); ?></a>
			<a class="button dub-echo-reset" href="<?php echo esc_url( wp_nonce_url( admin_url( 'admin-post.php?action=dub_echo_reset_stats' ), 'dub_echo_reset_stats' ) ); ?>"><?php esc_html_e( 'Alle Zähler zurücksetzen', 'dub-echo' ); ?></a>
		</p>
		<p class="description"><?php esc_html_e( 'Jeder Besucher kann einen Titel einmal liken (und das Like wieder zurücknehmen). Wiedergaben werden pro Besucher und Titel höchstens alle 30 Minuten gezählt.', 'dub-echo' ); ?></p>
		</div>
		<?php
	}

	public static function handle_reset_stats() {
		self::check( 'dub_echo_reset_stats' );
		Dub_Echo_Likes::reset_all();
		self::back( 'dub-echo-stats', 'reset' );
	}

	public static function handle_reset_track() {
		self::check( 'dub_echo_reset_track' );
		$p = Dub_Echo_Tracks::get( isset( $_GET['track'] ) ? absint( $_GET['track'] ) : 0 );
		if ( $p ) {
			Dub_Echo_Likes::delete_for_track( $p->ID );
			update_post_meta( $p->ID, '_dub_echo_likes', 0 );
			update_post_meta( $p->ID, '_dub_echo_plays', 0 );
		}
		self::back( 'dub-echo-stats', 'resettrack' );
	}

	public static function handle_export_csv() {
		self::check( 'dub_echo_export_csv' );
		$week = Dub_Echo_Likes::counts_since( 7 * DAY_IN_SECONDS );
		nocache_headers();
		header( 'Content-Type: text/csv; charset=utf-8' );
		header( 'Content-Disposition: attachment; filename="dub-echo-statistik-' . gmdate( 'Y-m-d' ) . '.csv"' );
		$out = fopen( 'php://output', 'w' );
		fwrite( $out, "\xEF\xBB\xBF" ); // UTF-8 BOM for Excel
		fputcsv( $out, array( 'Rang', 'Titel', 'Künstler', 'Likes', 'Likes 7 Tage', 'Wiedergaben', 'Like-Quote %', 'Sichtbar' ), ';' );
		$rank = 0;
		foreach ( Dub_Echo_Tracks::query( array( 'status' => 'any', 'orderby' => 'likes' ) ) as $p ) {
			$l  = Dub_Echo_Tracks::likes( $p->ID );
			$pl = Dub_Echo_Tracks::plays( $p->ID );
			fputcsv(
				$out,
				array(
					++$rank,
					$p->post_title,
					get_post_meta( $p->ID, '_dub_echo_artist', true ),
					$l,
					isset( $week[ $p->ID ] ) ? $week[ $p->ID ] : 0,
					$pl,
					$pl ? number_format( $l / $pl * 100, 1, ',', '' ) : '',
					'publish' === $p->post_status ? 'ja' : 'nein',
				),
				';'
			);
		}
		fclose( $out );
		exit;
	}

	// ------------------------------------------------------------------ page: settings
	public static function register_settings() {
		register_setting(
			'dub_echo',
			Dub_Echo_Settings::OPTION,
			array(
				'type'              => 'array',
				'sanitize_callback' => array( 'Dub_Echo_Settings', 'sanitize' ),
				'default'           => Dub_Echo_Settings::defaults(),
			)
		);
	}

	public static function page_settings() {
		$s = Dub_Echo_Settings::get();
		$o = Dub_Echo_Settings::OPTION;
		self::header( __( 'Dub Echo Plugin – Einstellungen', 'dub-echo' ) );
		settings_errors( 'dub_echo' );
		$check = function ( $key, $label, $desc = '' ) use ( $s, $o ) {
			printf(
				'<label><input type="checkbox" name="%1$s[%2$s]" value="1" %3$s> %4$s</label>%5$s<br>',
				esc_attr( $o ),
				esc_attr( $key ),
				checked( 1, (int) $s[ $key ], false ),
				esc_html( $label ),
				$desc ? ' <span class="description">' . esc_html( $desc ) . '</span>' : ''
			);
		};
		?>
		<form method="post" action="options.php">
			<?php settings_fields( 'dub_echo' ); ?>
			<h2><?php esc_html_e( 'Player', 'dub-echo' ); ?></h2>
			<table class="form-table" role="presentation">
				<tr>
					<th scope="row"><label for="de-title"><?php esc_html_e( 'Name im Player', 'dub-echo' ); ?></label></th>
					<td><input id="de-title" type="text" class="regular-text" name="<?php echo esc_attr( $o ); ?>[title]" value="<?php echo esc_attr( $s['title'] ); ?>">
						<p class="description"><?php esc_html_e( 'Steht links oben in der Leiste des Players.', 'dub-echo' ); ?></p></td>
				</tr>
				<tr>
					<th scope="row"><label for="de-subtitle"><?php esc_html_e( 'Untertitel', 'dub-echo' ); ?></label></th>
					<td><input id="de-subtitle" type="text" class="regular-text" name="<?php echo esc_attr( $o ); ?>[subtitle]" value="<?php echo esc_attr( $s['subtitle'] ); ?>"></td>
				</tr>
				<tr>
					<th scope="row"><?php esc_html_e( 'Start-Preset', 'dub-echo' ); ?></th>
					<td>
						<select name="<?php echo esc_attr( $o ); ?>[default_preset]">
							<?php foreach ( Dub_Echo_Settings::presets() as $p ) : ?>
								<option value="<?php echo esc_attr( $p ); ?>" <?php selected( $s['default_preset'], $p ); ?>><?php echo esc_html( $p ); ?></option>
							<?php endforeach; ?>
						</select>
						<p class="description"><?php esc_html_e( 'So ist das Echo eingestellt, wenn ein Besucher die Seite öffnet. Pro Titel kannst du in der Playlist ein eigenes Preset wählen.', 'dub-echo' ); ?></p>
					</td>
				</tr>
				<tr>
					<th scope="row"><label for="de-bpm"><?php esc_html_e( 'Standard-Tempo (BPM)', 'dub-echo' ); ?></label></th>
					<td><input id="de-bpm" type="number" min="30" max="300" step="0.1" class="small-text" name="<?php echo esc_attr( $o ); ?>[default_bpm]" value="<?php echo esc_attr( $s['default_bpm'] ); ?>">
						<p class="description"><?php esc_html_e( 'Für TIME SYNC bei Titeln ohne eigene BPM-Angabe.', 'dub-echo' ); ?></p></td>
				</tr>
				<tr>
					<th scope="row"><label for="de-width"><?php esc_html_e( 'Maximale Breite', 'dub-echo' ); ?></label></th>
					<td><input id="de-width" type="number" min="0" max="3000" step="10" class="small-text" name="<?php echo esc_attr( $o ); ?>[max_width]" value="<?php echo esc_attr( $s['max_width'] ); ?>"> px
						<p class="description"><?php esc_html_e( '0 = so breit wie der Inhaltsbereich des Themes.', 'dub-echo' ); ?></p></td>
				</tr>
				<tr>
					<th scope="row"><?php esc_html_e( 'Wiedergabe', 'dub-echo' ); ?></th>
					<td>
						<?php
						$check( 'auto_next', __( 'Nach dem Ende automatisch den nächsten Titel abspielen', 'dub-echo' ) );
						$check( 'loop_track', __( 'Titel in Schleife wiederholen', 'dub-echo' ), __( '(hat Vorrang vor „nächster Titel“)', 'dub-echo' ) );
						$check( 'shuffle', __( 'Titel im Dropdown in zufälliger Reihenfolge zeigen', 'dub-echo' ) );
						$check( 'show_presets', __( 'Preset-Auswahl für Besucher anzeigen', 'dub-echo' ) );
						?>
					</td>
				</tr>
				<tr>
					<th scope="row"><label for="de-note"><?php esc_html_e( 'Hinweis unter dem Player', 'dub-echo' ); ?></label></th>
					<td><textarea id="de-note" class="large-text" rows="2" name="<?php echo esc_attr( $o ); ?>[note]"><?php echo esc_textarea( $s['note'] ); ?></textarea>
						<p class="description"><?php esc_html_e( 'Optional, z. B. „Kopfhörer auf und an den Reglern drehen!“', 'dub-echo' ); ?></p></td>
				</tr>
			</table>

			<h2><?php esc_html_e( 'Likes & Statistik', 'dub-echo' ); ?></h2>
			<table class="form-table" role="presentation">
				<tr>
					<th scope="row"><?php esc_html_e( 'Likes', 'dub-echo' ); ?></th>
					<td>
						<?php
						$check( 'likes', __( 'Herz-Button („Gefällt mir“) anzeigen', 'dub-echo' ) );
						$check( 'show_counts', __( 'Anzahl der Likes öffentlich anzeigen', 'dub-echo' ) );
						$check( 'count_plays', __( 'Wiedergaben zählen', 'dub-echo' ) );
						?>
					</td>
				</tr>
				<tr>
					<th scope="row"><?php esc_html_e( 'Deinstallation', 'dub-echo' ); ?></th>
					<td><?php $check( 'delete_data', __( 'Beim Löschen des Plugins alle Titel-Einträge, Likes und Einstellungen entfernen', 'dub-echo' ), __( '(die MP3s in der Mediathek bleiben erhalten)', 'dub-echo' ) ); ?></td>
				</tr>
			</table>
			<?php submit_button(); ?>
		</form>
		</div>
		<?php
	}

	// ------------------------------------------------------------------ page: help
	public static function page_help() {
		$tracks = Dub_Echo_Tracks::query();
		$ex     = $tracks ? $tracks[0]->ID : 123;
		self::header( __( 'Dub Echo Plugin – Einbinden & Hilfe', 'dub-echo' ) );
		?>
		<div class="dub-echo-help">
			<h2><?php esc_html_e( 'Player auf einer Seite einbinden', 'dub-echo' ); ?></h2>
			<p><?php esc_html_e( 'Füge diesen Shortcode in eine Seite oder einen Beitrag ein:', 'dub-echo' ); ?></p>
			<p><code class="dub-echo-big">[dub_echo]</code></p>
			<p><?php esc_html_e( 'Im Block-Editor gibt es außerdem den Block „Dub Echo Player“ (Kategorie Medien). In Elementor, Divi & Co. nimmst du ein Shortcode-Element.', 'dub-echo' ); ?></p>

			<h2><?php esc_html_e( 'Optionen des Shortcodes', 'dub-echo' ); ?></h2>
			<table class="widefat striped">
				<thead><tr><th><?php esc_html_e( 'Beispiel', 'dub-echo' ); ?></th><th><?php esc_html_e( 'Wirkung', 'dub-echo' ); ?></th></tr></thead>
				<tbody>
					<tr><td><code>[dub_echo tracks="<?php echo (int) $ex; ?>,<?php echo (int) $ex + 1; ?>"]</code></td><td><?php esc_html_e( 'Nur diese Titel (IDs) im Dropdown, in dieser Reihenfolge.', 'dub-echo' ); ?></td></tr>
					<tr><td><code>[dub_echo track="<?php echo (int) $ex; ?>"]</code></td><td><?php esc_html_e( 'Mit diesem Titel starten.', 'dub-echo' ); ?></td></tr>
					<tr><td><code>[dub_echo preset="Spiral Runaway"]</code></td><td><?php esc_html_e( 'Mit diesem Preset starten.', 'dub-echo' ); ?></td></tr>
					<tr><td><code>[dub_echo title="DUB LAB"]</code></td><td><?php esc_html_e( 'Anderer Name in der Leiste des Players.', 'dub-echo' ); ?></td></tr>
				</tbody>
			</table>
			<?php if ( $tracks ) : ?>
				<p class="description">
					<?php esc_html_e( 'IDs deiner Titel:', 'dub-echo' ); ?>
					<?php
					echo esc_html(
						implode(
							', ',
							array_map(
								function ( $p ) {
									return $p->post_title . ' = ' . $p->ID;
								},
								$tracks
							)
						)
					);
					?>
				</p>
			<?php endif; ?>

			<h2><?php esc_html_e( 'Gut zu wissen', 'dub-echo' ); ?></h2>
			<ul class="ul-disc">
				<li><?php esc_html_e( 'Der Klang entsteht live im Browser des Besuchers mit derselben Echo-Engine wie im VST/AU-Plugin (Bandmodell mit drei Köpfen, Federhall, Wow & Flutter, Tape-Stop).', 'dub-echo' ); ?></li>
				<li><?php esc_html_e( 'Am besten läuft der Player auf Seiten mit HTTPS. Ohne HTTPS nutzt er eine ältere Audio-Technik, die etwas mehr Rechenleistung braucht.', 'dub-echo' ); ?></li>
				<li><?php esc_html_e( 'Die MP3s müssen von deiner eigenen Domain kommen (Mediathek). Liegen sie auf einem CDN, muss es CORS erlauben.', 'dub-echo' ); ?></li>
				<li><?php esc_html_e( 'Die Like-Zahlen werden beim Laden des Players frisch abgefragt, sie stimmen also auch mit Caching-Plugins.', 'dub-echo' ); ?></li>
				<li><?php esc_html_e( 'Unterstützte Formate: MP3 (empfohlen), außerdem M4A/AAC, OGG und WAV, sofern der Browser sie abspielen kann.', 'dub-echo' ); ?></li>
			</ul>

			<h2><?php esc_html_e( 'Datenschutz', 'dub-echo' ); ?></h2>
			<p><?php esc_html_e( 'Für die Likes speichert der Browser des Besuchers eine zufällige Kennung (localStorage, kein Cookie). Auf dem Server werden nur gesalzene Hashwerte dieser Kennung und der IP-Adresse gespeichert, keine IP-Adressen im Klartext. Es werden keine Daten an Dritte übertragen. Vorschlag für deine Datenschutzerklärung:', 'dub-echo' ); ?></p>
			<blockquote class="dub-echo-privacy"><?php esc_html_e( 'Auf dieser Website kannst du Musiktitel im „Dub Echo Player“ mit „Gefällt mir“ markieren. Dafür speichert dein Browser eine zufällig erzeugte Kennung im lokalen Speicher. Auf unserem Server speichern wir zu jedem Like nur einen nicht umkehrbaren Hashwert dieser Kennung und deiner IP-Adresse sowie den Zeitpunkt, um Mehrfach-Likes zu verhindern. Rechtsgrundlage ist unser berechtigtes Interesse an einer aussagekräftigen Statistik (Art. 6 Abs. 1 lit. f DSGVO). Du kannst die Kennung jederzeit löschen, indem du die Website-Daten in deinem Browser entfernst.', 'dub-echo' ); ?></blockquote>
		</div>
		</div>
		<?php
	}

	// ------------------------------------------------------------------ dashboard
	public static function dashboard_widget() {
		if ( current_user_can( self::CAP ) ) {
			wp_add_dashboard_widget( 'dub_echo_top', __( 'Dub Echo – beliebteste Titel', 'dub-echo' ), array( __CLASS__, 'render_dashboard_widget' ) );
		}
	}

	public static function render_dashboard_widget() {
		$tracks = array_slice( Dub_Echo_Tracks::query( array( 'orderby' => 'likes' ) ), 0, 5 );
		if ( ! $tracks ) {
			echo '<p>' . esc_html__( 'Noch keine Titel.', 'dub-echo' ) . ' <a href="' . esc_url( admin_url( 'admin.php?page=dub-echo' ) ) . '">' . esc_html__( 'Jetzt MP3s hochladen', 'dub-echo' ) . '</a></p>';
			return;
		}
		echo '<ol class="dub-echo-dash">';
		foreach ( $tracks as $p ) {
			printf(
				'<li><strong>%1$s</strong> <span style="float:right">&hearts; %2$s &nbsp; &#9654; %3$s</span></li>',
				esc_html( $p->post_title ),
				esc_html( number_format_i18n( Dub_Echo_Tracks::likes( $p->ID ) ) ),
				esc_html( number_format_i18n( Dub_Echo_Tracks::plays( $p->ID ) ) )
			);
		}
		echo '</ol><p><a href="' . esc_url( admin_url( 'admin.php?page=dub-echo-stats' ) ) . '">' . esc_html__( 'Zur Statistik', 'dub-echo' ) . '</a></p>';
	}
}
