#ifndef FFFPLAY_H
#define FFFPLAY_H
#include <thread>
#include <functional>
#include "ffmsg_queue.h"
#include "fffplay_def.h"
#include "sonic.h"

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
    int _packet_pending = 0;
    AVPacket _pkt;              
    PacketQueue *_queue = nullptr;         // 数据包队列
    AVCodecContext *_avcodec_ctx = nullptr;// 解码器上下文
    int _pkt_serial = 0;             // 数据包序列号
    int _finished = 0;               // =0，解码线程正常运行，还可以继续送包,取帧；=非0，输入队列 EOF/解码完成 或 异常退出/flush/seek
    std::thread _decoder_thread ;

    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
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

    int audio_open(uint64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate,struct AudioParams *audio_hw_params);

    void audio_close();

    //获取播放时长
    long ffp_get_duration_l();
    long ffp_get_current_position_l();

    // 暂停恢复
    int ffp_pause_l();
    void toggle_pause(int pause_on);
    void toggle_pause_l(int pause_on);
    void stream_update_pause_l();
    void stream_toggle_pause_l(int pause_on);

    // seek相关
    int ffp_seek_to_l(long msec);
    // 单位是秒 整数
    int ffp_forward_to_l(long incr);
    // 单位是秒 负数
    int ffp_back_to_l(long incr);
    int ffp_forward_or_back_to_l(long incr);
    void stream_seek(int64_t pos, int64_t rel, int seek_by_bytes);

    // 截屏相关
    int ffp_screenshot_l(char *screen_path);
    void screenshot(AVFrame *frame);

    // 变速相关
    int get_target_frequency();
    int     get_target_channels();
    void ffp_set_playback_rate(float rate);
    float ffp_get_playback_rate();
    bool is_normal_playback_rate();
    int ffp_get_playback_rate_change();
    void ffp_set_playback_rate_change(int change);

    //音量相关
    void ffp_set_playback_volume(int value);

    //播放完毕相关判断 1. av_read_frame返回eof; 2. audio没有数据可以输出; 3.video没有数据可以输出
    void check_play_finish();   //如果已经结束则通知ui调用停止函数
    // 供外包获取信息
    int64_t ffp_get_property_int64(int id, int64_t default_value);
    void ffp_track_statistic_l(AVStream *st, PacketQueue *q, FFTrackCacheStatistic *cache);
    void ffp_audio_statistic_l();
    void ffp_video_statistic_l();
    MessageQueue _msg_queue;
    char *_input_filename;
    int realtime = 0;
    int  stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
    int read_thread();
    std::thread *_read_thread;

    int video_refresh_thread();
    void video_refresh(double *remaining_time);
    double vp_duration(  Frame *vp, Frame *nextvp);
    double compute_target_delay(double delay);
    void  update_video_pts(double pts, int64_t pos, int serial);

    // 视频画面输出相关
    std::thread *_video_refresh_thread = NULL;

    std::function<int(const Frame *)> _video_refresh_callback = NULL;
    void AddVideoRefreshCallback(std::function<int(const Frame *)> callback);

    int get_master_sync_type();
    double get_master_clock();
    int av_sync_type = AV_SYNC_AUDIO_MASTER;           // 音视频同步类型, 默认audio master
    Clock	audclock;             // 音频时钟
    Clock	vidclock;             // 视频时钟
    //    Clock	extclk;

    double			audio_clock = 0;            // 当前音频帧的PTS+当前帧Duration
    int             audio_clock_serial;     // 播放序列，seek可改变此值, 解码后保存
    int64_t         audio_callback_time = 0;
    // 帧队列
    FrameQueue pictq;       // 视频Frame队列
    FrameQueue sampq;       // 采样Frame队列

    // 包队列
    PacketQueue audioq;        // 音频packet队列
    PacketQueue videoq;         // 视频packet队列

    int abort_request = 0;

    AVStream *audio_stream = nullptr;   // 音频流
    AVStream *video_stream = nullptr;   // 视频流
    int force_refresh = 0;
    double frame_timer = 0;

    int audio_stream_index = -1;
    int video_stream_index = -1;

    Decoder audio_dec; // 音频解码器
    Decoder video_dec; // 视频解码器

    int eof = 0;
    int audio_no_data = 0;
    int video_no_data = 0;
    AVFormatContext *ic = nullptr;

    int paused = 0;
    // 音频输出相关
    struct AudioParams audio_src;   // 音频包解码后的frame参数(最新解码的音频参数)
    struct AudioParams audio_tgt;   // 音频输出参数,即SDL支持的音频参数(SDL音频输出需要的参数)，重采样转换参数，audio_src->audio_tgt
    struct SwrContext *swr_ctx = nullptr; // 重采样器上下文
    int audio_hw_buf_size = 0;  // 音频硬件缓冲区大小,SDL音频缓冲区大小(单位为字节)
    // 指向待播放的一帧音频数据，指向的数据区将被拷入SDL音频缓冲区，若经过重采样则指向audio_buf1，否则指向frame中的音频数据
    uint8_t *audio_buf = nullptr; // 音频缓冲区,用于存储解码后的音频数据(原始PCM)，即可能需要重采样的数据，来自解码器，如avcodec_receive_frame()
    uint8_t *audio_buf1 = nullptr;// 音频缓冲区1,用于存储重采样后的音频数据,真正送给声卡播放的数据,由 swr_convert()输出
    unsigned int audio_buf_size = 0;  // 待播放的音频数据(audio_buf指向的)的大小,还有多少字节没有播完(剩余的数据量),用于播放进度控制
    unsigned int audio_buf1_size = 0; // 申请到的音频数据(audio_buf1指向的)的实际大小,即一次重采样后 实际产出的字节数
    int audio_buf_index = 0;        // 当前播放位置在 audio_buf（或 audio_buf1）中的偏移，记录“已经播到哪里了”，配合 audio_buf_size使用,分次把数据喂给声卡;更新拷贝位置,当前音频帧中已拷入SDL音频缓冲区



    int audio_write_buf_size;
    int audio_volume = 50;   // 音量相关
    int startup_volume = 50; // 起始音量
    // seek相关
    int64_t seek_req = 0;
    int64_t seek_rel = 0;
    int64_t seek_flags = 0;
    int64_t seek_pos = 0;  // seek的位置

    // 截屏相关
    bool req_screenshot_ = false;
    char *screen_path_ = NULL;

    //单步运行
    int step = 0;
    int framedrop = 1;
    int frame_drops_late = 0;

    int pause_req = 0;
    int auto_resume = 0;
    int buffering_on = 0;
    // 变速相关
    float       pf_playback_rate = 1.0;           // 播放速率
    int         pf_playback_rate_changed = 0;   // 播放速率改变
    // 变速相关
    sonicStreamStruct *audio_speed_convert = nullptr;
    int max_frame_duration = 3600;


    // 统计相关的操作
    FFStatistic         stat;
};

//    MessageQueue _msg_queue; // 消息队列
//    char *_input_filename = nullptr; // 输入文件名

//    // 线程的执行函数(线程入口函数)
//    int read_thread();// 读取线程, 这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染

//    // 负责读取和解码的后台线程对象
//    std::thread *_read_thread = nullptr;

//    /**
//     * @brief 视频刷新线程
//     *
//     * 负责：
//     * - 按 PTS 定时刷新视频帧
//     * - 驱动 video_refresh()
//     * - 控制音视频同步
//     *
//     * @return 线程退出码
//     */
//    int video_refresh_thread();

//    /**
//     * @brief 执行一次视频刷新
//     *
//     * 根据系统时钟决定是否显示新帧，
//     * 并通过 remaining_time 返回下一次刷新等待时间。
//     *
//     * @param remaining_time 距离下一帧需要等待的时间（秒）
//     */
//    void video_refresh(double *remaining_time);
//    // 视频画面输出相关
//    std::thread *_video_refresh_thread = nullptr;
//    std::function<int(const Frame *)> _video_refresh_callback = nullptr;
//    void AddVideoRefreshCallback(std::function<int(const Frame *)> callback);

//    int get_master_sync_type();
//    double get_master_clock();
//    int av_sync_type = AV_SYNC_AUDIO_MASTER;            // 音视频同步类型, 默认audio master
//    Clock audclock;                                  // 音频时钟
//    //Clock vidclock;                                // 视频时钟
//    double audio_clock = 0;            // 当前音频帧的PTS+当前帧Duration

//    // 帧队列
//    FrameQueue pictq;       // 视频Frame队列
//    FrameQueue sampq;       // 采样Frame队列

//    // 包队列
//    PacketQueue audioq;        // 音频packet队列
//    PacketQueue videoq;         // 视频packet队列

//    int abort_request = 0;

//    AVStream *audio_stream = nullptr;   // 音频流
//    AVStream *video_stream = nullptr;   // 视频流
//    int audio_stream_index = -1;
//    int video_stream_index = -1;

//    Decoder audio_dec; // 音频解码器
//    Decoder video_dec; // 视频解码器

//    int eof = 0;
//    AVFormatContext *ic = nullptr;

//    // 音频输出相关
//    struct AudioParams audio_src;   // 音频包解码后的frame参数(最新解码的音频参数)
//    struct AudioParams audio_tgt;   // 音频输出参数,即SDL支持的音频参数(SDL音频输出需要的参数)，重采样转换参数，audio_src->audio_tgt
//    struct SwrContext *swr_ctx = nullptr; // 重采样器上下文
//    int audio_hw_buf_size = 0;  // 音频硬件缓冲区大小,SDL音频缓冲区大小(单位为字节)
//    // 指向待播放的一帧音频数据，指向的数据区将被拷入SDL音频缓冲区，若经过重采样则指向audio_buf1，否则指向frame中的音频数据
//    uint8_t *audio_buf = nullptr; // 音频缓冲区,用于存储解码后的音频数据(原始PCM)，即可能需要重采样的数据，来自解码器，如avcodec_receive_frame()
//    uint8_t *audio_buf1 = nullptr;// 音频缓冲区1,用于存储重采样后的音频数据,真正送给声卡播放的数据,由 swr_convert()输出
//    unsigned int audio_buf_size = 0;  // 待播放的音频数据(audio_buf指向的)的大小,还有多少字节没有播完(剩余的数据量),用于播放进度控制
//    unsigned int audio_buf1_size = 0; // 申请到的音频数据(audio_buf1指向的)的实际大小,即一次重采样后 实际产出的字节数
//    int audio_buf_index = 0;        // 当前播放位置在 audio_buf（或 audio_buf1）中的偏移，记录“已经播到哪里了”，配合 audio_buf_size使用,分次把数据喂给声卡;更新拷贝位置,当前音频帧中已拷入SDL音频缓冲区
//};

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
