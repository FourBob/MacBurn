// MacBurn – GRBL-Gerät: eigener Thread, Statusabfrage, Streaming mit Zeichenzählung.
// Alle öffentlichen Methoden sind vom Main-Thread aus aufrufbar.
// Ereignisse kommen auf dem Geräte-Thread an; der Empfänger leitet sie weiter.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/Grbl.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mb::grbl {

struct DeviceEvent {
    enum class Kind {
        Connected,      // erste Statusmeldung empfangen
        Disconnected,   // Verbindung beendet oder verloren (text = Grund)
        Status,         // status gesetzt
        Identity,       // text = Firmware-Zeile aus $I
        Setting,        // key/value
        Alarm,          // code
        Error,          // code, text = betroffene Zeile
        Message,        // text = [MSG:…]
        Progress,       // done/total (Auftragszeilen bestätigt)
        JobFinished,    // Auftrag vollständig ausgeführt
        JobAborted,     // text = Grund
        Diagnostic,     // text = Hinweis fürs Protokoll (z. B. Verlauf vor einer Störung), nicht für die Statusleiste
        Warning,        // text = vorübergehende Störung (z. B. „antwortet seit 5 s nicht“), Statusleiste
    } kind;
    Status status;
    std::string text;
    int code = 0;
    double value = 0;
    size_t done = 0, total = 0;
};

// Ein Eintrag im Verbindungsverlauf (für das Verlaufsfenster).
struct TrafficEntry {
    uint64_t seq = 0;
    double time = 0;          // Uhrzeit, Sekunden seit 1970
    bool outgoing = false;    // → an den Laser, sonst ← vom Laser
    bool status = false;      // Statusabfrage '?' oder Statusantwort <…>
    std::string text;
};

class GrblDevice {
public:
    using Sink = std::function<void(const DeviceEvent&)>;

    // rxBuffer: Größe des Empfangspuffers der Steuerung (GRBL: 128, sicher: 120)
    explicit GrblDevice(Sink sink, size_t rxBuffer = 120);
    ~GrblDevice();
    GrblDevice(const GrblDevice&) = delete;
    GrblDevice& operator=(const GrblDevice&) = delete;

    // Übernimmt eine geöffnete Verbindung und startet den Geräte-Thread.
    // bootDelayMs: Wartezeit, bis die Steuerung nach dem Öffnen bereit ist.
    void attach(std::unique_ptr<Transport> transport, int bootDelayMs = 2500);
    void detach();
    bool attached() const { return running_; }

    void sendLine(const std::string& line);      // normaler Befehl ($H, $X, $J=…, G-Code)
    void realtime(uint8_t byte);                  // '?', '!', '~', 0x18, 0x85 …
    void startJob(std::vector<std::string> lines);
    void abortJob(const std::string& reason);     // Rest verwerfen, Laser aus, Steuerung zurücksetzen
    bool jobActive() const { return jobActive_; }

    // Verbindungsverlauf: alle Einträge mit seq > after (höchstens die letzten 5000). Thread-sicher.
    std::vector<TrafficEntry> trafficSince(uint64_t after) const;
    void clearTraffic();

    static constexpr uint8_t kFeedHold = '!';
    static constexpr uint8_t kResume = '~';
    static constexpr uint8_t kReset = 0x18;
    static constexpr uint8_t kJogCancel = 0x85;

private:
    struct Line { std::string text; bool job; };

    Sink sink_;
    size_t rxBuffer_;
    std::unique_ptr<Transport> transport_;
    std::thread worker_;
    std::atomic<bool> running_{false}, stopRequested_{false}, jobActive_{false};

    std::mutex mutex_;                 // schützt die folgenden Warteschlangen
    std::deque<Line> pending_;
    std::deque<uint8_t> realtime_;
    bool abortRequested_ = false;
    std::string abortReason_;

    // nur Geräte-Thread
    std::deque<Line> inflight_;
    size_t inflightBytes_ = 0;
    size_t jobTotal_ = 0, jobDone_ = 0;
    bool connectedReported_ = false;
    bool abortInProgress_ = false;   // eigener Reset läuft: dessen Begrüßung ist kein unerwarteter Neustart

    void run(int bootDelayMs);
    void remember(const std::string& entry);   // Verlauf der letzten Befehle/Antworten
    std::string history() const;
    std::deque<std::pair<double, std::string>> history_;   // (Sekunden seit Start, Eintrag) – nur Geräte-Thread
    std::chrono::steady_clock::time_point started_;
    void handleLine(const std::string& line);
    void pump();
    void emit(DeviceEvent e) { if (sink_) sink_(e); }
    void traffic(bool outgoing, const std::string& text, bool status = false);
    mutable std::mutex trafficMutex_;
    std::deque<TrafficEntry> traffic_;
    uint64_t trafficSeq_ = 0;
};

} // namespace mb::grbl
