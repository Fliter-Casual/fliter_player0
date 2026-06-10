#ifndef SHOWINDOW_H
#define SHOWINDOW_H

#include <QWidget>
#include <QMutex>
#include "ijkplayercore.h"
#include "imagescaler.h"

namespace Ui {
class showindow;
}

class showindow : public QWidget
{
    Q_OBJECT

public:
    explicit showindow(QWidget *parent = nullptr);
    ~showindow();
    int Draw(const Frame *frame);
protected:
    // 这里不要重载event事件，会导致paintEvent不被触发
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event);
private:
    Ui::showindow *ui;

    int m_nLastFrameWidth ; // 记录视频宽高
    int m_nLastFrameHeight ;
    bool _is_display_size_changed = false;

    int _x = 0; // 起始位置坐标
    int _y = 0; 
    int video_width = 0;
    int video_height = 0;
    int image_width = 0;
    int image_height = 0;
    QImage img;
    VideoFrame _dst_video_frame;
    QMutex m_mutex;
    ImageScaler *_image_scaler = NULL;

};

#endif // SHOWINDOW_H
