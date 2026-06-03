#pragma once

#include<iostream>
#include<string>
#include<ctime>
#include<filesystem>
#include<fstream>
#include<unistd.h>
#include<sstream>
#include<memory>
#include "Mutex.hpp"

namespace LogModule
{
    // 1. 获取时间 (兼容 C++17 及多平台)
    std::string GetTimeStamp()
    {
        time_t timestamp = time(nullptr); 
        struct tm data_time;  
        
        // 跨平台处理：Windows 用 localtime_s, Linux/Mac/Unix 用 localtime_r
        #ifdef _WIN32
            // Windows 下的线程安全版本
            localtime_s(&data_time, &timestamp);
        #else
            // Linux/Unix 下的线程安全版本
            localtime_r(&timestamp, &data_time);
        #endif

        char data_time_str[128];
        // 使用 snprintf 确保缓冲区安全
        snprintf(data_time_str, sizeof(data_time_str), "%4d-%02d-%02d %02d:%02d:%02d",
                data_time.tm_year + 1900,
                data_time.tm_mon + 1,
                data_time.tm_mday,
                data_time.tm_hour,
                data_time.tm_min,
                data_time.tm_sec);

        return std::string(data_time_str);
    }


    enum class LogLevel
    {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };

    // 2.日志等级
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

    // 3. 日志刷新

    // 定义一个抽象基类，用于实现策略模式
    // 含有纯虚函数的类叫做抽象类，因为这个类不完整，=0的纯虚函数还没有实现，不能实例化
    class LogStrategy
    {
    public:
        //virtual虚析构函数,确保通过基类指针删除派生类对象时，能正确调用派生类的析构函数
        // = default:告诉编译器生成默认的析构函数实现
        virtual ~LogStrategy() = default;

        //纯虚函数，定义接口，派生类必须实现
        virtual void SyncLog(const std::string &logmessage) = 0;
    };

    // 子类:继承纯虚接口类
    // 策略1: 控制台、终端、命令行窗口策略，就是将日志打印在终端上
    class ConsoleLogStrategy : public LogStrategy
    {
    public:
        ConsoleLogStrategy()
        {}
        ~ConsoleLogStrategy()
        {}

        //把基类未实现的纯虚函数用该子类对应的方法实现
        //这个方法(函数)把日志打印到终端上
        void SyncLog(const std::string &logmessage) override //override用于显式声明该函数是重写基类的虚函数,也会编译检查,如果基类没有对应的虚函数，编译器会报错
        {
            LockGuard LockGuard(&_mutex); //加锁，保证线程安全(原子性),策略开始执行就要执行完
            std::cout << logmessage <<std::endl;
        }

    private:
        Mutex _mutex;
    };


        static const std::string glogdir = "./log/";
        static const std::string glogfilename = "log.txt";

        // 子类：继承纯虚接口类
        // 策略2：日志输出到的文件内
        class FileLogStrategy : public LogStrategy
        {
        public:
            FileLogStrategy(const std::string &dir = glogdir, const std::string &filename = glogfilename)
            : _logdir(dir), _logfilename(filename)
            {
                // log / log.txt
                LockGuard lockguard(&_mutex);
                //std::filesystem::exists 用于检查文件或目录是否存在
                if(std::filesystem::exists(_logdir))
                {
                    return;
                }
                else
                {
                    //如果创建目录失败（比如没有写权限），程序会直接崩溃
                    //用 try-catch 处理异常,打印错误信息，程序继续运行
                    try
                    {
			//std::filesystem,C++17引入
                        std::filesystem::create_directories(_logdir);
                    }
                    //catch (const std::exception& e)	捕获所有继承自 std::exception 的异常
                    //catch (const std::filesystem::filesystem_error& e)    只捕获文件系统相关的异常,且能获得路径和错误码
                    catch(const std::filesystem::filesystem_error& e)
                    {
                        std::cerr << e.what() << '\n';
                    }
                    
                }
            }
            ~FileLogStrategy()
            {}
            //把日志输入到文件中
            void SyncLog(const std::string &logmessage) override
            {
                LockGuard lockguard(&_mutex);
                std::string target = _logdir + _logfilename;
                //创建一个文件输出流对象，以追加模式打开文件
                std::ofstream out(target,std::ios::app); //追加写入文件
                if(!out.is_open())
                {
                    return ;
                }
                out << logmessage << "\n";
                out.close();
            }

        private:
            std::string _logdir;
            std::string _logfilename;
            Mutex _mutex;
        };

    //日志类
    class Logger
    {
    public:
        Logger()
        {
        
        }
        ~Logger()
        {

        }
        void UseConsoleLogStrategy() //使用终端策略
        {
            _strategy = std::make_unique<ConsoleLogStrategy>();
        }
        void UseFileLogStrategy()
        {
            _strategy = std::make_unique<FileLogStrategy>();
        }

        //类内部的类:一条日志
        //目标是把一个类对象，变成一个string
        class LogMessage
        {
        public:
            LogMessage(LogLevel level,std::string &filename,int line,Logger &self)
                :_level(level),
                _curr_time(LogModule::GetTimeStamp()),
                _pid(getpid()),
                _filename(filename),
                _line(line),
                _logger(self)
            {
                std::stringstream ss;
                ss  << "[" << _curr_time << "] "
                    << "[" << LogLevel2String(_level) << "] "
                    << "[" << _pid << "] "
                    << "[" << _filename << "] "
                    << "[" << _line << "] "
                    << "- ";
                    _loginfo = ss.str();
            }

            template <typename T>
            LogMessage &operator<<(const T&info)
            {
                std::stringstream ss;
                ss << info;
                _loginfo +=ss.str();
                return *this;
            }

            ~LogMessage() //RAII风格的日志刷新!(析构时打印日志)
            {
                if(_logger._strategy)
                {
                    _logger._strategy->SyncLog(_loginfo);
                }
            }

        private:
            LogLevel _level;        //日志等级
            std::string _curr_time; //当前时间
            pid_t _pid;             //进程id
            std::string _filename;  //文件名
            int _line;              //行号
            std::string _loginfo;   //一条完整的日志
            Logger &_logger;        //外部类的引用,这样内部类就可以使用外部类了
        };


        // Logger对象打印日志，返回一个临时的LogMessage对象
        // 为何返回临时的内部类对象(右值)?
        // 因为作用域(生命周期)结束其析构时,其析构自动调用对应策略输出
//<<<<<<< HEAD
        //operator() 返回临时 LogMessage 对象，是为了实现链式调用和RAII 自动日志输出。
//=======
        // operator() 返回临时 LogMessage 对象，是为了实现链式调用和RAII 自动日志输出。
//>>>>>>> ddc8f3e767cf9fa73a26f0cf85d035fb65b5f188
        LogMessage operator()(LogLevel level,std::string filename,int line)
        {
            return LogMessage(level,filename,line,*this);
        }

    private:
        //父类指针，可以指向子类, 子类的临时对象可以赋值给它
        std::unique_ptr<LogStrategy> _strategy; //刷新日志的策略
	//unique_ptr:C++14,C++17完善
    };

    Logger logger;


    //__FILE__ 和 __LINE__ 是 C/C++ 的预定义宏，不需要你定义，编译器会自动提供
    //__FILE__	当前源文件的文件名
    //__LINE__	当前代码所在的行号
    // 使用宏，包装我们的日志打印过程，宏有一个特点，#define A B， B替换成为A
    #define LOG(level) logger(level, __FILE__, __LINE__)

    // 动态调整日志策略
    #define ENABLE_CONSOLE_LOG_STRATEGY() logger.UseConsoleLogStrategy()
    #define ENABLE_FILE_LOG_STRATEGY() logger.UseFileLogStrategy();
}
