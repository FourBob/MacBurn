// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/GrblDevice.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace mb::grbl {

using Clock = std::chrono::steady_clock;

GrblDevice::GrblDevice(Sink sink, size_t rxBuffer) : sink_(std::move(sink)), rxBuffer_(rxBuffer) {}

GrblDevice::~GrblDevice() { detach(); }

void GrblDevice::attach(std::unique_ptr<Transport> transport, int bootDelayMs) {
    detach();
    transport_ = std::move(transport);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
        realtime_.clear();
        abortRequested_ = false;
    }
    inflight_.clear();
    inflightBytes_ = 0;
    jobTotal_ = jobDone_ = 0;
    jobActive_ = false;
    connectedReported_ = false;
    abortInProgress_ = false;
    history_.clear();
    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&GrblDevice::run, this, bootDelayMs);
}

void GrblDevice::detach() {
    if (!worker_.joinable()) return;
    if (jobActive_) {
        // Beim Trennen nie einen laufenden Auftrag weiterlaufen lassen.
        std::lock_guard<std::mutex> lock(mutex_);
        realtime_.push_back(kFeedHold);
        realtime_.push_back(kReset);
    }
    stopRequested_ = true;
    worker_.join();
    running_ = false;
}

void GrblDevice::sendLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back({line, false});
}

void GrblDevice::realtime(uint8_t byte) {
    std::lock_guard<std::mutex> lock(mutex_);
    realtime_.push_back(byte);
}

void GrblDevice::startJob(std::vector<std::string> lines) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& l : lines) if (!l.empty() && l[0] != ';') pending_.push_back({std::move(l), true});
    jobActive_ = true;
    abortRequested_ = false;
}

void GrblDevice::abortJob(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    abortRequested_ = true;
    abortReason_ = reason;
}

namespace {
std::string realtimeName(uint8_t b) {
    switch (b) {
        case '?': return "?";
        case '!': return "! (anhalten)";
        case '~': return "~ (fortsetzen)";
        case 0x18: return "Strg-X (zurücksetzen)";
        case 0x85: return "Bewegung abbrechen (0x85)";
        default: { char x[16]; std::snprintf(x, sizeof x, "0x%02X", b); return x; }
    }
}
} // namespace

void GrblDevice::traffic(bool outgoing, const std::string& text, bool status) {
    std::lock_guard<std::mutex> lock(trafficMutex_);
    TrafficEntry e;
    e.seq = ++trafficSeq_;
    e.time = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    e.outgoing = outgoing;
    e.status = status;
    e.text = text;
    traffic_.push_back(std::move(e));
    while (traffic_.size() > 5000) traffic_.pop_front();
}

std::vector<TrafficEntry> GrblDevice::trafficSince(uint64_t after) const {
    std::lock_guard<std::mutex> lock(trafficMutex_);
    std::vector<TrafficEntry> out;
    auto it = std::lower_bound(traffic_.begin(), traffic_.end(), after + 1,
                               [](const TrafficEntry& e, uint64_t s) { return e.seq < s; });
    out.assign(it, traffic_.end());
    return out;
}

void GrblDevice::clearTraffic() {
    std::lock_guard<std::mutex> lock(trafficMutex_);
    traffic_.clear();
}

void GrblDevice::remember(const std::string& entry) {
    double t = std::chrono::duration<double>(Clock::now() - started_).count();
    history_.push_back({t, entry});
    while (history_.size() > 40) history_.pop_front();
}

std::string GrblDevice::history() const {
    std::string out;
    char b[32];
    for (const auto& [t, e] : history_) {
        std::snprintf(b, sizeof b, "%9.2f s  ", t);
        out += "\n    " + std::string(b) + e;
    }
    return out;
}

void GrblDevice::run(int bootDelayMs) {
    started_ = Clock::now();
    // Steuerung starten lassen; Begrüßung und Startmeldungen verwerfen.
    auto bootEnd = Clock::now() + std::chrono::milliseconds(bootDelayMs);
    while (Clock::now() < bootEnd && !stopRequested_)
        if (auto l = transport_->readLine(50); l && !l->empty()) traffic(false, *l);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_front({"$$", false});
        pending_.push_front({"$I", false});
    }
    transport_->write("?");
    traffic(true, "?", true);

    auto lastPoll = Clock::now();
    auto lastAnswer = Clock::now();
    std::string lostReason;
    bool warned = false;

    while (!stopRequested_) {
        // Abbruch: Rest verwerfen, anhalten, zurücksetzen (schaltet den Laser sofort ab)
        bool abort = false;
        std::string reason;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (abortRequested_) {
                abort = true;
                reason = abortReason_;
                abortRequested_ = false;
                std::deque<Line> keep;
                for (auto& l : pending_) if (!l.job) keep.push_back(l);
                pending_.swap(keep);
            }
        }
        if (abort) {
            abortInProgress_ = true;
            transport_->write(std::string(1, char(kFeedHold)));
            traffic(true, realtimeName(kFeedHold));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            transport_->write(std::string(1, char(kReset)));
            traffic(true, realtimeName(kReset));
            inflight_.clear();
            inflightBytes_ = 0;
            if (jobActive_) { jobActive_ = false; emit({DeviceEvent::Kind::JobAborted, {}, reason}); }
            emit({DeviceEvent::Kind::Diagnostic, {}, "Abbruch: " + reason + " Verlauf:" + history()});
        }

        pump();

        if (auto line = transport_->readLine(20)) {
            auto silent = Clock::now() - lastAnswer;
            if (warned) {
                emit({DeviceEvent::Kind::Diagnostic, {}, "Laser antwortet wieder (nach " +
                      std::to_string(std::chrono::duration_cast<std::chrono::seconds>(silent).count()) + " s Stille)."});
                emit({DeviceEvent::Kind::Warning, {}, ""});   // Hinweis in der Statusleiste löschen
                warned = false;
            }
            lastAnswer = Clock::now();
            if (!line->empty()) {
                traffic(false, *line, line->front() == '<');
                if (line->front() != '<') remember("← " + *line);   // Statuszeilen nicht, sonst ist der Verlauf nach 8 s voll
                handleLine(*line);
            }
        }

        auto now = Clock::now();
        if (now - lastPoll > std::chrono::milliseconds(200)) {
            if (!transport_->write("?")) {
                lostReason = transport_->stillPresent() ? "Die Verbindung zum Laser ist abgebrochen."
                                                        : "Die USB-Verbindung ist weg (Kabel, Wackelkontakt oder Störung).";
                break;
            }
            traffic(true, "?", true);
            lastPoll = now;
        }
        // Stille: erst warnen, im Auftrag länger warten. Steht die Verbindung wirklich, hat die Steuerung
        // ohnehin keine neuen Befehle und bleibt stehen (Laser-Modus: Strahl aus ohne Bewegung).
        auto silence = now - lastAnswer;
        if (!warned && connectedReported_ && silence > std::chrono::seconds(5)) {
            warned = true;
            emit({DeviceEvent::Kind::Warning, {}, transport_->stillPresent()
                      ? "Laser antwortet seit 5 s nicht – warte weiter …" : "USB-Anschluss ist verschwunden – warte …"});
            emit({DeviceEvent::Kind::Diagnostic, {}, "Seit 5 s keine Antwort. USB-Anschluss " +
                      std::string(transport_->stillPresent() ? "vorhanden" : "VERSCHWUNDEN") + ", Zeilen in Arbeit: " +
                      std::to_string(inflight_.size()) + ". Letzter Verlauf:" + history()});
        }
        auto limit = std::chrono::seconds(jobActive_ ? 20 : 5);
        if (silence > limit) {
            if (!transport_->stillPresent()) lostReason = "Die USB-Verbindung ist weg (Kabel, Wackelkontakt oder Störung).";
            else lostReason = connectedReported_ ? "Der Laser antwortet nicht mehr." :
                "Der Laser antwortet nicht. Ist er eingeschaltet (Netzteil) und der Not-Aus gelöst?";
            emit({DeviceEvent::Kind::Diagnostic, {}, "Aufgegeben nach " +
                      std::to_string(std::chrono::duration_cast<std::chrono::seconds>(silence).count()) + " s Stille. Verlauf:" + history()});
            break;
        }
    }

    if (jobActive_) {
        transport_->write(std::string(1, char(kFeedHold)));
        transport_->write(std::string(1, char(kReset)));
        jobActive_ = false;
    }
    transport_->close();
    running_ = false;
    emit({DeviceEvent::Kind::Disconnected, {}, lostReason});
}

void GrblDevice::pump() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!realtime_.empty()) {
        transport_->write(std::string(1, char(realtime_.front())));
        traffic(true, realtimeName(realtime_.front()), realtime_.front() == '?');
        realtime_.pop_front();
    }
    // Zeichenzählung: nur so viel senden, wie in den Empfangspuffer passt.
    while (!pending_.empty()) {
        size_t len = pending_.front().text.size() + 1;
        if (inflightBytes_ + len > rxBuffer_ && !inflight_.empty()) break;
        Line l = pending_.front();
        pending_.pop_front();
        if (!transport_->write(l.text + "\n")) break;
        remember("→ " + l.text);
        traffic(true, l.text);
        inflightBytes_ += len;
        inflight_.push_back(l);
        if (l.job) ++jobTotal_;
    }
}

void GrblDevice::handleLine(const std::string& line) {
    auto ack = [&](bool ok, int code) {
        if (inflight_.empty()) return;
        Line l = inflight_.front();
        inflight_.pop_front();
        inflightBytes_ -= std::min(inflightBytes_, l.text.size() + 1);
        if (!ok) {
            emit({DeviceEvent::Kind::Error, {}, l.text, code});
            if (l.job) abortJob("Fehler in Zeile „" + l.text + "“: " + describeError(code));
            return;
        }
        if (l.job) {
            ++jobDone_;
            size_t total;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                total = jobDone_;
                for (auto& p : pending_) if (p.job) ++total;
            }
            for (auto& p : inflight_) if (p.job) ++total;
            DeviceEvent e{DeviceEvent::Kind::Progress};
            e.done = jobDone_;
            e.total = total;
            emit(e);
        }
    };

    if (line == "ok") { ack(true, 0); return; }
    if (line.rfind("error:", 0) == 0) { ack(false, std::atoi(line.c_str() + 6)); return; }
    if (auto st = parseStatus(line)) {
        if (!connectedReported_) { connectedReported_ = true; emit({DeviceEvent::Kind::Connected, *st}); }
        DeviceEvent e{DeviceEvent::Kind::Status, *st};
        emit(e);
        // Auftrag fertig, wenn alles bestätigt ist und die Maschine steht.
        bool pendingJob = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& p : pending_) if (p.job) { pendingJob = true; break; }
        }
        bool inflightJob = false;
        for (auto& p : inflight_) if (p.job) { inflightJob = true; break; }
        if (jobActive_ && !pendingJob && !inflightJob && st->state == "Idle") {
            jobActive_ = false;
            emit({DeviceEvent::Kind::JobFinished});
            jobTotal_ = jobDone_ = 0;
        }
        return;
    }
    if (line.rfind("ALARM:", 0) == 0) {
        int code = std::atoi(line.c_str() + 6);
        {
            // Nach einem Alarm nie weiterfahren: alle wartenden Befehle verwerfen.
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.clear();
        }
        emit({DeviceEvent::Kind::Alarm, {}, describeAlarm(code), code});
        if (jobActive_) abortJob(describeAlarm(code));
        return;
    }
    if (auto s = parseSetting(line)) {
        DeviceEvent e{DeviceEvent::Kind::Setting};
        e.code = s->key;
        e.value = s->value;
        emit(e);
        return;
    }
    if (line.rfind("[VER:", 0) == 0 || line.rfind("[PRODUCER:", 0) == 0) {
        emit({DeviceEvent::Kind::Identity, {}, line.substr(1, line.size() - 2)});
        return;
    }
    if (line.rfind("[MSG:", 0) == 0) {
        emit({DeviceEvent::Kind::Message, {}, line.substr(5, line.size() - 6)});
        return;
    }
    if (line.rfind("Grbl", 0) == 0) {
        // Neustart der Steuerung (nach unserem Reset – oder unerwartet, z. B. Spannungseinbruch): Puffer ist leer.
        emit({DeviceEvent::Kind::Diagnostic, {}, "Steuerung meldet sich neu: „" + line + "“. Verlauf:" + history()});
        bool unexpected = jobActive_ && !abortInProgress_;
        abortInProgress_ = false;   // Begrüßung nach unserem Reset ist da – der nächste Neustart wäre wieder unerwartet
        inflight_.clear();
        inflightBytes_ = 0;
        if (unexpected) abortJob("Die Steuerung ist mitten im Auftrag neu gestartet (z. B. Spannungseinbruch oder Störung).");
    }
}

} // namespace mb::grbl
