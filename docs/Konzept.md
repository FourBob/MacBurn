# MacBurn – Konzept

Native, quelloffene Laser-Software für macOS. C++-Kern mit dünner Cocoa-Schicht.
Stand: 13.11.2026

---

## 1. Leitidee

Laser-Software ist heute meist mächtig, aber unübersichtlich: viele Fenster, große Parametertabellen, Fachbegriffe ab der ersten Minute. MacBurn soll den vollen Funktionsumfang bieten und sich dabei anfühlen wie eine gute Mac-App. Profile funktionieren wie bei einem 3D-Drucker-Slicer: erst die Maschine, dann das Material.

**Vier UX-Prinzipien**

1. **Material zuerst, Zahlen später.** Man wählt „3 mm Pappel-Sperrholz“ und weist Objekten eine Absicht zu: *Schneiden*, *Gravieren*, *Markieren*. Geschwindigkeit, Leistung und Durchgänge kommen aus dem Materialprofil der gewählten Maschine (siehe Abschnitt 3). Wer will, klappt die Zahlen auf.
2. **Drei Phasen statt eines Werkzeugkastens.** Oben im Fenster ein Umschalter: **Gestalten → Vorbereiten → Lasern**. Jede Phase zeigt nur, was man dort braucht.
3. **Nichts geht unbemerkt schief.** Vor dem Start eine kurze Prüfliste (Fokus, Luftunterstützung, Deckel, Objekt außerhalb der Arbeitsfläche, offene Pfade beim Schneiden). Zeitschätzung immer sichtbar. Jeder Schritt rückgängig machbar.
4. **Echter Mac.** Dokumentfenster, Werkzeugleiste, Seitenleiste + Inspektor, Menüleiste mit allen Befehlen, Trackpad-Gesten, Drag & Drop aus Finder/Safari/Fotos, Dark Mode, Quick Look für Projektdateien, Befehlspalette (⌘K).

**Zielgruppe:** Hobby und Maker mit Diodenlaser (Mecpow, xTool, Sculpfun, Atomstack, Ortur, Creality Falcon, Two Trees) – alle GRBL-basiert. **Referenzmaschine für Entwicklung und Tests: Mecpow X3 Pro (10 W).** CO₂ (Ruida) ist optional.

---

## 2. Funktionsumfang

Legende: **M** = MVP (v0.1), **1** = bis v1.0, **S** = später / evtl.

### 2.1 Gerät & Verbindung

| Funktion | Stufe | Anmerkung |
|---|---|---|
| GRBL 1.1 über USB-Seriell (inkl. CH340/CP210x) | M | Zeichenzähl-Streaming, Echtzeitbefehle (`?`, `!`, `~`, `0x18`) |
| Einrichtungsassistent: Gerät automatisch erkennen, `$$` lesen, Arbeitsfläche aus `$130/$131` | M | Ziel: Verbinden ohne ein einziges Eingabefeld |
| Steuerpanel: Joggen, Home, Ursprung setzen, Fokus-/Testpuls | M | Pfeiltasten joggen, ⇧ = große Schritte |
| Rahmen fahren (Rechteck, Umriss) | M | |
| Start / Pause / Stopp / Fortsetzen, Fortschritt, Restzeit | M | |
| Jobursprung: absolut, Benutzerursprung, aktuelle Position, 9-Punkt-Anker | M | |
| Konsole mit Befehlsverlauf | M | einklappbar, für Experten |
| GRBL-Einstellungseditor mit Erklärungen statt `$32=1` | 1 | |
| GRBL über WLAN/Telnet (xTool, Falcon Wi-Fi, FluidNC) | 1 | |
| Mehrere Geräteprofile | 1 | |
| Ruida-Controller über Ethernet/UDP (CO₂) | 1 | eigener Treiber, verschlüsseltes Protokoll |
| Marlin / Smoothieware | S | |
| Galvo (BJJCZ/EZCAD-Boards) | S | großer Brocken, eigenes Projekt |
| Drehachse (Walze/Futter) | 1 | |

### 2.2 Gestalten (Zeichnen & Bearbeiten)

| Funktion | Stufe |
|---|---|
| Rechteck (mit Eckradius), Ellipse, Polygon/Stern, Linie, Freihand-Bézier | M |
| Text mit allen Systemschriften (CoreText), Schriftgröße in mm | M |
| Auswählen, Verschieben, Skalieren, Drehen, Spiegeln; numerisch im Inspektor | M |
| Ausrichten / Verteilen, Gruppieren, Raster & Fangen, Lineale, Hilfslinien | M |
| Rückgängig/Wiederholen ohne Limit | M |
| Knotenbearbeitung (Punkte, Kurven, Glätten, Trennen, Verbinden) | 1 |
| Boolesche Operationen: Vereinen, Subtrahieren, Schnittmenge, Verschweißen | 1 |
| Versatz (Offset) innen/außen, Konturen schließen, Duplikate entfernen | 1 |
| Anordnungen: Raster, Kreis, entlang Pfad | 1 |
| Text auf Pfad, Text in Kreis | 1 |
| Bild nachzeichnen (Vektorisieren) | 1 |
| Variabler Text / Seriennummern / CSV-Serienbrief | 1 |
| Laschen/Haltestege (Tabs) | 1 |
| Verschachteln (Nesting) auf Platte | S |
| Box-/Fingerzinken-Generator | S |

### 2.3 Import / Export

| Format | Stufe | Technik |
|---|---|---|
| SVG | M | eigener Parser (Pfade, Transformationen, Farben → Ebenen) |
| PNG, JPG, HEIC, TIFF, BMP | M | ImageIO |
| PDF / AI (PDF-kompatibel) | 1 | CGPDF – Vektorpfade extrahieren |
| DXF | 1 | eigener Parser oder libdxfrw |
| Eigenes Projektformat `.macburn` (Paket mit JSON + Bildern) | M | mit Quick-Look-Vorschau |
| G-Code exportieren | M | |
| SVG/PDF exportieren | 1 | |

### 2.4 Bearbeitungsarten & Parameter (Vorbereiten)

**Absichten** (UI) ↔ **Modi** (intern):

| Absicht | Modus | Wesentliche Parameter |
|---|---|---|
| Schneiden | Linie | Geschwindigkeit, Leistung, Durchgänge, Z-Absenkung pro Durchgang, Luft |
| Markieren / Ritzen | Linie | wie oben, geringe Leistung |
| Gravieren (Fläche) | Füllung | Zeilenabstand/LPI, Winkel, bidirektional, Overscan, Kreuzschraffur |
| Foto gravieren | Bild | DPI, Raster-Algorithmus, Helligkeit/Kontrast/Gamma, Negativ |
| Offset-Füllung | Offset-Füllung | Abstand | 

- **Ebenen** nach Farben, in der Seitenleiste mit Namen, Absicht-Symbol, Sichtbar/Ausgabe-Schalter, Reihenfolge per Drag & Drop. **M**
- **Min/Max-Leistung, Overscan, Luftunterstützung (M7/M8)**. **M**
- **Raster-Algorithmen:** Schwellwert, Floyd-Steinberg, Jarvis, Stucki, Atkinson, Graustufen (variable Leistung). **M** (zwei davon), Rest **1**
- **Bildanpassungen nicht-destruktiv** mit Live-Vorschau des gerasterten Ergebnisses. **M**
- **Profile Maschine → Material** (Abschnitt 3). **M** (System- und Benutzerprofile), **1** (Import/Export, Community-Profile)
- **Materialtest-Assistent:** erzeugt Raster Geschwindigkeit × Leistung, Ergebnis anklicken → als getestetes Materialprofil für diese Maschine speichern. **1** (eines der wichtigsten Komfort-Features)
- **Optimierung:** Innen vor außen, Reihenfolge nach Ebene/Nähe, Schnittrichtung, Startpunkte, Lead-in. **M** (einfach), **1** (vollständig)
- **Kerf-Ausgleich**. **1**

### 2.5 Vorschau & Simulation

| Funktion | Stufe |
|---|---|
| Vorschau des Laserpfads, Leerfahrten ein/aus | M |
| Zeitschätzung unter Berücksichtigung von Beschleunigung (`$120/$121`) | M |
| Abspielen mit Schieberegler (Zeitachse) | 1 |
| Realistische Brennvorschau auf Materialtextur | S |

### 2.6 Kamera

| Funktion | Stufe |
|---|---|
| USB-Kamera / Continuity-Kamera (iPhone!) als Hintergrund | 1 |
| Linsenkalibrierung (Schachbrett) + Ausrichtung auf Arbeitsfläche | 1 |
| Design per Drag auf das Werkstück im Kamerabild legen | 1 |
| Automatisches Erkennen von Werkstückkanten | S |

### 2.7 Sicherheit & Komfort

- Startprüfung (Sheet vor dem Start): Fokus geprüft? Luft an? Objekte innerhalb der Fläche? Offene Pfade bei *Schneiden*? **M**
- Nach Abbruch: „Ab Zeile/Position fortsetzen“. **1**
- Jobverlauf mit Dauer und verwendeten Einstellungen. **1**
- Benachrichtigung (macOS + optional iPhone) bei Jobende. **1**
- Abschalten des Ruhezustands während eines Jobs (IOPMAssertion). **M**

---

## 3. Profile: Maschine → Material

Das Prinzip ist dasselbe wie beim 3D-Drucker-Slicer: Zuerst wählt man die **Maschine** (wie den Drucker), dann das **Material** (wie das Filament). Was mit einem Objekt passiert, legt die **Absicht** der Ebene fest (entspricht dem Prozessprofil).

| 3D-Drucker | MacBurn | Enthält |
|---|---|---|
| Drucker | **Maschine** | Hardware: was das Gerät kann |
| Filament | **Material** | Werte pro Absicht, gültig für eine bestimmte Maschine |
| Prozess | **Absicht** je Ebene | Schneiden, Gravieren, Foto, Markieren |

### 3.1 Maschinenprofil

- Name, Hersteller, Modell, Foto/Symbol
- Controller und Verbindung: GRBL 1.1, USB oder TCP, Baudrate
- Arbeitsfläche, Ursprung (Ecke), Homing vorhanden
- **Lasermodul:** Art (Diode, Infrarot, CO₂), optische Leistung in W, Wellenlänge, Fokusabstand, Spotgröße
- Grenzwerte: max. Geschwindigkeit, Beschleunigung, S-Wert für 100 % (`$30`), Laser-Modus (`$32`)
- Ausstattung: Luftunterstützung (über M7/M8 schaltbar?), Z-Achse/Autofokus, Kamera, Drehachse
- Beim Einrichten werden diese Werte so weit wie möglich aus `$$` gelesen.

Eine Maschine kann später mehrere Lasermodule haben (z. B. 10 W blau + 2 W Infrarot). Dann gilt die Kette **Maschine → Modul → Material**. Für v0.1 genügt ein Modul.

### 3.2 Maschinenbibliothek

MacBurn bringt Vorlagen für gängige Lasergravierer mit, so wie ein Slicer Druckerprofile mitbringt. Wer seine Maschine dort findet, muss beim Einrichten nichts eintippen.

- **Inhalt je Vorlage:** Hersteller, Modell, Variante (Leistungsstufe), Arbeitsfläche, Ursprung, Controller und Firmware, Baudrate, Lasermodul(e) mit Leistung und Wellenlänge, Fokusabstand, Grenzwerte (Geschwindigkeit, Beschleunigung, `$30`), Homing, Luftunterstützung, Z-Achse, Kamera, Bild/Symbol.
- **Erste Auswahl (Diodenlaser, GRBL):** Mecpow (X3, X3 Pro – Referenz), xTool (D1, D1 Pro, S1), Sculpfun (S9, S10, S30-Reihe), Atomstack (A5, A10, X20, S20, A40), Ortur (Laser Master 2/3, LM2 Pro), Creality (Falcon, Falcon2), Two Trees (TS2, TTS-55), Longer (Ray5), NEJE. Jede Vorlage wird vor der Aufnahme mit Herstellerangaben abgeglichen und mit ihrer Quelle versehen.
- **Erkennung beim Verbinden:** USB-Kennung (VID/PID) und GRBL-Build-Info (`$I`) schlagen ein Modell vor. Danach gleicht MacBurn die Vorlage mit `$$` ab und meldet Abweichungen (z. B. andere Arbeitsfläche nach Erweiterung).
- **Ohne Vorlage:** „Eigene Maschine anlegen“ liest alles Mögliche aus `$$`, der Rest wird abgefragt (Leistung, Wellenlänge).
- **Verhältnis zu Materialien:** Jede Vorlage gehört zu einer Leistungsklasse (z. B. „Diode 10 W, 455 nm“). Darüber findet sie sofort passende Systemmaterialien (Status ≈ abgeleitet), bis eigene Tests vorliegen.
- **Pflege:** Vorlagen liegen als JSON im Repository unter `profiles/machines/<hersteller>/`. Neue Modelle kommen per Pull Request. Eine Prüfroutine in der CI kontrolliert Pflichtfelder und Plausibilität.

### 3.2a Referenzmaschine: Mecpow X3 Pro

Erste Vorlage: [`profiles/machines/mecpow/x3-pro.json`](../profiles/machines/mecpow/x3-pro.json)

| Eigenschaft | Wert | Folge für MacBurn |
|---|---|---|
| Lasermodul | Diode 10 W optisch, 445 nm, Spot 0,06 × 0,08 mm | Leistungsklasse `diode-10w-445nm` für Systemmaterialien |
| Arbeitsfläche | 410 × 400 mm, mit Erweiterungskit 750 × 400 mm | Vorlage hat zwei Varianten |
| Ursprung / Homing | vorne links, Endschalter X/Y | Home-Knopf und absolute Positionierung ab v0.1 |
| Fokus | fester Fokus (40 mm), manuell mit Fokusplatte | Startprüfung zeigt die Fokusplatten-Anleitung statt Z-Steuerung; kein Autofokus |
| Geschwindigkeit | max. 10 000 mm/min, Gravur empfohlen bis 5 000 mm/min | Grenze für Regler und Zeitschätzung |
| Luftunterstützung | eigene Pumpe (30 l/min) mit Drehregler | nicht per M7/M8 schaltbar → bleibt eine Checkbox in der Startprüfung |
| Sicherheit | Not-Aus, Flammensensor (abschaltbar), Neigungssensor (~50°) | Auslösen muss als klare Meldung erscheinen („Flammensensor hat ausgelöst – Job gestoppt“), nicht als roher Alarmcode |
| Verbindung | USB-B | Seriell-Treiber zuerst; Hersteller-App deutet auf ein Board mit WLAN hin → später prüfen |

**Vor dem ersten Code am Gerät prüfen:** Ausgabe von `$I` (Firmware und Board) und `$$` (insbesondere `$30`, `$32`, `$110`–`$121`, `$130`/`$131`), Baudrate, und wie Flammen- und Neigungssensor dem Rechner gemeldet werden. Diese Werte stehen in der Vorlage unter `verify`.

### 3.3 Materialprofil

- Name, Kategorie (Holz, Kunststoff, Leder, Papier, Metall, Stein), Dicke, Farbe, Notiz
- Fokus-Versatz (z. B. bei dickem Material)
- **Einstellungen je Absicht:**
  - *Schneiden:* Geschwindigkeit, Leistung, Durchgänge, Z-Absenkung pro Durchgang, Luft
  - *Gravieren:* Geschwindigkeit, Leistung min/max, Zeilenabstand, Winkel, Overscan, Luft
  - *Foto:* zusätzlich DPI, Raster-Verfahren, Gamma
  - *Markieren:* Geschwindigkeit, Leistung
  - Fehlt eine Absicht (z. B. Schiefer schneiden), wird sie als „nicht möglich“ angezeigt.
- **Gültig für:** genau eine Maschine (Benutzerprofil) oder eine Leistungsklasse wie „Diode 10 W, 455 nm“ (Systemprofil).

### 3.4 Status eines Materials

Die Materialliste zeigt nur, was zur aktiven Maschine passt. Jedes Material trägt einen Status:

| Symbol | Status | Bedeutung |
|---|---|---|
| ✓ | **Getestet** | Auf genau dieser Maschine eingemessen |
| ≈ | **Abgeleitet** | Aus der Leistungsklasse umgerechnet. MacBurn empfiehlt einen Materialtest. |
| ✎ | **Geändert** | Im Projekt angepasst und noch nicht gespeichert |

Bei *Geändert* stehen drei Aktionen zur Wahl: **Nur im Projekt behalten**, **Als Material speichern** oder **Zurücksetzen**. Geänderte Werte sind im Inspektor markiert und lassen sich einzeln zurücksetzen.

### 3.5 Vererbung

```
Systemprofil (mitgeliefert, schreibgeschützt)
   └─ Benutzerprofil (erbt, speichert nur Abweichungen)
        └─ Projekt (speichert nur Abweichungen + Schnappschuss)
```

- Systemprofile liegen im Repository unter `profiles/` und werden per Pull Request gepflegt.
- Ein Benutzerprofil speichert nur, was vom Elternprofil abweicht. Verbessert ein Update das Systemprofil, profitiert das Benutzerprofil automatisch.
- Das Projekt speichert Maschine-ID, Material-ID, die eigenen Abweichungen **und** einen Schnappschuss der wirksamen Werte. So öffnet es sich auch auf einem Mac ohne diese Profile. Hat sich das Profil inzwischen geändert, fragt MacBurn: „Projektwerte verwenden“ oder „Aktuelles Profil verwenden“.

### 3.6 Maschine wechseln

- Das Projekt bleibt, wie es ist. MacBurn sucht dasselbe Material für die neue Maschine (über eine gemeinsame Material-Kennung wie `plywood-poplar-3mm`).
- Gibt es kein getestetes Profil, wird ein abgeleitetes verwendet und als ≈ markiert.
- Passt das Design nicht auf die neue Arbeitsfläche, erscheint ein Hinweis am Objekt.

### 3.7 Speicherung

- JSON-Dateien unter `~/Library/Application Support/MacBurn/profiles/` (`machines/`, `materials/`), getrennt nach `system/` und `user/`
- Weitergabe einzeln als `.mbmachine` / `.mbmaterial` oder gesammelt als `.mbprofiles`
- Lesbar und diff-freundlich, damit Beiträge zur Community-Sammlung einfach sind

```json
{
  "id": "plywood-poplar-3mm",
  "name": "Pappel-Sperrholz 3 mm",
  "inherits": "system/diode-10w/plywood-poplar-3mm",
  "machine": "user/mecpow-x3-pro",
  "status": "tested",
  "intents": {
    "cut":     { "speed": 280, "power": 100, "passes": 2 },
    "engrave": { "speed": 3000, "power": 35 }
  }
}
```

### 3.8 Im Code

Neues Modul `core/profiles/`: `MachineProfile`, `MaterialProfile`, `ProfileStore` (Laden, Vererbung auflösen, Speichern) und eine zentrale Funktion `resolve(machine, material, intent, overrides) → EffectiveSettings`. Job-Compiler, Zeitschätzung und Inspektor fragen nur diese Funktion ab.

---

## 4. Technische Architektur

**Grundsatz (wie bei BTNEdit):** Die ganze Logik steckt in **portablem C++20**. Cocoa ist nur eine **dünne Objective-C++-Schicht** (`.mm`) für App-Loop, Fenster, Menüs, Bedienelemente und die Metal-Ansicht. Diese Schicht trifft keine Entscheidungen, sie zeigt nur an, was der Kern vorgibt, und leitet Eingaben an ihn weiter. Swift kommt nicht vor.

```
┌──────────── Cocoa-Schicht (Objective-C++, dünn) ─────────────┐
│ main.mm, AppDelegate, NSWindow, NSMenu, NSToolbar            │
│ CanvasView (NSView + CAMetalLayer), Seitenleiste, Inspektor  │
│ Datei-Dialoge, Mitteilungen, Ruhezustand verhindern          │
├──────────────────── Schnittstelle: app/ ─────────────────────┤
│ AppController: Befehle rein, Zustand + Ereignisse raus       │
│ View-Modelle (Ebenen, Inspektor-Felder, Status) als C++      │
├───────────────┬──────────────────────┬───────────────────────┤
│ document/     │ geometry/            │ render/               │
│ Modell, Undo- │ Bézier, Flatten,     │ Szene → Vertex-Puffer │
│ Befehlsstapel │ Clipper2, Hit-Test   │ (Metal über metal-cpp)│
├───────────────┼──────────────────────┼───────────────────────┤
│ io/           │ job/                 │ device/               │
│ SVG, DXF, PDF,│ Planen → Rastern →   │ LaserDevice (abstrakt)│
│ Bilder, .mburn│ Optimieren → Moves   │ GRBL seriell/TCP,Ruida│
└───────────────┴──────────────────────┴───────────────────────┘
```

### Was in C++ liegt (alles Wesentliche)

- **Sprache:** C++20, Build mit **CMake** (wie BTNEdit), Clang aus den Xcode Command Line Tools. Xcode nur optional zum Debuggen.
- **Dokument:** eigenes Modell mit Befehlsstapel für Rückgängig/Wiederholen. Undo läuft im Kern, nicht über `NSUndoManager`. Das Cocoa-Menü ruft nur `controller.undo()`.
- **Geometrie:** Clipper2 (C++, Boost-Lizenz) für Boolesche Operationen und Offset; eigene Bézier-Routinen und Spatial Index für Hit-Tests.
- **Rendern:** Stand Grundgerüst: Der Kern liefert eine `RenderList` (Striche in mm), `CanvasView.mm` zeichnet sie mit CoreGraphics. Später baut der Kern daraus fertige Vertex-Puffer (Linien, Flächen, Rasterbilder als Texturen). Die Metal-Aufrufe laufen über **metal-cpp** (Apples Header-Bibliothek für C++). Die `.mm`-Seite stellt nur die `CAMetalLayer` bereit.
- **Bilder:** Laden über ImageIO/CoreGraphics (C-APIs, direkt aus C++ nutzbar) oder plattformneutral über stb_image. Rastern (Floyd-Steinberg usw.) und Bildanpassungen in eigenem C++, bei Bedarf mit Accelerate/vImage (ebenfalls C-API).
- **Text:** CoreText ist eine C-API, also aus C++ nutzbar: Glyphen-Pfade direkt in die Geometrie.
- **PDF/AI:** CGPDF (C-API) zum Auslesen der Vektorpfade.
- **SVG/DXF:** eigene Parser in C++ (XML z. B. über pugixml).
- **Seriell:** POSIX (`open`, `termios`, `poll`) auf `/dev/cu.*`; Port-Erkennung und Hotplug über die IOKit-C-API. Der Treiber läuft in eigenem `std::jthread`, Zustandsänderungen gehen über eine Warteschlange an den Controller. Die Cocoa-Schicht holt sie per `dispatch_async` auf den Main-Thread.
- **Netzwerk:** BSD-Sockets für GRBL über TCP und Ruida über UDP.
- **Job-Pipeline:** Dokument → geräteneutrale Bewegungsliste → Treiber übersetzt in G-Code bzw. Ruida-Befehle. Vorschau, Zeitschätzung und Export nutzen dieselbe Datenquelle.
- **Tests:** doctest oder Catch2; Golden-File-Tests für die G-Code-Ausgabe; Fake-GRBL als Testgerät. Der Kern lässt sich komplett ohne Fenster testen.

### Was in der Cocoa-Schicht liegt (bewusst wenig)

| Datei | Aufgabe |
|---|---|
| `main.mm`, `AppDelegate.mm` | App-Loop, Menüleiste, Tastenkürzel → Befehls-IDs des Kerns |
| `MainWindow.mm` | `NSWindow`, `NSToolbar` (Phasen-Umschalter, Hauptknopf), `NSSplitView` |
| `CanvasView.mm` | `CAMetalLayer`, Maus/Trackpad/Tastatur → Kern-Ereignisse |
| `LayerList.mm` | `NSTableView` über das Ebenen-View-Modell |
| `InspectorView.mm` | **ein generischer Baukasten:** Der Kern liefert eine Liste von Feldern (Zahl, Schieber, Auswahl, Schalter, Knopf), die Schicht baut daraus `NSStackView`-Zeilen. Neue Inspektor-Inhalte brauchen damit keine neue Cocoa-Zeile. |
| `Panels.mm` | Material-Popover, Startprüfungs-Sheet, Befehlspalette |
| `Platform.mm` | Öffnen/Sichern-Dialoge, Mitteilungen, `IOPMAssertion`, Kamera (AVFoundation) |

Ziel: insgesamt unter ca. 2 000 Zeilen Objective-C++. Wenn eine `.mm`-Datei anfängt, Entscheidungen zu treffen, gehört der Code in den Kern.

### Ordnerstruktur

```
MacBurn/
├── CMakeLists.txt
├── Info.plist
├── src/
│   ├── core/            # rein C++, keine Apple-Header außer C-APIs hinter Schnittstellen
│   │   ├── document/  geometry/  job/  device/  io/  render/  profiles/  app/
│   └── mac/             # *.mm – dünne Cocoa-Schicht
├── profiles/          # mitgelieferte System-Profile (JSON)
├── third_party/         # clipper2, metal-cpp, pugixml, stb, doctest
├── tests/
└── docs/
```

Apple-spezifische C-APIs (CoreText, ImageIO, CGPDF, IOKit) sitzen im Kern hinter kleinen Schnittstellen (`FontProvider`, `ImageLoader`, `PortEnumerator`). So bleibt eine spätere Linux- oder Windows-Version möglich, ohne dass das jetzt ein Ziel sein muss.

- **Vertrieb:** Developer ID + Notarisierung (Direktvertrieb), Auto-Update mit Sparkle (Objective-C, passt in die Cocoa-Schicht). App Store schwierig wegen Seriell-Zugriff in der Sandbox – erst später prüfen.
- **Lizenz:** Vorschlag GPL-3.0-or-later. Alle geplanten Abhängigkeiten sind damit verträglich (Clipper2: Boost, pugixml/doctest/Sparkle: MIT, metal-cpp: Apache-2.0, stb: Public Domain).
- **Mindestversion:** macOS 27. Damit sind die aktuellen AppKit- und Metal-APIs ohne Rücksicht auf Altversionen nutzbar. Ältere Versionen sind ein Bonus, kein Ziel.

---

## 5. Release-Plan

Annahme: eine Person, Teilzeit, mit Claude als Pair-Programmer. Zeiten sind grob. v0.1 enthält zusätzlich das Grundgerüst: CMake, Cocoa-Schicht, generischer Inspektor, Metal-Ansicht.

### v0.1 „Erster Schnitt“ – MVP (ca. 8–10 Wochen)
Ziel: Ein SVG oder Foto laden, Material wählen, auf dem Mecpow X3 Pro sauber schneiden und gravieren.
- GRBL über USB, Einrichtungsassistent, Joggen, Home, Rahmen, Start/Pause/Stopp
- Import SVG + Rasterbilder; Rechteck, Ellipse, Linie, Text
- Ebenen mit Absichten *Schneiden / Gravieren / Foto*
- Rastern: Schwellwert + Floyd-Steinberg; Bildanpassungen
- Profile Maschine → Material: Maschinenbibliothek mit den ersten Vorlagen (Mecpow X3 Pro + einige verbreitete Modelle), Systemmaterialien für 5/10/20 W
- Vorschau + Zeitschätzung, Startprüfung
- Projektformat `.macburn`, G-Code-Export
- **Meilenstein:** eigener Untersetzer aus Sperrholz mit Foto-Gravur, ohne Konsole benutzt zu haben.

### v0.2 „Werkstatt“ (ca. 6 Wochen)
- Knotenbearbeitung, Boolesche Ops, Offset, Ausrichten-Erweiterungen, Anordnungen
- PDF/AI- und DXF-Import
- Alle Raster-Algorithmen, Kreuzschraffur, Offset-Füllung
- **Materialtest-Assistent**

### v0.3 „Kontrolle“ (ca. 5 Wochen)
- Vollständige Pfadoptimierung, Laschen, Kerf
- GRBL-Einstellungseditor, WLAN/Telnet-Geräte, mehrere Maschinen
- Maschinenbibliothek ausbauen, Modellerkennung über USB-Kennung und `$I`, Beiträge per Pull Request
- Vorschau mit Zeitachse, Fortsetzen nach Abbruch, Jobverlauf
- Erste öffentliche Beta (TestFlight für macOS oder signiertes DMG)

### v0.5 „Kamera“ (ca. 6 Wochen)
- Kamera-Overlay inkl. iPhone-Continuity-Kamera, Kalibrierungsassistent
- Bild nachzeichnen
- Text auf Pfad, variabler Text / Seriennummern

### v0.7 „Erweiterungen“ (ca. 6 Wochen)
- Drehachse
- Mehrere Lasermodule pro Maschine (z. B. Blau + Infrarot)
- Optional: Ruida-Treiber für CO₂ (Ethernet)

### v1.0 „Fertig“ (ca. 4 Wochen)
- Feinschliff, Barrierefreiheit (VoiceOver für alle Panels), Lokalisierung DE/EN
- Hilfe direkt in der App (kontextuelle Tipps statt PDF-Handbuch)
- Signiertes und notarisiertes DMG auf GitHub, Homebrew-Cask, Auto-Update (Sparkle)

### Danach (unverbindlich)
Galvo-Unterstützung, Nesting, Box-Generator, iPad-Begleit-App als Fernbedienung/Kamera.

---

## 6. UI/UX-Vorschlag

Interaktives Mockup: `docs/ui-mockup.html` (auch als Artifact veröffentlicht).

### 6.1 Fensteraufbau

```
┌─────────────────────────────────────────────────────────────────────┐
│ ● ● ●  Untersetzer.macburn   [Gestalten | Vorbereiten | Lasern]  ⏱ 12:40  [▶ Lasern] │
├──────────┬──────────────────────────────────────────────┬───────────┤
│ Maschine │                                              │ Inspektor │
│ X3 Pro 10│          Arbeitsfläche 400 × 400 mm          │ (kontext- │
│ Material │             mit Kamera-Hintergrund           │  abhängig)│
│ Pappel 3✓│                                              │           │
│──────────│                                              │           │
│ Ebenen   │  [Werkzeug-Palette schwebend links]          │           │
│ ● Schnitt│                                              │           │
├──────────┴──────────────────────────────────────────────┴───────────┤
│ Mecpow X3 Pro · verbunden · X 120.0 Y 85.5 · Bereit                  │
└─────────────────────────────────────────────────────────────────────┘
```

- **Werkzeugleiste (oben):** Dokumentname, Phasen-Umschalter (Segmented Control), Zeitschätzung, großer Haupt-Button. Der Button ändert sich je nach Zustand: *Verbinden* → *Rahmen* → *Lasern* → *Pause*.
- **Seitenleiste (links):** oben zwei Profilkarten, **Maschine** und darunter **Material**, wie Drucker und Filament im Slicer. Jede Karte öffnet ein Popover zur Auswahl. Darunter die Ebenen mit Absicht-Symbol und Farbe.
- **Arbeitsfläche (Mitte):** Pinch-Zoom, Zwei-Finger-Scroll, Leertaste+Ziehen. Schwebende Werkzeugpalette (wie Freeform/Pixelmator), die sich bei Bedarf einklappt.
- **Inspektor (rechts):** zeigt immer genau das Ausgewählte: Geometrie (X, Y, B, H, Drehung, Proportionen sperren), Absicht, darunter einklappbar „Einstellungen anpassen“ mit Zahlen.
- **Statusleiste (unten):** Gerät, Verbindung, Position, Zustand. Klick öffnet das Gerätepanel.

### 6.2 Die drei Phasen

| | Gestalten | Vorbereiten | Lasern |
|---|---|---|---|
| Fokus | Formen, Text, Bilder | Material, Absichten, Reihenfolge | Maschine, Rahmen, Start |
| Mitte | Zeichenfläche | Pfadvorschau mit Brennfarben | Kamera/Arbeitsfläche + Laserkopf live |
| Rechts | Objekt-Inspektor | Ebenen-Einstellungen, Optimierung | Jog-Steuerkreuz, Ursprung, Fortschritt |
| Kurzbefehl | ⌘1 | ⌘2 | ⌘3 |

Die Phasen sind Ansichten, kein Assistent: Man kann jederzeit springen. Einsteiger werden aber sanft von links nach rechts geführt.

### 6.3 Wichtige Abläufe

**Erster Start (≤ 60 s bis zum ersten Rahmen)**
1. Willkommensfenster: „Laser anschließen und einschalten.“
2. MacBurn erkennt den USB-Port, liest die GRBL-Einstellungen, schlägt Modell + Arbeitsfläche vor.
3. Maschinenprofil wählen (Modell aus der Liste) oder anlegen (Leistung, Wellenlänge, Arbeitsfläche) → passende Materialprofile stehen sofort bereit.
4. Optional: Testpuls zum Fokussieren. Fertig.

**Typischer Job**
1. SVG aus dem Finder auf die Fläche ziehen → Farben werden zu Ebenen, Absicht wird geraten (geschlossene Konturen außen = Schneiden, Flächen = Gravieren).
2. Material oben links wählen (die Liste zeigt nur Materialien für die aktive Maschine) → alle Ebenen bekommen passende Werte.
3. ⌘3 → *Rahmen* → Werkstück zurechtlegen → *Lasern*.
4. Startprüfung erscheint als Sheet, ein Klick auf *Starten*.

**Material einmessen**
Material-Popover → *Neues Material testen* → Assistent erzeugt Testraster → nach dem Brennen das beste Feld anklicken → als getestetes Materialprofil für diese Maschine gespeichert.

### 6.4 Details, die den Unterschied machen

- **Absicht-Symbole** statt Modus-Kürzeln: ✂︎ Schneiden, ▦ Gravieren, ◐ Foto, ⌇ Markieren.
- **Brennvorschau** in der Phase *Vorbereiten*: Linien in der späteren Brennfarbe statt in Ebenenfarben.
- **Warnungen direkt am Objekt** (kleines Dreieck an offener Kontur, Tooltip mit „Pfad schließen“-Knopf) statt Fehlermeldungen beim Start.
- **Befehlspalette ⌘K:** „spiegeln“, „boolesch“, „Material Acryl“ … alles per Tastatur.
- **Einheiten** konsequent mm (umschaltbar), Geschwindigkeit wahlweise mm/min oder mm/s.
- **Leere Zustände** erklären, was zu tun ist („Ziehe eine Datei hierher oder wähle ein Werkzeug“).
- **Keine modalen Fehlerdialoge** während des Laserns; Probleme erscheinen als Banner mit Aktion.

---

## 7. Entscheidungen

| Thema | Entscheidung |
|---|---|
| Zielhardware | Diodenlaser mit GRBL, Verbindung zuerst über USB. Referenzmaschine: Mecpow X3 Pro 10 W. CO₂ optional. |
| Mindest-macOS | macOS 27; ältere Versionen sind ein Bonus |
| Sprache | C++20-Kern, dünne Cocoa-Schicht in Objective-C++, CMake |
| Lizenz | Open Source, Vorschlag GPL-3.0-or-later |
| Name | MacBurn |
| Eigenständigkeit | Kein erkennbarer Bezug zu anderen Laser-Programmen: eigene Begriffe, eigene Dateiformate, eigene Symbole, kein Import fremder Projektformate, keine Erwähnung in Code, Doku, UI-Texten oder Commits. |
| Profil-Logik | Maschine → Material, mit Vererbung und Statusanzeige (Abschnitt 3) |
| Maschinenbibliothek | Vorlagen für gängige Lasertypen, mindestens mit Arbeitsfläche, Controller und Lasermodul (Abschnitt 3.2) |

**Noch offen:** `$I`/`$$` des X3 Pro auslesen und die mit `verify` markierten Werte der Vorlage bestätigen.
