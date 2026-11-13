# MacBurn

Freie, native Laser-Software für macOS. Gestalten, vorbereiten und lasern mit GRBL-Diodenlasern.

- **Kern** in C++20 (`src/core`): Profile, Dokument, Job-Pipeline, GRBL, Zeichenliste, AppController
- **Dünne Cocoa-Schicht** in Objective-C++ (`src/mac`): Fenster, Menüs, Zeichenfläche, generischer Inspektor
- **Profile** (`profiles/`): Maschinenbibliothek und Materialprofile als JSON, Prinzip Maschine → Material
- Referenzmaschine: Mecpow X3 Pro (10 W)
- Konzept: [docs/Konzept.md](docs/Konzept.md)

## Voraussetzungen

macOS 27, Xcode Command Line Tools, CMake ≥ 3.24.

## Bauen

```bash
cmake -S . -B build
cmake --build build -j
open build/MacBurn.app
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

## SVG importieren

*Ablage › Importieren …* (⌘I), Datei auf die Fläche ziehen oder im Finder *Öffnen mit › MacBurn*.
Jede Farbe wird zu einer Ebene. Gefüllte Flächen werden graviert, geschlossene Konturen geschnitten.
Enthält eine Farbe offene Linien, wird sie nur markiert – MacBurn schneidet nie unbeabsichtigt.
Texte bitte vorher in Pfade umwandeln. Beispiel: [examples/schild.svg](examples/schild.svg).

## Laser auslesen

`mbprobe` sendet nur Abfragen (`?`, `$I`, `$$`), bewegt nichts und schaltet den Laser nicht ein.

```bash
./build/mbprobe
```

```bash
./build/mbprobe /dev/cu.usbserial-XXXX
```

## Lizenz

GPL-3.0-or-later, siehe [LICENSE](LICENSE).
