#pragma once

#define ENABLE_LOGGING 1

#if ENABLE_LOGGING

#include <sstream>

// Simple logger use normal C++ streams and file output
// TODO: consider using third-party logging library for more features

namespace Logger
{

enum class Level
{
    DEBUG = 0,
    INFO,
    WARNING,
    ERROR,
    OFF
};

void init(bool logToConsole = true, const char* filePath = nullptr);
void shutdown();
void setLevel(Level lvl);

// internal function used by LogStream
void commit(Level lvl, const char* file, int line, const std::string& msg);

class LogStream
{
public:
    LogStream(Level lvl, const char* file, int line) : level(lvl), file(file), line(line) {}

    // streaming operator
    template <typename T>
    LogStream& operator<<(const T& value)
    {
        buffer << value;
        return *this;
    }

    // support manipulators like std::endl
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        manip(buffer);
        return *this;
    }

    ~LogStream(); // implemented in logger.cpp

private:
    Level level;
    const char* file;
    int line;
    std::ostringstream buffer;
};
} // namespace Logger

// Macros — lightweight wrapper
#define OM_LOG_DEBUG() Logger::LogStream(Logger::Level::DEBUG, NULL, 0)
#define OM_LOG_INFO() Logger::LogStream(Logger::Level::INFO, NULL, 0)
#define OM_LOG_WARNING() Logger::LogStream(Logger::Level::WARNING, NULL, 0)
#define OM_LOG_ERROR() Logger::LogStream(Logger::Level::ERROR, __FILE__, __LINE__)

#else

namespace Logger
{
enum class Level
{
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    OFF
};
inline void init(const char*) {}
inline void shutdown() {}
inline void setLevel(Level) {}
} // namespace Logger

#define OM_LOG_DEBUG()
#define OM_LOG_INFO()
#define OM_LOG_WARNING()
#define OM_LOG_ERROR()

#endif