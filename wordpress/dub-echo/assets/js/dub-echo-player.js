/*
 * Dub Echo Player - front end of the WordPress plugin.
 *
 * Rebuilds the Dubasco Tape Echo editor (Source/PluginEditor.cpp, Source/gui) in the
 * browser: same artwork, same control positions (Source/Layout.h), same animation
 * behaviour. Audio runs through the ported engine in an AudioWorklet.
 */
(function () {
	'use strict';

	var E = window.DubascoEngine;
	if (!E) return;

	// ------------------------------------------------------------------ layout (Source/Layout.h)
	var BW = 1536, BH = 1324;
	var KNOB_START = -135, KNOB_END = 135, LEVER_ARM_TO_BASE = 1.5526, REEL_FILL = 0.9964;
	var KNOB_FILL = { knob_large: 0.9966, knob_medium: 0.9788, knob_small: 0.9872 };
	var CTL = {
		mode: ['lever', '', 192, 1106, 42],
		headLamp0: ['lamp', '', 120, 1216, 10], headLamp1: ['lamp', '', 162, 1216, 10],
		headLamp2: ['lamp', '', 204, 1216, 10], headLamp3: ['lamp', '', 258, 1216, 10],
		reverb: ['knob', 'knob_medium', 384, 1090, 38],
		springDecay: ['knob', 'knob_small', 494, 1090, 34],
		kick: ['button', '', 439, 1192, 22],
		wowFlutter: ['knob', 'knob_medium', 621, 1096, 40],
		tapeAge: ['knob', 'knob_medium', 729, 1096, 40],
		inertia: ['knob', 'knob_medium', 837, 1096, 40],
		tapeReadout: ['readout', '', 729, 1232, 0, 220, 22],
		sync: ['lever', '', 956, 1086, 21],
		syncNote: ['knob', 'knob_small', 1072, 1080, 30],
		noteReadout: ['readout', '', 1072, 1136, 0, 84, 22],
		stereoMode: ['lever', '', 1017, 1210, 20],
		lowCut: ['knob', 'knob_small', 1192, 1068, 27],
		duck: ['knob', 'knob_small', 1294, 1068, 27],
		dry: ['knob', 'knob_small', 1192, 1178, 27],
		output: ['knob', 'knob_small', 1294, 1178, 27],
		'throw': ['button', '', 1409, 1062, 24],
		hold: ['button', '', 1409, 1140, 24],
		sendMode: ['lever', '', 1409, 1216, 13],
		input: ['knob', 'knob_large', 170, 780, 60],
		delayTime: ['knob', 'knob_large', 385, 795, 61],
		feedback: ['knob', 'knob_large', 595, 786, 64],
		bass: ['knob', 'knob_medium', 775, 787, 60],
		treble: ['knob', 'knob_medium', 942, 787, 59],
		echoLevel: ['knob', 'knob_medium', 1107, 789, 60],
		tapeSat: ['knob', 'knob_small', 1274, 780, 55],
		power: ['toggle', '', 1405, 848, 30],
		powerLamp: ['lamp', '', 1405, 744, 33],
		jackMic: ['jack', '', 133, 891, 25], jackLine: ['jack', '', 214, 891, 25],
		reelL: ['reel', '', 322, 272, 248], reelR: ['reel', '', 1207, 273, 248],
		vu: ['vu', '', 622, 368, 0, 294, 121]
	};
	var LEVER_ANGLES = {
		mode: [-150, -122.73, -95.45, -68.18, -40.91, -13.64, 13.64, 40.91, 68.18, 95.45, 122.73, 150],
		sync: [-45, 45],
		stereoMode: [-90, -35, 35, 90],
		sendMode: [-50, 50]
	};
	var LEVER_WRAP = { mode: false, sync: true, stereoMode: false, sendMode: true };
	var TAPE_W = 9, TAPE_BOX = [60, 430, 1476, 640];
	var TAPE_PATH = [[196, 452], [89, 523], [238, 563], [372, 596], [452, 525], [540, 525], [998, 525], [1080, 528], [1176, 578], [1276, 578], [1358, 528], [1447, 520], [1338, 452]];
	var VU_PIVOT = [767, 505], VU_NEEDLE = 118;
	var VU_DB = [-20, -10, -7, -5, -3, -1, 0, 1, 3, 5];
	var VU_DEG = [-57.44, -42.85, -35.10, -23.20, -7.88, 9.19, 19.46, 28.07, 44.45, 57.91];

	function clamp(x, lo, hi) { return x < lo ? lo : (x > hi ? hi : x); }
	function el(tag, cls, parent) {
		var e = document.createElement(tag);
		if (cls) e.className = cls;
		if (parent) parent.appendChild(e);
		return e;
	}
	function pct(v, total) { return (v / total * 100) + '%'; }
	function place(e, cx, cy, halfW, halfH) {
		if (halfH === undefined) halfH = halfW;
		e.style.left = pct(cx - halfW, BW);
		e.style.top = pct(cy - halfH, BH);
		e.style.width = pct(2 * halfW, BW);
		e.style.height = pct(2 * halfH, BH);
	}
	function img(src, cls, parent) {
		var i = el('img', cls, parent);
		i.src = src; i.alt = ''; i.draggable = false; i.decoding = 'async';
		return i;
	}
	function shadow(parent, sizeRatio, dyRatio, strength, dx) {
		// soft drop shadow like gui::softShadow (radius 1.18 r, offset dy)
		var s = el('div', 'de-shadow', parent);
		var sz = sizeRatio * 100;
		s.style.width = s.style.height = sz + '%';
		s.style.left = (50 - sz / 2 + (dx || 0) * 100) + '%';
		s.style.top = (50 - sz / 2 + dyRatio * 100) + '%';
		s.style.background = 'radial-gradient(closest-side, rgba(0,0,0,' + strength + ') 0%, rgba(0,0,0,' + (strength * 0.75) + ') 72%, rgba(0,0,0,0) 100%)';
		return s;
	}
	function fmtTime(t) {
		if (!isFinite(t) || t < 0) t = 0;
		var m = Math.floor(t / 60), s = Math.floor(t % 60);
		return m + ':' + (s < 10 ? '0' : '') + s;
	}
	function storageGet(k) { try { return window.localStorage.getItem(k); } catch (e) { return null; } }
	function storageSet(k, v) { try { window.localStorage.setItem(k, v); } catch (e) { /* private mode */ } }

	var HELP_TEXT = [
		['HAUPTPANEL', [
			'INPUT: Aufnahmepegel aufs Band (5 = 0 dB). Mit der MIC/LINE-Buchse wählst du den Vorverstärker (MIC = mehr Gain und Zerre).',
			'DELAY TIME: Bandgeschwindigkeit wie beim RE-201. Beim Drehen pitcht das Echo hoch und runter (Motorträgheit unter TAPE).',
			'FEEDBACK: Wiederholungen. Ab etwa 7 geht das Band in die Eigenschwingung, der klassische Dub-Runaway.',
			'BASS / TREBLE: Klangregelung im Echo-Kreis, wirkt auf jede Wiederholung. ECHO LEVEL: Lautstärke der Echos.',
			'TAPE SAT: Bandsättigung von sauber bis dreckig. POWER aus: Das Band läuft langsam aus (Tape-Stop).',
			'VU-METER: anklicken zum Umschalten zwischen IN / ECHO / OUT.'
		]],
		['DUB SECTION', [
			'ECHO MODE 1-12 wie am Space Echo: 1-4 nur Echo (Köpfe 1, 2, 3, 2+3), 5-11 Echo mit Federhall, 12 nur Hall.',
			'SPRING REVERB: Hallanteil und Abklingzeit. KICK ist ein Tritt gegen den Federhall (Crash).',
			'TAPE: Wow & Flutter (Gleichlaufschwankungen), Tape Age (Höhenverlust, Rauschen, Dropouts), Inertia (Motorträgheit).',
			'TIME: FREE/SYNC. SYNC folgt dem Tempo des Titels, NOTE wählt den Notenwert (T = Triole, D = punktiert).',
			'STEREO: MONO (original), WIDE (Köpfe im Panorama verteilt), STEREO (zwei Bänder), PING (Ping-Pong).',
			'MIX: Low Cut im Echo-Kreis, Ducking (Echos weichen dem trockenen Signal), Dry-Pegel (0 = nur Effekt), Output.',
			'DUB: Mit Schalter THR geht nur Signal ins Echo, solange THROW gedrückt ist (Dub-Throw). Bei ALL lässt THROW das Feedback anschwellen. HOLD = Endlosschleife: Eingang zu, das Band dreht weiter.'
		]],
		['BEDIENUNG', [
			'Oben wählst du den Titel und startest ihn. Mit dem Herz zeigst du, welcher Titel dir gefällt.',
			'Regler ziehen (hoch/runter oder links/rechts), Mausrad oder Pfeiltasten. Shift beim Ziehen = fein. Doppelklick setzt einen Regler zurück.',
			'Wahlschalter: anklicken (links = zurück, rechts = weiter) oder ziehen. Bei ECHO MODE kannst du direkt auf eine Zahl klicken.',
			'Die Echos klingen weiter, auch wenn du die Wiedergabe anhältst. Mit POWER aus oder HOLD spielst du mit dem Band.'
		]]
	];

	// ================================================================== player
	function Player(root, cfg) {
		this.root = root;
		this.cfg = cfg;
		this.s = cfg.settings || {};
		this.i18n = cfg.i18n || {};
		this.assets = cfg.assetsUrl;
		this.tracks = cfg.tracks || [];
		this.values = E.defaults();
		this.controls = {};
		this.listeners = {};
		this.trackIndex = -1;
		this.liked = {};
		this.paramsDirty = true;
		this.vuSource = 0;
		this.meters = { i: 0, o: 0, e: 0 };
		this.motorLog = 0; this.motorRamp = 1;
		this.lastTick = 0;
		this.readoutTimer = 0;
		this.visible = true;
		this.audio = null;
		this.build();
		this.applyPreset(this.presetIndexByName(this.s.defaultPreset), true);
		var start = 0;
		if (cfg.startTrack) for (var i = 0; i < this.tracks.length; ++i) if (String(this.tracks[i].id) === String(cfg.startTrack)) start = i;
		if (this.tracks.length) this.selectTrack(start, false);
		this.loadLikes();
		this.refreshStats();
		this.startLoop();
	}

	Player.prototype.t = function (key, fallback) { return this.i18n[key] || fallback; };
	Player.prototype.img = function (name) { return this.assets + 'img/' + name; };

	Player.prototype.on = function (id, fn) { (this.listeners[id] = this.listeners[id] || []).push(fn); fn(this.values[id]); };

	Player.prototype.setParam = function (id, v, fromUser) {
		var d = E.PARAMS[id];
		if (!d) return;
		if (d.choices) v = clamp(Math.round(v), 0, d.choices.length - 1);
		else if (d.bool) v = v ? 1 : 0;
		else v = clamp(v, d.min, d.max);
		if (this.values[id] === v) return;
		this.values[id] = v;
		var l = this.listeners[id];
		if (l) for (var i = 0; i < l.length; ++i) l[i](v);
		this.paramsDirty = true;
		if (fromUser && !E.NON_PRESET[id]) this.markPresetEdited();
	};

	Player.prototype.bpm = function () {
		var tr = this.tracks[this.trackIndex];
		var b = tr && parseFloat(tr.bpm);
		return b > 0 ? b : (parseFloat(this.s.defaultBpm) || 120);
	};

	// ------------------------------------------------------------------ DOM
	Player.prototype.build = function () {
		var self = this, root = this.root;
		root.innerHTML = '';
		root.classList.add('dubecho--ready');
		if (this.s.maxWidth) root.style.maxWidth = parseInt(this.s.maxWidth, 10) + 'px';

		// ---- top bar (player + presets), styled like the plugin's top bar
		var bar = el('div', 'de-bar', root);
		var brand = el('div', 'de-brand', bar);
		el('span', 'de-brand-name', brand).textContent = this.s.title || 'DUB ECHO';
		el('span', 'de-brand-sub', brand).textContent = this.s.subtitle || 'TAPE ECHO  RE-201 STYLE';

		var transport = el('div', 'de-transport', bar);
		this.prevBtn = this.button(transport, '⏮', this.t('prevTrack', 'Vorheriger Titel'), function () { self.stepTrack(-1); });
		this.playBtn = this.button(transport, '', this.t('play', 'Abspielen'), function () { self.togglePlay(); });
		this.playBtn.classList.add('de-play');
		this.nextBtn = this.button(transport, '⏭', this.t('nextTrack', 'Nächster Titel'), function () { self.stepTrack(1); });

		var trackWrap = el('label', 'de-select de-select--track', bar);
		el('span', 'de-sr', trackWrap).textContent = this.t('chooseTrack', 'Titel wählen');
		this.trackSelect = el('select', '', trackWrap);
		this.trackSelect.addEventListener('change', function () { self.selectTrack(parseInt(self.trackSelect.value, 10), self.isPlaying()); });
		this.fillTrackSelect();

		if (this.s.likes) {
			this.likeBtn = el('button', 'de-btn de-like', bar);
			this.likeBtn.type = 'button';
			this.likeBtn.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 21s-7.5-4.6-10-9.3C.4 8.5 2.2 4.5 6 4.5c2.2 0 3.6 1.3 4.4 2.5L12 9l1.6-2c.8-1.2 2.2-2.5 4.4-2.5 3.8 0 5.6 4 4 7.2C19.5 16.4 12 21 12 21z"/></svg><span class="de-like-count"></span>';
			this.likeCount = this.likeBtn.querySelector('.de-like-count');
			this.likeBtn.addEventListener('click', function () { self.toggleLike(); });
		}

		var seek = el('div', 'de-seek', bar);
		this.timeNow = el('span', 'de-time', seek);
		this.seekInput = el('input', '', seek);
		this.seekInput.type = 'range'; this.seekInput.min = 0; this.seekInput.max = 1000; this.seekInput.value = 0; this.seekInput.step = 1;
		this.seekInput.setAttribute('aria-label', this.t('position', 'Position'));
		this.timeTotal = el('span', 'de-time', seek);
		this.timeNow.textContent = this.timeTotal.textContent = '0:00';
		this.seekInput.addEventListener('input', function () {
			var a = self.audioEl();
			if (a && isFinite(a.duration)) a.currentTime = self.seekInput.value / 1000 * a.duration;
		});

		var right = el('div', 'de-bar-right', bar);
		if (this.s.showPresets) {
			this.button(right, '‹', this.t('prevPreset', 'Voriges Preset'), function () { self.stepPreset(-1); }).classList.add('de-small');
			var pw = el('label', 'de-select de-select--preset', right);
			el('span', 'de-sr', pw).textContent = this.t('preset', 'Preset');
			this.presetSelect = el('select', '', pw);
			E.PRESETS.forEach(function (p, i) {
				var o = el('option', '', self.presetSelect); o.value = i; o.textContent = p.name;
			});
			this.presetEdited = el('option', '', this.presetSelect);
			this.presetEdited.value = -1; this.presetEdited.hidden = true;
			this.presetSelect.addEventListener('change', function () {
				var i = parseInt(self.presetSelect.value, 10);
				if (i >= 0) self.applyPreset(i);
			});
			this.button(right, '›', this.t('nextPreset', 'Nächstes Preset'), function () { self.stepPreset(1); }).classList.add('de-small');
		}
		this.bypassBtn = this.button(right, 'BYPASS', this.t('bypass', 'Effekt umgehen (Original hören)'), function () { self.setParam('bypass', self.values.bypass ? 0 : 1); });
		this.on('bypass', function (v) { self.bypassBtn.classList.toggle('is-on', !!v); self.bypassBtn.setAttribute('aria-pressed', v ? 'true' : 'false'); });
		this.button(right, '?', this.t('help', 'Kurzanleitung'), function () { self.toggleHelp(); });
		// on small screens the panel can be shown larger and scrolled sideways
		this.zoomBtn = this.button(right, 'ZOOM', this.t('zoom', 'Bedienfeld vergrößern'), function () {
			var on = !root.classList.contains('is-zoomed');
			root.classList.toggle('is-zoomed', on);
			self.zoomBtn.classList.toggle('is-on', on);
			self.zoomBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
			if (on) stage.scrollLeft = (panel.clientWidth - stage.clientWidth) / 2;
		});
		this.zoomBtn.classList.add('de-zoom');

		// ---- machine panel
		var stage = el('div', 'de-stage', root);
		var panel = this.panel = el('div', 'de-panel', stage);
		panel.style.backgroundImage = 'url("' + this.img('background.jpg') + '")';
		panel.setAttribute('role', 'group');
		panel.setAttribute('aria-label', 'Dubasco Tape Echo');

		this.buildTape(panel);
		this.reelL = this.buildReel(panel, 'reelL', 'reel_a.webp', 0.3);
		this.reelR = this.buildReel(panel, 'reelR', 'reel_b.webp', 1.1);
		this.buildVU(panel);

		Object.keys(CTL).forEach(function (id) { if (CTL[id][0] === 'knob') self.buildKnob(panel, id); });
		['mode', 'sync', 'stereoMode', 'sendMode'].forEach(function (id) { self.buildLever(panel, id); });
		this.buildToggle(panel);
		this.powerLamp = this.buildLamp(panel, 'powerLamp');
		this.headLamps = [0, 1, 2, 3].map(function (i) { return self.buildLamp(panel, 'headLamp' + i); });
		this.lampButtons = [
			this.buildLampButton(panel, 'kick', true),
			this.buildLampButton(panel, 'throw', true),
			this.buildLampButton(panel, 'hold', false)
		];
		this.buildJacks(panel);
		this.tapeReadout = this.buildReadout(panel, 'tapeReadout');
		this.noteReadout = this.buildReadout(panel, 'noteReadout');

		this.bubble = el('div', 'de-bubble', panel);
		this.bubbleTitle = el('div', 'de-bubble-title', this.bubble);
		this.bubbleValue = el('div', 'de-bubble-value', this.bubble);

		this.buildHelp(panel);

		this.status = el('div', 'de-status', root);
		this.status.setAttribute('role', 'status');
		this.status.setAttribute('aria-live', 'polite');
		if (this.s.note) { var n = el('div', 'de-note', root); n.textContent = this.s.note; }

		// scale the whole panel's text with its width
		var resize = function () {
			var w = panel.clientWidth || 1;
			root.style.setProperty('--de-scale', (w / BW).toFixed(4));
			self.sizeTapeCanvas();
		};
		if (window.ResizeObserver) new ResizeObserver(resize).observe(panel);
		else window.addEventListener('resize', resize);
		resize();

		if (window.IntersectionObserver) {
			new IntersectionObserver(function (entries) { self.visible = entries[0].isIntersecting; }).observe(panel);
		}

		this.updatePlayButton();
	};

	Player.prototype.button = function (parent, label, title, fn) {
		var b = el('button', 'de-btn', parent);
		b.type = 'button';
		b.textContent = label;
		b.title = title;
		b.setAttribute('aria-label', title);
		b.addEventListener('click', fn);
		return b;
	};

	Player.prototype.fillTrackSelect = function () {
		var self = this, sel = this.trackSelect;
		sel.innerHTML = '';
		if (!this.tracks.length) {
			var o = el('option', '', sel); o.textContent = this.t('noTracks', 'Noch keine Titel hochgeladen'); o.value = -1;
			sel.disabled = true;
			return;
		}
		this.tracks.forEach(function (tr, i) {
			var o = el('option', '', sel);
			o.value = i;
			o.textContent = self.trackLabel(tr);
		});
		if (this.trackIndex >= 0) sel.value = this.trackIndex;
	};

	Player.prototype.trackLabel = function (tr) {
		var s = (tr.artist ? tr.artist + ' – ' : '') + tr.title;
		if (this.s.likes && this.s.showCounts && tr.likes > 0) s += '   ♥ ' + tr.likes;
		return s;
	};

	// ------------------------------------------------------------------ controls
	Player.prototype.showValue = function (id, anchor, show) {
		if (!show) { if (!this.bubbleOwner || this.bubbleOwner === id) { this.bubble.classList.remove('is-visible'); this.bubbleOwner = null; } return; }
		this.bubbleOwner = id;
		this.bubbleAnchor = anchor;
		this.bubbleTitle.textContent = E.PARAMS[id].name.toUpperCase();
		this.bubbleValue.textContent = E.valueText(id, this.values[id]);
		var x = (parseFloat(anchor.style.left) + parseFloat(anchor.style.width) / 2);
		var y = parseFloat(anchor.style.top);
		var below = y < 8;
		this.bubble.style.left = clamp(x, 6, 94) + '%';
		this.bubble.style.top = (below ? y + parseFloat(anchor.style.height) : y) + '%';
		this.bubble.classList.toggle('is-below', below);
		this.bubble.classList.add('is-visible');
	};

	Player.prototype.attachHover = function (node, id) {
		var self = this;
		node.addEventListener('pointerenter', function (e) { if (e.pointerType === 'mouse') self.showValue(id, node, true); });
		node.addEventListener('pointerleave', function () { if (!node.classList.contains('is-dragging')) self.showValue(id, node, false); });
		node.addEventListener('focus', function () { self.showValue(id, node, true); });
		node.addEventListener('blur', function () { self.showValue(id, node, false); });
		this.on(id, function () { if (self.bubbleOwner === id) self.showValue(id, node, true); });
	};

	Player.prototype.buildKnob = function (panel, id) {
		var self = this, c = CTL[id], r = c[4], half = r * 1.18, fill = KNOB_FILL[c[1]];
		var node = el('div', 'de-ctl de-knob', panel);
		place(node, c[2], c[3], half);
		shadow(node, 1, (r * 0.08) / (2 * half), 0.55);
		var im = img(this.img(c[1] + '.webp'), 'de-sprite', node);
		var size = (2 * r / fill) / (2 * half) * 100;
		im.style.width = im.style.height = size + '%';
		im.style.left = im.style.top = (50 - size / 2) + '%';
		var d = E.PARAMS[id];
		node.tabIndex = 0;
		node.setAttribute('role', 'slider');
		node.setAttribute('aria-label', d.name);
		this.on(id, function (v) {
			var n = E.toNorm(id, v);
			im.style.transform = 'rotate(' + (KNOB_START + n * (KNOB_END - KNOB_START)) + 'deg)';
			node.setAttribute('aria-valuenow', Math.round(n * 100));
			node.setAttribute('aria-valuetext', E.valueText(id, v));
		});
		node.setAttribute('aria-valuemin', 0); node.setAttribute('aria-valuemax', 100);
		this.controls[id] = node;
		this.attachHover(node, id);

		var nudge = function (dn) { self.setParam(id, E.fromNorm(id, E.toNorm(id, self.values[id]) + dn), true); };
		var drag = null, lastTap = 0;
		node.addEventListener('pointerdown', function (e) {
			if (e.button !== 0) return;
			e.preventDefault();
			node.focus({ preventScroll: true });
			var now = Date.now();
			if (now - lastTap < 320) { self.setParam(id, d.def, true); lastTap = 0; return; }   // double click / double tap
			lastTap = now;
			drag = { x: e.clientX, y: e.clientY, n: E.toNorm(id, self.values[id]), id: e.pointerId };
			try { node.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ }
			node.classList.add('is-dragging');
			self.showValue(id, node, true);
		});
		node.addEventListener('pointermove', function (e) {
			if (!drag || e.pointerId !== drag.id) return;
			// like JUCE RotaryHorizontalVerticalDrag with sensitivity 260 px (scaled to the panel)
			var scale = Math.max(0.5, panel.clientWidth / BW);
			var sens = 260 * scale * (e.shiftKey ? 5 : 1);
			var dn = ((e.clientX - drag.x) - (e.clientY - drag.y)) / sens;
			self.setParam(id, E.fromNorm(id, drag.n + dn), true);
		});
		var end = function (e) {
			if (!drag || (e && e.pointerId !== drag.id)) return;
			drag = null;
			node.classList.remove('is-dragging');
			if (e && e.pointerType !== 'mouse') self.showValue(id, node, false);
		};
		node.addEventListener('pointerup', end);
		node.addEventListener('pointercancel', end);
		node.addEventListener('wheel', function (e) {
			e.preventDefault();
			var dy = e.deltaY !== 0 ? -e.deltaY : e.deltaX;
			nudge((dy > 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.025));
		}, { passive: false });
		node.addEventListener('keydown', function (e) {
			var step = e.shiftKey ? 0.005 : 0.02;
			if (e.key === 'ArrowUp' || e.key === 'ArrowRight') nudge(step);
			else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') nudge(-step);
			else if (e.key === 'PageUp') nudge(0.1);
			else if (e.key === 'PageDown') nudge(-0.1);
			else if (e.key === 'Home') self.setParam(id, E.fromNorm(id, 0), true);
			else if (e.key === 'End') self.setParam(id, E.fromNorm(id, 1), true);
			else if (e.key === 'Delete' || e.key === 'Backspace') self.setParam(id, d.def, true);
			else return;
			e.preventDefault();
		});
		if (id === 'delayTime') {
			this.on('sync', function (v) { node.classList.toggle('is-dimmed', !!v); });
		}
	};

	Player.prototype.buildLever = function (panel, id) {
		var self = this, c = CTL[id], angles = LEVER_ANGLES[id], chicken = id === 'mode';
		var baseW = 2 * c[4] / 0.98, half = chicken ? 82 : baseW * 0.8;
		var node = el('div', 'de-ctl de-lever' + (chicken ? ' de-lever--chicken' : ''), panel);
		place(node, c[2], c[3], half);
		var arm, armShadow;
		if (chicken) {
			// sprite: skirt radius 0.25 D, pointer tip at 0.47 D (D = 0.6 of the component)
			shadow(node, 0.6 * 0.25 * 2.36, 0.6 * 0.04, 0.6);
			armShadow = el('div', 'de-armshadow', node);
			armShadow.style.width = (0.6 * 0.11 * 100) + '%';
			armShadow.style.height = (0.6 * 0.42 * 100) + '%';
			arm = img(this.img('chickenhead.webp'), 'de-sprite', node);
			arm.style.width = arm.style.height = '60%';
			arm.style.left = arm.style.top = '20%';
		} else {
			var bw = 100 / 1.6;
			shadow(node, bw * 0.48 * 2.36 / 100, bw * 0.05 / 100, 0.5);
			var base = img(this.img('lever_base.webp'), 'de-sprite', node);
			base.style.width = base.style.height = bw + '%';
			base.style.left = base.style.top = (50 - bw / 2) + '%';
			armShadow = el('div', 'de-armshadow', node);
			armShadow.style.width = (bw * 0.12) + '%';
			armShadow.style.height = (bw * 0.55) + '%';
			arm = img(this.img('lever_arm.webp'), 'de-sprite', node);
			var aw = bw * LEVER_ARM_TO_BASE;
			arm.style.width = arm.style.height = aw + '%';
			arm.style.left = arm.style.top = (50 - aw / 2) + '%';
		}
		armShadow.style.marginLeft = '-' + (parseFloat(armShadow.style.width) / 2) + '%';
		var d = E.PARAMS[id];
		node.tabIndex = 0;
		node.setAttribute('role', 'slider');
		node.setAttribute('aria-label', d.name);
		node.setAttribute('aria-valuemin', 0);
		node.setAttribute('aria-valuemax', angles.length - 1);
		var lever = { node: node, angles: angles, shown: angles[this.values[id]], index: 0, arm: arm, armShadow: armShadow, chicken: chicken };
		lever.render = function () {
			lever.arm.style.transform = 'rotate(' + (chicken ? lever.shown : lever.shown - 180) + 'deg)';
			lever.armShadow.style.transform = 'translate(2px, 4px) rotate(' + (lever.shown + 180) + 'deg)';
		};
		this.on(id, function (v) {
			lever.index = v;
			node.setAttribute('aria-valuenow', v);
			node.setAttribute('aria-valuetext', E.valueText(id, v));
		});
		lever.shown = angles[lever.index];
		lever.render();
		(this.levers = this.levers || []).push(lever);
		this.controls[id] = node;
		this.attachHover(node, id);

		var n = angles.length;
		var setIndex = function (i) {
			i = LEVER_WRAP[id] ? ((i % n) + n) % n : clamp(i, 0, n - 1);
			self.setParam(id, i, true);
		};
		var drag = null;
		node.addEventListener('pointerdown', function (e) {
			if (e.button !== 0) return;
			e.preventDefault();
			node.focus({ preventScroll: true });
			drag = { x: e.clientX, y: e.clientY, start: lever.index, moved: false, id: e.pointerId };
			try { node.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ }
			node.classList.add('is-dragging');
		});
		node.addEventListener('pointermove', function (e) {
			if (!drag || e.pointerId !== drag.id) return;
			var dx = e.clientX - drag.x, dy = e.clientY - drag.y;
			if (Math.abs(dx) + Math.abs(dy) > 4) drag.moved = true;
			if (!drag.moved) return;
			var steps = (dx - dy) / Math.max(8, node.clientWidth * 0.22);
			var target = clamp(Math.round(drag.start + steps), 0, n - 1);
			if (target !== lever.index) self.setParam(id, target, true);
		});
		var up = function (e) {
			if (!drag || e.pointerId !== drag.id) return;
			var wasDrag = drag.moved;
			drag = null;
			node.classList.remove('is-dragging');
			if (e.type === 'pointercancel' || wasDrag) return;
			var rect = node.getBoundingClientRect();
			var px = e.clientX - rect.left, py = e.clientY - rect.top;
			// click: 2-position switches flip, otherwise left half = previous, right half = next
			if (n === 2) { setIndex(1 - lever.index); return; }
			if (chicken) {
				var ddx = px - rect.width / 2, ddy = py - rect.height / 2;
				if (Math.hypot(ddx, ddy) > rect.width * 0.12) {
					var ang = Math.atan2(ddx, -ddy) * 180 / Math.PI, best = 0;
					for (var i = 1; i < n; ++i) if (Math.abs(angles[i] - ang) < Math.abs(angles[best] - ang)) best = i;
					setIndex(best);
					return;
				}
			}
			setIndex(lever.index + (px >= rect.width / 2 ? 1 : -1));
		};
		node.addEventListener('pointerup', up);
		node.addEventListener('pointercancel', up);
		node.addEventListener('wheel', function (e) {
			e.preventDefault();
			var dd = e.deltaY !== 0 ? -e.deltaY : e.deltaX;
			if (dd) self.setParam(id, clamp(lever.index + (dd > 0 ? 1 : -1), 0, n - 1), true);
		}, { passive: false });
		node.addEventListener('keydown', function (e) {
			if (e.key === 'ArrowUp' || e.key === 'ArrowRight') setIndex(lever.index + 1);
			else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') setIndex(lever.index - 1);
			else if (e.key === ' ' || e.key === 'Enter') setIndex(n === 2 ? 1 - lever.index : lever.index + 1);
			else return;
			e.preventDefault();
		});
	};

	Player.prototype.buildToggle = function (panel) {
		var self = this, c = CTL.power, half = c[4] / 0.6;
		var node = el('div', 'de-ctl de-toggle', panel);
		place(node, c[2], c[3], half);
		shadow(node, 0.22 * 2.36, 0.12, 0.5);
		img(this.img('toggle_nut.webp'), 'de-sprite de-full', node);
		var batShadow = img(this.img('toggle_bat.webp'), 'de-sprite de-full de-batshadow', node);
		var bat = img(this.img('toggle_bat.webp'), 'de-sprite de-full', node);
		node.tabIndex = 0;
		node.setAttribute('role', 'switch');
		node.setAttribute('aria-label', 'Power');
		var t = this.toggle = { shown: this.values.power ? 1 : -1, on: !!this.values.power };
		t.render = function () {
			var sy = Math.abs(t.shown) < 0.12 ? (t.shown < 0 ? -0.12 : 0.12) : t.shown;
			var tr = 'scale(1,' + (sy * (sy < 0 ? 0.82 : 1)) + ')';
			bat.style.transform = tr;
			batShadow.style.transform = 'translate(2px,3px) ' + tr;
			batShadow.style.opacity = sy < 0 ? 0.35 : 0;
		};
		this.on('power', function (v) { t.on = !!v; node.setAttribute('aria-checked', v ? 'true' : 'false'); });
		t.render();
		var flip = function () { self.setParam('power', self.values.power ? 0 : 1); };
		node.addEventListener('pointerdown', function (e) { if (e.button === 0) { e.preventDefault(); flip(); } });
		node.addEventListener('keydown', function (e) { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); flip(); } });
		this.controls.power = node;
	};

	Player.prototype.buildLamp = function (panel, id, cls) {
		var c = CTL[id], half = c[4] * 1.6;
		var node = el('div', 'de-lamp' + (cls ? ' ' + cls : ''), panel);
		place(node, c[2], c[3], half);
		var glow = el('div', 'de-glow', node);
		var w = 100 / 1.6;
		shadow(node, w * 0.5 * 2.36 / 100, w * 0.05 / 100, 0.45);
		var off = img(this.img('lamp_off.webp'), 'de-sprite', node);
		var on = img(this.img('lamp_on.webp'), 'de-sprite', node);
		[off, on].forEach(function (i) { i.style.width = i.style.height = w + '%'; i.style.left = i.style.top = (50 - w / 2) + '%'; });
		var lamp = { node: node, target: 0, b: 0, glow: glow, on: on };
		lamp.render = function () { lamp.on.style.opacity = lamp.b; lamp.glow.style.opacity = lamp.b; };
		lamp.render();
		(this.lamps = this.lamps || []).push(lamp);
		return lamp;
	};

	Player.prototype.buildLampButton = function (panel, id, momentary) {
		var self = this;
		var lamp = this.buildLamp(panel, id, 'de-ctl de-lampbtn');
		var node = lamp.node;
		node.tabIndex = 0;
		node.setAttribute('role', 'button');
		node.setAttribute('aria-label', E.PARAMS[id].name);
		this.on(id, function (v) { lamp.target = v ? 1 : 0; node.setAttribute('aria-pressed', v ? 'true' : 'false'); });
		var down = function () {
			node.classList.add('is-pressed');
			if (momentary) {
				self.setParam(id, 1);
				if (id === 'kick') self.sendKick();
			} else self.setParam(id, self.values[id] ? 0 : 1);
		};
		var upFn = function () {
			node.classList.remove('is-pressed');
			if (momentary) self.setParam(id, 0);
		};
		var active = null;
		node.addEventListener('pointerdown', function (e) {
			if (e.button !== 0) return;
			e.preventDefault();
			active = e.pointerId;
			try { node.setPointerCapture(e.pointerId); } catch (err) { /* ignore */ }
			down();
		});
		var release = function (e) { if (active === null || e.pointerId !== active) return; active = null; upFn(); };
		node.addEventListener('pointerup', release);
		node.addEventListener('pointercancel', release);
		node.addEventListener('keydown', function (e) { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat) { e.preventDefault(); down(); } });
		node.addEventListener('keyup', function (e) { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); upFn(); } });
		node.title = E.PARAMS[id].name;
		this.controls[id] = node;
		return lamp;
	};

	Player.prototype.buildJacks = function (panel) {
		var self = this, m = CTL.jackMic, l = CTL.jackLine;
		[['jackMic', 0, 'MIC'], ['jackLine', 1, 'LINE']].forEach(function (j) {
			var c = CTL[j[0]];
			var node = el('div', 'de-ctl de-jack', panel);
			place(node, c[2], c[3], c[4] * 1.4);
			node.tabIndex = 0;
			node.setAttribute('role', 'radio');
			node.setAttribute('aria-label', 'Input ' + j[2]);
			node.title = 'Input: ' + j[2];
			el('div', 'de-plug', node);
			self.on('inputMode', function (v) {
				node.classList.toggle('is-active', v === j[1]);
				node.setAttribute('aria-checked', v === j[1] ? 'true' : 'false');
			});
			var pick = function () { self.setParam('inputMode', j[1], true); };
			node.addEventListener('pointerdown', function (e) { if (e.button === 0) { e.preventDefault(); pick(); } });
			node.addEventListener('keydown', function (e) { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); pick(); } });
		});
		void m; void l;
	};

	Player.prototype.buildReel = function (panel, id, sprite, startAngle) {
		var c = CTL[id], half = c[4] * 1.06;
		var node = el('div', 'de-reel', panel);
		place(node, c[2], c[3], half);
		shadow(node, 2.36 / 2.12, 0.03 / 2.12, 0.6);
		var im = img(this.img(sprite), 'de-sprite', node);
		var size = (2 / REEL_FILL) / 2.12 * 100;
		im.style.width = im.style.height = size + '%';
		im.style.left = im.style.top = (50 - size / 2) + '%';
		var reel = { im: im, angle: startAngle };
		reel.render = function () { im.style.transform = 'rotate(' + reel.angle + 'rad)'; };
		reel.render();
		return reel;
	};

	Player.prototype.buildTape = function (panel) {
		var node = el('canvas', 'de-tape', panel);
		node.style.left = pct(TAPE_BOX[0], BW);
		node.style.top = pct(TAPE_BOX[1], BH);
		node.style.width = pct(TAPE_BOX[2] - TAPE_BOX[0], BW);
		node.style.height = pct(TAPE_BOX[3] - TAPE_BOX[1], BH);
		var mask = 'url("' + this.img('tape_mask.png') + '")';
		node.style.webkitMaskImage = mask; node.style.maskImage = mask;
		this.tapeCanvas = node;
		this.tapeOffset = 0;
		var path = TAPE_PATH.map(function (p) { return [p[0] - TAPE_BOX[0], p[1] - TAPE_BOX[1]]; });
		var cum = [0];
		for (var i = 1; i < path.length; ++i) cum.push(cum[i - 1] + Math.hypot(path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]));
		this.tapePath = path; this.tapeCum = cum;
	};

	Player.prototype.sizeTapeCanvas = function () {
		var c = this.tapeCanvas;
		if (!c) return;
		var dpr = Math.min(2, window.devicePixelRatio || 1);
		var w = Math.max(1, Math.round(c.clientWidth * dpr)), h = Math.max(1, Math.round(c.clientHeight * dpr));
		if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
		this.drawTape();
	};

	// streaks are fixed on the tape (hashed by index) and travel along the path with it (gui::TapeRun)
	Player.prototype.drawTape = function () {
		var c = this.tapeCanvas, g = c.getContext('2d');
		if (!g) return;
		var s = c.width / (TAPE_BOX[2] - TAPE_BOX[0]);
		var path = this.tapePath, cum = this.tapeCum, total = cum[cum.length - 1], offset = this.tapeOffset;
		g.clearRect(0, 0, c.width, c.height);
		var pointAt = function (t, out) {
			var i = 1;
			while (i < cum.length - 1 && cum[i] < t) ++i;
			var segLen = Math.max(0.001, cum[i] - cum[i - 1]);
			var dx = (path[i][0] - path[i - 1][0]) / segLen, dy = (path[i][1] - path[i - 1][1]) / segLen;
			var u = clamp(t - cum[i - 1], 0, segLen);
			out[0] = path[i - 1][0] + dx * u; out[1] = path[i - 1][1] + dy * u; out[2] = dx; out[3] = dy;
		};
		var spacing = 23, pt = [0, 0, 0, 0];
		var j0 = Math.floor((-offset - 40) / spacing), j1 = Math.ceil((total - offset) / spacing);
		g.lineCap = 'butt';
		g.lineWidth = Math.max(1, TAPE_W * 0.28 * s);
		for (var j = j0; j <= j1; ++j) {
			var h = Math.imul(j, 2654435761) >>> 0;
			var r1 = ((h >>> 8) & 255) / 255, r2 = ((h >>> 16) & 255) / 255;
			var t = j * spacing + r1 * 14 + offset;
			var len = 6 + r2 * 20;
			if (t + len < 0 || t > total) continue;
			pointAt(Math.max(0, t), pt);
			var light = (h & 3) !== 0;
			g.strokeStyle = light ? 'rgba(224,180,140,' + (0.10 + 0.16 * r1).toFixed(3) + ')' : 'rgba(0,0,0,' + (0.18 + 0.15 * r2).toFixed(3) + ')';
			var across = (r2 - 0.5) * TAPE_W * 0.5, nx = -pt[3], ny = pt[2];
			var x0 = pt[0] + nx * across, y0 = pt[1] + ny * across;
			g.beginPath();
			g.moveTo(x0 * s, y0 * s);
			g.lineTo((x0 + pt[2] * len) * s, (y0 + pt[3] * len) * s);
			g.stroke();
		}
	};

	Player.prototype.buildVU = function (panel) {
		var self = this, c = CTL.vu;
		var node = el('div', 'de-vu', panel);
		node.style.left = pct(c[2], BW); node.style.top = pct(c[3], BH);
		node.style.width = pct(c[5], BW); node.style.height = pct(c[6], BH);
		var NS = 'http://www.w3.org/2000/svg';
		var svg = document.createElementNS(NS, 'svg');
		svg.setAttribute('viewBox', c[2] + ' ' + c[3] + ' ' + c[5] + ' ' + c[6]);
		svg.setAttribute('preserveAspectRatio', 'none');
		node.appendChild(svg);
		var mk = function (stroke, w, op) {
			var l = document.createElementNS(NS, 'line');
			l.setAttribute('stroke', stroke); l.setAttribute('stroke-width', w); l.setAttribute('stroke-linecap', 'round');
			if (op) l.setAttribute('stroke-opacity', op);
			svg.appendChild(l);
			return l;
		};
		var sh = mk('#000', c[5] * 0.008, 0.18);
		var needle = mk('#15120e', c[5] * 0.0055);
		var label = el('div', 'de-vu-label', node);
		node.title = this.t('vuTip', 'VU-Meter: Klick wechselt IN / ECHO / OUT');
		node.addEventListener('click', function () { self.vuSource = (self.vuSource + 1) % 3; });
		var vu = this.vu = { level: -60, needle: -60, label: label };
		vu.render = function () {
			var a = self.vuAngle(vu.needle) * Math.PI / 180;
			var px = VU_PIVOT[0], py = VU_PIVOT[1], L = VU_NEEDLE;
			var tx = px + Math.sin(a) * L, ty = py - Math.cos(a) * L;
			var bx = px + Math.sin(a) * L * 0.22, by = py - Math.cos(a) * L * 0.22;
			needle.setAttribute('x1', bx); needle.setAttribute('y1', by); needle.setAttribute('x2', tx); needle.setAttribute('y2', ty);
			sh.setAttribute('x1', bx + 3); sh.setAttribute('y1', by + 4); sh.setAttribute('x2', tx + 3); sh.setAttribute('y2', ty + 4);
		};
		vu.render();
	};

	Player.prototype.vuAngle = function (db) {
		var n = VU_DB.length;
		if (db <= VU_DB[0]) return VU_DEG[0] - 6 * clamp((VU_DB[0] - db) / 20, 0, 1);
		for (var i = 1; i < n; ++i)
			if (db <= VU_DB[i]) {
				var t = (db - VU_DB[i - 1]) / (VU_DB[i] - VU_DB[i - 1]);
				return VU_DEG[i - 1] + t * (VU_DEG[i] - VU_DEG[i - 1]);
			}
		return VU_DEG[n - 1] + 3;
	};

	Player.prototype.buildReadout = function (panel, id) {
		var c = CTL[id];
		var node = el('div', 'de-readout', panel);
		place(node, c[2], c[3], c[5] / 2, c[6] / 2);
		return node;
	};

	Player.prototype.buildHelp = function (panel) {
		var self = this;
		var ov = this.help = el('div', 'de-help', panel);
		ov.setAttribute('role', 'dialog');
		ov.setAttribute('aria-label', 'Kurzanleitung');
		ov.tabIndex = -1;
		var inner = el('div', 'de-help-inner', ov);
		el('h3', '', inner).textContent = (this.s.title || 'DUB ECHO') + ' – KURZANLEITUNG';
		HELP_TEXT.forEach(function (sec) {
			el('h4', '', inner).textContent = sec[0];
			sec[1].forEach(function (line) { el('p', '', inner).textContent = line; });
		});
		el('p', 'de-help-close', inner).textContent = this.t('helpClose', 'Klick schließt diese Hilfe.');
		ov.addEventListener('click', function () { self.toggleHelp(false); });
		ov.addEventListener('keydown', function (e) { if (e.key === 'Escape') self.toggleHelp(false); });
	};

	Player.prototype.toggleHelp = function (show) {
		if (show === undefined) show = !this.help.classList.contains('is-visible');
		this.help.classList.toggle('is-visible', show);
		if (show) this.help.focus({ preventScroll: true });
	};

	// ------------------------------------------------------------------ presets
	Player.prototype.presetIndexByName = function (name) {
		if (name === undefined || name === null || name === '') return 0;
		for (var i = 0; i < E.PRESETS.length; ++i) if (E.PRESETS[i].name === name || String(i) === String(name)) return i;
		return 0;
	};

	Player.prototype.applyPreset = function (i, silent) {
		var p = E.PRESETS[clamp(i, 0, E.PRESETS.length - 1)];
		var d = E.defaults();
		for (var id in d) {
			if (E.NON_PRESET[id]) continue;
			this.setParam(id, p.v[id] !== undefined ? p.v[id] : d[id]);
		}
		this.presetIndex = i;
		if (this.presetSelect) {
			this.presetEdited.hidden = true;
			this.presetSelect.value = i;
		}
		if (!silent) this.say(this.t('presetLoaded', 'Preset:') + ' ' + p.name);
	};

	Player.prototype.markPresetEdited = function () {
		if (!this.presetSelect || this.presetSelect.value === '-1') return;
		var p = E.PRESETS[this.presetIndex];
		this.presetEdited.textContent = (p ? p.name : 'Preset') + ' *';
		this.presetEdited.hidden = false;
		this.presetSelect.value = -1;
	};

	Player.prototype.stepPreset = function (d) {
		var n = E.PRESETS.length;
		this.applyPreset(((this.presetIndex || 0) + d + n) % n);
	};

	// ------------------------------------------------------------------ audio
	Player.prototype.audioEl = function () { return this.audio && this.audio.el; };
	Player.prototype.isPlaying = function () { var a = this.audioEl(); return !!(a && !a.paused && !a.ended); };

	Player.prototype.ensureAudio = function () {
		if (this.audio) return this.audio.ready;
		var self = this;
		var a = this.audio = { el: new Audio(), ctx: null, node: null, engine: null };
		a.el.preload = 'auto';
		a.el.addEventListener('timeupdate', function () { self.updateTime(); });
		a.el.addEventListener('durationchange', function () { self.updateTime(); });
		a.el.addEventListener('play', function () { self.updatePlayButton(); Player.pauseOthers(self); });
		a.el.addEventListener('pause', function () { self.updatePlayButton(); });
		a.el.addEventListener('playing', function () { self.countPlay(); });
		a.el.addEventListener('ended', function () {
			self.updatePlayButton();
			if (self.s.loopTrack) { a.el.currentTime = 0; a.el.play(); }
			else if (self.s.autoNext && self.tracks.length > 1) self.stepTrack(1, true);
		});
		a.el.addEventListener('error', function () {
			if (a.el.getAttribute('src')) self.say(self.t('loadError', 'Die Datei konnte nicht geladen werden.'), true);
		});
		if (this.pendingSrc) this.setSource(this.pendingSrc);

		var AC = window.AudioContext || window.webkitAudioContext;
		if (!AC) { a.ready = Promise.resolve(); return a.ready; }
		try { a.ctx = new AC({ latencyHint: 'interactive' }); } catch (e) { a.ctx = new AC(); }
		var ctx = a.ctx;
		var connect = function (node) {
			var src = ctx.createMediaElementSource(a.el);
			src.connect(node);
			node.connect(ctx.destination);
			a.node = node;
			self.paramsDirty = true;
		};
		var fallback = function () {
			// no AudioWorklet (old browser or page without HTTPS): run the engine on the main thread
			var eng = a.engine = new E.TapeEchoEngine();
			eng.prepare(ctx.sampleRate);
			eng.setParams(E.mapParams(self.values, self.bpm()));
			eng.reset();
			var sp = ctx.createScriptProcessor(2048, 2, 2);
			sp.onaudioprocess = function (ev) {
				var ib = ev.inputBuffer, ob = ev.outputBuffer;
				var inL = ib.getChannelData(0), inR = ib.numberOfChannels > 1 ? ib.getChannelData(1) : inL;
				eng.process(inL, inR, ob.getChannelData(0), ob.getChannelData(1), ob.length);
				self.meters.i = eng.meterIn; self.meters.o = eng.meterOut; self.meters.e = eng.meterEcho;
			};
			connect(sp);
		};
		if (ctx.audioWorklet && window.AudioWorkletNode) {
			a.ready = ctx.audioWorklet.addModule(this.cfg.workletUrl).then(function () {
				var node = new AudioWorkletNode(ctx, 'dubasco-tape-echo', {
					numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
					channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers'
				});
				node.port.onmessage = function (ev) {
					var d = ev.data;
					if (d && d.type === 'meters') { self.meters.i = d.i; self.meters.o = d.o; self.meters.e = d.e; }
				};
				node.port.postMessage({ type: 'params', params: E.mapParams(self.values, self.bpm()) });
				node.port.postMessage({ type: 'reset' });
				connect(node);
			}).catch(function (err) {
				if (window.console) console.warn('Dub Echo: AudioWorklet failed, using fallback', err);
				fallback();
			});
		} else {
			fallback();
			a.ready = Promise.resolve();
		}
		return a.ready;
	};

	Player.prototype.pushParams = function () {
		if (!this.paramsDirty || !this.audio) return;
		this.paramsDirty = false;
		var p = E.mapParams(this.values, this.bpm());
		if (this.audio.node && this.audio.node.port) this.audio.node.port.postMessage({ type: 'params', params: p });
		else if (this.audio.engine) this.audio.engine.setParams(p);
	};

	Player.prototype.sendKick = function () {
		if (!this.audio) return;
		if (this.audio.node && this.audio.node.port) this.audio.node.port.postMessage({ type: 'kick' });
		else if (this.audio.engine) this.audio.engine.kick();
	};

	Player.prototype.setSource = function (url) {
		if (!this.audio) { this.pendingSrc = url; return; }
		this.pendingSrc = null;
		var a = this.audio.el;
		try {
			var u = new URL(url, window.location.href);
			if (u.origin !== window.location.origin) a.crossOrigin = 'anonymous';
			else a.removeAttribute('crossorigin');
		} catch (e) { /* relative URL */ }
		a.src = url;
		a.load();
	};

	Player.prototype.selectTrack = function (i, autoplay) {
		if (!this.tracks.length) return;
		i = clamp(i, 0, this.tracks.length - 1);
		var changed = i !== this.trackIndex;
		this.trackIndex = i;
		this.trackSelect.value = i;
		var tr = this.tracks[i];
		if (changed) {
			this.setSource(tr.url);
			if (tr.preset !== undefined && tr.preset !== null && tr.preset !== '') this.applyPreset(this.presetIndexByName(tr.preset), true);
			this.paramsDirty = true;   // tempo sync follows the track's BPM
			this.updateLike();
			this.updateTime();
		}
		if (autoplay) this.play();
	};

	Player.prototype.stepTrack = function (d, autoplay) {
		if (!this.tracks.length) return;
		var n = this.tracks.length;
		this.selectTrack(((this.trackIndex + d) % n + n) % n, autoplay === undefined ? this.isPlaying() : autoplay);
	};

	Player.prototype.play = function () {
		var self = this;
		if (!this.tracks.length) return;
		var ready = this.ensureAudio();
		var ctx = this.audio.ctx;
		if (ctx && ctx.state === 'suspended') ctx.resume();
		// play() must be called within the click; the graph is connected as soon as the worklet is loaded
		var p = this.audio.el.play();
		if (p && p.catch) p.catch(function (err) {
			if (err && err.name !== 'AbortError') self.say(self.t('playError', 'Wiedergabe nicht möglich.'), true);
		});
		ready.then(function () { self.pushParams(); });
	};

	Player.prototype.togglePlay = function () {
		if (this.isPlaying()) this.audio.el.pause();
		else this.play();
	};

	Player.pauseOthers = function (current) {
		(Player.all || []).forEach(function (p) { if (p !== current && p.isPlaying()) p.audio.el.pause(); });
	};

	Player.prototype.updatePlayButton = function () {
		var playing = this.isPlaying();
		this.playBtn.innerHTML = playing
			? '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="5" width="4" height="14"/><rect x="14" y="5" width="4" height="14"/></svg>'
			: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 4.5v15l12-7.5z"/></svg>';
		var label = playing ? this.t('pause', 'Pause') : this.t('play', 'Abspielen');
		this.playBtn.title = label;
		this.playBtn.setAttribute('aria-label', label);
		this.playBtn.classList.toggle('is-on', playing);
		var none = !this.tracks.length;
		this.playBtn.disabled = none; this.prevBtn.disabled = none || this.tracks.length < 2; this.nextBtn.disabled = this.prevBtn.disabled;
	};

	Player.prototype.updateTime = function () {
		var a = this.audioEl();
		var d = a && isFinite(a.duration) ? a.duration : (this.tracks[this.trackIndex] ? this.tracks[this.trackIndex].length || 0 : 0);
		var t = a ? a.currentTime : 0;
		this.timeNow.textContent = fmtTime(t);
		this.timeTotal.textContent = fmtTime(d);
		if (document.activeElement !== this.seekInput) this.seekInput.value = d > 0 ? Math.round(t / d * 1000) : 0;
	};

	Player.prototype.say = function (msg, isError) {
		var s = this.status;
		s.textContent = msg;
		s.classList.toggle('is-error', !!isError);
		s.classList.add('is-visible');
		clearTimeout(this.sayTimer);
		this.sayTimer = setTimeout(function () { s.classList.remove('is-visible'); }, isError ? 6000 : 2200);
	};

	// ------------------------------------------------------------------ likes and plays
	Player.visitor = function () {
		var v = storageGet('dubecho_visitor');
		if (!v || !/^[a-f0-9]{32}$/.test(v)) {
			var a = new Uint8Array(16);
			(window.crypto || window.msCrypto).getRandomValues(a);
			v = Array.prototype.map.call(a, function (b) { return ('0' + b.toString(16)).slice(-2); }).join('');
			storageSet('dubecho_visitor', v);
		}
		return v;
	};

	Player.prototype.loadLikes = function () {
		try { this.liked = JSON.parse(storageGet('dubecho_liked') || '{}') || {}; } catch (e) { this.liked = {}; }
		this.updateLike();
	};

	Player.prototype.api = function (path, method, body) {
		var url = this.cfg.restUrl + path;
		var opt = { method: method || 'GET', credentials: 'omit', headers: { 'Accept': 'application/json' } };
		if (body) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
		return fetch(url, opt).then(function (r) {
			return r.json().then(function (j) { if (!r.ok) throw j; return j; });
		});
	};

	// fresh counters (the page itself may come from a cache)
	Player.prototype.refreshStats = function () {
		var self = this;
		if (!this.tracks.length || !this.cfg.restUrl || !window.fetch) return;
		var ids = this.tracks.map(function (t) { return t.id; }).join(',');
		var sep = this.cfg.restUrl.indexOf('?') >= 0 ? '&' : '?';
		this.api('tracks' + sep + 'ids=' + ids + '&visitor=' + Player.visitor()).then(function (res) {
			(res.tracks || []).forEach(function (r) {
				self.tracks.forEach(function (t) {
					if (String(t.id) === String(r.id)) {
						t.likes = r.likes;
						if (r.liked !== undefined) self.liked[t.id] = !!r.liked;
					}
				});
			});
			self.saveLiked();
			self.fillTrackSelect();
			self.updateLike();
		}).catch(function () { /* counters are optional */ });
	};

	Player.prototype.saveLiked = function () {
		var o = {};
		for (var k in this.liked) if (this.liked[k]) o[k] = 1;
		storageSet('dubecho_liked', JSON.stringify(o));
	};

	Player.prototype.updateLike = function () {
		if (!this.likeBtn) return;
		var tr = this.tracks[this.trackIndex];
		var on = !!(tr && this.liked[tr.id]);
		this.likeBtn.classList.toggle('is-on', on);
		this.likeBtn.disabled = !tr;
		this.likeBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
		var label = on ? this.t('unlike', 'Gefällt mir nicht mehr') : this.t('like', 'Gefällt mir');
		this.likeBtn.title = label;
		this.likeBtn.setAttribute('aria-label', label + (tr ? ': ' + tr.title : ''));
		this.likeCount.textContent = tr && this.s.showCounts ? (tr.likes || 0) : '';
	};

	Player.prototype.toggleLike = function () {
		var self = this, tr = this.tracks[this.trackIndex];
		if (!tr || this.likeBusy) return;
		var want = !this.liked[tr.id];
		// optimistic update
		this.liked[tr.id] = want;
		tr.likes = Math.max(0, (tr.likes || 0) + (want ? 1 : -1));
		this.updateLike();
		this.likeBtn.classList.remove('is-pop');
		void this.likeBtn.offsetWidth;
		if (want) this.likeBtn.classList.add('is-pop');
		this.likeBusy = true;
		this.api('like', 'POST', { track: tr.id, visitor: Player.visitor(), like: want }).then(function (r) {
			tr.likes = r.likes;
			self.liked[tr.id] = !!r.liked;
			if (want && r.liked) self.say(self.t('thanks', 'Danke für dein Like!'));
		}).catch(function (err) {
			self.liked[tr.id] = !want;
			tr.likes = Math.max(0, (tr.likes || 0) + (want ? -1 : 1));
			self.say((err && err.message) || self.t('likeError', 'Das Like konnte nicht gespeichert werden.'), true);
		}).then(function () {
			self.likeBusy = false;
			self.saveLiked();
			self.updateLike();
			self.fillTrackSelect();
		});
	};

	Player.prototype.countPlay = function () {
		var tr = this.tracks[this.trackIndex];
		if (!tr || !this.s.countPlays || !this.cfg.restUrl || !window.fetch) return;
		this.counted = this.counted || {};
		if (this.counted[tr.id]) return;
		this.counted[tr.id] = true;
		this.api('play', 'POST', { track: tr.id, visitor: Player.visitor() }).catch(function () { /* optional */ });
	};

	// ------------------------------------------------------------------ animation (DubascoEditor::tick)
	Player.prototype.startLoop = function () {
		var self = this;
		var frame = function (ts) {
			window.requestAnimationFrame(frame);
			self.tick(ts / 1000);
		};
		window.requestAnimationFrame(frame);
	};

	Player.prototype.tick = function (ts) {
		var dt = this.lastTick > 0 ? clamp(ts - this.lastTick, 0, 0.1) : 1 / 60;
		this.lastTick = ts;
		this.pushParams();
		if (!this.visible || document.hidden) return;
		var v = this.values;

		// reels: the GUI runs its own copy of the motor model, like the plugin editor
		var on = v.power > 0.5;
		var delaySec = v.sync > 0.5 ? E.SYNC_BEATS[v.syncNote] * 60 / clamp(this.bpm(), 30, 300) : v.delayTime * 0.001;
		var targetLog = Math.log(0.25 / clamp(delaySec, 0.025, 2));
		var tau = 0.02 * Math.pow(100, v.inertia / 10);
		if (this.motorLog === 0) this.motorLog = targetLog;
		this.motorLog += (targetLog - this.motorLog) * (1 - Math.exp(-dt / tau));
		this.motorRamp = clamp(this.motorRamp + (on ? dt / 0.45 : -dt / 1.1), 0, 1);
		var speed = Math.exp(this.motorLog) * this.motorRamp;
		var wf = v.wowFlutter / 10;
		var flutter = 0.06 * wf * Math.sin(ts * 4.1) + 0.03 * wf * Math.sin(ts * 13.7);
		if (speed > 0.0001) {
			var twoPi = Math.PI * 2;
			this.reelL.angle = (this.reelL.angle + dt * speed * 0.75 * twoPi * (1 + flutter)) % twoPi;
			this.reelR.angle = (this.reelR.angle + dt * speed * 0.75 * twoPi * (1 - flutter * 0.5)) % twoPi;
			this.reelL.render(); this.reelR.render();
			this.tapeOffset = (this.tapeOffset + dt * speed * (1 + flutter) * 140) % 1.0e6;
			this.drawTape();
		}

		// VU ballistics
		var src = this.vuSource, m = this.meters;
		var rms = src === 0 ? m.i : (src === 1 ? m.e : m.o);
		if (!this.isPlaying() && !(this.audio && this.audio.ctx && this.audio.ctx.state === 'running')) rms = 0;
		var db = (rms * 1.41 > 0.001 ? 20 * Math.log10(rms * 1.41) : -60) + 6;
		var vu = this.vu;
		vu.level += (db - vu.level) * (1 - Math.exp(-dt / 0.065));
		var prev = vu.needle;
		vu.needle += (vu.level - vu.needle) * (1 - Math.exp(-dt / 0.09));
		if (Math.abs(vu.needle - prev) > 0.01) vu.render();
		var lbl = src === 0 ? 'IN' : (src === 1 ? 'ECHO' : 'OUT');
		if (vu.label.textContent !== lbl) vu.label.textContent = lbl;

		// lamps
		this.powerLamp.target = on ? 1 : 0;
		var mh = E.modeHeads(v.mode + 1);
		var echoAct = clamp(m.e * 6, 0, 1);
		var hs = [mh.h1, mh.h2, mh.h3];
		for (var i = 0; i < 3; ++i) this.headLamps[i].target = hs[i] && on ? 0.45 + 0.55 * echoAct : 0;
		this.headLamps[3].target = mh.rev && on ? 0.85 : 0;
		var lamps = this.lamps;
		for (i = 0; i < lamps.length; ++i) {
			var l = lamps[i];
			if (Math.abs(l.target - l.b) > 0.002) {
				var rate = l.target > l.b ? 30 : 9;   // incandescent: fast on, slow off
				l.b += (l.target - l.b) * (1 - Math.exp(-dt * rate));
				l.render();
			}
		}
		for (i = 0; i < this.levers.length; ++i) {
			var lv = this.levers[i], target = lv.angles[lv.index];
			if (Math.abs(target - lv.shown) > 0.05) { lv.shown += (target - lv.shown) * (1 - Math.exp(-dt * 22)); lv.render(); }
			else if (lv.shown !== target) { lv.shown = target; lv.render(); }
		}
		var tg = this.toggle, tt = tg.on ? 1 : -1;
		if (Math.abs(tt - tg.shown) > 0.002) { tg.shown += (tt - tg.shown) * (1 - Math.exp(-dt * 26)); tg.render(); }

		this.readoutTimer += dt;
		if (this.readoutTimer > 0.08) {
			this.readoutTimer = 0;
			var ms = speed > 0.0001 ? 250 / speed : 0;
			var txt = speed < 0.01 ? 'MOTOR STOPPED' : speed.toFixed(2) + 'x  |  ' + Math.round(ms) + ' ms';
			if (this.tapeReadout.textContent !== txt) this.tapeReadout.textContent = txt;
			var nt = v.sync > 0.5 ? E.SYNC_NOTES[v.syncNote] : 'FREE';
			if (this.noteReadout.textContent !== nt) this.noteReadout.textContent = nt;
			this.noteReadout.title = v.sync > 0.5 ? Math.round(this.bpm()) + ' BPM' : '';
		}
	};

	// ================================================================== boot
	function boot() {
		var nodes = document.querySelectorAll('.dubecho[data-config]:not(.dubecho--ready)');
		Player.all = Player.all || [];
		Array.prototype.forEach.call(nodes, function (node) {
			var cfg;
			try { cfg = JSON.parse(node.getAttribute('data-config')); } catch (e) { return; }
			Player.all.push(new Player(node, cfg));
		});
	}
	window.DubEchoPlayer = { boot: boot, Player: Player };
	if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
	else boot();
})();
