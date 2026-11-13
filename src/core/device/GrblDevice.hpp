// MacBurn – GRBL-Gerät: eigener Thread, Statusabfrage, Streaming mit Zeichenzählung.
// Alle öffentlichen Methoden sind vom Main-Thread aus aufrufbar.
// Ereignisse kommen auf dem Geräte-Thread an; der Empfänger leitet sie weiter.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "device/Grbl.hpp"

#include <atomic>
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
    } kind;
    Status status;
    std::string text;
    int code = 0;
    double value = 0;
    size_t done = 0, total = 0;
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

    void run(int bootDelayMs);
    void handleLine(const std::string& line);
    void pump();
    void emit(DeviceEvent e) { if (sink_) sink_(e); }
};

} // namespace mb::grbl
