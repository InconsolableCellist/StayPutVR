#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <ctime>
#include <filesystem>

namespace StayPutVR {

    class Logger {
    public:
        enum class LogLevel {
            DEBUG,
            INFO,
            WARNING,
            E_ERROR,
            CRITICAL
        };

        enum class LogType {
            DRIVER,
            APPLICATION
        };

        static void Init(const std::string& logDirPath, LogType type = LogType::APPLICATION);
        static void Shutdown();

        static void Log(LogLevel level, const std::string& message);
        static void Debug(const std::string& message);
        static void Info(const std::string& message);
        static void Warning(const std::string& message);
        static void Error(const std::string& message);
        static void Critical(const std::string& message);
        
        static bool IsInitialized() { return initialized; }
        static void SetLogLevel(LogLevel level);

    private:
        static std::ofstream logFile;
        static bool initialized;
        static LogLevel minLogLevel;
        static LogType logType;
        static std::string GetTimeString();
        static std::string GetLevelString(LogLevel level);
    };

} // namespace StayPutVR 