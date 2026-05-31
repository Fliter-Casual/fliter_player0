#include "fffplay.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"

// FFPlayer类的构造函数
FFPlayer::FFPlayer()
{

}

// 创建播放器
int FFPlayer::ffplayer_create()
{
    std::cout << "ffp_create\n";
    msg_queue_init(&_msg_queue);
    return 0;
}

// 播放器异步准备，准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
int FFPlayer::ffplayer_prepare_async_1(char *file_name)
{
    // 保存文件名
    _input_filename = strdup(file_name); // 申请内存+拷贝字符串
    int reval = stream_open(file_name);
    return reval;
}

// 打开流
int FFPlayer::stream_open(const char *file_name)
{
    // 初始化Frame帧队列
    // 初始化Packet包队列
    // 初始化时钟

    // 插件read_thread

    _read_thread = new std::thread(&FFPlayer::read_thread,this);
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
    }
}











