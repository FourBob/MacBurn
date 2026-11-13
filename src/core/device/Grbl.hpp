// MacBurn – GRBL-Protokoll und serielle Schnittstelle (POSIX, ohne Cocoa).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "geometry/Geometry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mb::grbl {

// Antwort auf "?": <Idle|MPos:0.000,0.000,0.000|FS:0,0|Pn:XY>
struct Status {
    std::string state;          // Idle, Run, Hold:0, Alarm, Door:1, Home, Jog …
    Vec2 machinePos;
    std::optional<Vec2> workOffset;
    double feed = 0, spindle = 0;
    std::string pins;           // ausgelöste Eingänge, z. B. "XY" oder "D"
};
std::optional<Status> parseStatus(const std::string& line);

// "$130=410.000" → {130, 410}
struct Setting { int key = 0; double value = 0; };
std::optional<Setting> parseSetting(const std::string& line);

// "ALARM:1" / "error:20" → verständlicher Text für die Oberfläche.
std::string describeAlarm(int code);
std::string describeError(int code);

// Zustand in Alltagssprache für die Statusleiste.
std::string describeState(const std::string& state);

// Serielle Ports unter macOS: /dev/cu.*
std::vector<std::string> listSerialPorts();

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const std::string& path, int baud, std::string* error = nullptr);
    void close();
    bool isOpen() const { return fd_ >= 0; }

    bool write(const std::string& data);
    bool writeByte(unsigned char b);   // Echtzeitbefehle: '?', '!', '~', 0x18
    // Liest eine Zeile (ohne \r\n); wartet höchstens timeoutMs.
    std::optional<std::string> readLine(int timeoutMs);

private:
    int fd_ = -1;
    std::string buffer_;
};

} // namespace mb::grbl
