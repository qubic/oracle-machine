#include "om_common/logger.h"

#if ENABLE_LOGGING

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace Logger
{
std::mutex g_mutex;
std::ofstream gLogFile;
bool glogToFile = false;
bool gLogToConsole = true;
std::atomic<Logger::Level> gMinLevel(Level::DEBUG);

const char* levelName(Logger::Level lvl)
{
    switch (lvl)
    {
    case Logger::Level::DEBUG:
        return "DBG";
    case Logger::Level::INFO:
        return "INF";
    case Logger::Level::WARNING:
        return "WRN";
    case Logger::Level::ERR:
        return "ERR";
    default:
        return "UNK";
    }
}

std::string timestamp()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    auto tt = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm;
#if defined(_WIN32)
    gmtime_s(&tm, &tt);   // (UTC)
#else
    gmtime_r(&tt, &tm);   // (UTC)
#endif

    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d.%H%M%S");
    return ss.str();
}
} // namespace Logger

void Logger::init(bool logToConsole, const char* filePath)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (filePath)
    {
        gLogFile.open(filePath, std::ios::out | std::ios::trunc);
        if (gLogFile.is_open())
        {
            glogToFile = true;
        }
    }
    gLogToConsole = logToConsole;
}

void Logger::shutdown()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (gLogFile.is_open())
        gLogFile.close();
}

void Logger::setLevel(Level lvl)
{
    gMinLevel.store(lvl);
}

void Logger::commit(Level lvl, const char* file, int line, const std::string& msg)
{
    if (lvl < gMinLevel.load())
        return;

    std::ostringstream ss;

    ss << timestamp() << " [" << levelName(lvl) << "]";

    // file:line
    if (file)
    {
        std::string f(file);
        auto pos = f.find_last_of("/\\");
        if (pos != std::string::npos)
            f = f.substr(pos + 1);
        ss << "(" << f << ":" << line << ") ";
    }

    ss << msg;

    std::lock_guard<std::mutex> lk(g_mutex);

    if (glogToFile && gLogFile.is_open())
    {
        gLogFile << ss.str() << std::endl;
        gLogFile.flush();
    }

    if (gLogToConsole)
    {
        if (lvl == Level::ERR)
        {
            std::cerr << ss.str() << std::endl;
        }
        else
        {
            std::cout << ss.str() << std::endl;
        }
    }
}

Logger::LogStream::~LogStream()
{
    Logger::commit(level, file, line, buffer.str());
}

#endif // ENABLE_LOGGING