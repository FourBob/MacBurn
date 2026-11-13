// MacBurn – Schnittstelle zwischen Kern und Oberfläche.
// Die Cocoa-Schicht schickt Befehle hinein und zeigt die View-Modelle an.
// Sie trifft selbst keine Entscheidungen.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/GrblDevice.hpp"
#include "document/Document.hpp"
#include "job/Job.hpp"
#include "profiles/Profiles.hpp"
#include "render/RenderList.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mb {

struct CardModel { std::string kicker, title, subtitle; };

struct ListItem {
    std::string title, detail;
    bool selected = false;
    bool separatorBefore = false;   // Gruppengrenze in Menüs
};

struct LayerRow {
    std::string name, intent, values;
    Color color;
    bool selected = false, modified = false, output = true;
};

// Generischer Inspektor: Der Kern beschreibt Felder, die Oberfläche baut sie.
struct Field {
    // ButtonRow: options = "symbol|Beschriftung"; Klick → pressButton(key + ":" + Index)
    enum class Kind { Header, Info, Choice, Number, Toggle, Button, JogPad, Progress, Anchor, ButtonRow } kind = Kind::Info;
    std::string key, label;
    std::vector<std::string> options;   // Choice
    int selected = 0;                   // Choice
    double value = 0, min = 0, max = 100, step = 1;   // Number
    std::string unit;
    bool on = false;                    // Toggle
    bool highlight = false;             // geänderter Wert / Warnung
    bool enabled = true;

    bool operator==(const Field&) const = default;
};

// Punkt der Startprüfung vor dem Lasern
struct PreflightCheck {
    std::string text, detail;
    bool automatic = false;   // vom Programm geprüft (sonst: vom Menschen abzuhaken)
    bool ok = true;           // nur bei automatischen Prüfungen
};

// Was der Hauptknopf in der Werkzeugleiste gerade tut
enum class PrimaryAction { GoToLaser, Connect, Connecting, StartJob, Pause, Resume, Unlock };

class AppController {
public:
    // profileRoots: Systemprofile (im Bundle) und Benutzerprofile.
    AppController(const std::string& systemProfiles, const std::string& userProfiles);

    ~AppController();
    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }
    // Häufige Statusmeldungen (Position): nur Statusleiste und Zeichenfläche neu zeichnen.
    void setOnStatus(std::function<void()> cb) { onStatus_ = std::move(cb); }
    // Führt eine Funktion auf dem Main-Thread aus (Cocoa: dispatch_async).
    void setMainQueue(std::function<void(std::function<void()>)> q) { mainQueue_ = std::move(q); }

    // --- Befehle ---
    void setPhase(Phase p);
    void selectMachine(int index);
    void selectVariant(int index);
    void selectMaterial(int index);
    void selectLayer(int index);
    void setChoice(const std::string& key, int index);
    void setNumber(const std::string& key, double value);
    void setToggle(const std::string& key, bool on);
    void pressButton(const std::string& key);
    void undo();
    void redo();
    std::string exportGcode() const;
    void newDocument();
    struct ImportResult { bool ok = false; std::string message; };
    // SVG importieren: Farben werden zu Ebenen (gleiche Farbe = vorhandene Ebene).
    // Ein unverändertes Beispielprojekt wird dabei ersetzt.
    ImportResult importFile(const std::string& path);
    // Öffnet .macburn-Projekte und importiert SVG
    ImportResult openFile(const std::string& path);
    ImportResult saveProject(const std::string& path);
    bool isModified() const;
    const std::string& documentPath() const { return path_; }
    std::string documentTitle() const { return title_; }

    // --- Formen bearbeiten ---
    // Maus: Punkte in mm; tolerance in mm (aus der aktuellen Zoomstufe)
    void pointerDown(Vec2 p, double tolerance, bool extend);
    void pointerDrag(Vec2 p, bool shift = false);
    // Bildschirmmaßstab, damit Anfasser immer gleich groß wirken
    void setViewScale(double pxPerMm) { pxPerMm_ = pxPerMm > 0 ? pxPerMm : 1; }
    void pointerUp();
    void nudge(double dx, double dy);   // Pfeiltasten im Gestalten/Vorbereiten
    void selectAll();
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    void rotateSelection(double degrees);
    void flipSelection(bool horizontal);
    void centerSelection();
    void skewSelection(double degX, double degY);
    // 0 links, 1 horizontal mittig, 2 rechts, 3 oben, 4 vertikal mittig, 5 unten
    void alignSelection(int mode);
    void distributeSelection(bool horizontal);   // ab drei Objekten: gleiche Abstände
    bool canDistribute() const { return selection_.size() >= 3; }
    void groupSelection();
    void ungroupSelection();
    bool canGroup() const { return selection_.size() > 1; }
    bool canUngroup() const;
    bool hasSelection() const { return !selection_.empty(); }
    bool canEdit() const { return !dev_.jobRunning; }

    // --- Gerät ---
    PrimaryAction primaryAction() const;
    void connect();
    void disconnect();
    void jog(int dx, int dy);           // Richtung, Schrittweite aus dem Inspektor
    void home();
    void unlock();
    void frame();
    std::vector<PreflightCheck> preflight() const;
    void startJob();                    // erst nach bestätigter Startprüfung
    void pauseOrResume();
    void stop();
    bool deviceConnected() const { return dev_.connected; }
    int positionMode() const { return origin_; }   // 0 = wie auf der Fläche, 1 = am Laserkopf
    int anchor() const { return anchor_; }
    bool jobRunning() const { return dev_.jobRunning; }

    // --- View-Modelle ---
    Phase phase() const { return phase_; }
    CardModel machineCard() const;
    CardModel materialCard() const;
    std::vector<ListItem> machineItems() const;
    std::vector<ListItem> materialItems() const;
    std::vector<LayerRow> layerRows() const;
    std::vector<Field> inspector() const;
    std::string estimateText() const;
    std::string statusText() const;
    std::string primaryActionTitle() const;
    std::string undoTitle() const;
    std::string redoTitle() const;
    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    const std::vector<std::string>& profileErrors() const { return store_.errors(); }
    RenderList renderList() const;
    std::string lastMessage() const { return message_; }

private:
    ProfileStore store_;
    Document doc_;
    UndoStack undo_;
    Phase phase_ = Phase::Design;
    int machine_ = 0, variant_ = 0, material_ = 0, layer_ = 0;
    Job job_;
    std::string message_;
    std::string title_ = "Beispiel";
    std::string path_;
    uint64_t savedState_ = 0;
    bool settingsDirty_ = false;
    bool isSample_ = true;
    std::function<void()> onChange_, onStatus_;
    std::function<void(std::function<void()>)> mainQueue_;

    struct DeviceState {
        bool connected = false, connecting = false, jobRunning = false;
        std::string port, identity, state, lastProblem;
        Vec2 machinePos, wco;
        size_t done = 0, total = 0;
        std::map<int, double> settings;
    } dev_;
    std::vector<std::string> ports_;
    int port_ = 0, jogStep_ = 1, origin_ = 0, anchor_ = 4;   // Anker: Mitte
    Vec2 jobOffset_;

    ObjectSet selection_;
    bool lockRatio_ = true;
    struct Drag {
        Document before;
        Vec2 start;
        bool moved = false;
        int handle = -1;     // -1 = verschieben, 0–7 = skalieren, 8 = drehen
        Rect box;            // Auswahl beim Start
    };
    double pxPerMm_ = 3;
    std::vector<Vec2> selectionHandles() const;
    std::optional<Drag> drag_;

    const MachineProfile* machine() const;
    std::vector<ProfileStore::MaterialChoice> choices() const;
    const MaterialProfile* material() const;
    MaterialStatus materialStatus() const;
    Rect workArea() const;
    void rebuild();
    void changed();
    void editLayer(const std::string& label, std::function<void(Layer&)> fn);
    void refreshPorts();
    void handleDevice(const grbl::DeviceEvent& e);
    Vec2 workPos() const { return dev_.machinePos - dev_.wco; }
    bool deviceIdle() const;
    Rect outputBounds() const;            // nur ausgegebene Ebenen
    std::optional<Vec2> offsetFor() const; // Verschiebung je nach Jobursprung; nullopt = passt nicht
    void addDeviceFields(std::vector<Field>& f) const;
    void addSelectionFields(std::vector<Field>& f) const;
    void editDoc(const std::string& label, const std::function<void(Document&)>& fn);
    void setSelectionNumber(const std::string& key, double v);

    std::unique_ptr<grbl::GrblDevice> device_;   // zuletzt: wird zuerst zerstört
};

} // namespace mb
