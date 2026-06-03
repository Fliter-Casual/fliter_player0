#ifndef FFFPLAY_H
#define FFFPLAY_H
#include <thread>
#include "ffmsg_queue.h"
#include "fffplay_def.h"

class FFPlayer
{
public:
    FFPlayer();
    int ffplayer_create();  //创建播放器
    void ffplayer_destroy();
    //这个函数是 FFplay 播放器的核心准备函数，作用是异步准备媒体资源（打开文件、解封装、解码器初始化等），为后续播放做准备
    int ffplayer_prepare_async_1(char *file);

    // 播放控制
    int ffplayer_start_1();
    int ffplayer_stop_1();

    int stream_open(const char *file_name); // 打开流
    void stream_close();

    // 打开指定stream对应的解码器、创建解码线程、以及初始化对应的输出
    int stream_component_open(int stream_index);
    // 关闭指定stream对应的解码器、销毁解码线程、销毁输出，释放解码器资源
    int stream_component_close(int stream_index);

    MessageQueue _msg_queue; // 消息队列
    char *_input_filename; // 输入文件名

    // 线程的执行函数(线程入口函数)
    int read_thread();// 读取线程, 这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染

    // 负责读取和解码的后台线程对象
    std::thread *_read_thread;

    // 帧队列
    FrameQueue pictq;       // 视频Frame队列
    FrameQueue sampq;       // 采样Frame队列

    // 包队列
    PacketQueue audioq;        // 音频packet队列
    PacketQueue videoq;         // 视频packet队列

    int abort_request = 0;
    int audio_stream_index = -1;
    int video_stream_index = -1;
};

// 以下封装函数设置内联 inline :编译器会把这个函数直接展开到调用的地方，避免了函数调用的开销，提高了性能

//ffplayer_notify_msg1 是一个接口封装函数，它隐藏了消息队列的具体实现细节
inline static void ffp_notify_msg1(FFPlayer *ffp,int what)
{
    msg_queue_put_simple3(&ffp->_msg_queue,what,0,0);
}

inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1) {
    msg_queue_put_simple3(&ffp->_msg_queue, what, arg1, 0);
}

inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2) {
    msg_queue_put_simple3(&ffp->_msg_queue, what, arg1, arg2);
}

inline static void ffp_notify_msg4(FFPlayer *ffp, int what, int arg1, int arg2, void *obj, int obj_len) {
    msg_queue_put_simple4(&ffp->_msg_queue, what, arg1, arg2, obj, obj_len);
}

inline static void ffp_remove_msg(FFPlayer *ffp, int what) {
    msg_queue_remove(&ffp->_msg_queue, what);
}

#endif // FFFPLAY_H
