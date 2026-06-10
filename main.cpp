#include <stdio.h>
#include "mainwindow.h"
#include <QApplication>

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
    force_sdl_directsound();
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
