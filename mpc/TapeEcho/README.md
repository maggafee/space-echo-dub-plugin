# DUBASCO Tape Echo für Akai MPC (Standalone)

Das Dubasco Tape Echo als Plugin direkt auf der MPC-Hardware, ohne Computer.
Die Klangerzeugung ist exakt dieselbe Engine wie im VST3/AU-Plugin (`Source/dsp`).

**Voraussetzungen:** MPC OS **3.10.1** oder neuer und eine MPC aus dieser Liste:
MPC X, X SE, Live, Live II, One, One+, Key 61, Key 37, Force (32-Bit) sowie MPC Live III, MPC XL (64-Bit).
Das Paket enthält beide Builds, die MPC wählt den passenden selbst.

## Installation

1. Den Ordner `Dubasco - VST - Tape Echo` in einen Ordner namens `VST2 Plugins` im Hauptverzeichnis
   eines USB-Sticks oder einer SD-Karte kopieren (Ordner anlegen, falls es ihn nicht gibt).
2. Stick bzw. Karte in die MPC stecken.
3. Auf der MPC: **Preferences → Enhancements (Beta)** → **Enable Enhancements** und
   **External Plugin Support** einschalten (gibt es erst ab MPC OS 3.10.1).
4. Auf einer Spur einen Insert-Effekt hinzufügen: **External Plugins → Tape Echo**.

Hinweis von Akai: Drittanbieter-Plugins laufen ungeschützt in der MPC-Software. Projekte vor dem
ersten Test speichern. Zum Entfernen einfach den Plugin-Ordner vom Stick löschen.

## Bedienung

Der Touchscreen zeigt das Dubasco-Gerät mit denselben Knöpfen, Schaltern und Lampen wie die Desktop-Version.
Die Knöpfe werden durch Ziehen gedreht, Schalter und Lampen-Taster durch Antippen geschaltet.

- **DECK** (F1): das ganze Gerät mit Spulen, VU-Meter und der Hauptreihe
  (Input, Mic/Line, Delay Time, Feedback, Bass, Treble, Echo Level, Tape Sat, Power).
- **DUB** (F2): Hauptreihe plus Dub-Sektion (Echo Mode mit Kopf-Lampen, Spring Reverb, Tape, Time/Stereo, Mix, Dub).

Q-Links auf der Seite DUB (oben links nach unten rechts):
Echo Mode, Reverb, Spring Decay, Wow & Flutter / Tape Age, Inertia, Sync Note, Low Cut /
Ducking, Dry, Output, Stereo / Sync, Spring Kick, Throw, Hold.

Der Skin wird mit `python3 mpc/TapeEcho/tools/make_skin.py` (braucht Pillow) aus `Resources/images` und
`Source/Layout.h` erzeugt und liegt fertig in `mpc/TapeEcho/skins/`. Jedes Bedienelement wird auf sein
Stück Hintergrund gerechnet, dadurch gibt es keine Kanten oder Ränder um die Knöpfe.

Unterschiede zur Desktop-Version:

- **Dub Throw** und **Hold** sind Schalter (einmal drücken = an, nochmal = aus).
- **Spring Kick** gibt bei jedem Drücken einen Schlag auf den Federhall.
- Kein MIDI-Learn (Effekte bekommen auf der MPC kein MIDI), dafür die Q-Links.
- Kein eigener Bypass-Schalter, die MPC hat ihren eigenen.
- Die 16 Werkspresets sind eingebaut; eigene Presets speichert die MPC selbst.
- Tempo Sync folgt dem Tempo der MPC.

## Bauen

Der CI-Job `mpc` in `.github/workflows/build.yml` holt das offizielle
[MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk) (MIT, fester Commit),
baut arm32 + arm64, testet mit dem Host-Simulator und packt das ZIP (Artefakt `Dubasco-Tape-Echo-MPC`).
Lokal: SDK klonen, dann `python3 <sdk>/tools/build.py mpc/TapeEcho` und `python3 <sdk>/tools/package.py mpc/TapeEcho`.

CPU: Die Engine ist dieselbe wie am Desktop (drei Köpfe, Bandmodell, drei Federn). Auf den älteren
32-Bit-Geräten (Live, One, X) besser nur eine oder zwei Instanzen pro Projekt.
