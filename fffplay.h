#ifndef FFFPLAY_H
#define FFFPLAY_H
#include <thread>
#include "ffmsg_queue.h"
#include "fffplay_def.h"

/*
 可优化点汇总（Decoder 类）：

 1. decoder_init 的 PacketQueue* 参数可以移除
    - Decoder 内部已有 queue_ 成员
    - 应在构造或初始化阶段一次性绑定，避免重复传参

 2. AVPacket pkt_ 不适合作为类成员
    - AVPacket 是短期对象，生命周期应限制在 decode loop 内
    - 建议在 decoder_decode_frame 中作为局部变量使用

 3. std::thread* decoder_thread_ 应改为 std::thread 对象
    - 避免手动 new/delete
    - 提高异常安全性，防止悬空指针

 4. nullptr 应统一替换为 nullptr
    - 符合现代 C++ 规范
    - 类型安全，避免重载歧义

 5. finished_ 的注释语义不准确
    - 不应理解为“空闲 / 工作”
    - 应表示解码线程是否结束（EOF / flush / seek / error）

 6. 析构函数建议声明为 virtual
    - 若 Decoder 作为基类被继承（AudioDecoder / VideoDecoder）
    - 否则 delete 基类指针可能导致未定义行为

 7. 成员变量初始化应尽量使用类内初始化
    - 提高可读性
    - 减少构造函数遗漏初始化的风险

 8. decoder_decode_frame / video_thread / audio_thread
    - 建议拆分为基类纯虚接口 + 子类实现
    - 更贴近 ffplay 的多媒体类型抽象
*/
class Decoder
{
public:
    Decoder();
    ~Decoder();
    AVPacket _pkt;              
    PacketQueue *_queue = nullptr;         // 数据包队列
    AVCodecContext *_avcodec_ctx = nullptr;// 解码器上下文
    int _pkt_serial = 0;             // 数据包序列号
    int _finished = 0;               // =0，解码线程正常运行，还可以继续送包,取帧；=非0，输入队列 EOF/解码完成 或 异常退出/flush/seek
    std::thread _decoder_thread;
    // 初始化解码器
    void decoder_init(AVCodecContext *avcodec_ctx,PacketQueue *queue);
    // 创建和启动线程
    int decoder_start(enum AVMediaType codec_type,const char *thread_name,void* arg);
    // 停止线程 
    void decoder_abort(FrameQueue *fq);
    // 销毁解码器
    void decoder_destroy();
    // 解码一帧数据
    int decoder_decode_frame(AVFrame *frame);
    // 获取一帧视频数据
    int get_video_frame(AVFrame *frame);
    // 添加一帧数据到帧队列
    int queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts,
                            double duration, int64_t pos, int serial);
    
    // 音频解码线程
    int audio_thread(void *arg);
    // 视频解码线程
    int video_thread(void *arg);
    
};


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
    void stream_component_close(int stream_index);

    MessageQueue _msg_queue; // 消息队列
    char *_input_filename = nullptr; // 输入文件名

    // 线程的执行函数(线程入口函数)
    int read_thread();// 读取线程, 这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染

    // 负责读取和解码的后台线程对象
    std::thread *_read_thread = nullptr;

    // 帧队列
    FrameQueue pictq;       // 视频Frame队列
    FrameQueue sampq;       // 采样Frame队列

    // 包队列
    PacketQueue audioq;        // 音频packet队列
    PacketQueue videoq;         // 视频packet队列

    int abort_request = 0;

    AVStream *audio_stream = nullptr;   // 音频流
    AVStream *video_stream = nullptr;   // 视频流
    int audio_stream_index = -1;
    int video_stream_index = -1;

    Decoder audio_dec; // 音频解码器
    Decoder video_dec; // 视频解码器

    int eof = 0;
    AVFormatContext *ic = nullptr;
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
