#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ijkplayercore.h"

namespace Ui { class MainWindow; }


// 1. 主窗口类，负责UI界面和播放器的交互
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // 1. 构造函数和析构函数
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    // 2. 初始化信号槽相关的
    int InitSignalsAndSlots();
    // 3. 消息循环函数
    int message_loop(void *arg);
    // 4. 播放或者暂停的槽函数
    void OnPlayOrPause();

private:
    Ui::MainWindow *ui;
    IjkPlayerCore *_mp = NULL; // IjkMediaPlayer--mp
};

#endif // MAINWINDOW_H
