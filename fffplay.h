#ifndef FFFPLAY_H
#define FFFPLAY_H
#include <thread>
#include "ffmsg_queue.h"

class FFPlayer
{
public:
    FFPlayer();
    int ffplayer_create();  //创建播放器
    //这个函数是 FFplay 播放器的核心准备函数，作用是异步准备媒体资源（打开文件、解封装、解码器初始化等），为后续播放做准备
    int ffplayer_prepare_async_1(char *file);
    int stream_open(const char *file_name); // 打开流
    MessageQueue _msg_queue; // 消息队列
    char *_input_filename; // 输入文件名
    // 线程的执行函数(线程入口函数)
    int read_thread();// 读取线程, 这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染
    // 负责读取和解码的后台线程对象
    std::thread *_read_thread;
};

//ffplayer_notify_msg1 是一个接口封装函数，它隐藏了消息队列的具体实现细节
inline static void ffplayer_notify_msg1(FFPlayer *ffp,int what)
{
    msg_queue_put_simple3(&ffp->_msg_queue,what,0,0);
}

#endif // FFFPLAY_H
