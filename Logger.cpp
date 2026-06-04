#include "Logger.hpp"

namespace LogModule
{
    // 1. 获取时间实现
    std::string GetTimeStamp()
    {
        time_t timestamp = time(nullptr);
        struct tm data_time;

        #ifdef _WIN32
            localtime_s(&data_time, &timestamp);
        #else
            localtime_r(&timestamp, &data_time);
        #endif

        char data_time_str[128];
        snprintf(data_time_str, sizeof(data_time_str), "%4d-%02d-%02d %02d:%02d:%02d",
                data_time.tm_year + 1900,
                data_time.tm_mon + 1,
                data_time.tm_mday,
                data_time.tm_hour,
                data_time.tm_min,
                data_time.tm_sec);

        return std::string(data_time_str);
    }

    // 2. 日志等级转换实现
    std::string LogLevel2String(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    // ConsoleLogStrategy 实现
    ConsoleLogStrategy::ConsoleLogStrategy()
    {}

    ConsoleLogStrategy::~ConsoleLogStrategy()
    {}

    void ConsoleLogStrategy::SyncLog(const std::string &logmessage)
    {
        LockGuard lockGuard(&_mutex);
        std::cout << logmessage << std::endl;
    }

    // FileLogStrategy 实现
    FileLogStrategy::FileLogStrategy(const std::string &dir, const std::string &filename)
        : _logdir(dir), _logfilename(filename)
    {
        LockGuard lockGuard(&_mutex);
        if (std::experimental::filesystem::exists(_logdir))
        {
            return;
        }
        else
        {
            try
            {
                std::experimental::filesystem::create_directories(_logdir);
            }
            catch (const std::experimental::filesystem::filesystem_error& e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    }

    FileLogStrategy::~FileLogStrategy()
    {}

    void FileLogStrategy::SyncLog(const std::string &logmessage)
    {
        LockGuard lockGuard(&_mutex);
        std::string target = _logdir + _logfilename;
        std::ofstream out(target, std::ios::app);
        if (!out.is_open())
        {
            return;
        }
        out << logmessage << "\n";
        out.close();
    }

    // Logger 实现
    Logger::Logger()
    {
    }

    Logger::~Logger()
    {
    }

    void Logger::UseConsoleLogStrategy()
    {
        _strategy = std::make_unique<ConsoleLogStrategy>();
    }

    void Logger::UseFileLogStrategy()
    {
        _strategy = std::make_unique<FileLogStrategy>();
    }

    // LogMessage 实现
    Logger::LogMessage::LogMessage(LogLevel level, std::string &filename, int line, Logger &self)
        : _level(level),
          _curr_time(LogModule::GetTimeStamp()),
          _pid(getpid()),
          _filename(filename),
          _line(line),
          _logger(self)
    {
        std::stringstream ss;
        ss << "[" << _curr_time << "] "
           << "[" << LogLevel2String(_level) << "] "
           << "[" << _pid << "] "
           << "[" << _filename << "] "
           << "[" << _line << "] "
           << "- ";
        _loginfo = ss.str();
    }

    Logger::LogMessage::~LogMessage()
    {
        if (_logger._strategy)
        {
            _logger._strategy->SyncLog(_loginfo);
        }
    }

    Logger::LogMessage Logger::operator()(LogLevel level, std::string filename, int line)
    {
        return LogMessage(level, filename, line, *this);
    }

    // 全局 logger 对象定义
    Logger logger;
}
