#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ijkplayercore.h"

namespace Ui { 
class MainWindow; 
}


// 主窗口类，负责UI界面和播放器的交互
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    //  构造函数和析构函数
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    //  初始化信号槽相关的,connect连接信号和槽
    int InitSignalsAndSlots();
    //  消息循环函数
    int message_loop(void *arg);

    //  输出视频帧的函数
    int OutputVideo(const Frame *frame);

    //  播放或者暂停的槽函数
    void OnPlayOrPause();

    //  停止的槽函数
    void OnStop();

private:
    Ui::MainWindow *ui;
    IjkPlayerCore *_mp = nullptr; // IjkMediaPlayer--mp
};

#endif // MAINWINDOW_H
