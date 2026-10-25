=== Dub Echo Plugin ===
Contributors: dubasco
Tags: audio, mp3, player, echo, dub
Requires at least: 5.8
Tested up to: 6.8
Requires PHP: 7.4
Stable tag: 1.0.0
License: GPLv2 or later
License URI: https://www.gnu.org/licenses/gpl-2.0.html

Das Dubasco Tape Echo (RE-201 Style) im Browser. Besucher wählen deine MP3s, spielen sie durch das Bandecho, drehen an allen Reglern und vergeben Likes.

== Description ==

* Originaloptik des Dubasco Tape Echo VST/AU mit drehenden Spulen, laufendem Band, VU-Meter und Lampen.
* Dieselbe Echo-Engine wie im Plugin: Bandmodell mit drei Köpfen, 12 Echo-Modi, Federhall mit Kick, Wow & Flutter, Bandalterung, Motorträgheit, Tape-Stop, Hold, Dub-Throw, Ping-Pong, Ducking.
* 16 Werks-Presets, Tempo-Sync über die BPM des Titels.
* Dropdown mit deinen Titeln, Herz-Button („Gefällt mir“), Wiedergabezähler.
* Admin-Menü „Dub Echo Plugin“: MP3-Upload, Playlist mit Drag & Drop, Statistik mit Ranking, CSV-Export, Dashboard-Widget, Einstellungen.
* Shortcode `[dub_echo]` und Block „Dub Echo Player“.

== Installation ==

1. Plugins > Installieren > Plugin hochladen > dub-echo.zip wählen > Jetzt installieren > Aktivieren.
2. Links im Menü „Dub Echo Plugin“ öffnen und MP3s hochladen.
3. Den Shortcode `[dub_echo]` in eine Seite einfügen (oder den Block „Dub Echo Player“).

== Frequently Asked Questions ==

= Brauche ich HTTPS? =
Empfohlen. Mit HTTPS läuft das Echo in einem eigenen Audio-Thread (AudioWorklet). Ohne HTTPS nutzt der Player eine ältere Technik, die etwas mehr Rechenleistung braucht.

= Werden IP-Adressen gespeichert? =
Nein. Für die Likes werden nur gesalzene Hashwerte einer zufälligen Browser-Kennung und der IP-Adresse gespeichert.

== Changelog ==

= 1.0.0 =
* Erste Version.
