// SPDX-License-Identifier: GPL-3.0-or-later
#include "device/Grbl.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <termios.h>
#include <unistd.h>

namespace mb::grbl {

namespace {

std::vector<double> numbers(const std::string& csv) {
    std::vector<double> out;
    std::stringstream ss(csv);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(std::strtod(part.c_str(), nullptr));
    return out;
}

} // namespace

std::optional<Status> parseStatus(const std::string& line) {
    if (line.size() < 3 || line.front() != '<' || line.back() != '>') return std::nullopt;
    std::string body = line.substr(1, line.size() - 2);
    std::stringstream ss(body);
    std::string field;
    Status st;
    bool first = true;
    while (std::getline(ss, field, '|')) {
        if (first) { st.state = field; first = false; continue; }
        auto colon = field.find(':');
        if (colon == std::string::npos) continue;
        std::string key = field.substr(0, colon), val = field.substr(colon + 1);
        auto n = numbers(val);
        if ((key == "MPos" || key == "WPos") && n.size() >= 2) st.machinePos = {n[0], n[1]};
        else if (key == "WCO" && n.size() >= 2) st.workOffset = Vec2{n[0], n[1]};
        else if (key == "FS" && n.size() >= 2) { st.feed = n[0]; st.spindle = n[1]; }
        else if (key == "F" && !n.empty()) st.feed = n[0];
        else if (key == "Pn") st.pins = val;
    }
    if (st.state.empty()) return std::nullopt;
    return st;
}

std::optional<Setting> parseSetting(const std::string& line) {
    if (line.size() < 4 || line[0] != '$') return std::nullopt;
    auto eq = line.find('=');
    if (eq == std::string::npos) return std::nullopt;
    char* end = nullptr;
    long key = std::strtol(line.c_str() + 1, &end, 10);
    if (end != line.c_str() + eq) return std::nullopt;
    return Setting{int(key), std::strtod(line.c_str() + eq + 1, nullptr)};
}

std::string describeAlarm(int code) {
    switch (code) {
        case 1: return "Endschalter ausgelöst. Position ist nicht mehr sicher, bitte neu referenzieren.";
        case 2: return "Ziel liegt außerhalb der Arbeitsfläche.";
        case 3: return "Während der Bewegung zurückgesetzt. Position bitte neu referenzieren.";
        case 4: return "Sonde in falschem Zustand.";
        case 5: return "Sonde hat nicht ausgelöst.";
        case 6: return "Referenzfahrt abgebrochen.";
        case 7: return "Referenzfahrt abgebrochen: Sicherheitstür geöffnet.";
        case 8: return "Referenzfahrt: Endschalter wurde nicht freigefahren.";
        case 9: return "Referenzfahrt: Endschalter nicht gefunden.";
        case 10: return "Not-Aus oder Sicherheitssensor ausgelöst.";
        default: return "Alarm " + std::to_string(code) + ".";
    }
}

std::string describeError(int code) {
    switch (code) {
        case 1: return "Befehl nicht verstanden.";
        case 2: return "Zahl fehlt oder ist ungültig.";
        case 9: return "Gerät ist gesperrt (Alarm). Erst entsperren oder referenzieren.";
        case 15: return "Bewegung würde die Arbeitsfläche verlassen.";
        case 20: return "Nicht unterstützter G-Code-Befehl.";
        case 22: return "Vorschub fehlt.";
        case 24: return "Mehrere Achsbefehle in einer Zeile.";
        default: return "Fehler " + std::to_string(code) + ".";
    }
}

std::string describeState(const std::string& s) {
    std::string base = s.substr(0, s.find(':'));
    if (base == "Idle") return "Bereit";
    if (base == "Run") return "Lasert …";
    if (base == "Hold") return "Pausiert";
    if (base == "Jog") return "Bewegt sich";
    if (base == "Home") return "Referenzfahrt";
    if (base == "Alarm") return "Alarm";
    if (base == "Door") return "Sicherheitskontakt offen";
    if (base == "Check") return "Prüfmodus";
    if (base == "Sleep") return "Ruhezustand";
    return s;
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator("/dev", ec)) {
        auto name = e.path().filename().string();
        if (name.rfind("cu.", 0) != 0) continue;
        // Nur USB-Seriell (usbserial, usbmodem, wchusbserial, SLAB_USBtoUART) – keine Kopfhörer & Co.
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.find("usb") == std::string::npos) continue;
        out.push_back(e.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& path, int baud, std::string* error) {
    close();
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) { if (error) *error = std::strerror(errno); return false; }
    termios tio{};
    tcgetattr(fd_, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;
    cfsetspeed(&tio, speed_t(baud));
    if (tcsetattr(fd_, TCSANOW, &tio) != 0) {
        if (error) *error = std::strerror(errno);
        close();
        return false;
    }
    buffer_.clear();
    return true;
}

void SerialPort::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

bool SerialPort::write(const std::string& data) {
    if (fd_ < 0) return false;
    size_t done = 0;
    while (done < data.size()) {
        ssize_t n = ::write(fd_, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EAGAIN) { pollfd p{fd_, POLLOUT, 0}; ::poll(&p, 1, 50); continue; }
            return false;
        }
        done += size_t(n);
    }
    return true;
}

bool SerialPort::writeByte(unsigned char b) { return write(std::string(1, char(b))); }

std::optional<std::string> SerialPort::readLine(int timeoutMs) {
    auto take = [&]() -> std::optional<std::string> {
        auto nl = buffer_.find('\n');
        if (nl == std::string::npos) return std::nullopt;
        std::string line = buffer_.substr(0, nl);
        buffer_.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    };
    if (auto l = take()) return l;
    if (fd_ < 0) return std::nullopt;
    pollfd p{fd_, POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0) return std::nullopt;
    char buf[512];
    ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n > 0) buffer_.append(buf, size_t(n));
    return take();
}

} // namespace mb::grbl
