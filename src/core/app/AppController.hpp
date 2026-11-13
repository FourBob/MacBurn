// MacBurn – Schnittstelle zwischen Kern und Oberfläche.
// Die Cocoa-Schicht schickt Befehle hinein und zeigt die View-Modelle an.
// Sie trifft selbst keine Entscheidungen.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "document/Document.hpp"
#include "job/Job.hpp"
#include "profiles/Profiles.hpp"
#include "render/RenderList.hpp"

#include <functional>
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
    enum class Kind { Header, Info, Choice, Number, Toggle, Button } kind = Kind::Info;
    std::string key, label;
    std::vector<std::string> options;   // Choice
    int selected = 0;                   // Choice
    double value = 0, min = 0, max = 100, step = 1;   // Number
    std::string unit;
    bool on = false;                    // Toggle
    bool highlight = false;             // geänderter Wert / Warnung
};

class AppController {
public:
    // profileRoots: Systemprofile (im Bundle) und Benutzerprofile.
    AppController(const std::string& systemProfiles, const std::string& userProfiles);

    void setOnChange(std::function<void()> cb) { onChange_ = std::move(cb); }

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
    std::function<void()> onChange_;

    const MachineProfile* machine() const;
    std::vector<ProfileStore::MaterialChoice> choices() const;
    const MaterialProfile* material() const;
    MaterialStatus materialStatus() const;
    Rect workArea() const;
    void rebuild();
    void changed();
    void editLayer(const std::string& label, std::function<void(Layer&)> fn);
};

} // namespace mb
