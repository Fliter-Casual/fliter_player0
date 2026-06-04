#pragma once

#include <iostream>
#include <string>
#include <ctime>
#include <experimental/filesystem>
#include <fstream>
#include <unistd.h>
#include <sstream>
#include <memory>
#include "Mutex.hpp"

namespace LogModule
{
    // 1. 获取时间 (声明)
    std::string GetTimeStamp();

    enum class LogLevel
    {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };

    // 2.日志等级 (声明)
    std::string LogLevel2String(LogLevel level);

    // 3. 日志刷新策略基类
    class LogStrategy
    {
    public:
        virtual ~LogStrategy() = default;
        virtual void SyncLog(const std::string &logmessage) = 0;
    };

    // 策略1: 控制台
    class ConsoleLogStrategy : public LogStrategy
    {
    public:
        ConsoleLogStrategy();
        ~ConsoleLogStrategy();
        void SyncLog(const std::string &logmessage) override;

    private:
        Mutex _mutex;
    };

    // 策略2: 文件
    class FileLogStrategy : public LogStrategy
    {
    public:
        FileLogStrategy(const std::string &dir = "./log/", const std::string &filename = "log.txt");
        ~FileLogStrategy();
        void SyncLog(const std::string &logmessage) override;

    private:
        std::string _logdir;
        std::string _logfilename;
        Mutex _mutex;
    };

    // 日志类
    class Logger
    {
    public:
        Logger();
        ~Logger();

        void UseConsoleLogStrategy();
        void UseFileLogStrategy();

        //内部类:一条日志
        class LogMessage
        {
        public:
            LogMessage(LogLevel level, std::string &filename, int line, Logger &self);

            // 模板函数必须留在头文件中，或者在cpp中显式实例化
            template <typename T>
            LogMessage &operator<<(const T& info)
            {
                std::stringstream ss;
                ss << info;
                _loginfo += ss.str();
                return *this;
            }

            ~LogMessage();

        private:
            LogLevel _level;
            std::string _curr_time;
            pid_t _pid;
            std::string _filename;
            int _line;
            std::string _loginfo;
            Logger &_logger;
        };

        LogMessage operator()(LogLevel level, std::string filename, int line);

    private:
        std::unique_ptr<LogStrategy> _strategy;
    };

    // 声明全局 logger 对象，定义在 Logger.cpp 中
    extern Logger logger;

    // 宏定义保持不变
    #define LOG(level) logger(level, __FILE__, __LINE__)
    #define ENABLE_CONSOLE_LOG_STRATEGY() logger.UseConsoleLogStrategy()
    #define ENABLE_FILE_LOG_STRATEGY() logger.UseFileLogStrategy();
}
