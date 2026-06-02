QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

/* ==================== 优化改动：C++ 标准版本 ====================
 * 原始代码: CONFIG += c++11
 * 优化方案: CONFIG += c++17
 * 
 * 原因:
 *   1. 使用了 std::unique_ptr, std::make_unique (C++14+)
 *   2. 使用了 std::optional 可能性 (C++17)
 *   3. 更好的编译器优化
 *   4. 获得更多现代 C++ 特性
 * ================================================================ */
CONFIG += c++17

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know what to do.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
# as an example: DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ctrlbar.cpp \
    fffplay.cpp \
    ffmsg_queue.cpp \
    ijkplayercore.cpp \
    main.cpp \
    mainwindow.cpp \
    playlist.cpp \
    showindow.cpp \
    titlebar.cpp

HEADERS += \
    ctrlbar.h \
    fffplay.h \
    ffmsg.h \
    ffmsg_queue.h \
    ijkplayercore.h \
    mainwindow.h \
    playlist.h \
    showindow.h \
    titlebar.h \
    Logger.hpp \
    Mutex.hpp

FORMS += \
    ctrlbar.ui \
    mainwindow.ui \
    playlist.ui \
    showindow.ui \
    titlebar.ui

/* ==================== 优化改动：配置变量化 ====================
 * 原始代码:
 *   win32 {
 *   INCLUDEPATH += $$PWD/SDL2-2.0.10/include
 *   INCLUDEPATH += $$PWD/ffmpeg-4.2.1-win32-dev/include
 *   LIBS += $$PWD/SDL2-2.0.10/lib/x86/SDL2.lib  \
 *           $$PWD/ffmpeg-4.2.1-win32-dev/lib/avformat.lib   \
 *           ...
 *   }
 * 
 * 问题:
 *   1. 版本号硬编码，难以维护
 *   2. 如果升级 FFmpeg，需要修改多处地方
 *   3. 容易出错，不易迁移
 * 
 * 优化方案:
 *   1. 提取版本号为变量
 *   2. 统一管理依赖路径
 *   3. 便于跨平台迁移
 * ================================================================ */

# FFmpeg 配置变量，便于跨平台维护
FFMPEG_VERSION = ffmpeg-4.2.1
SDL2_VERSION = SDL2-2.0.10

win32 {
    /* 原始代码: 硬编码路径
     * 优化: 使用变量
     */
    # 定义 FFmpeg 和 SDL2 的根目录
    FFMPEG_PATH = $$PWD/$$FFMPEG_VERSION-win32-dev
    SDL2_PATH = $$PWD/$$SDL2_VERSION
    
    INCLUDEPATH += $$SDL2_PATH/include
    INCLUDEPATH += $$FFMPEG_PATH/include
    
    /* 原始代码:
     * INCLUDEPATH += $$PWD/SDL2-2.0.10/include
     * INCLUDEPATH += $$PWD/ffmpeg-4.2.1-win32-dev/include
     * LIBS += $$PWD/SDL2-2.0.10/lib/x86/SDL2.lib  \
     *         $$PWD/ffmpeg-4.2.1-win32-dev/lib/avformat.lib   \
     *         ...
     * 
     * 优化: 统一使用变量，便于维护
     */
    LIBS += $$SDL2_PATH/lib/x86/SDL2.lib \
            $$FFMPEG_PATH/lib/avformat.lib \
            $$FFMPEG_PATH/lib/avcodec.lib \
            $$FFMPEG_PATH/lib/avdevice.lib \
            $$FFMPEG_PATH/lib/avfilter.lib \
            $$FFMPEG_PATH/lib/avutil.lib \
            $$FFMPEG_PATH/lib/postproc.lib \
            $$FFMPEG_PATH/lib/swresample.lib \
            $$FFMPEG_PATH/lib/swscale.lib \
            "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x86\Ole32.Lib"
}

/* ==================== 优化改动：Unix/Linux/macOS 支持 ====================
 * 原始代码: 没有 Unix 系统的配置
 * 优化方案: 添加注释和示例配置
 * 
 * 说明:
 *   1. 大多数 Linux 发行版已预装 FFmpeg
 *   2. 使用包管理器安装更方便
 *   3. 添加配置便于开发者修改
 * ================================================================ */
unix:!android {
    # Linux 或 macOS 系统
    # 假设 FFmpeg 已通过包管理器安装到系统目录
    # 可根据实际环境调整
    # INCLUDEPATH += /usr/include
    # LIBS += -L/usr/lib -lavformat -lavcodec -lavutil -lswscale
}


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    icon.qrc
