#include "fffplay.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"
#include "Logger.hpp"
// #include <experimental/filesystem>
// namespace fs = std::experimental::filesystem;

using namespace LogModule;

// FFPlayer类的构造函数
FFPlayer::FFPlayer()
{
}

void print_error(const char *filename, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    av_log(nullptr, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
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
}

int FFPlayer::ffplayer_stop_1()
{
    // 触发停止
    abort_request = 1;            // 设置停止播放
    msg_queue_abort(&_msg_queue); // 停止消息队列,禁止再插入消息
    LOG(LogLevel::INFO) << "ffplayer_stop_1() called.";
}

// 打开流
int FFPlayer::stream_open(const char *file_name)
{
    // 初始化SDL,以允许音频输出,SDL_Init 成功返回0，失败返回负数
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        LOG(LogLevel::ERROR) << "Could not initialize SDL - " << SDL_GetError();
        // av_log(nullptr, AV_LOG_FATAL, "Did you set the DISPLAY variable?\n");
        return -1;
    }
    // 初始化视频帧队列
    if (frame_queue_init(&pictq, &videoq, VIDEO_PICTURE_QUEUE_SIZE) < 0)
        goto fail;
    //  初始化音频帧队列
    if (frame_queue_init(&sampq, &audioq, SAMPLE_QUEUE_SIZE) < 0)
        goto fail;

    // 初始化packet队列
    if (packet_queue_init(&videoq) < 0 ||
        packet_queue_init(&audioq) < 0)
        goto fail;

    // 初始化时钟

    // 初始化音量等

    // 创建解复用器读数据线程read_thread

    _read_thread = new std::thread(&FFPlayer::read_thread, this);

    // 创建视频刷新线程
    return 0;

// 集中错误处理,保证“要么全成功，要么全清理”,且代码可读性好
fail:
    stream_close();
    return -1;
}

void FFPlayer::stream_close()
{
    abort_request = 1;                            // 请求退出（请求关闭read_thread线程）
    if (_read_thread && _read_thread->joinable()) // 判断线程是否可执行(joinable)
    {
        _read_thread->join(); // 等待线程结束
    }

    /* close each stream */
    if (audio_stream_index >= 0)
    {
        stream_component_close(audio_stream_index);
    }
    if (video_stream_index >= 0)
    {
        stream_component_close(video_stream_index);
    }

    // 关闭解复用器 avformat_close_input(&is->ic);

    // 释放packet队列
    packet_queue_destroy(&videoq);
    packet_queue_destroy(&audioq);
    // 释放frame队列
    frame_queue_destory(&pictq);
    frame_queue_destory(&sampq);

    // 释放其他资源
    if (_input_filename)
    {
        free(_input_filename);
        _input_filename = nullptr;
    }
}

// 如果想指定解码器怎么处理
int FFPlayer::stream_component_open(int stream_index)
{
    // 解码器上下文(实例)(此处)
    AVCodecContext *avctx = nullptr;
    // 解码器
    AVCodec *codec = nullptr;
    int sample_rate = 0;
    int nb_channels = 0;
    int64_t channel_layout = 0;
    int ret = 0;

    // 判断stream_index是否合法
    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;

    /*  为解码器分配一个编解码器上下文结构体 */
    avctx = avcodec_alloc_context3(nullptr);
    if (!avctx)
    {
        LOG(LogLevel::ERROR) << "avcodec_alloc_context3() failed.";
        return AVERROR(ENOMEM);
    }
    /* 将码流中的编解码器信息拷贝到新分配的编解码器上下文结构体 */
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;

    // 设置pkt_timebase
    // avctx->pkt_timebase = ic->streams[stream_index]->time_base;

    /* 根据codec_id查找解码器 */
    codec = avcodec_find_decoder(avctx->codec_id);
    if (!codec)
    {
        av_log(nullptr, AV_LOG_WARNING,
               "No decoder could be found for codec %s\n", avcodec_get_name(avctx->codec_id));
        ret = AVERROR(EINVAL);
        goto fail;
    }
    if ((ret = avcodec_open2(avctx, codec, nullptr)) < 0) // 打开解码器
    {
        av_log(nullptr, AV_LOG_ERROR, "Failed to open codec for stream #%u\n", stream_index);
        goto fail;
    }
    switch (avctx->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        // 音频解码器
        // 从解码器实例(上下文)中获取音频参数
        audio_stream_index = stream_index;      // 保存音频流索引
        audio_stream = ic->streams[stream_index];   // 保存音频流
        sample_rate = avctx->sample_rate;       // 采样率
        nb_channels = avctx->channels;          // 通道数
        channel_layout = avctx->channel_layout; // 通道布局

        // 初始化ffplay封装的音频解码器, 并将解码器上下文 avctx和 解码器Decoder绑定
        audio_dec.decoder_init(avctx,&audioq);
        // 启动音频解码线程
        audio_dec.decoder_start(AVMEDIA_TYPE_AUDIO,"audio_thread",this);
        // 允许音频输出
        break;
    case AVMEDIA_TYPE_VIDEO:
        // 视频解码器
        // 从解码器实例(上下文)中获取视频参数
        video_stream_index = stream_index;    // 获取视频流索引
        video_stream = ic->streams[stream_index]; // 获取视频流
        // 初始化ffplay封装的视频解码器
        video_dec.decoder_init(avctx,&videoq); // 保存解码器上下文，并关联视频包队列
        // 启动视频频解码线程
        if ((ret = video_dec.decoder_start(AVMEDIA_TYPE_VIDEO, "video_decoder",this)) < 0)
             goto out;
        break;
    default:
        break;
    }

    goto out;
fail:
    avcodec_free_context(&avctx);

out:
    return ret;
}

/**
 * @brief 关闭指定的媒体流组件，释放相关资源并重置状态。
 *
 * 该函数用于停止指定索引流的解码过程，清理音频或视频相关的内部状态。
 * 目前具体的资源释放逻辑（如线程终止、设备关闭、内存释放）已被注释，
 * 仅保留状态重置部分。
 *
 * @param stream_index 要关闭的流在 AVFormatContext 中的索引。
 *                     如果索引无效，函数将直接返回。
 * @return 无返回值。
 */
void FFPlayer::stream_component_close(int stream_index)
{
    AVCodecParameters *codecpar = nullptr;

    // 验证流索引的有效性，防止越界访问
    if (stream_index < 0 || stream_index >= ic->nb_streams)
    {
        return;
    }
    // 获取指定索引流的编解码参数
    codecpar = ic->streams[stream_index]->codecpar;

    // 根据媒体类型执行特定的清理操作（当前具体实现已注释）
    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        LOG(LogLevel::DEBUG) << "stream_component_close() audio.";

        // 停止音频解码线程
        // decoder_abort(&is->auddec, &is->sampq);
        // 释放音频解码器
        // decoder_destroy(&is->auddec);
        // 释放音频解码器实例
        // avcodec_free_context()

        // 请求终止解码器线程
        audio_dec.decoder_abort(&sampq);
        // 关闭音频设备
        // 销毁解码器
        audio_dec.decoder_destroy();
        // 释放重采样器
        // 释放audio buf
        //      decoder_abort(&is->auddec, &is->sampq); // 解码器线程请求abort的时候有调用 packet_queue_abort
        //      SDL_CloseAudioDevice(audio_dev);        // 关闭音频设备
        //      decoder_destroy(&is->auddec);           // 销毁解码器
        //      swr_free(&is->swr_ctx);                 // 释放重采样器
        //      av_freep(&is->audio_buf1);              // 释放audio buf1
        //      is->audio_buf1_size = 0;                // 重置audio buf1_size
        //      is->audio_buf = nullptr;                   // 释放audio buf
        break;
    case AVMEDIA_TYPE_VIDEO:
        LOG(LogLevel::DEBUG) << "stream_component_close() video.";
        // 停止视频解码线程
        // decoder_abort(&is->viddec, &is->pictq);
        // 释放视频解码器
        // decoder_destroy(&is->viddec);
        // 释放视频解码器实例
        // avcodec_free_context()

        // 请求终止解码器线程
        // 关闭音频设备
        // 销毁解码器
        video_dec.decoder_abort(&pictq);
        video_dec.decoder_destroy();
        break;

    default:
        break;
    }

//    ic->streams[stream_index]->discard = AVDISCARD_ALL;  // 这个又有什么用?答：丢弃流数据，不进行解码
    // 重置对应媒体类型的全局状态指针和索引
    switch (codecpar->codec_type)
    {
    case AVMEDIA_TYPE_AUDIO:
        audio_stream = nullptr;
        audio_stream_index = -1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        video_stream = nullptr;
        video_stream_index = -1;
        break;
    default:
        break;
    }

}

// 读取线程,这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染
int FFPlayer::read_thread()
{
    int err,i,ret; 
    int st_index[AVMEDIA_TYPE_NB]; // 媒体类型索引
    AVPacket pkt1;
    AVPacket *pkt = &pkt1; //这是“用栈上临时变量 + 指针传递”的惯用法，用于兼容需要 AVPacket*的 FFmpeg API

    // 初始化为-1,如果一直为-1说明没相应stream
    memset(st_index, -1, sizeof(st_index));
    video_stream_index = -1;
    audio_stream_index = -1;
    eof = 0;

    // 创建解封装器实例，为最上层的结构体，表示输入上下文
    ic = avformat_alloc_context();
    if (!ic)
    {
        LOG(LogLevel::ERROR) <<  "Could not allocate context.";
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // Debug
    // std::string current_dir = fs::current_path().string();
    // LOG(LogLevel::INFO) << "Current Working Directory: " << current_dir;

    // 打开文件,主要是探测协议类型，如果是网络文件则创建网络连接等
    err = avformat_open_input(&ic, _input_filename, nullptr, nullptr);
    if (err < 0)
    {
        char errbuf[128];
        av_strerror(err, errbuf, sizeof(errbuf));
        LOG(LogLevel::ERROR) << "Could not open source file " << _input_filename<< ", Error code: " << err 
                             << ", Details: " << errbuf;
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT); // 发送消息给UI线程，通知开始打开输入文件
    LOG(LogLevel::INFO) << "read_thread: FFP_MSG_FIND_STREAM_INFO " << _input_filename  << this;


    // 获取输入流信息，填充AVFormatContext结构体
    err = avformat_find_stream_info(ic, nullptr);
    if (err < 0)
    {
        LOG(LogLevel::ERROR) << _input_filename << " Could not find stream info.";
        ret = -1;
        goto fail;
    }

    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO); // 发送消息给UI线程，通知开始寻找流信息
    LOG(LogLevel::INFO) << "read_thread: avformat_find_stream_info() success. FFP_MSG_FIND_STREAM_INFO " << this;


    // 寻找媒体类型索引
    st_index[AVMEDIA_TYPE_VIDEO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                st_index[AVMEDIA_TYPE_VIDEO], -1, nullptr, 0);

    st_index[AVMEDIA_TYPE_AUDIO] =
            av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                st_index[AVMEDIA_TYPE_AUDIO],
                                st_index[AVMEDIA_TYPE_VIDEO],
                                nullptr, 0);

    /* open the streams */
    // 打开视频、音频解码器。在此会打开相应解码器，并创建相应的解码线程
    if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {// 如果有音频流则打开音频流
        stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
    }

    ret = -1;
    if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) { // 如果有视频流则打开视频流
        ret = stream_component_open( st_index[AVMEDIA_TYPE_VIDEO]);
    }

    ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);// 通知UI线程，已经打开媒体流
    LOG(LogLevel::INFO) << "read_thread: FFP_MSG_COMPONENT_OPEN " << this;

    if (video_stream_index < 0 && audio_stream_index < 0) {
        av_log(nullptr, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               _input_filename);
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_PREPARED); // 发送消息给UI线程，通知准备完毕
    LOG(LogLevel::INFO) << "read_thread: FFP_MSG_PREPARED " << this;

    while (1)
    {
        // std::cout << "read_thread sleep, mp:" << this << std::endl;
        // 先模拟线程运行
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (abort_request)
        {
            break;
        }
    }

    LOG(LogLevel::INFO) << __FUNCTION__ << "read_thread exit.";

    return 0;
fail:
    return -1;
}

Decoder::Decoder()
{
    av_init_packet(&_pkt);
}

Decoder::~Decoder()
{

}

void Decoder::decoder_init(AVCodecContext *avctx, PacketQueue *queue)
{
    _avcodec_ctx = avctx;
    _queue = queue;
}

int Decoder::decoder_start(AVMediaType codec_type, const char *thread_name, void *arg)
{
    // 启用包队列
    packet_queue_start(_queue);
    // 创建线程
    if(codec_type == AVMEDIA_TYPE_VIDEO)
        _decoder_thread = std::thread(&Decoder::video_thread, this,arg);
    else if(codec_type == AVMEDIA_TYPE_AUDIO)
        _decoder_thread = std::thread(&Decoder::audio_thread, this,arg);
    else
        return -1;
    return 0;
}

void Decoder::decoder_abort(FrameQueue *fq)
{
    packet_queue_abort(_queue); // 请求退出包队列
    frame_queue_signal(fq);     // 唤醒阻塞的帧队列
    if(_decoder_thread.joinable()) // 线程可执行,可被 join（等待结束）
    {
        _decoder_thread.join(); // 等待解码线程结束
    }
    packet_queue_flush(_queue);
}

void Decoder::decoder_destroy()
{
    av_packet_unref(&_pkt);
    avcodec_free_context(&_avcodec_ctx);
}

// 返回值-1: 请求退出
//       0: 解码已经结束了，不再有数据可以读取
//       1: 获取到解码后的frame
int Decoder::decoder_decode_frame(AVFrame *frame) // 解码一帧数据
{
    int ret = AVERROR(EAGAIN);

    for(;;)
    {
        AVPacket pkt;
        do  // 第一个循环，先把codec里的frame全部读取
        {
            // decoder_abort调用的时候 触发queue_->abort_request为1
            if(_queue->abort_request) // 请求退出
            {
                return -1;
            }
            switch (_avcodec_ctx->codec_type)
            {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(_avcodec_ctx, frame);
                    if(ret >= 0)
                    {
//                      if (decoder_reorder_pts == -1) {
//                          frame->pts = frame->best_effort_timestamp;
//                    } else if (!decoder_reorder_pts) {
//                          frame->pts = frame->pkt_dts;
//                    }
                    }
                    else
                    {
                        char errbuf[1024] = {0};
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        LOG(LogLevel::ERROR) << "avcodec_receive_frame() failed. Error code: " << ret 
                                         << ", Details: " << errbuf;
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(_avcodec_ctx, frame);
                    if(ret >= 0)
                    {
                        AVRational time_base = {1, frame->sample_rate};
                        if (frame->pts != AV_NOPTS_VALUE)
                        {
                            // 如果frame->pts正常则先将其从pkt_timebase转成{1, frame->sample_rate}
                            // pkt_timebase实质就是stream->time_base
                            frame->pts = av_rescale_q(frame->pts, _avcodec_ctx->pkt_timebase, time_base);
                        }
//                      else if (d->next_pts != AV_NOPTS_VALUE) {
//                        // 如果frame->pts不正常则使用上一帧更新的next_pts和next_pts_tb
//                        // 转成{1, frame->sample_rate}
//                        frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
//                      }
                    }
                    else{
                        char errbuf[1024] = {0};
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        LOG(LogLevel::ERROR) << "avcodec_receive_frame() failed. Error code: " << ret 
                                         << ", Details: " << errbuf;
                    }
                    break;
            }
            // 检查解码是否已经结束，解码结束返回0
            if(ret == AVERROR_EOF)
            {
                printf("avcodec_flush_buffers %s(%d)\n", __FUNCTION__, __LINE__); // 打印日志
                avcodec_flush_buffers(_avcodec_ctx); // 刷新解码器
                return 0;
            }
            // 正常解码返回1
            if(ret >= 0)
            {
                return 1;   // 获取到一帧frame
            }
        } while (ret != AVERROR(EAGAIN)); //没帧可读时ret返回EAGIN，需要继续送packet

        //  在目前这个版本我们还不去检测播放序列的问题
        //  如果上面的循环获取到了frame这里不会被执行，第二个循环，主要是读取packet送给解码器
//        do { //  在目前这个版本我们还不去检测播放序列的问题

//        if (queue_->nb_packets == 0)  // 没有数据可读
//            SDL_CondSignal(d->empty_queue_cond);// 通知read_thread放入packet

        // 阻塞式读取一个packet
        if(packet_queue_get(_queue, &pkt, 1, &_pkt_serial) < 0)
        {
            return -1; 
        }

//   } while (d->queue->serial != d->pkt_serial);// 如果不是同一播放序列(流不连续)则继续读取

        if(avcodec_send_packet(_avcodec_ctx, &pkt) == AVERROR(EAGAIN))
        {
            // Receive_frame and send_packet both returned EAGAIN, which is an API violation.
            LOG(LogLevel::ERROR) << "avcodec_send_packet() failed. Error code: " << ret;
            // 先暂存这个pkt
        }
        av_packet_unref(&pkt); // 释放pkt
    }
}

// 获取视频帧
int Decoder::get_video_frame(AVFrame *frame)
{
    int got_picture = 0;
    // 获取解码后的视频帧
    if((got_picture = decoder_decode_frame(frame)) < 0)
    {
        return -1; // 返回-1 意味着要退出解码线程，所以要分析decoder_decode_frame什么情况下返回-1
    }
    if(got_picture)
    {
        // 分析获取到的该帧是否要drop掉, 该机制的目的是在放入帧队列前先drop掉过时的视频帧
        // frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);
    }

    return got_picture;
}

// 放入帧
int Decoder::queue_picture(FrameQueue *fq, AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;  
    if (!(vp = frame_queue_peek_writable(fq))) // 检测队列是否有可写空间
        return -1;      // 请求退出则返回-1
    // 执行到这步说已经获取到了可写入的Frame
//    vp->sar = src_frame->sample_aspect_ratio;
//    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
//    vp->pos = pos;
//    vp->serial = serial;

    // 资源管理权限转移
    av_frame_move_ref(vp->frame, src_frame); // 将src中所有数据转移到dst中，并复位src。
    frame_queue_push(fq);   // 放入帧队列,内部更新写索引位置
    return 0;
}

int Decoder::audio_thread(void *arg)
{ 
    LOG(LogLevel::DEBUG) << "audio_thread() start";
    FFPlayer *ffp = (FFPlayer *)arg;
    AVFrame *frame = av_frame_alloc();
    Frame *af = nullptr;
    int got_frame = 0;    // 是否读取到帧
    AVRational time_base = {1, 44100};
    int ret = 0;

    if(!frame)
    {
        return AVERROR(ENOMEM);
    }

    do{
        // 读取解码后的帧
        if((got_frame = decoder_decode_frame(frame)) < 0) // 是否获取到一帧数据
            goto the_end; // <=0 abort
        if(got_frame)
        {
            time_base = (AVRational){1, frame->sample_rate}; // 设置为sample_rate为timebase

            // 获取可写Frame
            if(!(af = frame_queue_peek_writable(&ffp->sampq))) // 检测队列是否有可写空间,有则返回一个可写帧
                goto the_end;

            // 设置Frame并放入Frame队列
            af->pts = (frame->pts == AV_NOPTS_VALUE ? NAN : frame->pts * av_q2d(time_base));// pts转换成时间戳,单位为秒
//          af->pos = frame->pkt_pos;
//          af->serial = is->auddec.pkt_serial;
            //af->duration = av_q2d(time_base) * frame->nb_samples; // 设置时长
            af->duration = av_q2d((AVRational){frame->nb_samples, frame->sample_rate}); // 时长

            av_frame_move_ref(af->frame, frame); // 资源管理权限转移
            frame_queue_push(&ffp->sampq);  // 放入帧队列,内部更新写索引位置(此处代表队列真正插入一帧数据)
        }
    }while(ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:
    av_frame_free(&frame);
    LOG(LogLevel::DEBUG) << "audio_thread() end";
    return ret;
}

int Decoder::video_thread(void *arg)
{
    LOG(LogLevel::DEBUG) << "video_thread() start";
    FFPlayer *ffp = (FFPlayer *)arg;
    AVFrame *frame = av_frame_alloc();
    double pts;
    double duration;
    int ret = 0;
    // 1. 获取stream timebase
    AVRational tb = ffp->video_stream->time_base;
    // 2. 获取帧率, 以便计算每帧picture的duration
    AVRational frame_rate = av_guess_frame_rate(ffp->ic, ffp->video_stream, NULL);

    if(!frame)
    {
        return AVERROR(ENOMEM);
    }

    for(;;) // 循环取出视频解码的帧数据
    {
        // 获取解码后的视频帧
        ret = get_video_frame(frame);
        if (ret < 0)
            goto the_end;   // 解码结束
        if(!ret)
            continue;   // 获取不到帧则继续循环

        // 1/25 = 0.04秒
        // 计算帧持续时间和换算pts值为秒
        // 1/帧率 = duration 单位秒, 没有帧率时则设置为0, 有帧率帧计算出帧间隔
        duration = frame_rate.num && frame_rate.den ? av_q2d((AVRational){frame_rate.den, frame_rate.num}) : 0;
        // 根据AVStream timebase计算出pts值, 单位为秒
        pts = (frame->pts == AV_NOPTS_VALUE ? NAN : frame->pts * av_q2d(tb)); // 单位为秒
        // 将解码后的视频帧插入帧队列
        ret = queue_picture(&ffp->pictq, frame, pts, duration, frame->pkt_pos, ffp->video_dec._pkt_serial);
        // 释放frame对应的数据
        av_frame_unref(frame);

        if(ret < 0) // 插入帧队列失败,退出线程
            goto the_end;
    }
the_end:
    LOG(LogLevel::DEBUG) << "video_thread() end";
    av_frame_free(&frame);
    return 0;
}

