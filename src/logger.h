#pragma once

#define ENABLE_LOGGING 1

#if ENABLE_LOGGING


#include <sstream>

namespace Logger {

    enum class Level {
        DEBUG = 0,
        INFO,
        WARNING,
        ERROR,
        OFF
    };

    void init(const char* filePath = nullptr);
    void shutdown();
    void setLevel(Level lvl);

    // internal function used by LogStream
    void commit(Level lvl, const char* file, int line, const std::string& msg);

    class LogStream {
    public:
        LogStream(Level lvl, const char* file, int line)
            : level(lvl), file(file), line(line) {}

        // streaming operator
        template<typename T>
        LogStream& operator<<(const T& value) {
            buffer << value;
            return *this;
        }

        // support manipulators like std::endl
        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            manip(buffer);
            return *this;
        }

        ~LogStream();  // implemented in logger.cpp

    private:
        Level level;
        const char* file;
        int line;
        std::ostringstream buffer;
    };
}

// Macros — lightweight wrapper
#define LOG_DEBUG()   Logger::LogStream(Logger::Level::DEBUG,   __FILE__, __LINE__)
#define LOG_INFO()    Logger::LogStream(Logger::Level::INFO,    __FILE__, __LINE__)
#define LOG_WARNING() Logger::LogStream(Logger::Level::WARNING, __FILE__, __LINE__)
#define LOG_ERROR()   Logger::LogStream(Logger::Level::ERROR,   __FILE__, __LINE__)

#else

namespace Logger {
    enum class Level { DEBUG, INFO, WARNING, ERROR, OFF };
    inline void init(const char*) {}
    inline void shutdown() {}
    inline void setLevel(Level) {}
}

#define LOG_DEBUG() 
#define LOG_INFO()   
#define LOG_WARNING() 
#define LOG_ERROR()   


#endif