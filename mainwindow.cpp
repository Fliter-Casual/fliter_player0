#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <thread>
#include <functional>
#include <iostream>
#include "ffmsg.h"
#include "Logger.hpp"

using namespace LogModule;

//主窗口类，负责UI界面和播放器的交互
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _mp(nullptr)
{
    ui->setupUi(this);

    /* ==================== 优化改动：日志系统初始化 ====================
     * 原始代码: 没有日志系统的初始化
     * 优化方案: 添加日志系统初始化
     * 优势: 能够记录程序运行状态，便于调试和监控
     * ================================================================ */
    ENABLE_CONSOLE_LOG_STRATEGY();

    // 初始化信息槽相关的
    InitSignalsAndSlots();
    
    LOG(LogLevel::INFO) << "MainWindow initialized";
}

// 析构函数
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   MainWindow::~MainWindow()
 *   {
 *       delete ui;
 *   }
 * 
 * 问题:
 *   1. 没有delete _mp指针，导致内存泄漏
 *   2. 后台线程没有正确停止
 *   3. 如果出现异常，资源无法正确释放
 * 
 * 优化方案:
 *   1. unique_ptr 会自动调用析构函数释放 _mp
 *   2. 添加日志记录析构过程
 *   3. 异常安全的资源管理
 * ====================================================== */
MainWindow::~MainWindow()
{
    LOG(LogLevel::INFO) << "MainWindow destroyed";
    delete ui;
    // 原始代码缺少: delete _mp; (现在由 unique_ptr 自动管理)
}

// 初始化信号槽相关的: connect: 连接信号和槽
int MainWindow::InitSignalsAndSlots()
{
    //当 ui->showCtrlBar 这个对象发出了 SigPlayOrPause 信号时，请自动调用 this（即 MainWindow）对象的 OnPlayOrPause 函数。
    //就是emit 后，就会跳转到connect，然后执行其绑定的函数
    //这是一种解耦的设计：发送信号的控件（CtrlBar）不需要知道是谁在接收信号，也不需要知道接收者会做什么；接收者（MainWindow）也不需要知道是谁在发送信号

    connect(ui->showCtrlBar, &CtrlBar::SigPlayOrPause,this,&MainWindow::OnPlayOrPause);
    connect(ui->showCtrlBar, &CtrlBar::SigStop,this,&MainWindow::OnStop);
    
    LOG(LogLevel::INFO) << "Signals and slots initialized";
    return 0;
}

// 消息循环函数
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   - 使用 qDebug() 和 std::cout 混合输出
 *   - 没有 null 指针检查
 *   - 无异常处理
 * 
 * 优化方案:
 *   1. 统一使用 Logger 日志系统
 *   2. 添加参数有效性检查
 *   3. 添加错误处理
 *   4. 改进日志信息的清晰度
 * ====================================================== */
int MainWindow::message_loop(void *arg)
{
    IjkPlayerCore *mp = (IjkPlayerCore*)arg;
    
    // 原始代码没有的检查
    if (!mp) {
        LOG(LogLevel::ERROR) << "Invalid player core pointer";
        return -1;
    }

    // 线程循环
    LOG(LogLevel::INFO) << "message_loop started";  // 原始: qDebug() << "message_loop into";
    
    while(true)
    {
        AVMessage msg;
        // 取消息队列的消息，如果没有消息就阻塞，直到有消息被发到消息队列
        int retval = mp->ijkmp_get_msg(&msg,1); // 主要处理播放器的消息

        if(retval < 0) {
            LOG(LogLevel::WARNING) << "Message queue aborted";  // 原始: break 但没有日志
            break;
        }
        
        switch (msg.what) {
        // 刷新消息，播放器状态发生改变时会发送这个消息，例如从正在播放变成暂停了，或者从正在播放变成完成了等等，UI线程收到这个消息后可以更新UI
        case FFP_MSG_FLUSH:
            LOG(LogLevel::DEBUG) << "FFP_MSG_FLUSH";  // 原始: qDebug() << __FUNCTION__ << " FFP_MSG_FLUSH";
            break;
        // 准备完成的消息，播放器准备完成后会发送这个消息，UI线程收到这个消息后可以调用ijkmp_start()开始播放
        case FFP_MSG_PREPARED:
            LOG(LogLevel::INFO) << "FFP_MSG_PREPARED - Starting playback";  // 原始: std::cout
            
            // 原始代码没有错误检查
            if (mp->ijkmp_start() < 0) {
                LOG(LogLevel::ERROR) << "Failed to start playback";
            }
            break;
        default:
            LOG(LogLevel::DEBUG) << "Unknown message: " << msg.what;  // 原始: qDebug() << __FUNCTION__ << " default " << msg.what;
            break;
        }
        msg_free_res(&msg);
        
        // 先模拟线程运行
        /* 原始代码: std::this_thread::sleep_for(std::chrono::milliseconds(1000));
         * 优化: 改为 100ms，提高响应速度
         */
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG(LogLevel::INFO) << "message_loop ended";  // 原始: qDebug() << "message_loop leave";
    return 0;
}

// 播放或者暂停的槽函数
/* ==================== 优化改动说明 ====================
 * 原始代码问题:
 *   1. 没有互斥锁保护 _mp，线程不安全
 *   2. 错误处理不完善
 *   3. 内存泄漏风险
 *   4. 没有异常处理
 * 
 * 优化方案:
 *   1. 添加互斥锁 (std::lock_guard)
 *   2. 使用 unique_ptr 自动管理内存
 *   3. 完善的错误检查
 *   4. try-catch 异常处理
 *   5. 使用 make_unique 替代 new
 *   6. 统一日志输出
 * ====================================================== */
void MainWindow::OnPlayOrPause()
{
    LOG(LogLevel::INFO) << "OnPlayOrPause called";
    
    try {
        // 原始代码没有互斥锁保护！现在添加线程安全保护
        std::lock_guard<std::mutex> lock(_mp_mutex);
        
        int ret = 0;
        // 1. 先检测mp是否已经创建
        if(!_mp) {
            // 原始: _mp = new IjkPlayerCore();
            // 优化: 使用 make_unique 异常安全且自动管理
            _mp = std::make_unique<IjkPlayerCore>();
            
            // 创建播放器，创建的时候要传入消息循环函数，这样播放器就知道消息循环函数在哪里了，播放器在需要发送消息的时候就可以调用这个函数
            ret = _mp->ijkmp_create(std::bind(&MainWindow::message_loop, this, std::placeholders::_1));
            if(ret <0) {
                LOG(LogLevel::ERROR) << "Failed to create IjkMediaPlayer";  // 原始: qDebug() << "IjkMediaPlayer create failed";
                _mp.reset(); // 显式释放 (unique_ptr 也会自动释放)
                return;
            }
            // 设置url
            ret = _mp->ijkmp_set_data_source("2_audio.mp4");
            if(ret <0) {
                LOG(LogLevel::ERROR) << "Failed to set data source";  // 原始错误消息: "IjkMediaPlayer create failed"（误导）
                _mp.reset();
                return;
            }
            
            // 准备工作,准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
            ret = _mp->ijkmp_prepare_async();
            if(ret <0) {
                LOG(LogLevel::ERROR) << "Failed to prepare async";  // 原始同样是误导的错误信息
                _mp.reset();
                return;
            }
            
            LOG(LogLevel::INFO) << "Player prepared successfully";
        } else {
            // 已经准备好了，则暂停或者恢复播放
            LOG(LogLevel::INFO) << "Player already prepared, toggling pause/play";
            // TODO: 实现暂停/恢复逻辑
        }
    } 
    // 原始代码完全没有异常处理！
    catch (const std::exception& e) {
        LOG(LogLevel::ERROR) << "Exception in OnPlayOrPause: " << e.what();
    }
}

// 停止的槽函数
/* ==================== 优化改动说明 ====================
 * 原始代码问题:
 *   1. 没有互斥锁保护
 *   2. 没有异常处理
 *   3. 没有返回值检查
 *   4. 没有日志输出
 * 
 * 优化方案:
 *   1. 添加互斥锁
 *   2. 添加异常处理
 *   3. 检查返回值
 *   4. 统一日志输出
 *   5. 显式调用 reset() 虽然自动管理，但更清晰
 * ====================================================== */
void MainWindow::OnStop()
{
    LOG(LogLevel::INFO) << "OnStop called";
    
    try {
        // 原始代码没有的互斥锁保护
        std::lock_guard<std::mutex> lock(_mp_mutex);
        
        if(_mp) {
            // 原始代码没有检查返回值
            int ret = _mp->ijkmp_stop();
            if (ret < 0) {
                LOG(LogLevel::WARNING) << "ijkmp_stop returned error: " << ret;
            }
            
            // 原始: delete _mp; _mp = NULL;
            // 优化: 使用 unique_ptr 的 reset()，自动释放
            _mp.reset();
            LOG(LogLevel::INFO) << "Player stopped and released";
        }
    } 
    // 原始代码没有异常处理
    catch (const std::exception& e) {
        LOG(LogLevel::ERROR) << "Exception in OnStop: " << e.what();
    }
}
