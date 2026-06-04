#include "fffplay.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"
#include "Logger.hpp"

using namespace LogModule;




// FFPlayer类的构造函数
FFPlayer::FFPlayer()
{
    
}

// 创建播放器
int FFPlayer::ffplayer_create()
{

    // 【新增】在这里设置日志策略
    // 选项 A: 只输出到控制台 (调试时用)
    ENABLE_CONSOLE_LOG_STRATEGY();
    
    // 选项 B: 只输出到文件 (发布时用)，后调用的会覆盖前一个。
    // ENABLE_FILE_LOG_STRATEGY();

    // 选项 C: 如果想同时输出到控制台和文件，需要先优化 Logger.hpp 
    // 目前Logger.hpp未实现该策略
    msg_queue_init(&_msg_queue);
    LOG(LogLevel::INFO) << "FFPlayer created successfully.";
    return 0;

}

// 销毁播放器
void FFPlayer::ffplayer_destroy()
{
    stream_close();

    // 销毁消息队列
    msg_queue_destroy(&_msg_queue);
    LOG(LogLevel::INFO) << "FFPlayer destroyed successfully.";
}

// 播放器异步准备，准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
int FFPlayer::ffplayer_prepare_async_1(char *file_name)
{
    // 保存文件名
    _input_filename = strdup(file_name); // 申请内存+拷贝字符串
    int reval = stream_open(file_name);
    return reval;
}

int FFPlayer::ffplayer_start_1()
{
    // 触发播放
    LOG(LogLevel::INFO) << "ffplayer_start_1() called.";
    return 0;
}

int FFPlayer::ffplayer_stop_1()
{
    // 触发停止
    abort_request = 1; // 设置停止播放
    msg_queue_abort(&_msg_queue); // 停止消息队列,禁止再插入消息
    LOG(LogLevel::INFO) << "ffplayer_stop_1() called.";
    return 0;
}

// 打开流
int FFPlayer::stream_open(const char *file_name)
{
    // 初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        LOG(LogLevel::ERROR) << "Could not initialize SDL - " << SDL_GetError();
        av_log(NULL, AV_LOG_FATAL, "Did you set the DISPLAY variable?\n");
        return -1;
    }
    // 初始化Frame帧队列
    if (frame_queue_init(&pictq , &videoq, VIDEO_PICTURE_QUEUE_SIZE) < 0)
        goto fail;
    if (frame_queue_init(&sampq, &audioq, SAMPLE_QUEUE_SIZE) < 0)
        goto fail;

    if (packet_queue_init(&videoq) < 0 ||
            packet_queue_init(&audioq) < 0 )
        goto fail;
    

    // 初始化时钟

    // 初始化音量等

    // 创建解复用器读数据线程read_thread

    _read_thread = new std::thread(&FFPlayer::read_thread,this);

    // 创建视频刷新线程
    return 0;

// 集中错误处理,保证“要么全成功，要么全清理”,且代码可读性好
fail:
    stream_close();
    return -1;
}

void FFPlayer::stream_close()
{
    abort_request = 1; // 请求退出
    if(_read_thread && _read_thread->joinable()) // 判断线程是否可执行(joinable)
    {
        _read_thread->join(); // 等待线程结束
    }

    // 关闭解复用器 avformat_close_input(&is->ic);
    // 释放packet队列
    packet_queue_destroy(&videoq);
    packet_queue_destroy(&audioq);
    // 释放frame队列
    frame_queue_destory(&pictq);
    frame_queue_destory(&sampq);

    if(_input_filename)
    {
        free(_input_filename);
        _input_filename = NULL;
    }
}

// 读取线程,这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染
int FFPlayer::read_thread()
{
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
    std::cout << "read_thread FFP_MSG_OPEN_INPUT " << this << std::endl;
    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO);
    std::cout << "read_thread FFP_MSG_FIND_STREAM_INFO " << this << std::endl;
    ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);
    std::cout << "read_thread FFP_MSG_COMPONENT_OPEN " << this << std::endl;
    ffp_notify_msg1(this, FFP_MSG_PREPARED);
    std::cout << "read_thread FFP_MSG_PREPARED " << this << std::endl;
    while (1) {
        // std::cout << "read_thread sleep, mp:" << this << std::endl;
        // 先模拟线程运行
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if(abort_request) {
            break;
        }
    }

    LOG(LogLevel::INFO) << __FUNCTION__ << "read_thread exit.";

    return 0;
}











