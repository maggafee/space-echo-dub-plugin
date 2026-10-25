/* Dub Echo Plugin - admin pages */
(function ($, cfg) {
	'use strict';
	cfg = cfg || {};

	// ---- add MP3s from the media library / upload in the media modal
	var frame;
	$('#dub-echo-add').on('click', function (e) {
		e.preventDefault();
		if (!window.wp || !wp.media) return;
		if (!frame) {
			frame = wp.media({
				title: cfg.frameTitle,
				button: { text: cfg.frameButton },
				library: { type: 'audio' },
				multiple: 'add'
			});
			frame.on('select', function () {
				var ids = frame.state().get('selection').map(function (a) { return a.id; });
				if (!ids.length) return;
				$('#dub-echo-attachments').val(ids.join(','));
				$('#dub-echo-add-form').trigger('submit');
			});
		}
		frame.open();
	});

	// ---- drag & drop order
	var $form = $('#dub-echo-tracks-form'), dirty = false;
	var $body = $('.dub-echo-tracks tbody');
	if ($body.length && $.fn.sortable) {
		$body.sortable({
			handle: '.col-handle',
			axis: 'y',
			helper: function (e, tr) {
				var $orig = tr.children(), $h = tr.clone();
				$h.children().each(function (i) { $(this).width($orig.eq(i).width()); });
				return $h;
			},
			update: function () {
				$body.find('tr').each(function (i) { $(this).find('input.order').val(i + 1); });
				dirty = true;
			}
		});
	}
	$form.on('change input', ':input', function () { dirty = true; });
	$form.on('change', 'input[type=checkbox][name$="[visible]"]', function () {
		$(this).closest('tr').toggleClass('is-hidden', !this.checked);
	});
	$form.on('submit', function () { dirty = false; });
	$(window).on('beforeunload', function () { if (dirty) return cfg.confirmLeave; });

	// ---- delete / reset confirmations
	$('.dub-echo-delete').on('click', function (e) { if (!window.confirm(cfg.confirmDel)) e.preventDefault(); });
	$('.dub-echo-reset').on('click', function (e) { if (!window.confirm(cfg.confirmReset)) e.preventDefault(); });
	$('.dub-echo-reset-track').on('click', function (e) { if (!window.confirm(cfg.confirmResetTrack)) e.preventDefault(); });

	// ---- preview
	var audio = null, $current = null;
	function setIcon($b, playing) {
		$b.find('.dashicons').toggleClass('dashicons-controls-play', !playing).toggleClass('dashicons-controls-pause', playing);
	}
	$('.dub-echo-preview').on('click', function () {
		var $b = $(this);
		if (!audio) {
			audio = new Audio();
			audio.addEventListener('ended', function () { if ($current) setIcon($current, false); });
		}
		if ($current && $current[0] === this && !audio.paused) {
			audio.pause();
			setIcon($b, false);
			return;
		}
		if ($current) setIcon($current, false);
		if (!$current || $current[0] !== this) audio.src = $b.data('src');
		$current = $b;
		audio.play();
		setIcon($b, true);
	});
})(jQuery, window.dubEchoAdmin);
