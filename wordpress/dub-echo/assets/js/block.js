/* Block "Dub Echo Player" for the block editor (no build step needed). */
(function (wp, data) {
	'use strict';
	if (!wp || !wp.blocks) return;
	var el = wp.element.createElement;
	var be = wp.blockEditor || wp.editor;
	var C = wp.components;

	wp.blocks.registerBlockType('dub-echo/player', {
		apiVersion: 2,
		title: 'Dub Echo Player',
		description: 'Das Dubasco Tape Echo mit deinen MP3s zum Ausprobieren.',
		icon: 'format-audio',
		category: 'media',
		keywords: ['dub', 'echo', 'tape', 'mp3', 'delay'],
		supports: { html: false, align: ['wide', 'full'] },
		attributes: {
			track: { type: 'string', default: '' },
			preset: { type: 'string', default: '' }
		},
		edit: function (props) {
			var a = props.attributes;
			var blockProps = be.useBlockProps ? be.useBlockProps() : {};
			var trackOpts = [{ label: '– erster Titel –', value: '' }].concat((data.tracks || []).map(function (t) { return { label: t.title, value: String(t.id) }; }));
			var presetOpts = [{ label: '– Standard aus den Einstellungen –', value: '' }].concat((data.presets || []).map(function (p) { return { label: p, value: p }; }));
			return el('div', blockProps,
				el(be.InspectorControls, null,
					el(C.PanelBody, { title: 'Dub Echo' },
						el(C.SelectControl, { label: 'Starttitel', value: a.track, options: trackOpts, onChange: function (v) { props.setAttributes({ track: v }); } }),
						el(C.SelectControl, { label: 'Start-Preset', value: a.preset, options: presetOpts, onChange: function (v) { props.setAttributes({ preset: v }); } })
					)
				),
				el('div', { style: { position: 'relative', borderRadius: '8px', overflow: 'hidden', background: '#1d1b17' } },
					el('img', { src: data.preview, alt: '', style: { display: 'block', width: '100%', height: 'auto', opacity: 0.55 } }),
					el('div', { style: { position: 'absolute', inset: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', color: '#ffb347', fontSize: '28px', fontWeight: 600, textShadow: '0 2px 6px #000', textAlign: 'center', padding: '1em' } },
						'Dub Echo Player',
						el('span', { style: { fontSize: '15px', color: '#eac48e', fontWeight: 400, marginTop: '0.5em' } },
							(data.tracks || []).length + ' Titel in der Playlist. Titel verwaltest du im Menü „Dub Echo Plugin“.')
					)
				)
			);
		},
		save: function () { return null; }
	});
})(window.wp, window.dubEchoBlock || {});
