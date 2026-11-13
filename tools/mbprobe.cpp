// mbprobe – liest Firmware-Info ($I) und Einstellungen ($$) von einem GRBL-Laser.
// Nutzung: mbprobe            → listet serielle Ports
//          mbprobe <port> [baud] [lines]
//          lines: "low" (Standard, DTR/RTS aus), "high" (beide an), "reset" (ESP32-Reset-Puls)
// Sendet nur Abfragen, bewegt nichts und schaltet den Laser nicht ein.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/Grbl.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace mb::grbl;

static void drain(SerialPort& port, int quietMs, int totalMs) {
    auto start = std::chrono::steady_clock::now();
    auto last = start;
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() > quietMs) break;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > totalMs) break;
        if (auto line = port.readLine(50)) {
            last = std::chrono::steady_clock::now();
            if (line->empty()) continue;
            std::printf("%s", line->c_str());
            if (auto s = parseSetting(*line)) {
                if (s->key == 30) std::printf("    ← S-Wert für 100 %% Leistung");
                if (s->key == 32) std::printf("    ← Laser-Modus %s", s->value != 0 ? "an" : "AUS");
                if (s->key == 110 || s->key == 111) std::printf("    ← max. Geschwindigkeit mm/min");
                if (s->key == 120 || s->key == 121) std::printf("    ← Beschleunigung mm/s²");
                if (s->key == 130 || s->key == 131) std::printf("    ← Verfahrweg mm");
            } else if (line->rfind("ALARM:", 0) == 0) {
                std::printf("    ← %s", describeAlarm(std::atoi(line->c_str() + 6)).c_str());
            } else if (auto st = parseStatus(*line)) {
                std::printf("    ← %s", describeState(st->state).c_str());
            }
            std::printf("\n");
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        auto ports = listSerialPorts();
        std::printf("Serielle Ports:\n");
        for (auto& p : ports) std::printf("  %s\n", p.c_str());
        if (ports.empty()) std::printf("  (keine – Laser per USB anschließen und einschalten)\n");
        std::printf("\nNutzung: %s <port> [baud=115200] [low|high|reset]\n", argv[0]);
        return 0;
    }
    int baud = argc > 2 ? std::atoi(argv[2]) : 115200;
    SerialPort port;
    std::string err;
    if (!port.open(argv[1], baud, &err)) {
        std::fprintf(stderr, "Port lässt sich nicht öffnen: %s\n", err.c_str());
        return 1;
    }
    std::string lines = argc > 3 ? argv[3] : "low";
    if (lines == "high") port.setLines(true, true);
    else if (lines == "reset") {
        port.setLines(false, true);   // EN low
        usleep(150000);
        port.setLines(false, false);  // EN high, IO0 high → normaler Start
    } else port.setLines(false, false);
    std::printf("Warte auf Start der Steuerung …\n");
    usleep(2500000);
    std::printf("== Begrüßung ==\n");
    port.write("\r\n\r\n");
    drain(port, 1500, 4000);
    std::printf("\n== Status (?) ==\n");
    port.writeByte('?');
    drain(port, 500, 2000);
    std::printf("\n== Firmware ($I) ==\n");
    port.write("$I\n");
    drain(port, 800, 3000);
    std::printf("\n== Einstellungen ($$) ==\n");
    port.write("$$\n");
    drain(port, 800, 5000);
    return 0;
}
