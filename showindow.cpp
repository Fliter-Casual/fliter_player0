#include "showindow.h"
#include "ui_showindow.h"

#include <QPainter>
showindow::showindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::showindow)
{
    ui->setupUi(this);
}


showindow::~showindow()
{
    delete ui;
    if(_dst_video_frame.data[0])
        free(_dst_video_frame.data[0]);
    if(_image_scaler)
    {
        delete _image_scaler;
        _image_scaler = NULL;
    }
}

int showindow::Draw(const Frame *frame)
{ 
    QMutexLocker locker(&m_mutex);

    if(!_image_scaler)
    {
        int win_width = width();
        int win_height = height();
        video_width = frame->width;
        video_height = frame->height;
        _image_scaler = new ImageScaler();
        double video_aspect_ratio = frame->width * 1.0 / frame->height; // 视频本身宽高比
        double win_aspect_ratio = win_width * 1.0 / win_height; //  窗口宽高比
        if(win_aspect_ratio > video_aspect_ratio) // 窗口宽高比大于视频宽高比
        {
            // 窗口宽高比大于视频宽高比
            // 此时应该是调整x的起始位置，以高度为基准
            image_height = win_height;
            if(image_height %2 != 0) {
                image_height -= 1;
            }

            image_width = image_height * video_aspect_ratio;
            if(image_width %2 != 0) {
                image_width -= 1;
            }
            _y = 0;
            _x = (win_width - image_width) / 2;
        }
        else 
        {
            //此时应该是调整y的起始位置，以宽度为基准
            image_width = win_width;
            if(image_width %2 != 0) {
                image_width -= 1;
            }
            image_height = image_width / video_aspect_ratio;
            if(image_height %2 != 0) {
                image_height -= 1;
            }
            _x = 0;
            _y = (win_height - image_height) / 2;
        }
        _image_scaler->Init(video_width, video_height , frame->format, image_width, image_height, AV_PIX_FMT_RGB24);
        memset(&_dst_video_frame, 0, sizeof(VideoFrame));
        _dst_video_frame.width = image_width;
        _dst_video_frame.height = image_height;
        _dst_video_frame.format = AV_PIX_FMT_RGB24;
        // 在 RGB24 格式中，每一个像素需要 3 个字节来存储（通常顺序为 R, G, B，各占 1 字节）,所以要乘3
        _dst_video_frame.data[0] = (uint8_t*)malloc(image_width * image_height * 3);
        _dst_video_frame.linesize[0] = image_width * 3;  // 每行的字节数
    }
    _image_scaler->Scale3(frame, &_dst_video_frame);

    QImage imageTmp = QImage((uint8_t *)_dst_video_frame.data[0], image_width, image_height, QImage::Format_RGB888);
    img = imageTmp.copy(0,0,image_width, image_height);

    update();
    // repaint();
    return 0;
}

void showindow::paintEvent(QPaintEvent *)
{ 
    QMutexLocker locker(&m_mutex);
    if(img.isNull())
        return;
    QPainter painter(this);

    //    //    p.translate(X, Y);
    //    //    p.drawImage(QRect(0, 0, W, H), img);
    QRect rect = QRect(_x, _y, img.width(), img.height());
    painter.drawImage(rect, img);
}

void showindow::resizeEvent(QResizeEvent *)
{
    
}
