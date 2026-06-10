//  imagescaler.h 文件定义了 ImageScaler 类，它是一个图像缩放和格式转换工具类
// 它的核心作用是封装 FFmpeg 的 libswscale 库，用于将解码后的视频帧从一种格式（如 YUV）和尺寸转换为另一种格式（如 RGB）和尺寸，以便在屏幕上显示。

/*在播放器中的角色：
在 DisplayWind 中，解码得到的视频帧通常是 YUV 格式且分辨率固定（例如 1920x1080）。而屏幕上的窗口大小是可变的，且 Qt 的 QImage 通常需要 RGB 格式。

ImageScaler 就负责完成这个“翻译”工作： YUV (1920x1080) --(ImageScaler)--> RGB (根据窗口大小调整) --> QImage --> 屏幕显示*/

#ifndef IMAGESCALER_H
#define IMAGESCALER_H 
#include "ijkplayercore.h"

// Scale算法
enum ScaleAlgorithm
{
    SWS_SA_FAST_BILINEAR    = 0x1,
    SWS_SA_BILINEAR            = 0x2,
    SWS_SA_BICUBIC            = 0x4,
    SWS_SA_X                = 0x8,
    SWS_SA_POINT            = 0x10,
    SWS_SA_AREA                = 0x20,
    SWS_SA_BICUBLIN            = 0x40,
    SWS_SA_GAUSS            = 0x80,
    SWS_SA_SINC                = 0x100,
    SWS_SA_LANCZOS            = 0x200,
    SWS_SA_SPLINE            = 0x400,
};
#define LogError printf

/**
 * @struct VideoFrame
 * @brief 视频帧数据结构，用于存储解码后的原始视频数据及其属性。
 *        该结构体兼容 FFmpeg 的 AVFrame 数据布局，便于直接进行图像缩放和格式转换。
 */
typedef struct VideoFrame
{
    /** @brief 指向实际像素数据的指针数组。
     *         对于平面格式（如 YUV420P），data[0], data[1], data[2] 分别指向 Y, U, V 分量。
     *         对于打包格式（如 RGB24），通常只有 data[0] 有效。
     */
    uint8_t *data[8] = {NULL};  // 通常只需要3个就够了，这里定为8个是因为FFmpeg 支持多达 8 个声道 的平面音频格式（Planar Audio）,为了兼容性

    /** @brief 每一行数据的字节数（步长）。
     *         linesize[i] 对应 data[i] 中每一行的字节宽度。
     *         注意：由于内存对齐，linesize 可能大于 width * pixel_bytes。
     */
    int32_t linesize[8] = {0};

    /** @brief 视频的宽度（像素） */
    int32_t width = 0;

    /** @brief 视频的高度（像素） */
    int32_t height = 0;

    /** @brief 像素格式标识。
     *         使用 AVPixelFormat 枚举值，例如 AV_PIX_FMT_YUV420P, AV_PIX_FMT_RGB24 等。
     */
    int format = AV_PIX_FMT_YUV420P; 
}VideoFrame;

class ImageScaler
{
public:
    ImageScaler(void)
    {
        _sws_ctx = NULL;
        _src_pix_fmt = AV_PIX_FMT_NONE;
        _dst_pix_fmt = AV_PIX_FMT_NONE;
        _en_algorithm = SWS_SA_FAST_BILINEAR;
        _src_width = _src_height = 0;
        _dst_width = _dst_height = 0;
    }
    ~ImageScaler(void)
    {
        DeInit();
    }

    /**
     * @brief 初始化缩放器。
     * @param src_width 源视频的宽度（像素）
     * @param src_height 源视频的高度（像素）
     * @param src_format 源视频的像素格式标识
     * @param dst_width 目标视频的宽度（像素）
     * @param dst_height 目标视频的高度（像素）
     * @param dst_format 目标视频的像素格式标识
     * @param algorithm 缩放算法标识
     * @return 返回码
     */
    RET_CODE Init(uint32_t src_width,uint32_t src_height, int src_pix_fmt, uint32_t dst_width, uint32_t dst_height, int dst_pix_fmt, int en_algorithm = SWS_SA_FAST_BILINEAR)
    {
        _src_width = src_width;
        _src_height = src_height;
        _src_pix_fmt = (AVPixelFormat)src_pix_fmt;
        _dst_width = dst_width;
        _dst_height = dst_height;
        _dst_pix_fmt = (AVPixelFormat)dst_pix_fmt;
        _en_algorithm = en_algorithm;

        // 创建缩放上下文
        _sws_ctx = sws_getContext(
            _src_width, _src_height, _src_pix_fmt,
            _dst_width, _dst_height, _dst_pix_fmt,
            SWS_FAST_BILINEAR, NULL, NULL, NULL
        );
        if (!_sws_ctx)
        {
            LogError("Impossible to create scale context for the conversion "
                     "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                     av_get_pix_fmt_name(_src_pix_fmt), _src_width, _src_height,
                     av_get_pix_fmt_name(_dst_pix_fmt), _dst_width, _dst_height);
            return RET_FAIL;
        }
        return RET_OK; 
    }

    void DeInit()
    {
        if(_sws_ctx)
        {
            sws_freeContext(_sws_ctx);
            _sws_ctx = NULL;
        }
    }

    // 以下3个函数是兼容 FFmpeg 的 AVFrame 数据布局的
    // 这种设计是为了适配不同的数据结构
    // 提供这三个重载函数可以让调用者无需手动转换数据结构，直接传入当前持有的对象即可进行缩放操作

    // 1. 处理 AVFrame
    RET_CODE Scale(const AVFrame *src_frame, AVFrame *dst_frame)
    {
        // 如果源帧的属性与缩放器的属性不一致，则重新初始化缩放器
        if(src_frame->width != _src_width 
            || src_frame->height != _src_height
            || src_frame->format != _src_pix_fmt
            || dst_frame->width != _dst_width
            || dst_frame->height != _dst_height
            || dst_frame->format != _dst_pix_fmt
            || !_sws_ctx)
        {
            // 重新初始化
            DeInit();
            RET_CODE ret = Init(src_frame->width, src_frame->height, src_frame->format, dst_frame->width, dst_frame->height, dst_frame->format, _en_algorithm);
            if(ret != RET_OK)
            {
                LogError("ImageScaler::Init failed\n");
                return ret;
            }
        }

        int dst_slice_h = sws_scale(_sws_ctx,                          
                                    (const uint8_t **)src_frame->data, // 源帧的像素数据
                                    src_frame->linesize,               // 源帧的每一行字节数
                                    0,                                 // 源帧的起始位置
                                    src_frame->height,                 // 处理多少行
                                    dst_frame->data,                   // 目标帧的像素数据
                                    dst_frame->linesize);              // 目标帧的每一行字节数
        if(dst_slice_h > 0)
            return RET_OK;
        else
            return RET_FAIL;
    }

    // 2. 处理 VideoFrame
    RET_CODE Scale2(const VideoFrame *src_frame, VideoFrame *dst_frame)
    {
        if(src_frame->width != _src_width 
            || src_frame->height != _src_height
            || src_frame->format != _src_pix_fmt
            || dst_frame->width != _dst_width
            || dst_frame->height != _dst_height
            || dst_frame->format != _dst_pix_fmt
            || !_sws_ctx)
        {
            // 重新初始化
            DeInit();
            RET_CODE ret = Init(src_frame->width, src_frame->height, src_frame->format, dst_frame->width, dst_frame->height, dst_frame->format, _en_algorithm);
            if(ret != RET_OK)
            {
                LogError("ImageScaler::Init failed\n");
                return ret;
            }
        }
        int dst_slice_h = sws_scale(_sws_ctx, (const uint8_t **)src_frame->data, src_frame->linesize, 0, src_frame->height, dst_frame->data, dst_frame->linesize);
        if(dst_slice_h > 0)
            return RET_OK;
        else
            return RET_FAIL;
    }

    // 3. 处理 Frame (嵌套结构）
    RET_CODE Scale3(const Frame *src_frame,VideoFrame *dst_frame)
    {
        if(src_frame->width != _src_width 
            || src_frame->height != _src_height
            || src_frame->format != _src_pix_fmt
            || dst_frame->width != _dst_width
            || dst_frame->height != _dst_height
            || dst_frame->format != _dst_pix_fmt
            || !_sws_ctx)
        {
            // 重新初始化
            DeInit();
            RET_CODE ret = Init(src_frame->width, src_frame->height, src_frame->format, dst_frame->width, dst_frame->height, dst_frame->format, _en_algorithm);
            if(ret != RET_OK)
            {
                LogError("ImageScaler::Init failed\n");
                return ret;
            }
        }
        int dst_slice_h = sws_scale(_sws_ctx, (const uint8_t **)src_frame->frame->data, src_frame->frame->linesize, 0, src_frame->height, dst_frame->data, dst_frame->linesize);
        if(dst_slice_h > 0)
            return RET_OK;
        else
            return RET_FAIL;
    }

private:
    SwsContext* _sws_ctx;  // SWS缩放上下文
    AVPixelFormat _src_pix_fmt ;    // 源像素格式
    AVPixelFormat _dst_pix_fmt ;    // 目标像素格式
    int _en_algorithm = SWS_SA_FAST_BILINEAR; // Resize 算法

    int           _src_width;     // 源图像宽高
    int           _src_height;
    int           _dst_width;     // 目标图像宽高
    int           _dst_height;
};

#endif  // IMAGESCALER_H