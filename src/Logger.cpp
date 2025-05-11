#include "Logger.hpp"
#include <iomanip>
#include <sstream>

namespace StayPutVR {

    std::ofstream Logger::logFile;
    bool Logger::initialized = false;

    void Logger::Init(const std::string& logDirPath) {
        if (initialized) {
            return;
        }

        try {
            std::filesystem::path logDir(logDirPath);
            if (!std::filesystem::exists(logDir)) {
                std::filesystem::create_directories(logDir);
            }

            std::filesystem::path logFilePath = logDir / "stayputvr.log";
            logFile.open(logFilePath, std::ios::out | std::ios::app);
            
            if (logFile.is_open()) {
                initialized = true;
                std::string separator(50, '-');
                logFile << separator << std::endl;
                logFile << "Log started at " << GetTimeString() << std::endl;
                logFile << separator << std::endl;
                logFile.flush();
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error initializing logger: " << e.what() << std::endl;
        }
    }

    void Logger::Shutdown() {
        if (initialized && logFile.is_open()) {
            logFile << "Log ended at " << GetTimeString() << std::endl;
            logFile.close();
            initialized = false;
        }
    }

    void Logger::Log(LogLevel level, const std::string& message) {
        if (!initialized || !logFile.is_open()) {
            return;
        }

        try {
            logFile << GetTimeString() << " [" << GetLevelString(level) << "] " << message << std::endl;
            logFile.flush(); // Ensure it's written immediately in case of crashes
        }
        catch (const std::exception& e) {
            std::cerr << "Error writing to log: " << e.what() << std::endl;
        }
    }

    void Logger::Debug(const std::string& message) {
        Log(LogLevel::DEBUG, message);
    }

    void Logger::Info(const std::string& message) {
        Log(LogLevel::INFO, message);
    }

    void Logger::Warning(const std::string& message) {
        Log(LogLevel::WARNING, message);
    }

    void Logger::Error(const std::string& message) {
        Log(LogLevel::E_ERROR, message);
    }

    void Logger::Critical(const std::string& message) {
        Log(LogLevel::CRITICAL, message);
    }

    std::string Logger::GetTimeString() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    std::string Logger::GetLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO";
            case LogLevel::WARNING:  return "WARNING";
            case LogLevel::E_ERROR:    return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default:                 return "UNKNOWN";
        }
    }

} // namespace StayPutVR 