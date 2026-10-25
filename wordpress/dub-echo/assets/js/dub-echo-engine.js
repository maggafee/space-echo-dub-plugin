/*
 * Dubasco Tape Echo - browser engine.
 *
 * Line-by-line port of Source/dsp (TapeEchoEngine, SpringReverb, Filters) from the
 * VST3/AU plugin, so the web version sounds like the plugin. Runs inside an
 * AudioWorklet (registers "dubasco-tape-echo"), or on the main thread as a
 * ScriptProcessor fallback (exposes window.DubascoEngine).
 *
 * Tape model: the tape loop is a buffer indexed by tape position, not by time.
 * The record head writes at the current position, the three playback heads read
 * at fixed distances behind it (1 : 2 : 3). Changing the repeat rate changes the
 * tape speed, so echoes pitch like on the real machine.
 */
(function (root) {
	'use strict';

	var kPi = Math.PI;
	var fr = Math.fround;

	function flush(x) { return (x < 1.0e-20 && x > -1.0e-20) ? 0 : x; }
	function clamp(x, lo, hi) { return x < lo ? lo : (x > hi ? hi : x); }
	function nextPow2(n) { var p = 1; while (p < n) p <<= 1; return p; }

	// ------------------------------------------------------------------ filters
	function Biquad() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
	Biquad.prototype.reset = function () { this.z1 = 0; this.z2 = 0; };
	Biquad.prototype.process = function (x) {
		var y = this.b0 * x + this.z1;
		this.z1 = flush(this.b1 * x - this.a1 * y + this.z2);
		this.z2 = flush(this.b2 * x - this.a2 * y);
		return y;
	};
	Biquad.prototype.set = function (nb0, nb1, nb2, na0, na1, na2) {
		this.b0 = nb0 / na0; this.b1 = nb1 / na0; this.b2 = nb2 / na0;
		this.a1 = na1 / na0; this.a2 = na2 / na0;
	};
	Biquad.prototype.copyCoeffs = function (o) { this.b0 = o.b0; this.b1 = o.b1; this.b2 = o.b2; this.a1 = o.a1; this.a2 = o.a2; };
	function clampF(f, sr) { return clamp(f, 10.0, sr * 0.49); }
	Biquad.prototype.lowPass = function (sr, f, q) {
		if (q === undefined) q = 0.7071;
		var w = 2 * kPi * clampF(f, sr) / sr, c = Math.cos(w), al = Math.sin(w) / (2 * q);
		this.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
	};
	Biquad.prototype.highPass = function (sr, f, q) {
		if (q === undefined) q = 0.7071;
		var w = 2 * kPi * clampF(f, sr) / sr, c = Math.cos(w), al = Math.sin(w) / (2 * q);
		this.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
	};
	Biquad.prototype.peak = function (sr, f, q, dB) {
		var A = Math.pow(10, dB / 40), w = 2 * kPi * clampF(f, sr) / sr, c = Math.cos(w), al = Math.sin(w) / (2 * q);
		this.set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
	};
	Biquad.prototype.lowShelf = function (sr, f, dB, S) {
		if (S === undefined) S = 0.8;
		var A = Math.pow(10, dB / 40), w = 2 * kPi * clampF(f, sr) / sr, c = Math.cos(w), s = Math.sin(w);
		var al = s / 2 * Math.sqrt((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * Math.sqrt(A) * al;
		this.set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
			(A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
	};
	Biquad.prototype.highShelf = function (sr, f, dB, S) {
		if (S === undefined) S = 0.8;
		var A = Math.pow(10, dB / 40), w = 2 * kPi * clampF(f, sr) / sr, c = Math.cos(w), s = Math.sin(w);
		var al = s / 2 * Math.sqrt((A + 1 / A) * (1 / S - 1) + 2), sa = 2 * Math.sqrt(A) * al;
		this.set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
			(A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
	};

	function OnePoleLP() { this.a = 0; this.z = 0; }
	OnePoleLP.prototype.setCutoff = function (sr, f) { this.a = Math.exp(-2 * kPi * clamp(f, 1.0, sr * 0.49) / sr); };
	OnePoleLP.prototype.reset = function () { this.z = 0; };
	OnePoleLP.prototype.process = function (x) { this.z = flush(x + this.a * (this.z - x)); return this.z; };

	function OnePoleHP() { this.lp = new OnePoleLP(); }
	OnePoleHP.prototype.setCutoff = function (sr, f) { this.lp.setCutoff(sr, f); };
	OnePoleHP.prototype.reset = function () { this.lp.reset(); };
	OnePoleHP.prototype.process = function (x) { return x - this.lp.process(x); };

	// sine of a normalised phase (0..1), parabolic approximation
	function fastSin01(phase) {
		var x = phase - Math.floor(phase);
		x = x * 2 - 1;
		var ax = x < 0 ? -x : x;
		var y = 4 * x * (1 - ax);
		var ay = y < 0 ? -y : y;
		return -(0.225 * (y * ay - y) + y);
	}

	function Smoother() { this.coeff = 0; this.value = 0; this.target = 0; }
	Smoother.prototype.setTime = function (sr, seconds) { this.coeff = seconds <= 0 ? 0 : Math.exp(-1 / (seconds * sr)); };
	Smoother.prototype.snap = function (v) { this.value = v; this.target = v; };
	Smoother.prototype.next = function () { this.value = this.target + this.coeff * (this.value - this.target); return this.value; };

	// xorshift32, identical sequence to the C++ version
	function Noise(seed) { this.s = (seed | 0) || 1; }
	Noise.prototype.white = function () {
		var s = this.s;
		s ^= s << 13; s ^= s >>> 17; s ^= s << 5;
		this.s = s | 0;
		return this.s / 2147483648;
	};
	Noise.prototype.uni = function () { return 0.5 * (this.white() + 1); };

	function fastTanh(x) {
		if (x > 4.97) return 1;
		if (x < -4.97) return -1;
		var x2 = x * x;
		var a = x * (135135 + x2 * (17325 + x2 * (378 + x2)));
		var b = 135135 + x2 * (62370 + x2 * (3150 + x2 * 28));
		return clamp(a / b, -1, 1);
	}

	function tapeSaturate(x, drive, tb) {
		return (fastTanh(drive * (x + 0.08)) - tb) / (drive * (1 - tb * tb));
	}

	function softClipOut(x) {
		var a = x < 0 ? -x : x;
		if (a < 0.9) return x;
		var y = 0.9 + 0.1 * Math.tanh((a - 0.9) * 10);
		return x < 0 ? -y : y;
	}

	function panL(p) { return Math.cos((p + 1) * 0.25 * kPi) * 1.41421356; }
	function panR(p) { return Math.sin((p + 1) * 0.25 * kPi) * 1.41421356; }

	// ------------------------------------------------------------------ spring reverb
	var kSprings = 3, kChirpStages = 36, kChirpA = 0.62;

	function SpringReverb() {
		this.sr = 44100; this.K = 5;
		this.springs = [];
		this.diff = [[], []];
		this.inHP = new OnePoleHP(); this.inLP = new Biquad(); this.outHP = new Biquad(); this.outHP2 = new Biquad();
		this.noise = new Noise(0xBADC0FFE | 0);
		this.kickEnv = 0; this.kickPhase = 0; this.kickDec = 0;
		this.o = new Float64Array(3);
	}

	SpringReverb.prototype.prepare = function (sampleRate) {
		var sr = this.sr = sampleRate;
		var K = this.K = Math.max(1, Math.round(sr / (2 * 4400)));
		var lengthsMs = [31.3, 37.1, 41.9];
		this.springs = [];
		for (var s = 0; s < kSprings; ++s) {
			var baseDelay = Math.fround(lengthsMs[s] * 0.001 * sr);
			var lineLen = nextPow2(Math.floor(Math.fround(baseDelay * 1.1)) + 8);
			var apLen = nextPow2(K + 1);
			var sp = {
				baseDelay: baseDelay,
				line: new Float32Array(lineLen), mask: lineLen - 1, w: 0,
				// all 36 stretched all-passes advance in lockstep, so they share one write index
				chirp: new Float32Array(apLen * kChirpStages), apLen: apLen, apMask: apLen - 1, apIdx: 0,
				loopLP: new Biquad(), loopHP: new OnePoleHP(),
				lfoPhase: 0.31 * s, lfoInc: (0.37 + 0.13 * s) / sr,
				fb: 0, g: 0.7
			};
			sp.loopLP.lowPass(sr, 4300, 0.6);
			sp.loopHP.setCutoff(sr, 70);
			this.springs.push(sp);
		}
		var diffMs = [4.7, 7.3];
		this.diff = [[], []];
		for (var i = 0; i < 2; ++i) {
			for (var ch = 0; ch < 2; ++ch) {
				var len = Math.floor(diffMs[i] * (ch ? 1.13 : 1.0) * 0.001 * sr);
				var size = nextPow2(len + 1);
				this.diff[ch][i] = { buf: new Float32Array(size), mask: size - 1, idx: 0, len: len };
			}
		}
		this.inHP.setCutoff(sr, 180);
		this.inLP.lowPass(sr, 5200, 0.7);
		this.outHP.highPass(sr, 90, 0.7);
		this.outHP2.highPass(sr, 90, 0.7);
		this.kickEnv = 0; this.kickPhase = 0;
		this.kickDec = Math.exp(-1 / (0.012 * sr));
		this.setDecay(0.5);
	};

	// 0..1 -> roughly 1.2 s .. 5 s RT60
	SpringReverb.prototype.setDecay = function (d01) {
		var rt60 = 1.2 + 3.8 * clamp(d01, 0, 1);
		for (var s = 0; s < this.springs.length; ++s) {
			var sp = this.springs[s];
			sp.g = Math.pow(10, -3 * (sp.baseDelay / this.sr) / rt60);
		}
	};

	SpringReverb.prototype.kick = function () { this.kickEnv = 1; this.kickPhase = 0; };

	SpringReverb.prototype.reset = function () {
		for (var s = 0; s < this.springs.length; ++s) {
			var sp = this.springs[s];
			sp.line.fill(0); sp.chirp.fill(0); sp.fb = 0; sp.loopLP.reset(); sp.loopHP.reset();
		}
		for (var c = 0; c < 2; ++c) for (var i = 0; i < this.diff[c].length; ++i) this.diff[c][i].buf.fill(0);
		this.inLP.reset(); this.outHP.reset(); this.outHP2.reset(); this.inHP.reset();
	};

	function schroeder(d, x, g) {
		var dl = d.buf[(d.idx - d.len) & d.mask];
		var w = x + g * dl;
		d.buf[d.idx] = flush(w);
		d.idx = (d.idx + 1) & d.mask;
		return dl - g * w;
	}

	// mono in, stereo out written to out[0], out[1]
	SpringReverb.prototype.process = function (input, out) {
		var x = this.inLP.process(this.inHP.process(input));
		if (this.kickEnv > 1.0e-4) {
			// knock on the tank: noisy click plus a low thump
			this.kickPhase += 2 * kPi * 55 / this.sr;
			x += this.kickEnv * (0.9 * this.noise.white() + 0.8 * Math.sin(this.kickPhase));
			this.kickEnv *= this.kickDec;
		}
		var K = this.K, o = this.o;
		for (var s = 0; s < kSprings; ++s) {
			var sp = this.springs[s];
			var v = x * 0.6 + sp.fb;
			var buf = sp.chirp, apLen = sp.apLen, m = sp.apMask, idx = sp.apIdx, rd = (idx - K) & m;
			for (var st = 0, base = 0; st < kChirpStages; ++st, base += apLen) {
				var wd = buf[base + rd];
				var w = v - kChirpA * wd;
				buf[base + idx] = flush(w);
				v = kChirpA * w + wd;
			}
			sp.apIdx = (idx + 1) & m;
			sp.line[sp.w] = v;
			sp.lfoPhase += sp.lfoInc; if (sp.lfoPhase >= 1) sp.lfoPhase -= 1;
			var d = sp.baseDelay * (1 + 0.0025 * fastSin01(sp.lfoPhase));
			var rp = sp.w - d;
			if (rp < 0) rp += sp.line.length;
			var i0 = Math.floor(rp), fr = rp - i0;
			var a = sp.line[i0 & sp.mask], b = sp.line[(i0 + 1) & sp.mask];
			var y = a + fr * (b - a);
			sp.w = (sp.w + 1) & sp.mask;
			y = sp.loopHP.process(sp.loopLP.process(y));
			sp.fb = flush(y * sp.g);
			o[s] = y;
		}
		var l = o[0] - 0.55 * o[2] + 0.35 * o[1];
		var r = o[1] + 0.55 * o[2] - 0.35 * o[0];
		l = schroeder(this.diff[0][0], l, 0.6); l = schroeder(this.diff[0][1], l, 0.6);
		r = schroeder(this.diff[1][0], r, 0.6); r = schroeder(this.diff[1][1], r, 0.6);
		out[0] = this.outHP.process(l) * 0.7;
		out[1] = this.outHP2.process(r) * 0.7;
	};

	// ------------------------------------------------------------------ tape
	function Tape() { this.buf = null; this.mask = 0; this.P = 0; this.prevIn = 0; }
	Tape.prototype.init = function (size) { this.buf = new Float32Array(size); this.mask = size - 1; this.P = size; this.prevIn = 0; };
	Tape.prototype.clear = function () { this.buf.fill(0); this.prevIn = 0; };
	Tape.prototype.write = function (x, v) {
		var P0 = this.P, P1 = this.P + v, buf = this.buf, mask = this.mask, prev = this.prevIn;
		if (v > 1.0e-9) {
			var inv = 1 / v, end = Math.floor(P1);
			for (var j = Math.floor(P0) + 1; j <= end; ++j)
				buf[j & mask] = prev + (x - prev) * ((j - P0) * inv);
		}
		this.P = P1;
		var size = mask + 1;
		if (this.P > size * 4096) this.P -= size * 4096 - size * 8;   // subtract a multiple of the size
		this.prevIn = x;
	};
	Tape.prototype.read = function (dist) {
		var pos = this.P - dist, fl = Math.floor(pos), t = pos - fl, buf = this.buf, m = this.mask;
		var xm1 = buf[(fl - 1) & m], x0 = buf[fl & m], x1 = buf[(fl + 1) & m], x2 = buf[(fl + 2) & m];
		// 4-point Hermite
		var c1 = 0.5 * (x1 - xm1);
		var c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2;
		var c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
		return ((c3 * t + c2) * t + c1) * t + x0;
	};

	function ToneChain() { this.bq = []; for (var i = 0; i < 8; ++i) this.bq.push(new Biquad()); }
	ToneChain.prototype.reset = function () { for (var i = 0; i < 8; ++i) this.bq[i].reset(); };
	ToneChain.prototype.process = function (x) {
		var b = this.bq;
		x = b[0].process(x); x = b[1].process(x); x = b[2].process(x); x = b[3].process(x);
		x = b[4].process(x); x = b[5].process(x); x = b[6].process(x); return b[7].process(x);
	};
	ToneChain.prototype.copyCoeffs = function (o) { for (var i = 0; i < 8; ++i) this.bq[i].copyCoeffs(o.bq[i]); };

	function Channel() {
		this.tape = new Tape();
		this.recLP = new Biquad(); this.preEmph = new Biquad();
		this.loop = new ToneChain();
		this.fbIn = 0; this.echo = 0;
		this.dropGain = 1; this.dropTarget = 1; this.dropCount = 0;
		this.flutPhase = 0; this.flut2Phase = 0; this.drift = 0; this.driftTarget = 0;
		this.noise = new Noise(1);
	}

	// ------------------------------------------------------------------ engine
	var kRefDelay = 0.25, kMaxDelay = 2.0, kMinDelay = 0.025;
	var MODE_TABLE = [
		[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 1, 1, 0],
		[1, 0, 0, 1], [0, 1, 0, 1], [0, 0, 1, 1], [1, 1, 0, 1],
		[0, 1, 1, 1], [1, 0, 1, 1], [1, 1, 1, 1], [0, 0, 0, 1]];

	function defaultEngineParams() {
		return {
			inputGain: 1, micMode: false, delaySec: 0.3, feedback: 0.45, bassDb: 0, trebleDb: 0,
			echoGain: 0.7, tapeSat: 0.4, mode: 5, reverbGain: 0.3, springDecay: 0.5, kick: false,
			wowFlutter: 0.3, tapeAge: 0.3, inertia: 0.4, stereoMode: 1, lowCutHz: 60, duck: 0,
			dryGain: 1, outGain: 1, throwOn: false, hold: false, throwOnly: false, power: true, bypass: false
		};
	}

	function TapeEchoEngine() {
		this.params = defaultEngineParams();
		this.sr = 44100; this.subBlock = 32; this.D1 = 11025;
		this.ch = [new Channel(), new Channel()];
		this.outChainL = new ToneChain(); this.outChainR = new ToneChain();
		this.spring = new SpringReverb();
		this.logV = 0; this.motorV = 1; this.powerSpeed = 1; this.wowPhase = 0;
		var S = function () { return new Smoother(); };
		this.sendS = S(); this.fbS = S(); this.echoS = S(); this.revS = S(); this.dryS = S(); this.outS = S();
		this.bypassS = S(); this.powerWetS = S(); this.revWidthS = S(); this.duckS = S(); this.inGainS = S();
		this.headG = [S(), S(), S()];
		this.cAL = S(); this.cAR = S(); this.cBL = S(); this.cBR = S(); this.selfFb = S(); this.crossFb = S();
		this.wLA = [S(), S(), S()]; this.wRA = [S(), S(), S()]; this.wLB = [S(), S(), S()]; this.wRB = [S(), S(), S()];
		this.duckEnv = 0; this.duckAtk = 0; this.duckRel = 0;
		this.meterInAcc = 0; this.meterOutAcc = 0; this.meterEchoAcc = 0; this.meterCoeff = 0;
		this.lastKick = false; this.pendingKick = false;
		this.hissLevel = 0; this.dropRate = 0; this.wfDepth = 0;
		this.cBass = 99; this.cTreble = 99; this.cAge = -1; this.cLowCut = -1; this.cDecay = -1; this.lastRecFc = -1;
		// GUI state
		this.meterIn = 0; this.meterOut = 0; this.meterEcho = 0; this.reelSpeed = 0; this.headDelayMs = 0;
		this._heads = new Float64Array(6);
		this._g3 = new Float64Array(3);
		this._w = new Float64Array(12);
		this._rev = new Float64Array(2);
	}

	TapeEchoEngine.modeHeads = function (mode) {
		var m = clamp(Math.round(mode), 1, 12) - 1, t = MODE_TABLE[m];
		return { h1: !!t[0], h2: !!t[1], h3: !!t[2], rev: !!t[3] };
	};

	TapeEchoEngine.prototype.setParams = function (p) {
		for (var k in p) if (Object.prototype.hasOwnProperty.call(p, k)) this.params[k] = p[k];
	};

	// trigger a spring kick even if the button press was shorter than one audio block
	TapeEchoEngine.prototype.kick = function () { this.pendingKick = true; };

	TapeEchoEngine.prototype.prepare = function (sampleRate) {
		var sr = this.sr = sampleRate;
		this.D1 = kRefDelay * sr;
		var tapeLen = nextPow2(Math.floor(3 * this.D1) + 256);
		var seed = 0x9E3779B9 >>> 0;
		for (var i = 0; i < 2; ++i) {
			var c = this.ch[i];
			c.tape.init(tapeLen);
			c.noise = new Noise(seed | 0);
			seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
			c.flutPhase = c.noise.uni(); c.flut2Phase = c.noise.uni();
		}
		this.spring.prepare(sr);
		var fast = 0.02, self = this;
		[this.sendS, this.echoS, this.revS, this.dryS, this.outS, this.inGainS, this.cAL, this.cAR, this.cBL, this.cBR, this.selfFb, this.crossFb]
			.forEach(function (s) { s.setTime(sr, fast); });
		[this.wLA, this.wRA, this.wLB, this.wRB, this.headG].forEach(function (arr) { arr.forEach(function (s) { s.setTime(sr, 0.03); }); });
		this.fbS.setTime(sr, 0.05);
		this.bypassS.setTime(sr, 0.015);
		this.powerWetS.setTime(sr, 0.15);
		this.revWidthS.setTime(sr, 0.05);
		this.duckS.setTime(sr, 0.05);
		this.duckAtk = 1 - Math.exp(-1 / (0.008 * sr));
		this.duckRel = 1 - Math.exp(-1 / (0.30 * sr));
		this.meterCoeff = 1 - Math.exp(-1 / (0.05 * sr));
		self.reset();
	};

	TapeEchoEngine.prototype.reset = function () {
		var p = this.params;
		for (var i = 0; i < 2; ++i) {
			var c = this.ch[i];
			c.tape.clear(); c.recLP.reset(); c.preEmph.reset(); c.loop.reset();
			c.fbIn = 0; c.echo = 0; c.dropGain = 1; c.dropTarget = 1; c.dropCount = 0; c.drift = 0; c.driftTarget = 0;
		}
		this.outChainL.reset(); this.outChainR.reset();
		this.spring.reset();
		var v = kRefDelay / clamp(p.delaySec, kMinDelay, kMaxDelay);
		this.logV = Math.log(v);
		this.motorV = v;
		this.powerSpeed = p.power ? 1 : 0;
		this.sendS.snap(1); this.fbS.snap(0); this.echoS.snap(p.echoGain); this.revS.snap(p.reverbGain);
		this.dryS.snap(p.dryGain); this.outS.snap(p.outGain); this.bypassS.snap(p.bypass ? 1 : 0);
		this.powerWetS.snap(p.power ? 1 : 0); this.revWidthS.snap(1); this.duckS.snap(p.duck); this.inGainS.snap(p.inputGain);
		for (i = 0; i < 3; ++i) this.headG[i].snap(0);
		this.cAL.snap(0.5); this.cAR.snap(0.5); this.cBL.snap(0); this.cBR.snap(0); this.selfFb.snap(1); this.crossFb.snap(0);
		[this.wLA, this.wRA, this.wLB, this.wRB].forEach(function (arr) { arr.forEach(function (s) { s.snap(0); }); });
		this.duckEnv = 0;
		this.cBass = 99; this.cTreble = 99; this.cAge = -1; this.cLowCut = -1; this.cDecay = -1; this.lastRecFc = -1;
	};

	TapeEchoEngine.prototype.updateCoefficients = function () {
		var p = this.params, sr = this.sr;
		var toneChanged = p.bassDb !== this.cBass || p.trebleDb !== this.cTreble || p.tapeAge !== this.cAge || p.lowCutHz !== this.cLowCut;
		if (toneChanged) {
			this.cBass = p.bassDb; this.cTreble = p.trebleDb; this.cAge = p.tapeAge; this.cLowCut = p.lowCutHz;
			var c = this.ch[0].loop.bq;
			c[0].highShelf(sr, 2500, -5, 0.7);                       // playback de-emphasis
			c[1].highPass(sr, 38, 0.6);                              // head low end roll-off
			c[2].peak(sr, 95, 0.9, 2.5);                             // head bump
			c[3].peak(sr, 1300, 0.6, 1.2);                           // mid "honk" of the RE-201 preamps
			var ageFc = 7200 * Math.pow(0.38, p.tapeAge);            // new 7.2k -> worn ~2.7k
			c[4].lowPass(sr, ageFc, 0.62);
			c[5].lowShelf(sr, 160, p.bassDb, 0.7);
			c[6].highShelf(sr, 3000, p.trebleDb, 0.7);
			c[7].highPass(sr, p.lowCutHz, 0.65);
			this.ch[1].loop.copyCoeffs(this.ch[0].loop);
			this.outChainL.copyCoeffs(this.ch[0].loop);
			this.outChainR.copyCoeffs(this.ch[0].loop);
			for (var i = 0; i < 2; ++i) this.ch[i].preEmph.highShelf(sr, 2500, 5, 0.7);
			this.hissLevel = Math.pow(10, (-84 + 26 * p.tapeAge) / 20);
			this.dropRate = p.tapeAge > 0.35 ? (p.tapeAge - 0.35) * 1.6 : 0;   // dropouts per second
		}
		if (p.springDecay !== this.cDecay) { this.cDecay = p.springDecay; this.spring.setDecay(p.springDecay); }
	};

	TapeEchoEngine.prototype.wowFlutterMod = function (c, wowPhaseOffset) {
		var W = this.wfDepth;
		if (W <= 0) return 0;
		// phases are accumulated in single precision like in the plugin, so the modulation stays in step with it
		c.flutPhase = fr(c.flutPhase + fr(6.8 / this.sr)); if (c.flutPhase >= 1) c.flutPhase = fr(c.flutPhase - 1);     // capstan
		c.flut2Phase = fr(c.flut2Phase + fr(13.9 / this.sr)); if (c.flut2Phase >= 1) c.flut2Phase = fr(c.flut2Phase - 1);  // pinch roller
		var wow = fastSin01(this.wowPhase + wowPhaseOffset);
		var fl = fastSin01(c.flutPhase) * (0.75 + 0.25 * c.drift) + 0.35 * fastSin01(c.flut2Phase);
		return W * (0.0040 * wow * 0.7 + 0.0022 * c.drift + 0.0011 * fl);
	};

	// inR may be the same array as inL; out arrays may alias the inputs
	TapeEchoEngine.prototype.process = function (inL, inR, outL, outR, numSamples) {
		var p = this.params, sr = this.sr, i, k;
		this.updateCoefficients();

		// ---- block level targets
		var t = MODE_TABLE[clamp(Math.round(p.mode), 1, 12) - 1];
		var h0 = !!t[0], h1 = !!t[1], h2 = !!t[2], revOn = !!t[3];
		var nActive = (h0 ? 1 : 0) + (h1 ? 1 : 0) + (h2 ? 1 : 0);
		var hg = nActive > 0 ? Math.pow(nActive, -0.75) : 0;
		this.headG[0].target = h0 ? hg : 0; this.headG[1].target = h1 ? hg : 0; this.headG[2].target = h2 ? hg : 0;

		var fbT = 1.10 * Math.pow(clamp(p.feedback, 0, 1), 1.15);
		if (p.hold) fbT = Math.max(fbT, 1.01);
		if (p.throwOn && !p.throwOnly) fbT = Math.max(fbT, 1.12);   // THROW in ALL mode = feedback swell
		this.fbS.target = fbT;

		var send = p.throwOnly ? (p.throwOn ? 1 : 0) : 1;
		if (p.hold || !p.power) send = 0;
		this.sendS.target = send;
		this.echoS.target = p.echoGain * (nActive > 0 ? 1 : 0);
		this.revS.target = revOn ? p.reverbGain : 0;
		this.dryS.target = p.dryGain;
		this.outS.target = p.outGain;
		this.inGainS.target = p.inputGain * (p.micMode ? 2.8 : 1);
		this.bypassS.target = p.bypass ? 1 : 0;
		this.duckS.target = p.duck;

		// stereo routing
		var pl = [panL(-0.75), panL(0.75), panL(0)], pr = [panR(-0.75), panR(0.75), panR(0)];
		var sm = clamp(Math.round(p.stereoMode), 0, 3);
		this.cAL.target = sm === 2 ? 1 : 0.5; this.cAR.target = sm === 2 ? 0 : 0.5;
		this.cBL.target = 0; this.cBR.target = sm === 2 ? 1 : 0;
		this.selfFb.target = sm === 3 ? 0 : 1;
		this.crossFb.target = sm === 3 ? 1 : 0;
		this.revWidthS.target = sm === 0 ? 0 : 1;
		for (i = 0; i < 3; ++i) {
			var g = this.headG[i].target;
			this.wLA[i].target = sm === 1 ? g * pl[i] : g;
			this.wRA[i].target = sm === 1 ? g * pr[i] : (sm <= 1 ? g : 0);
			this.wLB[i].target = 0;
			this.wRB[i].target = sm >= 2 ? g : 0;
		}

		if ((p.kick && !this.lastKick) || this.pendingKick) this.spring.kick();
		this.lastKick = p.kick; this.pendingKick = false;

		var vTarget = kRefDelay / clamp(p.delaySec, kMinDelay, kMaxDelay);
		var logVT = Math.log(vTarget);
		var tau = 0.02 * Math.pow(100, clamp(p.inertia, 0, 1));
		var kV = 1 - Math.exp(-1 / (tau * sr));
		var powerStep = fr(p.power ? 1 / (0.45 * sr) : -1 / (1.1 * sr));
		var satDrive = 0.55 + 2.6 * p.tapeSat;
		var satBias = fastTanh(satDrive * 0.08);
		var preDrive = p.micMode ? 1.5 : 0.6;
		var wowInc = fr(0.55 / sr);
		this.wfDepth = Math.pow(clamp(p.wowFlutter, 0, 1), 1.5) * 2.5 + p.tapeAge * 0.25;

		var ch0 = this.ch[0], ch1 = this.ch[1], chs = this.ch;
		var heads = this._heads, g3 = this._g3, W = this._w, rev = this._rev;
		var D1 = this.D1, hiss = this.hissLevel, dropRate = this.dropRate;
		var duckAtk = this.duckAtk, duckRel = this.duckRel, mc = this.meterCoeff;

		for (var start = 0; start < numSamples; start += this.subBlock) {
			var n = Math.min(this.subBlock, numSamples - start);

			// per sub-block: drift targets, record LP tracking the tape speed
			for (i = 0; i < 2; ++i) {
				var cc = chs[i];
				if (cc.noise.uni() < 0.02) cc.driftTarget = cc.noise.white();
				cc.drift += (cc.driftTarget - cc.drift) * 0.02;
			}
			var vNow0 = Math.exp(this.logV) * this.powerSpeed;
			var recFc = clamp(0.42 * sr * Math.max(vNow0, 0.05), 1500, Math.min(16000, 0.45 * sr));
			if (Math.abs(recFc - this.lastRecFc) > 1) {
				this.lastRecFc = recFc;
				ch0.recLP.lowPass(sr, recFc, 0.6); ch1.recLP.lowPass(sr, recFc, 0.6);
			}

			for (k = 0; k < n; ++k) {
				var idx = start + k;
				var xL = inL[idx], xR = inR[idx];

				// ducking envelope
				var e = Math.max(Math.abs(xL), Math.abs(xR));
				this.duckEnv += (e > this.duckEnv ? duckAtk : duckRel) * (e - this.duckEnv);

				// motor
				if (Math.abs(logVT - this.logV) > 1.0e-9) { this.logV += (logVT - this.logV) * kV; this.motorV = Math.exp(this.logV); }
				this.powerSpeed = clamp(fr(this.powerSpeed + powerStep), 0, 1);   // float, as in the plugin (sets the tape-stop length)
				var motor = this.motorV * this.powerSpeed;
				this.wowPhase = fr(this.wowPhase + wowInc); if (this.wowPhase >= 1) this.wowPhase = fr(this.wowPhase - 1);

				var sendG = this.sendS.next();
				var inG = this.inGainS.next();
				var fbG = this.fbS.next();
				var selfG = this.selfFb.next(), crossG = this.crossFb.next();
				var aL = this.cAL.next(), aR = this.cAR.next(), bL = this.cBL.next(), bR = this.cBR.next();
				for (i = 0; i < 3; ++i) g3[i] = this.headG[i].next();
				for (i = 0; i < 3; ++i) {
					W[i] = this.wLA[i].next(); W[3 + i] = this.wRA[i].next();
					W[6 + i] = this.wLB[i].next(); W[9 + i] = this.wRB[i].next();
				}

				// input preamp (both tape channels)
				var srcA = (aL * xL + aR * xR) * inG;
				var srcB = (bL * xL + bR * xR) * inG;
				var pre0 = fastTanh(preDrive * srcA) / preDrive;
				var pre1 = fastTanh(preDrive * srcB) / preDrive;

				for (var c = 0; c < 2; ++c) {
					var C = chs[c];
					// record path
					var rec = (c ? pre1 : pre0) * sendG + C.fbIn + C.noise.white() * hiss;
					rec = C.recLP.process(rec);
					rec = C.preEmph.process(rec);
					rec = tapeSaturate(rec, satDrive, satBias);

					var v = motor * (1 + this.wowFlutterMod(C, c ? 0.23 : 0));
					C.tape.write(rec, v > 0 ? v : 0);

					// dropouts on worn tape
					if (C.dropCount > 0) { if (--C.dropCount === 0) C.dropTarget = 1; }
					else if (dropRate > 0 && C.noise.uni() < dropRate / sr) {
						C.dropTarget = 0.25 + 0.55 * C.noise.uni();
						C.dropCount = Math.floor((0.02 + 0.12 * C.noise.uni()) * sr);
					}
					C.dropGain += (C.dropTarget - C.dropGain) * 0.004;

					var o = c * 3;
					heads[o] = C.tape.read(D1) * C.dropGain;
					heads[o + 1] = C.tape.read(D1 * 2) * C.dropGain;
					heads[o + 2] = C.tape.read(D1 * 3) * C.dropGain;

					var loopIn = g3[0] * heads[o] + g3[1] * heads[o + 1] + g3[2] * heads[o + 2];
					C.echo = C.loop.process(loopIn);
				}
				// feedback routing (self / ping-pong)
				ch0.fbIn = fbG * (selfG * ch0.echo + crossG * ch1.echo);
				ch1.fbIn = fbG * (selfG * ch1.echo + crossG * ch0.echo);

				var oL = 0, oR = 0;
				for (i = 0; i < 3; ++i) {
					oL += W[i] * heads[i] + W[6 + i] * heads[3 + i];
					oR += W[3 + i] * heads[i] + W[9 + i] * heads[3 + i];
				}
				oL = this.outChainL.process(oL);
				oR = this.outChainR.process(oR);

				// spring reverb gets the input and part of the echoes
				this.spring.process((pre0 + pre1) * sendG * 0.8 + (oL + oR) * 0.35, rev);
				var rw = this.revWidthS.next();
				var rL = rev[0], rR = rev[1], rm = 0.5 * (rL + rR);
				rL = rm + rw * (rL - rm); rR = rm + rw * (rR - rm);

				var powerWet = this.powerWetS.next();
				this.powerWetS.target = (p.power || this.powerSpeed > 0) ? 1 : 0;
				var duckG = 1 - 0.92 * this.duckS.next() * Math.min(1, this.duckEnv * 4);
				var eg = this.echoS.next(), rg = this.revS.next();
				var wetL = (oL * eg + rL * rg) * duckG * powerWet;
				var wetR = (oR * eg + rR * rg) * duckG * powerWet;

				var dg = this.dryS.next(), og = this.outS.next();
				var yL = softClipOut((xL * dg + wetL) * og);
				var yR = softClipOut((xR * dg + wetR) * og);
				var bp = this.bypassS.next();
				var fL = yL + bp * (xL - yL), fR = yR + bp * (xR - yR);
				outL[idx] = fL;
				outR[idx] = fR;

				this.meterEchoAcc += mc * (0.5 * (oL * oL + oR * oR) * eg * eg - this.meterEchoAcc);
				this.meterOutAcc += mc * (0.5 * (fL * fL + fR * fR) - this.meterOutAcc);
				this.meterInAcc += mc * ((pre0 * pre0 + pre1 * pre1) - this.meterInAcc);
			}
		}

		this.meterIn = Math.sqrt(this.meterInAcc);
		this.meterOut = Math.sqrt(this.meterOutAcc);
		this.meterEcho = Math.sqrt(this.meterEchoAcc);
		var vNow = Math.exp(this.logV) * this.powerSpeed;
		this.reelSpeed = vNow;
		this.headDelayMs = vNow > 1.0e-4 ? 1000 * kRefDelay / vNow : 0;
	};

	// ------------------------------------------------------------------ parameters
	// Same parameter set, ranges, defaults and value mapping as Source/Parameters.cpp and
	// DubascoProcessor::collectParams().
	var SYNC_NOTES = ['1/32', '1/16T', '1/16', '1/16D', '1/8T', '1/8', '1/8D', '1/4T', '1/4', '1/4D', '1/2T', '1/2', '1/2D', '1/1'];
	var SYNC_BEATS = [0.125, 1 / 6, 0.25, 0.375, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 4 / 3, 2, 3, 4];

	var PARAMS = {
		input:       { name: 'Input', min: 0, max: 10, def: 5 },
		inputMode:   { name: 'Input Jack', choices: ['Mic', 'Line'], def: 1 },
		delayTime:   { name: 'Delay Time', min: 50, max: 600, def: 320, log: true, unit: 'ms' },
		feedback:    { name: 'Feedback', min: 0, max: 10, def: 4.5 },
		bass:        { name: 'Bass', min: -5, max: 5, def: 0, signed: true },
		treble:      { name: 'Treble', min: -5, max: 5, def: 0, signed: true },
		echoLevel:   { name: 'Echo Level', min: 0, max: 10, def: 6 },
		tapeSat:     { name: 'Tape Saturation', min: 0, max: 10, def: 4 },
		power:       { name: 'Power', bool: true, def: 1 },
		mode:        { name: 'Echo Mode', choices: ['Mode 1', 'Mode 2', 'Mode 3', 'Mode 4', 'Mode 5', 'Mode 6', 'Mode 7', 'Mode 8', 'Mode 9', 'Mode 10', 'Mode 11', 'Mode 12'], def: 4 },
		reverb:      { name: 'Reverb Level', min: 0, max: 10, def: 3.5 },
		springDecay: { name: 'Spring Decay', min: 0, max: 10, def: 5 },
		kick:        { name: 'Spring Kick', bool: true, def: 0 },
		wowFlutter:  { name: 'Wow & Flutter', min: 0, max: 10, def: 3 },
		tapeAge:     { name: 'Tape Age', min: 0, max: 10, def: 3 },
		inertia:     { name: 'Motor Inertia', min: 0, max: 10, def: 4 },
		sync:        { name: 'Tempo Sync', bool: true, def: 0 },
		syncNote:    { name: 'Sync Note', choices: SYNC_NOTES, def: 6 },
		stereoMode:  { name: 'Stereo Mode', choices: ['Mono', 'Wide', 'Stereo', 'Ping-Pong'], def: 1 },
		lowCut:      { name: 'Low Cut', min: 20, max: 500, def: 60, log: true, unit: 'Hz' },
		duck:        { name: 'Ducking', min: 0, max: 10, def: 0 },
		dry:         { name: 'Dry Level', min: 0, max: 10, def: 10 },
		output:      { name: 'Output', min: -24, max: 12, def: 0, skew: Math.log(0.5) / Math.log(24 / 36), unit: 'dB', signed: true },
		'throw':     { name: 'Dub Throw', bool: true, def: 0 },
		hold:        { name: 'Hold (Infinite)', bool: true, def: 0 },
		sendMode:    { name: 'Send Mode', choices: ['All', 'Throw Only'], def: 0 },
		bypass:      { name: 'Bypass', bool: true, def: 0 }
	};

	function toNorm(id, v) {
		var d = PARAMS[id];
		if (d.choices) return v / (d.choices.length - 1);
		if (d.bool) return v ? 1 : 0;
		if (d.log) return Math.log(v / d.min) / Math.log(d.max / d.min);
		var n = (v - d.min) / (d.max - d.min);
		return d.skew ? Math.pow(clamp(n, 0, 1), d.skew) : n;
	}
	function fromNorm(id, n) {
		var d = PARAMS[id];
		n = clamp(n, 0, 1);
		if (d.choices) return Math.round(n * (d.choices.length - 1));
		if (d.bool) return n > 0.5 ? 1 : 0;
		if (d.log) return d.min * Math.pow(d.max / d.min, n);
		if (d.skew) n = Math.pow(n, 1 / d.skew);
		return d.min + n * (d.max - d.min);
	}
	function valueText(id, v) {
		var d = PARAMS[id];
		if (d.choices) return d.choices[Math.round(v)];
		if (d.bool) return v ? 'On' : 'Off';
		if (d.unit === 'ms' || d.unit === 'Hz') return Math.round(v) + ' ' + d.unit;
		var s = (d.signed && v > 0.04 ? '+' : '') + v.toFixed(1);
		return d.unit ? s + ' ' + d.unit : s;
	}
	function defaults() { var o = {}; for (var k in PARAMS) o[k] = PARAMS[k].def; return o; }

	// DubascoProcessor::collectParams
	function mapParams(u, bpm) {
		var e = {};
		var inp = u.input / 5; e.inputGain = inp * inp;              // 5 = unity, 10 = +12 dB
		e.micMode = Math.round(u.inputMode) === 0;
		if (u.sync > 0.5) e.delaySec = SYNC_BEATS[clamp(Math.round(u.syncNote), 0, 13)] * 60 / clamp(bpm || 120, 30, 300);
		else e.delaySec = u.delayTime * 0.001;
		e.feedback = u.feedback / 10;
		e.bassDb = u.bass * 2; e.trebleDb = u.treble * 2;
		var el = u.echoLevel / 10; e.echoGain = el * el * 1.6;
		e.tapeSat = u.tapeSat / 10;
		e.power = u.power > 0.5;
		e.mode = Math.round(u.mode) + 1;
		var rv = u.reverb / 10; e.reverbGain = rv * rv * 1.4;
		e.springDecay = u.springDecay / 10;
		e.kick = u.kick > 0.5;
		e.wowFlutter = u.wowFlutter / 10;
		e.tapeAge = u.tapeAge / 10;
		e.inertia = u.inertia / 10;
		e.stereoMode = Math.round(u.stereoMode);
		e.lowCutHz = u.lowCut;
		e.duck = u.duck / 10;
		var dr = u.dry / 10; e.dryGain = dr * dr;
		e.outGain = Math.pow(10, u.output / 20);
		e.throwOn = u['throw'] > 0.5;
		e.hold = u.hold > 0.5;
		e.throwOnly = Math.round(u.sendMode) === 1;
		e.bypass = u.bypass > 0.5;
		return e;
	}

	// Factory presets of the plugin (Source/PresetManager.cpp). Unlisted values = defaults.
	var PRESETS = [
		{ name: 'Init - Classic Space', v: {} },
		{ name: 'Kingston Skank Echo', v: { sync: 1, syncNote: 6, mode: 4, feedback: 5.6, reverb: 4.0, bass: -1.0, treble: 1.0, stereoMode: 1 } },
		{ name: 'Roots Rockers Quarter', v: { sync: 1, syncNote: 8, mode: 0, feedback: 5.0, reverb: 2.5, tapeAge: 4.0, stereoMode: 0 } },
		{ name: 'Steppers Ping-Pong', v: { sync: 1, syncNote: 6, mode: 0, feedback: 5.8, stereoMode: 3, reverb: 2.0, lowCut: 120 } },
		{ name: 'Spiral Runaway', v: { delayTime: 260, mode: 3, feedback: 7.9, tapeSat: 7.0, wowFlutter: 4.0, inertia: 6.5, reverb: 3.0, treble: -1.5 } },
		{ name: 'Hazy Ark Session', v: { sync: 1, syncNote: 9, mode: 8, feedback: 5.2, tapeAge: 7.0, wowFlutter: 5.0, treble: -2.0, tapeSat: 6.0, reverb: 5.0, springDecay: 6.5 } },
		{ name: 'UK Sound System', v: { sync: 1, syncNote: 9, mode: 7, feedback: 6.0, bass: 2.0, lowCut: 35, duck: 3.0, reverb: 3.0, stereoMode: 1 } },
		{ name: 'Sci-Fi Space Sweep', v: { delayTime: 120, mode: 10, feedback: 6.4, reverb: 6.0, wowFlutter: 6.0, inertia: 7.5, stereoMode: 1, treble: 2.0 } },
		{ name: 'Throw The Snare', v: { sync: 1, syncNote: 6, mode: 4, feedback: 5.5, sendMode: 1, reverb: 4.5, stereoMode: 1 } },
		{ name: 'Vocal Dub Tail', v: { sync: 1, syncNote: 9, mode: 5, feedback: 5.8, duck: 5.5, lowCut: 150, treble: -1.0, reverb: 3.5 } },
		{ name: 'Melodica Chamber', v: { sync: 1, syncNote: 7, mode: 9, feedback: 4.8, reverb: 5.5, springDecay: 7.0, stereoMode: 1 } },
		{ name: 'Bass Heavy Echo', v: { sync: 1, syncNote: 8, mode: 1, feedback: 5.0, bass: 3.0, treble: -2.5, lowCut: 20, stereoMode: 0, reverb: 1.5 } },
		{ name: 'Worn Out Tape', v: { delayTime: 340, mode: 0, feedback: 5.5, tapeAge: 9.0, wowFlutter: 7.0, tapeSat: 7.5, stereoMode: 0 } },
		{ name: 'Clean Modern Tape', v: { delayTime: 375, mode: 7, feedback: 4.0, tapeAge: 0.0, wowFlutter: 0.5, tapeSat: 1.5, stereoMode: 2 } },
		{ name: 'Slapback 70s', v: { delayTime: 95, mode: 0, feedback: 1.5, reverb: 0.0, echoLevel: 7.0, stereoMode: 0 } },
		{ name: 'Spring Tank Only', v: { mode: 11, reverb: 7.0, springDecay: 7.0 } }
	];
	// performance switches are not part of presets
	var NON_PRESET = { power: 1, bypass: 1, kick: 1, 'throw': 1, hold: 1 };

	var api = {
		TapeEchoEngine: TapeEchoEngine,
		PARAMS: PARAMS, PRESETS: PRESETS, NON_PRESET: NON_PRESET,
		SYNC_NOTES: SYNC_NOTES, SYNC_BEATS: SYNC_BEATS,
		toNorm: toNorm, fromNorm: fromNorm, valueText: valueText, defaults: defaults, mapParams: mapParams,
		modeHeads: TapeEchoEngine.modeHeads
	};

	// ------------------------------------------------------------------ AudioWorklet
	if (typeof registerProcessor === 'function' && typeof AudioWorkletProcessor === 'function') {
		class DubascoProcessor extends AudioWorkletProcessor {
			constructor(options) {
				super(options);
				var self = this;
				this.engine = new TapeEchoEngine();
				this.engine.prepare(sampleRate);
				this.zero = new Float32Array(128);
				this.meterTimer = 0;
				this.port.onmessage = function (ev) {
					var d = ev.data;
					if (!d) return;
					if (d.type === 'params') self.engine.setParams(d.params);
					else if (d.type === 'kick') self.engine.kick();
					else if (d.type === 'reset') self.engine.reset();
				};
			}
			process(inputs, outputs) {
				var out = outputs[0];
				if (!out || !out.length) return true;
				var n = out[0].length;
				if (this.zero.length < n) this.zero = new Float32Array(n);
				var inp = inputs[0] || [];
				var inL = inp[0] || this.zero, inR = inp[1] || inL;
				var outL = out[0], outR = out[1] || out[0];
				this.engine.process(inL, inR, outL, outR, n);
				this.meterTimer += n;
				if (this.meterTimer >= sampleRate / 40) {
					this.meterTimer = 0;
					var e = this.engine;
					this.port.postMessage({ type: 'meters', i: e.meterIn, o: e.meterOut, e: e.meterEcho, v: e.reelSpeed });
				}
				return true;
			}
		}
		registerProcessor('dubasco-tape-echo', DubascoProcessor);
	}

	root.DubascoEngine = api;
	if (typeof module === 'object' && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : (typeof self !== 'undefined' ? self : this));
