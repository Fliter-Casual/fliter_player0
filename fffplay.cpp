#include "fffplay.h"
#include <iostream>
#include <string.h>
#include <cmath>
#include "ffmsg.h"
#include "sonic.h"
#include "screenshot.h"
#include "log/easylogging++.h"
// #include <experimental/filesystem>
// namespace fs = std::experimental::filesystem;

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30
int infinite_buffer = 0;
static int decoder_reorder_pts = -1;
static int seek_by_bytes = -1;
// FFPlayer类的构造函数
FFPlayer::FFPlayer()
{
    pf_playback_rate = 1.0; // 播放速率
    // 初始化统计信息
    ffplayer_reset_statistic(&stat);
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

    // 选项 B: 只输出到文件 (发布时用)，后调用的会覆盖前一个。
    // ENABLE_FILE_LOG_STRATEGY();

    // 选项 C: 如果想同时输出到控制台和文件，需要先优化 Logger.hpp
    // 目前Logger.hpp未实现该策略

    msg_queue_init(&_msg_queue);
    LOG(INFO) << "FFPlayer created successfully.";
    return 0;
}

// 销毁播放器
void FFPlayer::ffplayer_destroy()
{
    stream_close();

    // 销毁消息队列
    msg_queue_destroy(&_msg_queue);
    LOG(INFO) << "FFPlayer destroyed.";
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
    LOG(INFO) << "ffplayer_start_1() called.";
    toggle_pause(0);
    return 0;
}

int FFPlayer::ffplayer_stop_1()
{
    // 触发停止
    abort_request = 1;            // 设置停止播放
    msg_queue_abort(&_msg_queue); // 停止消息队列,禁止再插入消息
    LOG(INFO) << "ffplayer_stop_1() called.";
    return 0;
}

// 打开流
int FFPlayer::stream_open(const char *file_name)
{
    // 初始化SDL,以允许音频输出,SDL_Init 成功返回0，失败返回负数
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        LOG(ERROR) << "Could not initialize SDL - " << SDL_GetError();
        av_log(nullptr, AV_LOG_FATAL, "Did you set the DISPLAY variable?\n");
        return -1;
    }
    // 初始化视频帧队列
    if (frame_queue_init(&pictq, &videoq, VIDEO_PICTURE_QUEUE_SIZE,1) < 0)
        goto fail;
    //  初始化音频帧队列
    if (frame_queue_init(&sampq, &audioq, SAMPLE_QUEUE_SIZE,1) < 0)
        goto fail;

    // 初始化packet队列
    if (packet_queue_init(&videoq) < 0 ||
        packet_queue_init(&audioq) < 0)
        goto fail;

    /*
     * 初始化时钟
     * 时钟序列->queue_serial，实际上指向的是videoq.serial
     */
    init_clock(&vidclock,&videoq.serial);
    init_clock(&audclock, &audioq.serial);
    audio_clock_serial = -1;
    // 初始化音量等
    startup_volume = av_clip(startup_volume,0,100);
    startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100,0,SDL_MIX_MAXVOLUME);
    audio_volume = startup_volume;

    // 创建解复用器读数据线程read_thread
    _read_thread = new std::thread(&FFPlayer::read_thread, this);

    // 创建视频刷新线程
    _video_refresh_thread = new std::thread(&FFPlayer::video_refresh_thread,this);
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

// 根据流索引找到对应的解码器，初始化并打开它，然后启动解码线程，为后续的播放做准备
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
        LOG(ERROR) << "avcodec_alloc_context3() failed.";
        return AVERROR(ENOMEM);
    }
    /* 将码流中的编解码器信息拷贝到新分配的编解码器上下文结构体 */
    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto fail;

    // 设置pkt_timebase
     avctx->pkt_timebase = ic->streams[stream_index]->time_base;

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

        // 准备音频输出
        // 调用audio_open打开sdl音频输出，实际打开的设备参数保存在audio_tgt中，返回值表示输出设备的缓冲区大小
        if((ret = audio_open(channel_layout, nb_channels, sample_rate, &audio_tgt)) < 0) // 音频输出打开失败
            goto fail;
        audio_hw_buf_size = ret;  // 音频输出缓冲区大小
        audio_src = audio_tgt;    // 暂且将数据源参数等同于目标输出参数
        //初始化audio_buf相关参数
        audio_buf_size = 0;       // 音频输出缓冲区大小
        audio_buf_index = 0;      // 音频输出缓冲区索引

        // 初始化ffplay封装的音频解码器, 并将解码器上下文 avctx和 解码器Decoder绑定
        audio_dec.decoder_init(avctx,&audioq);
        // 启动音频解码线程
        audio_dec.decoder_start(AVMEDIA_TYPE_AUDIO,"audio_thread",this);
        // 允许音频输出
        // play audio
        SDL_PauseAudio(0);
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
        LOG(DEBUG) << "stream_component_close() audio.";

        // 请求终止解码器线程
        audio_dec.decoder_abort(&sampq);
        // 关闭音频设备
        audio_close();
        // 销毁解码器
        audio_dec.decoder_destroy();
        // 释放重采样器
        swr_free(&swr_ctx);
        // 释放audio buf
        av_freep(&audio_buf1);
        audio_buf = nullptr;
        audio_buf1_size = 0;
        break;

    case AVMEDIA_TYPE_VIDEO:
        LOG(DEBUG) << "stream_component_close() video.";
        // 请求退出视频画面刷新线程
        if(_video_refresh_thread && _video_refresh_thread->joinable())
        {
            _video_refresh_thread->join(); // 等待线程退出
        }

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

// 音频解码
static int audio_decode_frame(FFPlayer *is)
{
    int data_size = 0; 
    int resampled_data_size = 0;
    int64_t dec_channel_layout = 0; 
    int wanted_nb_samples = 0; 
    Frame *af = nullptr; 
    int ret = 0;
    if(is->paused) {
        return -1;
    }
    // 读取一帧数据
    do{
        //若队列头部可读，则由af指向可读帧
        if(!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
        frame_queue_next(&is->sampq); // 出队列
    }while(af->serial != is->audioq.serial);



    // 根据frame中指定的音频参数获取缓冲区的大小 af->frame->channels * af->frame->nb_samples * av_get_bytes_per_sample(af->frame->format)
    data_size = av_samples_get_buffer_size(nullptr, 
                                           af->frame->channels, 
                                           af->frame->nb_samples, // 样本数量
                                           (enum AVSampleFormat)af->frame->format, 1);
    // 获取声道布局
    dec_channel_layout = (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
                         af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);

    // 处理音频通道布局（channel layout）为 0 的异常情况，防止后续音频处理崩溃
    if(dec_channel_layout == 0)
    {
        LOG(INFO) << af->frame->channel_layout << ", failed: " <<  av_get_default_channel_layout(af->frame->channels) ;
        dec_channel_layout = 3; // fixme
        return -1; // 这个是异常情况
    }
    // 获取样本数校正值: 若同步时钟是音频，则不调整样本数: 否则根据同步需要调整样本数
//    wanted_nb_samples = synchronize_audio(is, af->frame->nb_samples);  // 目前不考虑非音视频同步的是情况
    wanted_nb_samples = af->frame->nb_samples;

    // is->audio_tgt是SDL可接受的音频帧数，是audio_open()中取得的参数
    // 在audio_open()函数中又有"is->audio_src = is->audio_tgt""
    // 此处表示：如果frame中的音频参数 == is->audio_src == is->audio_tgt，
    // 那音频重采样的过程就免了(因此时is->swr_ctr是NULL)
    // 否则使用frame(源)和is->audio_tgt(目标)中的音频参数来设置is->swr_ctx，
    // 并使用frame中的音频参数来赋值is->audio_src
    // 以下为标准的重采样过程

    // 此处判断是否需要“准备”重采样器（配置阶段），如果音频参数不一致
    if(af->frame->format       != is->audio_src.format ||
        dec_channel_layout     != is->audio_src.channel_layout ||
        af->frame->sample_rate != is->audio_src.freq ||
        (wanted_nb_samples != af->frame->nb_samples && !is->swr_ctx) )
    {
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(nullptr,                        // 已有 ctx（这里新建）
                                         is->audio_tgt.channel_layout,   // 目标声道布局（stereo / 5.1）
                                         is->audio_tgt.format,           // 目标采样格式（S16）
                                         is->audio_tgt.freq,             // 目标采样率（48000）
                                         dec_channel_layout,             // 源:当前帧声道布局, 解码器输出的 channel_layout
                                         (enum AVSampleFormat)af->frame->format,//源：当前帧采样格式sample format
                                         af->frame->sample_rate,         // 源：当前帧采样率
                                         0, nullptr );                   // log_offset / log_ctx
        int ret = 0;
        if(!is->swr_ctx || swr_init(is->swr_ctx) < 0) //是否分配成功 || 是否初始化成功
        {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                   af->frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)af->frame->format), af->frame->channels,
                   is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.format), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            ret = -1;
            goto fail;
        }
        // 同步状态,更新“当前源格式”,下一次判断就有正确基准
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.format = (enum AVSampleFormat)af->frame->format;
    }

    // 执行真正的“重采样”数据转换（执行阶段）
    if(is->swr_ctx)
    {
        //重采样 swr_convert 函数调用中的输入端参数: 输入音频样本数af->frame->nb_samples , 输入音频缓冲区

        // 获取音频解码后原始数据（PCM）的指针数组
        const uint8_t **in = (const uint8_t **)af->frame->extended_data; //data[0] data[1],平面模式 ; 交错模式则交错存放在data[0]中

        // 重采样输出参数1：输出音频缓冲区
        uint8_t **out = &is->audio_buf1; // 真正分配缓存audio_buf1，指向是用audio_buf

        // 重采样输出参数2：输出音频缓冲区尺寸， 高采样率往低采样率转换时得到更少的样本数量，比如 96k->48k, wanted_nb_samples=1024
        // 则wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate 为1024*48000/96000 = 512
        // +256 的目的是重采样内部是有一定的缓存，就存在上一次的重采样还缓存数据和这一次重采样一起输出的情况，所以目的是多分配输出buffer
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256; 
        // 计算对应的样本数 对应的采样格式 以及通道数，共需要多少buffer空间
        int out_size = av_samples_get_buffer_size(nullptr, is->audio_tgt.channels, out_count, is->audio_tgt.format, 0);
        int len2;   // 重采样后的音频数据中单个声道的样本数
        if(out_size < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
            ret = -1;
            goto fail;
        }
        // if(audio_buf1_size < out_size) {重新分配out_size大小的缓存给audio_buf1, 并将audio_buf1_size设置为out_size }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if(!is->audio_buf1)
        {
            av_log(NULL, AV_LOG_ERROR, "out of memory\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        // swr_convert()函数真正开始重采样: len2返回值是重采样后得到的音频数据中单个声道的样本数
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if(len2 < 0)  // 重采样失败
        {
            av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
            ret = -1;
            goto fail;
        }

        //缓冲区溢出风险检测与自我保护机制: 连我们之前特意加的256都被占满了,说明实际的输出数据可能比预想的还要多
        if(len2 == out_count) // 重采样后得到的音频数据中单个声道的样本数等于重采样后得到的音频数据中单个声道的样本数
        {
            // 这里的意思是我已经多分配了buffer，实际输出的样本数不应该超过我多分配的数量
            av_log(NULL,AV_LOG_WARNING, "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx); // 重新初始化并清空重采样器的内部缓存状态,避免因为内部缓存堆积而导致后续输出不可控
        }

        // 重采样返回的一帧音频数据大小(以字节为单位)
        // 更新输出缓冲区指针并计算最终转换后的音频数据大小。
        is->audio_buf = is->audio_buf1; 
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.format);
    }
    else
    {
        // 未经重采样，则将指针指向frame中的音频数据
        is->audio_buf = af->frame->data[0]; // s16交错模式data[0], 平面模式fltp data[0] data[1]
        resampled_data_size = data_size;
    }

    // 如果pts可用，则更新音频时钟，否则使用NAN
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;
    

    is->audio_clock_serial = af->serial;    // 保存当前解码帧的serial
    ret = resampled_data_size;  
fail:
    return ret;
}

/**
 * @brief SDL 音频回调函数，用于向 SDL 音频设备提供解码后的音频数据。
 *
 * 该函数由 SDL 音频子系统在需要填充音频缓冲区时自动调用。它负责从 FFPlayer 上下文中获取已解码的音频帧，
 * 并将其复制到 SDL 提供的输出流中。如果内部缓冲区耗尽，则会触发新的音频帧解码。
 *
 * @param userdata 用户自定义数据指针，此处指向 FFPlayer 实例。
 * @param stream   指向 SDL 音频缓冲区的指针，用于写入音频数据。
 * @param len      需要填充的音频数据长度（字节数）。
 */
static void sdl_audio_callback(void *userdata, uint8_t *stream, int len)//void *userdata : 你塞什么进去，回调函数就收到什么。通常塞 this 或播放器对象指针
{
    FFPlayer *ffp = (FFPlayer *)userdata;
    int audio_size = 0;
    int len1 = 0;   
    ffp->audio_callback_time = av_gettime_relative();
    // 循环处理，直到填满 SDL 请求的全部音频数据长度
    while(len > 0)
    {
        // 循环读取，直到读取到足够的数据
        /* (1)如果is->audio_buf_index < is->audio_buf_size则说明上次拷贝还剩余一些数据，
         * 先拷贝到stream再调用audio_decode_frame
         * (2)如果audio_buf消耗完了，则调用audio_decode_frame重新填充audio_buf
         */

        // 当内部音频缓冲区的数据已被完全读取时，解码下一帧音频数据
        if(ffp->audio_buf_index >= ffp->audio_buf_size)
        {
            // 音频数据解码
            audio_size = audio_decode_frame(ffp);// 返回有效的PCM数据长度
            if(audio_size < 0)
            {
                // 静音的逻辑
                /* if error, just output silence */
                ffp->audio_buf = nullptr;
                ffp->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / ffp->audio_tgt.frame_size * ffp->audio_tgt.frame_size;
                ffp->audio_no_data  = 1;      // 没有数据可以读取
                if(ffp->eof)
                {
                    // 如果文件以及读取完毕，此时应该判断是否还有数据可以读取，如果没有就该发送通知ui停止播放
                    ffp->check_play_finish();
                }
            }
            else
            {
                ffp->audio_buf_size = audio_size; 
                ffp->audio_no_data = 0;
            }
            ffp->audio_buf_index = 0; // 重置索引

            //是否需要做变速
            if(ffp->ffp_get_playback_rate_change()) {
                ffp->ffp_set_playback_rate_change(0);
                // 初始化
                if(ffp->audio_speed_convert) {
                    // 先释放
                    sonicDestroyStream(ffp->audio_speed_convert);
                }
                // 再创建
                ffp->audio_speed_convert = sonicCreateStream(ffp->get_target_frequency(),
                                          ffp->get_target_channels());
                // 设置变速系数
                sonicSetSpeed(ffp->audio_speed_convert, ffp->ffp_get_playback_rate());
                sonicSetPitch(ffp->audio_speed_convert, 1.0);
                sonicSetRate(ffp->audio_speed_convert, 1.0);
            }
            if(!ffp->is_normal_playback_rate() && ffp->audio_buf) {
                // 不是正常播放则需要修改
                // 需要修改  audio_buf_index audio_buf_size audio_buf
                int actual_out_samples = ffp->audio_buf_size /
                                         (ffp->audio_tgt.channels * av_get_bytes_per_sample(ffp->audio_tgt.format));
                // 计算处理后的点数
                int out_ret = 0;
                int out_size = 0;
                int num_samples = 0;
                int sonic_samples = 0;
                if(ffp->audio_tgt.format == AV_SAMPLE_FMT_FLT) {
                    out_ret = sonicWriteFloatToStream(ffp->audio_speed_convert,
                                                      (float *)ffp->audio_buf,
                                                      actual_out_samples);
                } else  if(ffp->audio_tgt.format == AV_SAMPLE_FMT_S16) {
                    out_ret = sonicWriteShortToStream(ffp->audio_speed_convert,
                                                      (short *)ffp->audio_buf,
                                                      actual_out_samples);
                } else {
                    av_log(NULL, AV_LOG_ERROR, "sonic unspport ......\n");
                }
                num_samples =  sonicSamplesAvailable(ffp->audio_speed_convert);
                // 2通道  目前只支持2通道的
                out_size = (num_samples) * av_get_bytes_per_sample(ffp->audio_tgt.format) * ffp->audio_tgt.channels;
                av_fast_malloc(&ffp->audio_buf1, &ffp->audio_buf1_size, out_size);
                if(out_ret) {
                    // 从流中读取处理好的数据
                    if(ffp->audio_tgt.format == AV_SAMPLE_FMT_FLT) {
                        sonic_samples = sonicReadFloatFromStream(ffp->audio_speed_convert,
                                        (float *)ffp->audio_buf1,
                                        num_samples);
                    } else  if(ffp->audio_tgt.format == AV_SAMPLE_FMT_S16) {
                        sonic_samples = sonicReadShortFromStream(ffp->audio_speed_convert,
                                        (short *)ffp->audio_buf1,
                                        num_samples);
                    } else {
                        LOG(ERROR) << "sonic unspport fmt: " << ffp->audio_tgt.format;
                    }
                    ffp->audio_buf = ffp->audio_buf1;
                    //                     LOG(INFO) << "mdy num_samples: " << num_samples;
                    //                     LOG(INFO) << "orig audio_buf_size: " << audio_buf_size;
                    ffp->audio_buf_size = sonic_samples * ffp->audio_tgt.channels * av_get_bytes_per_sample(ffp->audio_tgt.format);
                    //                    LOG(INFO) << "mdy audio_buf_size: " << audio_buf_size;
                    ffp->audio_buf_index = 0;
                }
            }
        }
        if(ffp->audio_buf_size == 0) {
            continue;
        }
        
        // 计算本次可拷贝的数据量：取剩余未读数据长度与 SDL 请求长度的较小值
        len1 = ffp->audio_buf_size - ffp->audio_buf_index;
        len1 = FFMIN(len1, len);

        // 将内部缓冲区中的数据拷贝到 SDL 输出流
        if(ffp->audio_buf)
            memcpy(stream, (uint8_t *)ffp->audio_buf + ffp->audio_buf_index, len1);
        else
        {
            memset(stream, 0, len1);
            if (ffp->audio_buf) {
                SDL_MixAudio(stream, (uint8_t *)ffp->audio_buf + ffp->audio_buf_index, len1, ffp->audio_volume);
            }
        }
        // 更新剩余需要填充的长度、输出流指针位置以及内部缓冲区的读取索引
        len -= len1;      
        stream += len1;   
        /* 更新ffp->audio_buf_index，指向audio_buf中未被拷贝到stream的数据（剩余数据）的起始位置 */
        ffp->audio_buf_index += len1;
    }
    ffp->audio_write_buf_size = ffp->audio_buf_size - ffp->audio_buf_index;
    //更新音频时钟（Audio Clock），以确保播放器能够准确追踪当前的音频播放进度
    if (!std::isnan(ffp->audio_clock))
    {
        double audio_clock = ffp->audio_clock / ffp->ffp_get_playback_rate();
        // 设置时钟
        set_clock_at(&ffp->audclock,
                  audio_clock  - (double)(2 * ffp->audio_hw_buf_size + ffp->audio_write_buf_size) / ffp->audio_tgt.bytes_per_sec,
                  ffp->audio_clock_serial,
                  ffp->audio_callback_time / 1000000.0);
    }
}

int FFPlayer::audio_open(uint64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate,struct AudioParams *audio_hw_params)
{ 
    // 1. 配置 SDL 期望的音频规格
    SDL_AudioSpec wanted_spec; // SDL音频参数
    wanted_spec.freq = wanted_sample_rate;  // 采样率
    wanted_spec.format = AUDIO_S16SYS;      // 采样格式 SDL的宏
    wanted_spec.channels = wanted_nb_channels;// 声道数（2）
    wanted_spec.silence = 0;                // 静音

    // 样本数量：决定 SDL 音频回调函数的触发频率。
    // 公式: 持续时间(ms) = samples * 1000 / freq
    // 2048 samples / 44100Hz ≈ 46.4ms 调用一次回调
    wanted_spec.samples = 2048;             // 23.2ms -> 46.4ms 每次读取的采样数量，多久产生一次回调和 samples
    wanted_spec.callback = sdl_audio_callback;  // 设置音频数据填充回调函数 (注册回调函数)
    wanted_spec.userdata = this;                //将 FFPlayer 实例指针传给回调，以便在回调中访问成员变量

    // 2. 打开 SDL 音频设备
    // SDL_OpenAudio 会尝试按照 wanted_spec 打开设备. 第二个硬件支持的参数暂不考虑
    if(SDL_OpenAudio(&wanted_spec, nullptr) != 0)
    {
        LOG(ERROR) << "SDL_OpenAudio() failed ," << SDL_GetError();
        return -1;
    }

    // 3. 同步 FFmpeg 音频参数 (用于后续的重采样配置)
    // 我们将 SDL 实际确定的输出格式保存下来，作为音频重采样的“目标格式”。
    // 即：无论输入音频是什么格式，最终都要重采样成这个格式交给 SDL
    audio_hw_params->format = AV_SAMPLE_FMT_S16; //  FFmpeg 的枚举值,和上面的format对应的内存布局是一样的，都是16位PCM
    audio_hw_params->freq = wanted_spec.freq; // 实际输出的采样率
    audio_hw_params->channels = wanted_spec.channels;// 实际声道数
    audio_hw_params->channel_layout = wanted_channel_layout;

    // 当音频设备的通道布局（channel layout）为 0 时，根据声道数自动补一个默认的通道布局
    if(audio_hw_params->channel_layout == 0) {
        audio_hw_params->channel_layout =
            av_get_default_channel_layout(audio_hw_params->channels);
        LOG(WARNING) << "layout is 0, force change to " << audio_hw_params->channel_layout;
    }

    // 4. 预计算常用音频参数，避免在音频回调中重复计算
    // frame_size: 单个采样点在多声道下的总字节数 (例如: 2 channels * 2 bytes/sample = 4 bytes)
    audio_hw_params->frame_size = av_samples_get_buffer_size(nullptr, audio_hw_params->channels,
                                                                1, // 仅计算1个样本的时间跨度
                                                                audio_hw_params->format, 1);
    // bytes_per_sec: 每秒产生的音频数据字节数，用于计算缓冲区大小或时长
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(nullptr, audio_hw_params->channels,
                                                                audio_hw_params->freq,  // 44100(1秒内的样本数)
                                                                audio_hw_params->format, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
        return -1;
    }
    // 5. 返回 SDL 内部缓冲区的大小
    // wanted_spec.size 是 SDL 根据 samples, channels, format 计算出的单次回调最大数据量
    // 这通常也是我们需要维护的内部音频环形缓冲区的最小合理大小
    return wanted_spec.size;	                            /* SDL内部缓存的数据字节, samples * channels *byte_per_sample */
}

void FFPlayer::audio_close()
{
    SDL_CloseAudio();  // SDL_CloseAudioDevice
}

long FFPlayer::ffp_get_duration_l()
{
    if(!ic) {
        return 0;
    }
    int64_t duration = fftime_to_milliseconds(ic->duration);
    if (duration < 0) {
        return 0;
    }
    return (long)duration;
}

// 当前播放的位置
long FFPlayer::ffp_get_current_position_l()
{
    if(!ic) {
        return 0;
    }
    int64_t start_time = ic->start_time;    // 起始时间 一般为0
    int64_t start_diff = 0;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE) {
        start_diff = fftime_to_milliseconds(start_time);    // 返回只需ms这个级别的
    }
    int64_t pos = 0;
    double pos_clock = get_master_clock();  // 获取当前时钟
    if (std::isnan(pos_clock)) {
        pos = fftime_to_milliseconds(seek_pos);
    } else {
        pos = pos_clock * 1000;     //转成msg
    }
    if (pos < 0 || pos < start_diff) {
        return 0;
    }
    int64_t adjust_pos = pos - start_diff;
    return (long)adjust_pos * pf_playback_rate; // 变速的系数
}

// 暂停的请求
int FFPlayer::ffp_pause_l()
{
    toggle_pause(1);
    return 0;
}

void FFPlayer::toggle_pause(int pause_on)
{
    toggle_pause_l(pause_on);
}

void FFPlayer::toggle_pause_l(int pause_on)
{
    if (pause_req && !pause_on) {
        set_clock(&vidclock, get_clock(&vidclock), vidclock.serial);
        set_clock(&audclock, get_clock(&audclock), audclock.serial);
    }
    pause_req = pause_on;
    auto_resume = !pause_on;
    stream_update_pause_l();
    step = 0;
}

void FFPlayer::stream_update_pause_l()
{
    if (!step && (pause_req || buffering_on)) {
        stream_toggle_pause_l(1);
    } else {
        stream_toggle_pause_l(0);
    }
}

void FFPlayer::stream_toggle_pause_l(int pause_on)
{
    if (paused && !pause_on) {
        frame_timer += av_gettime_relative() / 1000000.0 - vidclock.last_updated;
        set_clock(&vidclock, get_clock(&vidclock), vidclock.serial);
        set_clock(&audclock, get_clock(&audclock), audclock.serial);
    } else {
    }
    if (step && (pause_req || buffering_on)) {
        paused = vidclock.paused = pause_on;
    } else {
        paused = audclock.paused = vidclock.paused =  pause_on;
        //        SDL_AoutPauseAudio(ffp->aout, pause_on);
    }
}

int FFPlayer::ffp_seek_to_l(long msec)
{
    int64_t start_time = 0;
    int64_t seek_pos = milliseconds_to_fftime(msec);
    int64_t duration = milliseconds_to_fftime(ffp_get_duration_l());
    if (duration > 0 && seek_pos >= duration) {
        ffp_notify_msg1(this, FFP_MSG_SEEK_COMPLETE);        // 超出了范围
        return 0;
    }
    start_time =  ic->start_time;
    if (start_time > 0 && start_time != AV_NOPTS_VALUE) {
        seek_pos += start_time;
    }
    LOG(INFO) << "seek to:  " << seek_pos / 1000 ;
    stream_seek(seek_pos, 0, 0);
    return 0;
}

int FFPlayer::ffp_forward_to_l(long incr)
{
    ffp_forward_or_back_to_l(incr);
    return 0;
}

int FFPlayer::ffp_back_to_l(long incr)
{
    ffp_forward_or_back_to_l(incr);
    return 0;
}

int FFPlayer::ffp_forward_or_back_to_l(long incr)
{
    double pos;
    if (seek_by_bytes) {
        pos = -1;
        if (pos < 0 &&  video_stream_index >= 0) {
            pos = frame_queue_last_pos(&pictq);
        }
        if (pos < 0 && audio_stream_index >= 0) {
            pos = frame_queue_last_pos(&sampq);
        }
        if (pos < 0) {
            pos = avio_tell(ic->pb);
        }
        if (ic->bit_rate) {
            incr *= ic->bit_rate / 8.0;
        } else {
            incr *= 180000.0;
        }
        pos += incr;
        stream_seek(pos, incr, 1);
    } else {
        pos = get_master_clock();       // 单位是秒
        if (std::isnan(pos)) {
            pos = (double)seek_pos / AV_TIME_BASE;
        }
        pos += incr;   // 单位转成秒
        if (ic->start_time != AV_NOPTS_VALUE && pos < ic->start_time / (double)AV_TIME_BASE) {
            pos = ic->start_time / (double)AV_TIME_BASE;
        }
        //转成 AV_TIME_BASE
        stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
    }
    return 0;
}

void FFPlayer::stream_seek(int64_t pos, int64_t rel, int seek_by_bytes)
{
    if (!seek_req) {
        seek_pos = pos;
        seek_rel = rel;
        seek_flags &= ~AVSEEK_FLAG_BYTE;
        if (seek_by_bytes) {
            seek_flags |= AVSEEK_FLAG_BYTE;
        }
        seek_req = 1;
        //        SDL_CondSignal( continue_read_thread);
    }
}

int FFPlayer::ffp_screenshot_l(char *screen_path)
{
    // 存在视频的情况下才能截屏
    if(video_stream && !req_screenshot_) {
        if(screen_path_) {
            free(screen_path_);
            screen_path_ = NULL;
        }
        screen_path_ = strdup(screen_path);
        req_screenshot_ = true;
    }
    return 0;
}

void FFPlayer::screenshot(AVFrame *frame)
{
    if(req_screenshot_) {
        ScreenShot shot;
        int ret = -1;
        if(frame) {
            ret = shot.SaveJpeg(frame, screen_path_, 70);
        }
        // 如果正常则ret = 0; 异常则为 < 0
        ffp_notify_msg4(this, FFP_MSG_SCREENSHOT_COMPLETE, ret, 0, screen_path_, strlen(screen_path_) + 1);
        // 截屏完毕后允许再次截屏
        req_screenshot_ = false;
    }
}

int FFPlayer::get_target_frequency()
{
    return audio_tgt.freq;
}

int FFPlayer::get_target_channels()
{
    return audio_tgt.channels;
}

void FFPlayer::ffp_set_playback_rate(float rate)
{
    pf_playback_rate = rate;
    pf_playback_rate_changed = 1;
}

float FFPlayer::ffp_get_playback_rate()
{
    return pf_playback_rate;
}

bool FFPlayer::is_normal_playback_rate()
{
    if(pf_playback_rate > 0.99 && pf_playback_rate < 1.01) {
        return true;
    } else {
        return false;
    }
}

int FFPlayer::ffp_get_playback_rate_change()
{
    return pf_playback_rate_changed;
}

void FFPlayer::ffp_set_playback_rate_change(int change)
{
    pf_playback_rate_changed = change;
}

void FFPlayer::ffp_set_playback_volume(int value)
{
    value = av_clip(value, 0, 100);
    value = av_clip(SDL_MIX_MAXVOLUME *  value / 100, 0, SDL_MIX_MAXVOLUME);
    audio_volume = value;
    LOG(INFO) << "audio_volume: " << audio_volume  ;
}

void FFPlayer::check_play_finish()
{
    //    LOG(INFO) << "eof: " << eof << ", audio_no_data: " << audio_no_data  ;
    if(eof == 1) { // 1. av_read_frame已经返回了AVERROR_EOF
        if(audio_stream_index >= 0 && video_stream_index >= 0) { // 2.1 音频、视频同时存在的场景
            if(audio_no_data == 1 && video_no_data == 1) {
                // 发送停止
                ffp_notify_msg1(this, FFP_MSG_PLAY_FNISH);
            }
            return;
        }
        if(audio_stream_index >= 0) { // 2.2 只有音频存在
            if(audio_no_data == 1) {
                // 发送停止
                ffp_notify_msg1(this, FFP_MSG_PLAY_FNISH);
            }
            return;
        }
        if(video_stream_index >= 0) { // 2.3 只有视频存在
            if(video_no_data == 1) {
                // 发送停止
                ffp_notify_msg1(this, FFP_MSG_PLAY_FNISH);
            }
            return;
        }
    }
}
int64_t FFPlayer::ffp_get_property_int64(int id, int64_t default_value)
{
    switch (id) {
        case FFP_PROP_INT64_AUDIO_CACHED_DURATION:
            return  stat.audio_cache.duration;
        case FFP_PROP_INT64_VIDEO_CACHED_DURATION:
            return  stat.video_cache.duration;
        default:
            return default_value;
    }
}
void FFPlayer::ffp_track_statistic_l(AVStream * st, PacketQueue * q, FFTrackCacheStatistic * cache)
{
    if (q) {
        cache->bytes   = q->size;
        cache->packets = q->nb_packets;
    }
    if (q && st && st->time_base.den > 0 && st->time_base.num > 0) {
        cache->duration = q->duration * av_q2d(st->time_base) * 1000;  // 单位毫秒ms
    }
}
// 在audio_thread解码线程做统计
void FFPlayer::ffp_audio_statistic_l()
{
    ffp_track_statistic_l(audio_stream, &audioq, &stat.audio_cache);
}
// 在audio_thread解码线程做统计
void FFPlayer::ffp_video_statistic_l()
{
    ffp_track_statistic_l(video_stream, &videoq, &stat.video_cache);
}
int FFPlayer::stream_has_enough_packets(AVStream * st, int stream_id, PacketQueue * queue)
{
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}
static int is_realtime(AVFormatContext * s)
{
    if(   !strcmp(s->iformat->name, "rtp")
          || !strcmp(s->iformat->name, "rtsp")
          || !strcmp(s->iformat->name, "sdp")
          ||  !strcmp(s->iformat->name, "rtmp")
      ) {
        return 1;
    }
    if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
                    || !strncmp(s->filename, "udp:", 4)
                ))
    {
        return 1;
    }
    return 0;
}

// 读取线程,这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染
int FFPlayer::read_thread()
{
    int err,ret;
    int st_index[AVMEDIA_TYPE_NB]; // 媒体类型索引
    AVPacket pkt1;
    AVPacket *pkt = &pkt1; //这是“用栈上临时变量 + 指针传递”的惯用法，用于兼容需要 AVPacket*的 FFmpeg API

    // 初始化为-1,如果一直为-1说明没相应stream
    memset(st_index, -1, sizeof(st_index));
    video_stream_index = -1;
    audio_stream_index = -1;
    eof = 0;

    // 创建解封装器实例(封装器上下文)，为最上层的结构体，表示输入上下文
    ic = avformat_alloc_context();
    if (!ic)
    {
        LOG(ERROR) <<  "Could not allocate context.";
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // Debug
    // std::string current_dir = fs::current_path().string();
    // LOG(INFO) << "Current Working Directory: " << current_dir;

    // 打开文件,主要是探测协议类型，如果是网络文件则创建网络连接等
    err = avformat_open_input(&ic, _input_filename, nullptr, nullptr);
    if (err < 0)
    {
        char errbuf[128];
        av_strerror(err, errbuf, sizeof(errbuf));
        LOG(ERROR) << "Could not open source file " << _input_filename<< ", Error code: " << err 
                             << ", Details: " << errbuf;
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT); // 发送消息给UI线程，通知开始打开输入文件
    LOG(INFO) << "read_thread: FFP_MSG_FIND_STREAM_INFO " << _input_filename  << " " <<this;
    if (seek_by_bytes < 0) //决定是否按字节（而非时间戳）进行 seek（跳转）
    {
        seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);
    }//文件格式是 OGG（OGG 按字节 seek 更可靠）

    // 获取输入流信息，填充AVFormatContext结构体
    err = avformat_find_stream_info(ic, nullptr);
    if (err < 0)
    {
        LOG(ERROR) << _input_filename << " Could not find stream info.";
        ret = -1;
        goto fail;
    }

    ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO); // 发送消息给UI线程，通知开始寻找流信息
    LOG(INFO) << "read_thread: avformat_find_stream_info() success. FFP_MSG_FIND_STREAM_INFO " << this;
    realtime = is_realtime(ic);
    av_dump_format(ic, 0, _input_filename, 0);

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
    LOG(INFO) << "read_thread: FFP_MSG_COMPONENT_OPEN " << this;

    if (video_stream_index < 0 && audio_stream_index < 0) {
        av_log(nullptr, AV_LOG_FATAL, "Failed to open file '%s' or configure filtergraph\n",
               _input_filename);
        ret = -1;
        goto fail;
    }
    ffp_notify_msg1(this, FFP_MSG_PREPARED); // 发送消息给UI线程，通知准备完毕
    LOG(INFO) << "read_thread: FFP_MSG_PREPARED " << this;

    while (1)
    {
        // std::cout << "read_thread sleep, mp:" << this << std::endl;
        // 先模拟线程运行
//        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (abort_request)
        {
            break;
        }
        // 如果有seek请求
        if (seek_req) {
            // seek的位置
            int64_t seek_target = seek_pos;
            int64_t seek_min    = seek_rel > 0 ? seek_target - seek_rel + 2 : INT64_MIN;
            int64_t seek_max    =  seek_rel < 0 ? seek_target -  seek_rel - 2 : INT64_MAX;
            // 是 av_seek_frame 的增强版，可以更精细地控制 Seek 行为，支持按帧序号跳转
            ret = avformat_seek_file(ic, -1, seek_min, seek_target, seek_max,  seek_flags);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR,
                       "%s: error while seeking\n",  ic->filename);
            } else {
                if (audio_stream_index >= 0) {    //有audio流
                    packet_queue_flush(&audioq);
                    packet_queue_put(&audioq, &flush_pkt);
                }
                if (video_stream_index >= 0) { //有video流
                    packet_queue_flush(&videoq);
                    packet_queue_put(&videoq, &flush_pkt);
                }
            }
            seek_req = 0;
            eof = 0;
            ffp_notify_msg1(this, FFP_MSG_SEEK_COMPLETE);
        }
        /* if the queue are full, no need to read more */
        if (infinite_buffer < 1 &&
            (audioq.size + videoq.size  > MAX_QUEUE_SIZE
             || (stream_has_enough_packets(audio_stream, audio_stream_index, &audioq) &&
                 stream_has_enough_packets(video_stream, video_stream_index, &videoq) ))) {
            /* wait 10 ms */
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 读取一个packet，得到的是音视频分离后，解码前的数据(压缩数据（H264 / AAC 等）)
        ret = av_read_frame(ic, pkt); // packet要自己去释放
        if (ret < 0) // 读取失败或读取完毕了
        {
            //vio_feof(ic->pb)是 FFmpeg 中用于判断 AVIOContext 是否已经到达文件末尾（EOF）的函数
            if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !eof) // 文件读取完毕
            {
                // 刷空包给队列
                if(video_stream_index >= 0)
                {
                    packet_queue_put_nullpacket(&videoq,video_stream_index);
                }
                if(audio_stream_index >= 0)
                {
                    packet_queue_put_nullpacket(&audioq,audio_stream_index);
                }
                eof = 1;
            }
            if(ic->pb && ic->pb->error)  // io异常 / 退出循环
            {
                LOG(ERROR) << "read_thread: av_read_frame() error: " << ret << ", pb error: " << ic->pb->error;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 读取完数据了, 休眠10ms
            continue;   // 继续循环
        }
        else
        {
            eof = 0; // 重置eof
        }
        // 插入队列，先只处理音频包
        if(pkt->stream_index == audio_stream_index)
        {
            //printf("read_thread: pkt->pts: %lld, pkt->dts: %lld, pkt->duration: %d, pkt->size: %d\n", pkt->pts/48, pkt->dts, pkt->duration, pkt->size);
            packet_queue_put(&audioq, pkt);
        }
        else if(pkt->stream_index == video_stream_index)
        {
            //printf("video ===== pkt pts:%ld, dts:%ld\n", pkt->pts/48, pkt->dts);
            packet_queue_put(&videoq, pkt); // 音视频分离后的数据入队列
        }
        else
        {
            av_packet_unref(pkt); // 不入队列则直接释放packet
        }
    }

    LOG(INFO) << __FUNCTION__ << "read_thread exit.";

fail:
    return 0;
}


/* 
 * 视频刷新轮询(polls)间隔 (秒)
 * 该值应小于视频帧间隔 (1/FPS)，以确保能及时捕获到需要渲染的时刻。
 * 例如 60FPS 的视频，帧间隔约 0.016s，设置 0.01s (10ms) 可以保证每帧都被检测到。
 */
#define REFRESH_RATE 0.01  // 每帧休眠10ms

/**
 * @brief 视频刷新控制线程 (Video Refresh Thread)
 * 
 * 这是一个独立的后台线程，负责以固定的高频节奏驱动视频画面的更新。
 * 它不直接解码视频，而是充当“节拍器”，定期调用 video_refresh() 检查是否到了显示下一帧的时间。
 */
int FFPlayer::video_refresh_thread()
{
    double remaining_time = 0.0; // 剩余等待时间，用于精确控制休眠时长，实现音视频同步或帧率控制
    while (!abort_request) {
        // 1. 精确休眠：如果计算出的剩余等待时间大于0，则让出CPU，避免忙等待消耗资源
        if (remaining_time > 0.0)
            // av_usleep 参数单位为微秒 (us)
            av_usleep((int)(int64_t)(remaining_time * 1000000.0));
        
        // 2. 重置基准时间：每次循环开始，设定下一个检测周期为 REFRESH_RATE
        remaining_time = REFRESH_RATE;

        // 3. 执行刷新逻辑：
        //    - 检查队列中是否有待显示帧
        //    - 判断当前系统时间是否达到了该帧的显示时间 (PTS)
        //    - 如果到了时间，通过回调函数将帧发送给UI层渲染
        //    - remaining_time 会被 video_refresh 内部修改，返回“距离下一帧还需要等待多久”
        video_refresh(&remaining_time);
    }
    LOG(INFO) <<  " leave" ;
    return 0;
}
// 计算当前视频帧的显示时长（该帧在屏幕上停留多久）,目的是让视频以正确的速度播放，实现音视频同步
double FFPlayer::vp_duration(Frame * vp, Frame * nextvp)
{
    if (vp->serial == nextvp->serial) {             // 同一序列
        double duration = nextvp->pts - vp->pts;    // 两帧时间差
        if (std::isnan(duration) || duration <= 0 || duration >  max_frame_duration) {
            return vp->duration / pf_playback_rate; // 用帧自带时长(PTS 异常时兜底)
        } else {
            return duration / pf_playback_rate;     // 用 PTS 差值(正常播放)
        }
    } else {
        return 0.0;         // 不同序列 → 不显示
    }
}
// 计算目标延迟,根据音视频时钟差值，调整当前帧的显示延迟，使视频跟上音频
double FFPlayer::compute_target_delay(double delay)
{
    double sync_threshold, diff = 0;
    /* update delay to follow master synchronisation source */
    if (get_master_sync_type() != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
        duplicating or deleting a frame */
        diff = get_clock(&vidclock) - get_master_clock();
        /* skip or repeat frame. We take into account the
        delay to compute the threshold. I still don't know
        if it is the best guess */
        // 下者约等于 delay 的 0.04 ~ 0.1 倍,可小幅调整
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (! std::isnan(diff) && fabs(diff) <  max_frame_duration) {
            if (diff <= -sync_threshold) {      // 视频加速追上音频
                delay = FFMAX(0, delay + diff);
            } else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD) { // 延迟大于阈值(大幅调整，可丢帧):视频大幅减速
                delay = delay + diff;
            } else if (diff >= sync_threshold) { // 视频小幅减速
                delay = 2 * delay;
            }
        }
    }
    av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n",
           delay, -diff);
    return delay;
}

//更新视频时钟,将当前帧的 PTS 记录到视频时钟中，供同步计算使用
void FFPlayer::update_video_pts(double pts, int64_t pos, int serial)
{
    /* update current video pts */
    set_clock(&vidclock, pts / pf_playback_rate, serial);
}

//上面的 compute_target_delay 根据音视频时钟差值动态调整帧延迟，update_video_pts 更新视频时钟供下次同步使用。两者配合实现平滑的音视频同步

/**
 * @brief 视频帧渲染调度函数
 *
 * 这是播放器视频输出的核心函数。它决定了每一帧应该在什么时候显示，
 * 通过音视频同步机制控制显示节奏，确保视频流畅且与音频对齐。
 *
 * 本函数每次调用都会检查当前帧队列的状态，并根据系统时钟和帧时间戳
 * 决定是立即渲染当前帧，还是等待一段时间后再渲染。
 *
 * @param remaining_time 输出参数，返回距离下一次“应该渲染”还需要等待的时间（秒）。
 *                        如果当前帧还不到显示时间，此值会被更新。
 */
void FFPlayer::video_refresh(double *remaining_time)
{
    Frame *vp = nullptr, *lastvp = nullptr; // vp - video picture

    // 1. 检查视频流是否存在，不存在则直接返回
    if(video_stream) {
retry: // 重试标签：当帧需要被跳过时，回到此处重新处理下一帧

        // 2. 检查帧队列是否为空（没有待显示的帧）
        if (frame_queue_nb_remaining(&pictq) == 0) {
            video_no_data = 1;          // 标记无数据
            if(eof == 1) {
                check_play_finish();    // 文件结束，检查是否播放完毕
            }
            // 队列为空，无法渲染，退出函数
        }
        else
        {
            // 3. 队列有帧，准备处理
            video_no_data = 0;
            double last_duration, duration, delay;

            /* --- 3.1 获取帧数据 --- */
            // 查看上一帧（用于计算当前帧的显示时长）
            lastvp = frame_queue_peek_last(&pictq);
            screenshot(lastvp->frame); // 截图功能（可选）

            // 查看当前待显示的帧（Peek 而非 Pop，暂不移出队列）
            // 这样可以在未到显示时间时多次检查，而不会丢失帧
            vp = frame_queue_peek(&pictq);

            // 3.1.1 检查序列号：如果帧的序列号与当前视频流序列号不匹配（如跳转后），丢弃该帧
            if(vp->serial != videoq.serial) {
                frame_queue_next(&pictq); // 移除当前帧
                goto retry;               // 尝试处理下一帧
            }

            // 3.1.2 序列变化：如果上一帧和当前帧序列号不同，重置时间基准
            if(lastvp->serial != vp->serial) {
                frame_timer = av_gettime_relative() / 1000000.0; // 重新计时
            }

            // 3.1.3 暂停处理：如果处于暂停状态，直接跳到显示步骤
            if(paused) {
                goto display;
            }

            /* --- 3.2 计算显示延迟（核心同步逻辑） --- */
            // 计算上一帧的理想持续时间（根据 PTS 差值）
            last_duration = vp_duration(lastvp, vp);
            // 根据主时钟（通常是音频）调整目标延迟（实现音视频同步）
            delay = compute_target_delay(last_duration);

            /* --- 3.3 判断渲染时机 --- */
            double time = av_gettime_relative() / 1000000.0; // 当前系统时间（秒）

            // 如果当前时间还没到“预期显示时间 + 延迟”，说明需要等待
            if (time < frame_timer + delay) {
                // 计算还需要等待多久，并返回给主循环（用于 sleep）
                *remaining_time = FFMIN(frame_timer + delay - time, *remaining_time);
                goto display; // 不渲染，直接跳到显示步骤（但显示条件不满足，不会真正显示）
            }

            // 3.3.1 更新帧定时器：当前帧的预期显示时间累加延迟
            frame_timer += delay;

            // 3.3.2 时钟修正：如果帧延迟大于0，但系统时间已经远远超过预期（卡顿），则重置帧定时器
            if(delay > 0 && time - frame_timer > AV_SYNC_THRESHOLD_MAX) {
                frame_timer = time;
            }

            /* --- 3.4 更新视频时钟（用于音视频同步） --- */
            SDL_LockMutex(pictq.mutex);
            if(!std::isnan(vp->pts)) {
                // 更新视频时钟为当前帧的 PTS
                update_video_pts(vp->pts, vp->pos, vp->serial);
            }
            SDL_UnlockMutex(pictq.mutex);

            /* --- 3.5 丢帧逻辑（Late Frame Drop） --- */
            // 如果队列中还有下一帧，且当前帧已经“过时”，就丢弃它
            if(frame_queue_nb_remaining(&pictq) > 1) {
                Frame *nextvp = frame_queue_peek_next(&pictq);
                duration = vp_duration(vp, nextvp); // 计算当前帧到下一帧的时长

                // 条件：非单步模式 且 允许丢帧 且 系统时间已超过“预期显示时间 + 帧时长”
                // 说明当前帧已经显示得太晚，应该直接丢弃
                if (!step && (framedrop > 0 || (framedrop && get_master_sync_type() != AV_SYNC_VIDEO_MASTER))
                    && time > frame_timer + duration) {
                    frame_drops_late++;               // 统计丢弃的帧数
                    frame_queue_next(&pictq);          // 丢弃当前帧
                    goto retry;                        // 重新处理队列中的下一帧
                }
            }

            // 3.6 帧已准备好显示：将当前帧从队列中真正移除
            frame_queue_next(&pictq);
            force_refresh = 1; // 标记需要刷新画面
        }

display:
        /* --- 3.7 画面渲染 --- */
        // 满足显示条件时，将帧数据传递给 UI 层进行绘制
        if (force_refresh && pictq.rindex_shown) {
            if(vp && _video_refresh_callback) {
                _video_refresh_callback(vp); // 调用外部回调，在 UI 线程中渲染
            }
        }
    }
    // 重置强制刷新标志
    force_refresh = 0;
}
/**
 * @brief 注册视频刷新回调函数
 * 
 * UI 层通过此函数注入渲染逻辑。当视频线程准备好一帧画面时，
 * 会调用这个 std::function 将帧数据传递回 UI 主线程或绘图上下文。
 * 
 * @param callback 符合签名 int(const Frame*) 的回调函数
 */
void FFPlayer::AddVideoRefreshCallback (std::function<int (const Frame *)> callback)
{
    _video_refresh_callback = callback;
}


/**
 * @brief 获取主同步类型
 * 
 * 根据当前配置的同步类型（av_sync_type）以及音视频流的存在状态，
 * 确定实际使用的主时钟源。如果首选的流不存在，则回退到另一种可用的流作为主时钟。
 * 
 * @return int 返回确定的主同步类型，可能的值包括：
 *             - AV_SYNC_VIDEO_MASTER: 以视频时钟为主
 *             - AV_SYNC_AUDIO_MASTER: 以音频时钟为主
 *             - AV_SYNC_UNKNOW_MASTER: 未知或无有效主时钟
 */
int FFPlayer::get_master_sync_type()
{
    // 当配置为视频主同步时，检查视频流是否存在
    if (av_sync_type == AV_SYNC_VIDEO_MASTER) 
    {
        if (video_stream)
            return AV_SYNC_VIDEO_MASTER;
        else
            /* 如果没有视频成分则使用 audio master */
            return AV_SYNC_AUDIO_MASTER;
    } 
    // 当配置为音频主同步时，检查音频流是否存在，若不存在则尝试回退到视频
    else if (av_sync_type == AV_SYNC_AUDIO_MASTER) 
    {
        if (audio_stream)
            return AV_SYNC_AUDIO_MASTER;
        else if(video_stream)
            // 只有音频的存在
            return AV_SYNC_VIDEO_MASTER;
        else
            return AV_SYNC_UNKNOW_MASTER;
    }
    else
    {
        return AV_SYNC_AUDIO_MASTER;
    }
}

/**
 * @brief 获取主时钟时间。
 *
 * 根据当前的同步类型（视频主同步或音频主同步）返回相应的主时钟值。
 * 目前视频主同步分支被注释，默认回退到音频时钟。
 *
 * @return double 当前主时钟的时间值（秒）。若为视频主同步模式，由于代码被注释，返回值可能未初始化（取决于编译器行为），实际使用中应注意此逻辑缺陷。
 */
double FFPlayer::get_master_clock()
{
    double val;

    // 根据同步类型选择主时钟源
    switch (get_master_sync_type()) 
    {
    case AV_SYNC_VIDEO_MASTER:
        val = get_clock(&vidclock);
        break;
    case AV_SYNC_AUDIO_MASTER:
        val = get_clock(&audclock);
        break;
    default:
        val = get_clock(&audclock);
        break;
    }
    return val;
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
            if(_queue->abort_request) // 是否请求退出
            {
                return -1;
            }
            switch (_avcodec_ctx->codec_type)
            {
                case AVMEDIA_TYPE_VIDEO:
                    ret = avcodec_receive_frame(_avcodec_ctx, frame);
                    if(ret >= 0)
                    {
                        LOG(INFO) << "audio frame pts:" <<  frame->pts << ", dts:" << frame->pkt_dts;
                        if (decoder_reorder_pts == -1) {
                              frame->pts = frame->best_effort_timestamp;
                        } else if (!decoder_reorder_pts) {
                              frame->pts = frame->pkt_dts;
                        }
                    }
                    else
                    {
                        char errbuf[1024] = {0};
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        LOG(ERROR) << "avcodec_receive_frame() failed.video frame, Error code: " << ret
                                         << ", Details: " << errbuf;
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    ret = avcodec_receive_frame(_avcodec_ctx, frame);
                    if(ret >= 0)
                    {
                        LOG(INFO) << "audio frame pts:" <<  frame->pts << ", dts:" << frame->pkt_dts;
                        AVRational time_base = {1, frame->sample_rate};
                        if (frame->pts != AV_NOPTS_VALUE)
                        {
                            // 如果frame->pts正常则先将其从pkt_timebase转成{1, frame->sample_rate}
                            // pkt_timebase实质就是stream->time_base
                            frame->pts = av_rescale_q(frame->pts, _avcodec_ctx->pkt_timebase, time_base);
                        }
                        else if (next_pts != AV_NOPTS_VALUE)
                        {
                            // 如果frame->pts不正常则使用上一帧更新的next_pts和next_pts_tb
                            // 转成{1, frame->sample_rate}
                            frame->pts = av_rescale_q(next_pts,next_pts_tb, time_base);
                        }
                        if (frame->pts != AV_NOPTS_VALUE) {
                            // 根据当前帧的pts和nb_samples预估下一帧的pts
                            next_pts = frame->pts + frame->nb_samples;
                            next_pts_tb = time_base; // 设置timebase
                        }
                    }
                    else{
                        char errbuf[1024] = {0};
                        av_strerror(ret, errbuf, sizeof(errbuf));
                        LOG(ERROR) << "avcodec_receive_frame() failed. audio frame,Error code: " << ret
                                         << ", Details: " << errbuf;
                    }
                    break;
            }
            // 检查解码是否已经结束，解码结束返回0
            if(ret == AVERROR_EOF)
            {
                _finished = _pkt_serial;
                LOG(INFO) << "avcodec_flush_buffers pkt_serial:" << _pkt_serial;
                avcodec_flush_buffers(_avcodec_ctx); // 刷新解码器
                return 0;
            }
            // 正常解码返回1
            if(ret >= 0)
            {
                return 1;   // 获取到一帧frame
            }
        } while (ret != AVERROR(EAGAIN)); //没帧可读时ret返回EAGIN，需要继续送packet

        //  获取一个packet，如果播放序列不一致(数据不连续)则过滤掉“过时”的packet
        do {
            //  如果没有数据可读则唤醒read_thread, 实际是continue_read_thread SDL_cond
            //            if (queue_->nb_packets == 0)  // 没有数据可读
            //                SDL_CondSignal(empty_queue_cond);// 通知read_thread放入packet
            //  如果还有pending的packet则使用它(_packet_pending = 有数据包待处理)
            if (_packet_pending) {
                av_packet_move_ref(&pkt, &_pkt);
                _packet_pending = 0;
            } else {
                //  阻塞式读取packet
                if (packet_queue_get(_queue, &pkt, 1, &_pkt_serial) < 0) {
                    return -1;
                }
            }
            if(_queue->serial != _pkt_serial) {
                LOG(INFO) << "discontinue:queue->serial:" << _queue->serial << ", pkt_serial:" << _pkt_serial;
                av_packet_unref(&pkt); // fixed me? 释放要过滤的packet
            }
        } while (_queue->serial != _pkt_serial);// 如果不是同一播放序列(流不连续)则继续读取
        //  将packet送入解码器
        if (pkt.data == flush_pkt.data) {//
            // when seeking or when switching to a different stream
            avcodec_flush_buffers(_avcodec_ctx); //清空里面的缓存帧
            _finished = 0;        // 重置为0
            next_pts = start_pts;     // 主要用在了audio
            next_pts_tb = start_pts_tb;// 主要用在了audio
        } else {
            if (_avcodec_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                //                int got_frame = 0;
                //                ret = avcodec_decode_subtitle2(avctx_, sub, &got_frame, &pkt);
                //                if (ret < 0) {
                //                    ret = AVERROR(EAGAIN);
                //                } else {
                //                    if (got_frame && !pkt.data) {
                //                        packet_pending = 1;
                //                        av_packet_move_ref(&pkt, &pkt);
                //                    }
                //                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                //                }
            } else {
                if (avcodec_send_packet(_avcodec_ctx, &pkt) == AVERROR(EAGAIN)) {
                    //                    av_log(avctx, AV_LOG_ERROR, "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    _packet_pending = 1;
                    av_packet_move_ref(&_pkt, &pkt);
                }
            }
            av_packet_unref(&pkt);	// 一定要自己去释放音视频数据
        }
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
    vp->pos = pos;
    vp->serial = serial;

    // 资源管理权限转移
    av_frame_move_ref(vp->frame, src_frame); // 将src中所有数据转移到dst中，并复位src。
    frame_queue_push(fq);   // 放入帧队列,内部更新写索引位置
    return 0;
}

int Decoder::audio_thread(void *arg)
{ 
    LOG(DEBUG) << "audio_thread() start";
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
        // 获取缓存统计情况
        ffp->ffp_audio_statistic_l();
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
            af->pos = frame->pkt_pos;
            af->serial = ffp->audio_dec._pkt_serial;
            AVRational temp_a;
            temp_a.num = frame->nb_samples;
            temp_a.den = frame->sample_rate;
            af->duration = av_q2d(temp_a);

            av_frame_move_ref(af->frame, frame); // 资源管理权限转移
            frame_queue_push(&ffp->sampq);  // 放入帧队列,内部更新写索引位置(此处代表队列真正插入一帧数据)
        }
    }while(ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
the_end:
    av_frame_free(&frame);
    LOG(DEBUG) << "audio_thread() end";
    return ret;
}

int Decoder::video_thread(void *arg)
{
    LOG(DEBUG) << "video_thread() start";
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
        ffp->ffp_video_statistic_l();// 统计视频packet缓存
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
    LOG(DEBUG) << "video_thread() end";
    av_frame_free(&frame);
    return 0;
}



