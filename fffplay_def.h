#ifndef FFFPLAY_DEF_H
#define FFFPLAY_DEF_H 

#include <inttypes.h> // 提供固定宽度的整数类型及其相关的格式化宏
#include <stdint.h> // 提供宽指针类型
#include <math.h> // 提供数学函数
#include <limits.h> // 提供一些数学常数
#include <signal.h> // 提供信号处理函数

extern "C" {
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"
}
#include <SDL.h>
#include <SDL_thread.h>

#include <assert.h>

// 返回码
enum RET_CODE 
{
    RET_ERR_UNKNOWN = -2,                   // 未知错误
    RET_FAIL = -1,							// 失败
    RET_OK	= 0,							// 正常
    RET_ERR_OPEN_FILE,						// 打开文件失败
    RET_ERR_NOT_SUPPORT,					// 不支持
    RET_ERR_OUTOFMEMORY,					// 没有内存
    RET_ERR_STACKOVERFLOW,					// 溢出
    RET_ERR_NULLREFERENCE,					// 空参考
    RET_ERR_ARGUMENTOUTOFRANGE,				// 参数超出范围
    RET_ERR_PARAMISMATCH,					// 参数不匹配
    RET_ERR_MISMATCH_CODE,                  // 没有匹配的编解码器
    RET_ERR_EAGAIN,
    RET_ERR_EOF 
};

//这是一个简单的链表节点结构，用于包裹 FFmpeg 的 AVPacket
typedef struct MyAVPacketList
{
    AVPacket pkt;       // ffmpeg解封装后得到的数据包
    struct MyAVPacketList *next; // 下一个数据包节点
    int serial;          // 播放序列号

// 播放序列号。这是一个非常关键的概念，用于处理“seek”（跳转/快进/快退）操作。
// 当用户执行 seek 操作时，播放器会生成一个新的序列号。旧序列号的数据包会被丢弃或忽略，确保解码器不会混合跳转前和跳转后的数据，避免画面花屏或声音异常。

} MyAVPacketList;

// 这是一个简单的队列结构，用于存放 FFmpeg 的 AVPacket
// 管理 MyAVPacketList 链表的控制器，提供了线程同步和统计功能。通常每个流（视频流、音频流、字幕流）都会有一个独立的 PacketQueue
typedef struct PacketQueue
{
    MyAVPacketList *first_pkt, *last_pkt; // 队头和队尾指针
    int nb_packets; // 队列中数据包数量
    int size; // 队列中所有数据包（AVPacket）的数据大小总和
    //用途：通常用于限制队列占用的内存总量。当 size 超过某个阈值时，生产者线程可能会阻塞，防止内存无限增长
    int64_t duration; // 队列中所有数据包的播放持续时间总和
    //用途: 用于估算当前缓冲了多少秒的内容。播放器可以通过这个值判断缓冲是否充足，或者用于同步音频/视频时钟
    int abort_request;// 用户退出请求标志
    int serial; // 当前队列的有效序列号。与新入队的 MyAVPacketList->serial 配合使用，用于检测是否需要清空队列（例如在 seek 之后）
    SDL_mutex *mutex; // 互斥锁
    SDL_cond *cond; // 条件变量
}PacketQueue;

/* 队列大小配置宏定义 */

/**
 * @brief 视频帧显示队列的大小配置
 * 
 * 视频帧队列用于存储解码后等待显示的视频帧。
 * 队列大小需要在内存占用和播放流畅度之间取得平衡：
 * - 过小可能导致播放卡顿（缓冲不足）
 * - 过大则增加内存占用并可能增加延迟
 */
#define VIDEO_PICTURE_QUEUE_SIZE_MIN        (3)     /**< 最小视频帧队列长度 */
#define VIDEO_PICTURE_QUEUE_SIZE_MAX        (16)    /**< 最大视频帧队列长度 */
#define VIDEO_PICTURE_QUEUE_SIZE_DEFAULT    (VIDEO_PICTURE_QUEUE_SIZE_MIN) /**< 默认视频帧队列长度 */
#define VIDEO_PICTURE_QUEUE_SIZE            VIDEO_PICTURE_QUEUE_SIZE_DEFAULT /**< 当前使用的视频帧队列长度 */

/**
 * @brief 字幕帧队列大小
 * 
 * 字幕通常数据量较小，但需要预加载以确保与音视频同步显示。
 * 设置为 16 可以容纳较长时间的字幕数据，避免频繁读取。
 */
#define SUBPICTURE_QUEUE_SIZE               (16)

/**
 * @brief 音频采样帧队列大小
 * 
 * 音频队列存储解码后的音频样本帧。
 * 音频对延迟敏感，且数据流连续，9 个帧通常足以保证平滑播放。
 */
#define SAMPLE_QUEUE_SIZE                   (9)

/**
 * @brief 通用帧队列最大尺寸
 * 
 * 取视频、音频、字幕三者队列大小的最大值。
 * 用于初始化某些共享资源或分配最大可能的缓冲区大小，
 * 确保能够容纳任何一种媒体类型所需的最大队列深度。
 */
#define FRAME_QUEUE_SIZE                    FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))


typedef struct AudioParams // 音频参数结构体
{
    int freq;    // 音频采样频率
    int channels;// 音频通道数
    uint64_t channel_layout; // 音频通道布局
    enum AVSampleFormat format; // 音频采样格式,如AV_SAMPLE_FMT_S16
    int frame_size; // 音频帧大小,即一个音频帧中每个声道的采样数，一个采样为多少个字节，由音频采样格式的位深决定，通常有S16,即16-bit,即2个字节
    int bytes_per_sec; // 采样频率的每秒采样数,采样率 × 声道数 × 每采样字节数,单位:字节/秒 
}AudioParams;

/* Common struct for handling all types of decoded data and allocated render buffers. */
//用于处理所有类型的解码数据以及已分配的渲染缓冲区的通用结构体
//用于缓存解码后的数据帧(和字幕)
typedef struct Frame
{
    AVFrame *frame; // ffmpeg解码后的数据帧
    //AVSubtitle sub; // ffmpeg解码后的字幕
    //int serial; // 播放序列号
    double pts; // 播放时间戳
    double duration; // 播放持续时间
    //int64_t pos; // 数据帧在文件中的位置
    int width, height; // 帧的分辨率
    int format; // 帧的像素格式
} Frame;

/**
 * @brief 帧队列结构体 (循环队列)
 * 
 * 用于存储解码后的视频、音频或字幕帧。
 * 这是一个生产者-消费者模型：
 * - 解码线程作为生产者，将解码后的帧写入队列 (通过 windex)。
 * - 播放/渲染线程作为消费者，从队列中读取帧进行显示或播放 (通过 rindex)。
 */
/* 这是一个循环队列，rindex 指向队列头部（待读取的最旧帧），windex 指向队列尾部之后的下一个空闲位置（待写入的新帧位置） */
typedef struct FrameQueue {
    Frame	queue[FRAME_QUEUE_SIZE];        /**< 帧数组，最大容量由 FRAME_QUEUE_SIZE 定义。注意：过大的值会占用大量内存 */
    int		rindex;                         /**< 读索引 (Read Index)。指向当前待读取/播放的帧。消费者使用此索引获取数据。 */
    int		windex;                         /**< 写索引 (Write Index)。指向下一个可写入的位置。生产者使用此索引存入新解码的帧。 */
    int		size;                           /**< 当前队列中已存在的帧数量 */
    int		max_size;                       /**< 队列允许的最大帧数量 (通常 <= FRAME_QUEUE_SIZE) */
    SDL_mutex	*mutex;                     /**< 互斥锁，用于保护队列数据的线程安全访问 */
    SDL_cond	*cond;                      /**< 条件变量，用于线程间同步（如队列满时阻塞生产者，队列为空时阻塞消费者） */
    PacketQueue	*pktq;                      /**< 关联的数据包队列指针，用于在需要时回溯或同步 AVPacket 信息 */
} FrameQueue;

// 队列相关
int packet_queue_put(PacketQueue *q, AVPacket *pkt);//添加一个数据包到队列
int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);//添加一个空数据包到队列, 用于结束播放
int packet_queue_init(PacketQueue *q);//初始化队列
void packet_queue_flush(PacketQueue *q);//清空队列
void packet_queue_destroy(PacketQueue *q);//销毁队列
void packet_queue_abort(PacketQueue *q);//停止队列
void packet_queue_start(PacketQueue *q);//启动队列
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);//从队列中获取一个数据包

/* 初始化FrameQueue，视频和音频keep_last（只处理最新的消息，忽略旧的）设置为1，字幕设置为0 */
int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size);
void frame_queue_destory(FrameQueue *f);
// 唤醒所有等待的线程
void frame_queue_signal(FrameQueue *f); 
/* 获取队列当前Frame, 在调用该函数前先调用frame_queue_nb_remaining确保有frame可读 */
Frame *frame_queue_peek(FrameQueue *f);

/* 获取当前Frame的下一Frame, 此时要确保queue里面至少有2个Frame */
// 不管你什么时候调用，返回来肯定不是 nullptr
Frame *frame_queue_peek_next(FrameQueue *f);
//获取last Frame：
Frame *frame_queue_peek_last(FrameQueue *f);
// 获取可写指针
Frame *frame_queue_peek_writable(FrameQueue *f);
// 获取当前待读取的帧指针（指向 queue[rindex]）
Frame *frame_queue_peek_readable(FrameQueue *f);
// 提交刚才写入的帧。更新 windex（环形向前移动），增加 size，并发送信号 (SDL_CondSignal) 唤醒正在等待读取的消费者线程
void frame_queue_push(FrameQueue *f);
// 释放当前正在被读取(消费)的帧并移动到下一帧。调用 av_frame_unref 释放当前 AVFrame 占用的内存，更新 rindex（环形向前移动），减少 size。这会腾出空间，允许生产者继续写入
void frame_queue_next(FrameQueue *f);
//检查队列中是否有可读的帧。通常用于循环判断
int frame_queue_nb_remaining(FrameQueue *f);
//用于在 Seek（跳转）操作后，告诉解复用线程应该从文件的哪个位置开始重新读取数据包
int64_t frame_queue_last_pos(FrameQueue *f);



#endif //FFFPLAY_DEF_H
