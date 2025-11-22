#include "logger.h"

#if ENABLE_LOGGING

#include <mutex>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <atomic>

namespace Logger {
    std::mutex g_mutex;
    std::ofstream g_file;
    bool g_toFile = false;
    std::atomic<Logger::Level> gMinLevel(Level::DEBUG);

    const char* levelName(Logger::Level lvl) {
        switch (lvl) {
            case Logger::Level::DEBUG:   return "DEBUG";
            case Logger::Level::INFO:    return "INFO ";
            case Logger::Level::WARNING: return "WARN ";
            case Logger::Level::ERROR:   return "ERROR";
            default: return "UNKWN";
        }
    }

    std::string timestamp() {
        using namespace std::chrono;

        auto now = system_clock::now();
        auto tt = system_clock::to_time_t(now);
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm;
    #if defined(_WIN32)
        localtime_s(&tm, &tt);
    #else
        localtime_r(&tt, &tm);
    #endif

        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
           << "." << std::setw(3) << std::setfill('0') << ms.count();
        return ss.str();
    }
}

void Logger::init(const char* filePath) {
    std::lock_guard<std::mutex> lk(g_mutex);

    if (filePath) {
        g_file.open(filePath, std::ios::app);
        if (g_file.is_open()) g_toFile = true;
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_file.is_open()) g_file.close();
}

void Logger::setLevel(Level lvl) {
    gMinLevel.store(lvl);
}

void Logger::commit(Level lvl, const char* file, int line, const std::string& msg) {
    if (lvl < gMinLevel.load()) return;

    std::ostringstream ss;

    ss << timestamp()
       << " [" << levelName(lvl) << "] ";

    // file:line
    if (file) {
        std::string f(file);
        auto pos = f.find_last_of("/\\");
        if (pos != std::string::npos) f = f.substr(pos + 1);
        ss << "(" << f << ":" << line << ") ";
    }

    ss << msg;

    std::lock_guard<std::mutex> lk(g_mutex);

    if (g_toFile && g_file.is_open()) {
        g_file << ss.str() << std::endl;
        g_file.flush();
    } else {
        std::cout << ss.str() << std::endl;
    }
}

Logger::LogStream::~LogStream() {
    Logger::commit(level, file, line, buffer.str());
}


#endif // ENABLE_LOGGING