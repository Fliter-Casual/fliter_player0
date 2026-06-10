#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <thread>
#include <functional>
#include <iostream>
#include "ffmsg.h"

//主窗口类，负责UI界面和播放器的交互
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 初始化信息槽相关的
    InitSignalsAndSlots();
}

// 析构函数
MainWindow::~MainWindow()
{
    delete ui;
}

// 初始化信号槽相关的: connect: 连接信号和槽
int MainWindow::InitSignalsAndSlots()
{
    //当 ui->showCtrlBar 这个对象发出了 SigPlayOrPause 信号时，请自动调用 this（即 MainWindow）对象的 OnPlayOrPause 函数。
    //就是emit 后，就会跳转到connect，然后执行其绑定的函数
    //这是一种解耦的设计：发送信号的控件（CtrlBar）不需要知道是谁在接收信号，也不需要知道接收者会做什么；接收者（MainWindow）也不需要知道是谁发出的信号

    connect(ui->showCtrlBar, &CtrlBar::SigPlayOrPause,this,&MainWindow::OnPlayOrPause);
    connect(ui->showCtrlBar, &CtrlBar::SigStop,this,&MainWindow::OnStop);
    return 0;
}

// 消息循环函数
int MainWindow::message_loop(void *arg)
{
    IjkPlayerCore *mp = (IjkPlayerCore*)arg;
    // 线程循环
    qDebug() << "message_loop into";
    while(1)
    {
        AVMessage msg;
        // 取消息队列的消息，如果没有消息就阻塞，直到有消息被发到消息队列
        int retval = mp->ijkmp_get_msg(&msg,1); // 主要处理Java->C的消息

        if(retval < 0)
            break;
        switch (msg.what) {
        // 刷新消息，播放器状态发生改变时会发送这个消息，例如从正在播放变成暂停了，或者从正在播放变成完成了等等，UI线程收到这个消息后可以刷新UI界面，例如把正在播放的图标
        case FFP_MSG_FLUSH:
            qDebug() << __FUNCTION__ << " FFP_MSG_FLUSH";
            break;
        // 准备完成的消息，播放器准备完成后会发送这个消息，UI线程收到这个消息后可以调用ijkmp_start()开始播放
        case FFP_MSG_PREPARED:
            std::cout << __FUNCTION__ << " FFP_MSG_PREPARED" << std::endl;
            mp->ijkmp_start();
            break;
        default:
            qDebug()  << __FUNCTION__ << " default " << msg.what ;
            break;
        }
        msg_free_res(&msg);
        //        qDebug() << "message_loop sleep, mp:" << mp;
        // 先模拟线程运行
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    }
    qDebug() << "message_loop leave";
}

// 输出视频帧的函数
// 总结流程： 视频解码线程解码出一帧图像 -> 回调 OutputVideo 函数 
// -> 通过此句代码将图像数据交给 UI 控件 -> 用户在屏幕上看到视频画面
int MainWindow::OutputVideo(const Frame *frame)
{
    // 将解码后的视频帧数据传递给自定义显示控件进行渲染
    // ui->showWindow用于显示视频的自定义控件实例，类型为 showindow
    // 调用 showindow 类中的 Draw 成员函数(执行动作)
    // 这个函数内部通常会使用 OpenGL、SDL 或 Qt 的 QPainter 等技术，将 frame 中的像素数据绘制到屏幕上的 showWindow 区域
    qDebug() << "OutputVideo call";
    return ui->showWindow->Draw(frame);
}

// 播放或者暂停的槽函数
void MainWindow::OnPlayOrPause()
{
    qDebug() << "OnPlayOrPause call";
    int ret = 0;
    // 1. 先检测mp是否已经创建
    if(!_mp) {
        _mp = new IjkPlayerCore();

        // 创建播放器，创建的时候要传入消息循环函数，这样播放器就知道消息循环函数在哪里了，播放器在需要发送消息的时候就可以调用这个函数把消息发到UI线程的消息队列里
        // ret = mp_->ijkmp_create(std::bind(&MainWind::message_loop, this, std::placeholders::_1));不清晰，此处用lambda
        ret = _mp->ijkmp_create([this](void *arg) {
            return this->message_loop(arg);});
        if(ret <0) {
            qDebug() << "IjkMediaPlayer create failed";
            delete _mp;
            _mp = nullptr;
            return;
        }
        _mp->AddVideoRefreshCallback(std::bind(&MainWindow::OutputVideo, this, std::placeholders::_1));
        // 设置url
        _mp->ijkmp_set_data_source("2_audio.mp4");
        // 准备工作,准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
        ret = _mp->ijkmp_prepare_async();
        if(ret <0) {
            qDebug() << "IjkMediaPlayer create failed";
            delete _mp;
            _mp = nullptr;
            return;
        }
    } else {
        // 已经准备好了，则暂停或者恢复播放
    }
}

// 停止的槽函数
void MainWindow::OnStop()
{
    qDebug() << "OnStop call";
    if(_mp) {
        _mp->ijkmp_stop();
        _mp->ijkmp_destroy();
        delete _mp;
        _mp = nullptr;
    }
}
