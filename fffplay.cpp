#include "fffplay.h"
#include <iostream>
#include <string.h>
#include "ffmsg.h"
#include "Logger.hpp"
// #include <experimental/filesystem>
// namespace fs = std::experimental::filesystem;

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

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
        swr_free(&swr_ctx);
        // 释放audio buf
        av_freep(&audio_buf1);
        audio_buf = nullptr;
        audio_buf1_size = 0;
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

static int audio_decode_frame(FFPlayer *is)
{
    int data_size = 0; 
    int resampled_data_size = 0;
    int64_t dec_channel_layout = 0; 
    int wanted_nb_samples = 0; 
    Frame *af = nullptr; 
    int ret = 0;

    // 读取一帧数据
//    do{
        // 若队列头部可读，则由af指向可读帧
        if(!(af = frame_queue_peek_readable(&is->sampq)))
            return -1;
//        frame_queue_next(&ffp->sampq);
//    }while(af->serial != ffp->audioq.serial);



    // 根据frame中指定的音频参数获取缓冲区的大小 af->frame->channels * af->frame->nb_samples * av_get_bytes_per_sample(af->frame->format)
    data_size = av_samples_get_buffer_size(nullptr, 
                                           af->frame->channels, 
                                           af->frame->nb_samples, // 样本数量
                                           (enum AVSampleFormat)af->frame->format, 1);
    // 获取声道布局
    dec_channel_layout = af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout) ?
                         af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
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
        af->frame->sample_rate != is->audio_src.freq )
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
        if(out_size <= 0)
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
    
    frame_queue_next(&is->pictq);  // 移除队列中的第一个元素,真正释放frame

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
static void sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    FFPlayer *ffp = (FFPlayer *)userdata;
    int audio_size = 0;
    int len1 = 0;   

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
            audio_size = audio_decode_frame(ffp);// 返回有效的PCM数据长度
            if(audio_size < 0)
            {
                // 静音的逻辑
                /* if error, just output silence */
                ffp->audio_buf = nullptr;
                ffp->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / ffp->audio_tgt.frame_size * ffp->audio_tgt.frame_size;
            }
            else
            {
                ffp->audio_buf_size = audio_size; 
            }
            ffp->audio_buf_index = 0; // 重置索引
        }
        
        // 计算本次可拷贝的数据量：取剩余未读数据长度与 SDL 请求长度的较小值
        len1 = ffp->audio_buf_size - ffp->audio_buf_index;
        len1 = FFMIN(len1, len);

        // 将内部缓冲区中的数据拷贝到 SDL 输出流
        if(ffp->audio_buf)
            memcpy(stream, (uint8_t *)ffp->audio_buf + ffp->audio_buf_index, len1);
        
        // 更新剩余需要填充的长度、输出流指针位置以及内部缓冲区的读取索引
        len -= len1;      
        stream += len1;   
        /* 更新ffp->audio_buf_index，指向audio_buf中未被拷贝到stream的数据（剩余数据）的起始位置 */
        ffp->audio_buf_index += len1;
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
    wanted_spec.callback = sdl_audio_callback;  // 设置音频数据填充回调函数
    wanted_spec.userdata = this;                //将 FFPlayer 实例指针传给回调，以便在回调中访问成员变量

    // 2. 打开 SDL 音频设备
    // SDL_OpenAudio 会尝试按照 wanted_spec 打开设备. 第二个硬件支持的参数暂不考虑
    if(SDL_OpenAudio(&wanted_spec, nullptr) != 0)
    {
        LOG(LogLevel::ERROR) << "SDL_OpenAudio() failed";
        return -1;
    }

    // 3. 同步 FFmpeg 音频参数 (用于后续的重采样配置)
    // 我们将 SDL 实际确定的输出格式保存下来，作为音频重采样的“目标格式”。
    // 即：无论输入音频是什么格式，最终都要重采样成这个格式交给 SDL
    audio_hw_params->format = AV_SAMPLE_FMT_S16; //  FFmpeg 的枚举值,和上面的format对应的内存布局是一样的，都是16位PCM
    audio_hw_params->freq = wanted_spec.freq; // 实际输出的采样率
    audio_hw_params->channels = wanted_spec.channels;// 实际声道数
    audio_hw_params->channel_layout = wanted_channel_layout;

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
//        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (abort_request)
        {
            break;
        }

        // 读取一个packet，得到的是音视频分离后，解码前的数据
        ret = av_read_frame(ic, pkt); // packet要自己去释放
        if (ret < 0) // 读取失败或读取完毕了
        {
            if (ret == AVERROR_EOF || avio_feof(ic->pb) && !eof) // 文件读取完毕
            {
                eof = 1;
            }
            if(ic->pb && ic->pb->error)  // io异常 / 退出循环
            {
                LOG(LogLevel::ERROR) << "read_thread: av_read_frame() error: " << ret << ", pb error: " << ic->pb->error;
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
            printf("read_thread: pkt->pts: %lld, pkt->dts: %lld, pkt->duration: %d, pkt->size: %d\n",
                   pkt->pts/48, pkt->dts, pkt->duration, pkt->size);
            packet_queue_put(&audioq, pkt);
        }
        else
        {
            av_packet_unref(pkt); // 不入队列则直接释放packet
        }
    }

    LOG(LogLevel::INFO) << __FUNCTION__ << "read_thread exit.";

fail:
    return 0;
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

