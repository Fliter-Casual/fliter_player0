#include <stdio.h>
#include "mainwindow.h"
#include <QApplication>
#include "log/easylogging++.h"

INITIALIZE_EASYLOGGINGPP    // 初始化宏，有且只能使用一次

// 在 FFPlayer::ffplayer_create() 或 main() 的最开始调用，DirectSound 对老旧硬件和不同驱动程序的兼容性极好，几乎在所有 Windows 电脑上都能工作
void force_sdl_directsound()
{
    // Windows 下设置环境变量
    #ifdef _WIN32
        _putenv("SDL_AUDIODRIVER=directsound");
    #else
        setenv("SDL_AUDIODRIVER", "directsound", 1);
    #endif
}

#undef main
int main(int argc, char *argv[])
{
    //    el::Loggers::reconfigureAllLoggers(el::ConfigurationType::Format, "%datetime %level %func(L%line) %msg");

    el::Configurations conf;
    conf.setToDefault();
    conf.setGlobally(el::ConfigurationType::Format, "[%datetime | %level] %func(L%line) %msg");
    conf.setGlobally(el::ConfigurationType::Filename, "log_%datetime{%Y%M%d}.log");
    conf.setGlobally(el::ConfigurationType::Enabled, "true");
    conf.setGlobally(el::ConfigurationType::ToFile, "true");
    el::Loggers::reconfigureAllLoggers(conf);
    el::Loggers::reconfigureAllLoggers(el::ConfigurationType::ToStandardOutput, "true"); // 也输出一份到终端

    //    LOG(VERBOSE) << "logger test"; //该级别只能用宏VLOG而不能用宏 LOG(VERBOSE)
    LOG(TRACE) << " logger";
    //    LOG(DEBUG) << "logger test";
    LOG(INFO) << "logger test";
    LOG(WARNING) << "logger test";
    LOG(ERROR) << "logger test";

    force_sdl_directsound();
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
