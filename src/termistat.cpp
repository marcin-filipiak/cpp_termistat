#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sys/statvfs.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

void setNonBlocking(bool enable) {
    static struct termios oldt;
    static bool saved = false;

    if (enable) {
        if (!saved) {
            tcgetattr(STDIN_FILENO, &oldt);
            saved = true;
        }
        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // disable canonical mode and echo
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    } else {
        if (saved) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
        }
    }
}

/**
 * @brief Structure representing battery information.
 */
struct BatteryInfo {
    int capacity;           ///< Battery charge percentage
    std::string status;     ///< Battery status (Charging, Discharging, Full, etc.)
    bool available;         ///< Indicates if battery info is available
};

using namespace std;

/**
 * @brief Clears the terminal screen using ANSI escape codes.
 */
void clearScreen() {
    cout << "\033[2J\033[1;1H";
}

/**
 * @brief Draws a colored progress bar with optional inverted color logic.
 *
 * Colors indicate usage levels:
 * - Normal mode: Green (low), Yellow (medium), Red (high)
 * - Inverted mode: Red (low), Yellow (medium), Green (high)
 *
 * @param percent Progress percentage (0-100).
 * @param width Width of the progress bar in characters.
 * @param invertColors Whether to invert the color scheme (default: false).
 */
void drawProgressBar(float percent, int width = 30, bool invertColors = false) {
    int pos = percent * width / 100;
    string color;

    if (!invertColors) {
        // Normal color scheme: green -> yellow -> red
        if (percent < 60.0f)
            color = "\033[42m"; // Green background
        else if (percent < 85.0f)
            color = "\033[43m"; // Yellow background
        else
            color = "\033[41m"; // Red background
    } else {
        // Inverted color scheme: red -> yellow -> green
        if (percent < 30.0f)
            color = "\033[41m"; // Red background
        else if (percent < 75.0f)
            color = "\033[43m"; // Yellow background
        else
            color = "\033[42m"; // Green background
    }

    cout << "[";
    for (int i = 0; i < width; ++i) {
        if (i < pos)
            cout << color << " " << "\033[0m";  // Colored block for used portion
        else
            cout << "\033[100m \033[0m";         // Gray block for remaining portion
    }
    cout << "] " << fixed << setprecision(1) << percent << "%";
}

/**
 * @brief Prints a formatted section title in blue bold.
 *
 * @param title Section title text.
 */
void drawTitle(const string& title) {
    cout << "\033[1;34m==== " << title << " ====\033[0m\n";
}

/**
 * @brief Displays memory usage statistics with a progress bar.
 *
 * Reads /proc/meminfo for total and available memory,
 * calculates usage and displays numeric and graphical info.
 */
void showMemory() {
    ifstream meminfo("/proc/meminfo");
    string line;
    long memTotal = 0, memAvailable = 0;

    while (getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0)
            memTotal = stol(line.substr(9));
        if (line.find("MemAvailable:") == 0)
            memAvailable = stol(line.substr(13));
    }

    long used = memTotal - memAvailable;
    float percent = 100.0f * used / memTotal;

    drawTitle("Memory");
    cout << "Used: " << used / 1024 << " MB / " << memTotal / 1024 << " MB\n";
    drawProgressBar(percent, 40);
    cout << "\n\n";
}

/**
 * @brief Reads CPU temperature from sysfs.
 *
 * @return CPU temperature in degrees Celsius or -1.0 if unavailable.
 */
float readCPUTemperature() {
    const char* path = "/sys/class/thermal/thermal_zone0/temp";
    ifstream file(path);
    if (!file.is_open()) return -1.0f;

    int tempMilliC;
    file >> tempMilliC;
    return tempMilliC / 1000.0f;  // Convert millidegrees to degrees Celsius
}

/**
 * @brief Attempts to read CPU fan RPM from available hwmon devices.
 *
 * Iterates over /sys/class/hwmon to find fan input files.
 *
 * @return Fan speed in RPM, or -1 if not available.
 */
int readFanRPM() {
    const string basePath = "/sys/class/hwmon/";
    for (const auto& entry : filesystem::directory_iterator(basePath)) {
        string namePath = entry.path().string() + "/name";
        ifstream nameFile(namePath);
        if (!nameFile.is_open()) continue;

        string chipName;
        getline(nameFile, chipName);

        // Optional: filter chipName if needed (e.g., == "nct6775")

        for (int i = 1; i <= 5; ++i) {
            string fanPath = entry.path().string() + "/fan" + to_string(i) + "_input";
            ifstream fanFile(fanPath);
            if (fanFile.is_open()) {
                int rpm;
                fanFile >> rpm;
                if (rpm > 0)
                    return rpm;
            }
        }
    }
    return -1;
}

/**
 * @brief Displays CPU usage, temperature, and fan speed with progress bars.
 *
 * Parses /proc/stat to calculate CPU usage delta,
 * reads CPU temperature and fan RPM if available.
 */
void showCPU() {
    static long prevIdle = 0, prevTotal = 0;

    ifstream stat("/proc/stat");
    string cpu;
    long user, nice, system, idle, iowait, irq, softirq;
    stat >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

    long idleTime = idle + iowait;
    long totalTime = user + nice + system + idleTime + irq + softirq;

    long deltaIdle = idleTime - prevIdle;
    long deltaTotal = totalTime - prevTotal;
    float usage = 0.0f;
    if (deltaTotal != 0)
        usage = 100.0f * (deltaTotal - deltaIdle) / deltaTotal;

    prevIdle = idleTime;
    prevTotal = totalTime;

    float temp = readCPUTemperature();

    drawTitle("CPU");
    cout << "Usage: ";
    drawProgressBar(usage, 40);
    cout << "\n";

    if (temp > 0)
        cout << "Temp: " << fixed << setprecision(1) << temp << " °C\n";

    int fanRPM = readFanRPM();
    if (fanRPM > 0)
        cout << "Fan:  " << fanRPM << " RPM\n";

    cout << "\n";
}

/**
 * @brief Reads battery capacity and status from sysfs.
 *
 * @return BatteryInfo struct with capacity, status, and availability.
 */
BatteryInfo readBattery() {
    BatteryInfo info{ -1, "Unknown", false };
    const std::string basePath = "/sys/class/power_supply/";
    const std::string batteryPath = basePath + "BAT0/";

    ifstream capFile(batteryPath + "capacity");
    ifstream statusFile(batteryPath + "status");

    if (capFile.is_open() && statusFile.is_open()) {
        capFile >> info.capacity;
        getline(statusFile, info.status);
        info.available = true;
    }

    return info;
}

/**
 * @brief Displays battery status and charge percentage with an inverted color progress bar.
 */
void showBattery() {
    BatteryInfo bat = readBattery();
    drawTitle("Battery");

    if (bat.available) {
        cout << bat.status << "\n";
        drawProgressBar(bat.capacity, 40, true);
        cout << "\n\n";
    } else {
        cout << "Battery info not available\n\n";
    }
}

/**
 * @brief Displays disk usage for mounted filesystems excluding system and device mounts.
 *
 * Uses statvfs to retrieve space information and displays usage percentage.
 */
void showDisk() {
    drawTitle("Disks");
    ifstream mounts("/proc/mounts");
    string line;

    while (getline(mounts, line)) {
        istringstream iss(line);
        string device, mountpoint;
        iss >> device >> mountpoint;

        // Skip device and system mounts
        if (mountpoint.find("/dev") != string::npos || mountpoint.find("/sys") != string::npos)
            continue;

        struct statvfs stat;
        if (statvfs(mountpoint.c_str(), &stat) == 0) {
            unsigned long long total = stat.f_blocks * stat.f_frsize;
            unsigned long long free = stat.f_bfree * stat.f_frsize;
            unsigned long long used = total - free;
            float percent = 100.0f * used / total;

            cout << mountpoint << ": "
                 << used / (1024 * 1024) << " MB / "
                 << total / (1024 * 1024) << " MB ("
                 << fixed << setprecision(1) << percent << "%)\n";
        }
    }
    cout << "\n";
}

/**
 * @brief Displays network interface RX and TX statistics, including WiFi signal if available.
 *
 * Parses /proc/net/dev for interface byte counters,
 * uses iwconfig output to find wireless signal strength.
 */
void showNetwork() {
    drawTitle("Network");

    ifstream net("/proc/net/dev");
    string line;
    getline(net, line); // skip header
    getline(net, line); // skip header

    while (getline(net, line)) {
        istringstream iss(line);
        string iface;
        getline(iss, iface, ':');
        string trimmed_iface = iface;
        trimmed_iface.erase(remove(trimmed_iface.begin(), trimmed_iface.end(), ' '), trimmed_iface.end());

        long rx, tx;
        iss >> rx;
        for (int i = 0; i < 7; ++i) iss >> tx; // skip fields to reach tx

        cout << trimmed_iface << " → RX: " << rx / 1024 << " KB, TX: " << tx / 1024 << " KB\n";
    }

    // Retrieve WiFi signal level if available
    FILE* fp = popen("iwconfig 2>/dev/null", "r");
    if (fp) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), fp)) {
            string s(buffer);
            if (s.find("Signal level=") != string::npos) {
                size_t pos = s.find("Signal level=");
                cout << "\nWiFi Signal: " << s.substr(pos);
            }
        }
        pclose(fp);
    }
    cout << "\n";
}

/**
 * @brief Main application loop.
 *
 * Clears the screen, displays all system statistics every second.
 */
int main() {
    cout << "Press ENTER to quit\n";
    setNonBlocking(true);

    while (true) {
        clearScreen();
        cout << "\033[1;32m*** TermiStat ***\033[0m\n\n";

        showMemory();
        showCPU();
        showBattery();
        showDisk();
        showNetwork();

        using namespace std::chrono_literals;
        for (int i = 0; i < 10; ++i) { // 1 sec total
            std::this_thread::sleep_for(100ms);
            char c;
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if (n > 0 && (c == '\n' || c == '\r')) {
                setNonBlocking(false);
                return 0; // exit on Enter
            }
        }
    }
    setNonBlocking(false);
    return 0;
}


