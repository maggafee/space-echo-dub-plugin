# Dub Echo Plugin für WordPress

Web-Version des Dubasco Tape Echo: Besucher wählen MP3s aus einem Dropdown, spielen sie durch das
Bandecho und experimentieren mit allen Reglern. Mit Likes und Statistik im Admin.

## Installieren

`./build-zip.sh` erzeugt `dub-echo.zip`. In WordPress: Plugins > Installieren > Plugin hochladen.
Danach links im Menü **Dub Echo Plugin** MP3s hochladen und `[dub_echo]` in eine Seite setzen
(oder den Block „Dub Echo Player“ nehmen).

## Aufbau

| Pfad | Inhalt |
| --- | --- |
| `dub-echo/dub-echo.php` | Plugin-Header, Hooks |
| `dub-echo/includes/` | Titel (Post-Type), Likes (eigene Tabelle), REST-API, Shortcode/Block, Admin-Seiten |
| `dub-echo/assets/js/dub-echo-engine.js` | Port von `Source/dsp` (Tape-Engine, Federhall, Filter, Parameter-Mapping, Presets). Läuft als AudioWorklet, sonst als ScriptProcessor |
| `dub-echo/assets/js/dub-echo-player.js` | Oberfläche: Nachbau von `PluginEditor`/`gui` mit den Positionen aus `Source/Layout.h` |
| `dub-echo/assets/img`, `fonts` | Grafiken aus `Resources/images` (als WebP), Oswald |
| `tests/` | Vergleich JS-Engine gegen C++-Engine |

Ändert sich die DSP in `Source/dsp`, muss `dub-echo-engine.js` mitgezogen werden; `tests/` zeigt,
ob beide noch gleich klingen. Ändern sich Grafiken oder `Layout.h`, die Bilder neu nach
`assets/img` kopieren und die Konstanten oben in `dub-echo-player.js` anpassen.

## REST-Endpunkte (`/wp-json/dub-echo/v1/`)

* `GET tracks?ids=1,2&visitor=…` frische Like-Zahlen
* `POST like {track, visitor, like}` Like setzen/zurücknehmen (1 pro Besucher, max. 3 pro IP und Tag, Rate-Limit)
* `POST play {track, visitor}` Wiedergabe zählen (pro Besucher und Titel alle 30 Minuten)
