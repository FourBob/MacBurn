# MacBurn

Freie, native Laser-Software für macOS. Gestalten, vorbereiten und lasern mit GRBL-Diodenlasern.

- **Kern** in C++20 (`src/core`): Profile, Dokument, Geometrie, Job-Pipeline, GRBL-Treiber, AppController
- **Dünne Cocoa-Schicht** in Objective-C++ (`src/mac`): Fenster, Menüs, Zeichenfläche, generischer Inspektor
- **Profile** (`profiles/`): Maschinenbibliothek und Materialprofile als JSON, Prinzip Maschine → Material
- Referenzmaschine: Mecpow X3 Pro (10 W)
- Konzept und Release-Plan: [docs/Konzept.md](docs/Konzept.md)

> **Stand:** frühe Entwicklungsversion (v0.1 in Arbeit). Verbinden, Bewegen und Rahmen sind am X3 Pro getestet,
> ein Auftrag mit Laserstrahl noch nicht. Bitte nur unter Aufsicht und mit Schutzbrille verwenden.

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

Die Tests laufen ohne Fenster und ohne Hardware; die Gerätesteuerung wird gegen eine simulierte GRBL-Steuerung geprüft.

## Bedienung

### Drei Phasen

| Phase | Kurzbefehl | Wofür |
|---|---|---|
| **Gestalten** | ⌘1 | Formen importieren, anordnen, bearbeiten |
| **Vorbereiten** | ⌘2 | Absicht je Ebene, Werte aus dem Material, Brennvorschau mit Leerfahrten |
| **Lasern** | ⌘3 | Verbinden, Bewegen, Rahmen, Auftrag mit Startprüfung |

### Maschine → Material

Wie Drucker → Filament beim 3D-Druck: Oben links wählst du die Maschine, darunter das Material. Die Werte für
*Schneiden*, *Gravieren*, *Foto* und *Markieren* kommen aus dem Materialprofil. Jedes Material zeigt seine Herkunft:

| Status | Bedeutung |
|---|---|
| ✓ getestet | auf genau dieser Maschine eingemessen |
| ◇ Herstellerwert | aus der Anleitung des Herstellers |
| ≈ abgeleitet | aus der Leistungsklasse umgerechnet – Materialtest empfohlen |
| ✎ geändert | im Projekt angepasst |

### SVG importieren

*Ablage › Importieren …* (⌘I), Datei auf die Fläche ziehen oder im Finder *Öffnen mit › MacBurn*.
Jede Farbe wird zu einer Ebene. Gefüllte Flächen werden graviert, geschlossene Konturen geschnitten.
Enthält eine Farbe offene Linien, wird sie nur markiert – MacBurn schneidet nie unbeabsichtigt.
Texte in SVG-Dateien bitte vorher im Zeichenprogramm in Pfade umwandeln. Beispiele: [examples/schild.svg](examples/schild.svg),
[examples/test-30mm.svg](examples/test-30mm.svg) (kleiner Testauftrag, ca. 1:20 min).

### Zeichnen

Die Werkzeugleiste links auf der Zeichenfläche (oder die Taste, solange die Fläche aktiv ist):

| Werkzeug | Taste | Bedienung |
|---|---|---|
| Auswahl | V | auswählen, verschieben, Anfasser |
| Linie | L | ziehen; ⇧ = 45°-Schritte |
| Rechteck | R | ziehen; ⇧ = Quadrat; Eckenradius im Inspektor |
| Ellipse | E | ziehen; ⇧ = Kreis |
| Dreieck | D | von der Mitte aus ziehen; Richtung dreht, ⇧ = 15°-Schritte |
| Polygon | P | wie Dreieck; Eckenzahl im Inspektor |
| Stern | S | wie Dreieck; Zacken und Innenradius im Inspektor |
| Polylinie | N | Punkte klicken; Doppelklick oder ↩ beendet, Klick auf den Startpunkt schließt, Esc bricht ab |
| Text | T | klicken (Grundlinie beginnt dort) und lostippen |

**Genaue Maße:** Ist ein Werkzeug aktiv, zeigt der Inspektor seine Maße – X/Y und Breite/Höhe (Rechteck,
Ellipse, Eckenradius), Mittelpunkt, Durchmesser und Drehung (Dreieck, Polygon, Stern) bzw. Start, Länge und
Winkel (Linie). *Erstellen* oder ↩ legt die Form genau so an. Ein Klick auf die Fläche legt sie mit diesen Maßen
mittig an der Stelle an.

Neue Formen kommen in die links gewählte Ebene (in einem leeren Projekt wird eine Schnittebene angelegt)
und sind danach ausgewählt.

**Zahlenfelder rechnen mit:** Komma oder Punkt, `100/3`, `25+12,5`, `(10+2)*3`, Einheiten `mm`, `cm`, `in`, `pt`.
Angezeigt werden bis zu drei Nachkommastellen.

### Farben (Ebenen) zuweisen

Die Farbleiste unten auf der Zeichenfläche, das Menü *Objekt › Farbe* oder ⌥1–⌥3:

| Farbe | Taste | Absicht neuer Ebenen |
|---|---|---|
| Rot | ⌥1 | Schneiden |
| Blau | ⌥2 | Gravieren |
| Grün | ⌥3 | Markieren |

Mit Auswahl: Die Formen kommen in die Ebene dieser Farbe – vorhandene Ebenen werden erkannt (z. B. „Rot“ aus
einem SVG oder „Schnitt“ aus dem Beispiel), fehlende angelegt, leer gewordene entfernt. Ohne Auswahl wählt der
Klick die Farbe für die nächsten gezeichneten Formen. Im Inspektor legt das Feld *Ebene* die Auswahl in jede
vorhandene Ebene.

### Text

Alle auf dem Mac installierten Schriften. Im Inspektor: Text (⌥↩ für eine neue Zeile), Schrift, Schnitt,
**Höhe der Großbuchstaben in mm**, Zeichen- und Zeilenabstand, Ausrichtung. Text kommt in eine Gravurebene
(gewählte Ebene, wenn sie graviert oder markiert; sonst die erste Gravurebene, notfalls neu angelegt).

Text bleibt bearbeitbar – auch nach Verschieben, Drehen, Skalieren, Spiegeln, Duplizieren und nach dem
Sichern. *In Pfade umwandeln* macht daraus normale Formen; beim Kombinieren geschieht das automatisch.

### Formen bearbeiten

- **Auswählen:** Klick auf Kontur oder Fläche, ⇧ für mehrere. Ein importiertes SVG ist ein Objekt.
- **Verschieben:** mit der Maus oder den Pfeiltasten.
- **Skalieren:** Anfasser am Auswahlrahmen – Ecken proportional, Kanten einachsig, ⇧ kehrt um.
  Oder X, Y, Breite, Höhe im Inspektor eingeben. Der **Bezugspunkt** (9-Punkt-Raster) legt fest, auf welchen
  Punkt sich X/Y beziehen und welcher Punkt beim Ändern der Größe stehen bleibt.
- **Drehen:** Dreh-Anfasser über der Auswahl (⇧ = 15°-Schritte), beliebiger Winkel im Inspektor, 90° links/rechts.
- **Neigen** in X und Y, **Spiegeln**, **auf der Fläche zentrieren**.
- **Ausrichten** links, mittig, rechts, oben, mittig, unten – ein Objekt an der Fläche, mehrere aneinander.
  **Verteilen** ab drei Objekten.
- **Kombinieren** (Menü *Objekt › Kombinieren*): Vereinen, Vorderes vom hinteren abziehen, Schnittmenge, Teilen.
  Jede Ebene bleibt dabei für sich – Schnittkonturen bleiben Schnittkonturen.
- **Versatz:** neue Kontur außen (+) oder innen (−), z. B. als Schneiderand um eine Gravur.
- **Gruppieren** und **Gruppierung aufheben** – importierte SVGs zerfallen in ihre ursprünglichen Elemente.
- Alles lässt sich widerrufen.

### Lasern

1. Laser per USB anschließen, einschalten (Netzteil an, Not-Aus gelöst), *Laser › Verbinden* (⌘K).
2. *Referenzfahrt*, dann Position wählen:
   - **Wie auf der Fläche:** Das Design wird genau dort gelasert, wo es auf der Arbeitsfläche liegt.
   - **Am Laserkopf:** Ein wählbarer Ankerpunkt des Designs (Standard: Mitte) liegt unter dem Kopf.
     Kopf mit den Pfeiltasten über das Werkstück fahren.
3. *Rahmen abfahren* (⌘R) – der Kopf fährt den Umriss ab, der Laser bleibt aus.
4. *Lasern …* → Startprüfung abhaken (Fokus, Luftpumpe, Schutzbrille, Aufsicht) → *Starten*.
5. **Esc** oder **⌘.** stoppt sofort: Die Steuerung wird zurückgesetzt, der Laser ist aus.

Leerfahrten und Rahmen laufen mit einer schonenden Fahrgeschwindigkeit aus dem Maschinenprofil
(X3 Pro: 3000 mm/min), weil der Eilgang im Test Schritte verloren hat.

### Projekte

*Ablage › Sichern* (⌘S) legt eine `.macburn`-Datei an: lesbares JSON mit Formen, Ebenen, Projekt-Änderungen,
Maschine, Material, Position und einer Kopie der Materialwerte. So öffnet sich ein Projekt auch auf einem Mac,
dem das Materialprofil fehlt. Doppelklick im Finder öffnet das Projekt. Ungesicherte Änderungen zeigt der Punkt
im Schließknopf; vor dem Schließen fragt MacBurn nach.

### Tastenkürzel

| Kürzel | Befehl |
|---|---|
| ⌘N / ⌘O / ⌘S / ⇧⌘S | Neu / Öffnen / Sichern / Sichern unter |
| ⌘I | SVG importieren |
| ⇧⌘E | G-Code exportieren |
| ⌘Z / ⇧⌘Z | Widerrufen / Wiederholen |
| ⌘A / ⇧⌘A | Alles auswählen / Auswahl aufheben |
| ⌘D | Duplizieren |
| ⌫ | Löschen |
| ⌘G / ⇧⌘G | Gruppieren / Gruppierung aufheben |
| ⌥1 / ⌥2 / ⌥3 | Farbe Rot (Schneiden) / Blau (Gravieren) / Grün (Markieren) |
| ← → ↑ ↓ | Auswahl um 1 mm verschieben (⇧: 10 mm); in *Lasern* verbunden: Kopf bewegen |
| V L R E D P S N T | Werkzeuge: Auswahl, Linie, Rechteck, Ellipse, Dreieck, Polygon, Stern, Polylinie, Text |
| ↩ | Polylinie beenden |
| ⌘1 / ⌘2 / ⌘3 | Gestalten / Vorbereiten / Lasern |
| ⌘K / ⇧⌘K | Laser verbinden / trennen |
| ⌥⌘H | Referenzfahrt |
| ⌘R | Rahmen abfahren |
| Esc / ⌘. | Stopp (bei laufendem Auftrag), sonst Zeichnen abbrechen bzw. Auswahl aufheben |

## Laser auslesen

`mbprobe` sendet nur Abfragen (`?`, `$I`, `$$`), bewegt nichts und schaltet den Laser nicht ein.
Ohne Argument listet es die USB-Anschlüsse.

```bash
./build/mbprobe
```

```bash
./build/mbprobe /dev/cu.wchusbserial120
```

## Projektaufbau

```
src/core/      C++-Kern (ohne Cocoa): app, device, document, geometry, io, job, profiles, render, util
src/mac/       dünne Cocoa-Schicht (.mm)
profiles/      Maschinenbibliothek und Materialprofile (JSON)
examples/      Beispieldateien
tests/         Kerntests
tools/         mbprobe
third_party/   Fremdbibliotheken, unverändert
docs/          Konzept und UI-Entwurf
```

## Beitragen

Neue Maschinen und Materialien sind als JSON-Dateien unter `profiles/` willkommen – bitte mit Quellenangabe
(Herstelleranleitung oder eigener Materialtest).

## Lizenz

MacBurn: GPL-3.0-or-later, siehe [LICENSE](LICENSE).

Verwendete Bibliotheken:

| Bibliothek | Version | Lizenz | Zweck |
|---|---|---|---|
| [Clipper2](https://github.com/AngusJohnson/Clipper2) | 2.0.1 | Boost Software License 1.0 | Boolesche Operationen, Versatz |
