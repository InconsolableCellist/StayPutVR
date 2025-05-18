#include "Logger.hpp"
#include <iomanip>
#include <sstream>
#include <filesystem>
#include "Config.hpp"

namespace StayPutVR {

    std::ofstream Logger::logFile;
    bool Logger::initialized = false;
    Logger::LogLevel Logger::minLogLevel = Logger::LogLevel::WARNING;
    Logger::LogType Logger::logType = Logger::LogType::APPLICATION;

    void Logger::Init(const std::string& logDirPath, LogType type) {
        if (initialized) {
            return;
        }

        logType = type;

        try {
            std::filesystem::path logDir(logDirPath);
            if (!std::filesystem::exists(logDir)) {
                std::filesystem::create_directories(logDir);
            }

            // Create different log files for driver and application
            std::string logFileName;
            if (logType == LogType::DRIVER) {
                logFileName = "stayputvr_driver.log";
            } else {
                logFileName = "stayputvr_application.log";
            }

            std::filesystem::path logFilePath = logDir / logFileName;
            
            // Handle old log file
            if (std::filesystem::exists(logFilePath)) {
                // Check if _old exists and delete it
                std::filesystem::path oldLogPath = logDir / (logFileName + "_old");
                if (std::filesystem::exists(oldLogPath)) {
                    std::filesystem::remove(oldLogPath);
                }
                // Rename current to _old
                std::filesystem::rename(logFilePath, oldLogPath);
            }
            
            logFile.open(logFilePath, std::ios::out | std::ios::app);
            
            if (logFile.is_open()) {
                initialized = true;
                std::string separator(50, '-');
                logFile << separator << std::endl;
                logFile << "Log started at " << GetTimeString() << std::endl;
                logFile << separator << std::endl;
                logFile.flush();
                
                std::cout << "StayPutVR Logger initialized with log file: " << logFilePath << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error initializing logger: " << e.what() << std::endl;
        }
    }

    void Logger::SetLogLevel(LogLevel level) {
        minLogLevel = level;
    }

    void Logger::Shutdown() {
        if (initialized && logFile.is_open()) {
            logFile << "Log ended at " << GetTimeString() << std::endl;
            logFile.close();
            initialized = false;
        }
    }

    void Logger::Log(LogLevel level, const std::string& message) {
        if (level < minLogLevel) {
            return;
        }

        if (!initialized || !logFile.is_open()) {
            std::cerr << GetTimeString() << " [" << GetLevelString(level) << "] " << message << std::endl;
            return;
        }

        try {
            std::string logEntry = GetTimeString() + " [" + GetLevelString(level) + "] " + message;
            
            logFile << logEntry << std::endl;
            logFile.flush();
            
            if (level >= LogLevel::WARNING) {
                std::cerr << logEntry << std::endl;
            }
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
    
    void Logger::LoadLogLevelFromConfig(const Config& config) {
        // Get log level from config
        minLogLevel = StringToLogLevel(config.log_level);
    }
    
    Logger::LogLevel Logger::StringToLogLevel(const std::string& levelStr) {
        if (levelStr == "DEBUG") return LogLevel::DEBUG;
        if (levelStr == "INFO") return LogLevel::INFO;
        if (levelStr == "WARNING") return LogLevel::WARNING;
        if (levelStr == "ERROR") return LogLevel::E_ERROR;
        if (levelStr == "CRITICAL") return LogLevel::CRITICAL;
        
        // Default to WARNING if invalid string
        return LogLevel::WARNING;
    }
    
    std::string Logger::LogLevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO";
            case LogLevel::WARNING:  return "WARNING";
            case LogLevel::E_ERROR:  return "ERROR";
            case LogLevel::CRITICAL: return "CRITICAL";
            default:                 return "WARNING"; // Default
        }
    }
} // namespace StayPutVR 