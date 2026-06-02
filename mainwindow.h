#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include <mutex>
#include "ijkplayercore.h"

namespace Ui { class MainWindow; }


// 主窗口类，负责UI界面和播放器的交互
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    //  构造函数和析构函数
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    //  初始化信号槽相关的
    int InitSignalsAndSlots();
    //  消息循环函数
    int message_loop(void *arg);

    //  播放或者暂停的槽函数
    void OnPlayOrPause();

    //  停止的槽函数
    void OnStop();

private:
    Ui::MainWindow *ui;
    
    /* ==================== 优化改动说明 ====================
     * 原始代码: IjkPlayerCore *_mp = NULL;
     * 问题:
     *   1. 使用裸指针，需要手动管理内存
     *   2. 容易导致内存泄漏
     *   3. 析构函数中没有delete操作
     * 
     * 优化方案: 使用 std::unique_ptr 智能指针
     * 优势:
     *   1. 自动管理内存生命周期
     *   2. 析构时自动释放资源
     *   3. 避免重复delete
     *   4. 异常安全
     * ======================================================
     */
    // IjkPlayerCore *_mp = NULL; // 原始代码 - 裸指针
    std::unique_ptr<IjkPlayerCore> _mp;  // 优化后 - 智能指针

    /* ==================== 线程安全优化 ====================
     * 原始代码: 没有互斥锁保护 _mp
     * 问题:
     *   1. OnPlayOrPause() 和 OnStop() 可能同时访问 _mp
     *   2. 检查和赋值不是原子操作
     *   3. 可能导致数据竞争(race condition)
     * 
     * 优化方案: 添加互斥锁保护
     * 用途:
     *   1. 确保对 _mp 的访问是线程安全的
     *   2. 使用 std::lock_guard 自动加锁/解锁
     * ======================================================
     */
    mutable std::mutex _mp_mutex;  // 新增 - 保护 _mp 的互斥锁
};

#endif // MAINWINDOW_H
