#ifndef FFFPLAY_H
#define FFFPLAY_H
#include <thread>
#include <memory>
#include "ffmsg_queue.h"
#include "Logger.hpp"

using namespace LogModule;

class FFPlayer
{
public:
    FFPlayer();
    ~FFPlayer();
    
    int ffplayer_create();  //创建播放器
    //这个函数是 FFplay 播放器的核心准备函数，作用是异步准备媒体资源（打开文件、解封装、解码器初始化等），为后续播放做准备
    int ffplayer_prepare_async_1(char *file);
    int stream_open(const char *file_name); // 打开流
    
    /* ==================== 优化改动：线程停止函数 ====================
     * 原始代码: 没有优雅停止线程的方式
     * 问题:
     *   1. read_thread 会无限循环
     *   2. 析构时无法停止线程
     *   3. 可能导致程序挂起或崩溃
     * 
     * 优化方案: 添加 stop_read_thread() 函数
     * 功能:
     *   1. 设置退出标志 _abort_request
     *   2. join() 等待线程退出
     *   3. 异常处理
     * ================================================================ */
    void stop_read_thread();
    
    MessageQueue _msg_queue; // 消息队列
    char *_input_filename; // 输入文件名
    
    /* ==================== 优化改动：线程退出标志 ====================
     * 原始代码: 没有退出标志，线程无法优雅退出
     * 优化方案: 添加 volatile bool _abort_request
     * 用途:
     *   1. read_thread() 会检查这个标志
     *   2. 当需要停止时，设置为 true
     *   3. volatile 保证读取最新值（防止编译器优化）
     * ================================================================ */
    volatile bool _abort_request;
    
    // 线程的执行函数(线程入口函数)
    int read_thread();// 读取线程, 这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染
    
    /* ==================== 优化改动：使用智能指针管理线程 ====================
     * 原始代码:
     *   std::thread *_read_thread;
     * 
     * 问题:
     *   1. 使用裸指针，需要手动 delete
     *   2. 线程对象没有被 join()，可能导致资源泄漏
     *   3. 析构时没有正确清理
     * 
     * 优化方案: 使用 std::unique_ptr<std::thread>
     * 优势:
     *   1. 自动管理线程对象内存
     *   2. 结合 stop_read_thread() 实现优雅退出
     *   3. 避免手动 delete
     *   4. 异常安全
     * ================================================================ */
    // 原始代码: std::thread *_read_thread;
    std::unique_ptr<std::thread> _read_thread;
};

// 以下封装函数设置内联 inline :编译器会把这个函数直接展开到调用的地方，避免了函数调用的开销，提高了性能

//ffplayer_notify_msg1 是一个接口封装函数，它隐藏了消息队列的具体实现细节

/* ==================== 优化改动：null 指针检查 ====================
 * 原始代码: 直接调用 msg_queue_put_simple3，没有检查 ffp
 * 问题:
 *   1. 如果 ffp 为 nullptr，程序会崩溃
 *   2. 没有错误处理
 * 
 * 优化方案: 添加 if (ffp) 检查
 * 优势:
 *   1. 防止空指针解引用
 *   2. 提高程序的鲁棒性
 * ================================================================ */
inline static void ffp_notify_msg1(FFPlayer *ffp, int what)
{
    // 原始代码: msg_queue_put_simple3(&ffp->_msg_queue,what,0,0);
    if (ffp) {
        msg_queue_put_simple3(&ffp->_msg_queue, what, 0, 0);
    }
    // 优化: 添加 null 指针检查
}

inline static void ffp_notify_msg2(FFPlayer *ffp, int what, int arg1) {
    // 原始代码没有检查
    if (ffp) {
        msg_queue_put_simple3(&ffp->_msg_queue, what, arg1, 0);
    }
}

inline static void ffp_notify_msg3(FFPlayer *ffp, int what, int arg1, int arg2) {
    // 原始代码没有检查
    if (ffp) {
        msg_queue_put_simple3(&ffp->_msg_queue, what, arg1, arg2);
    }
}

inline static void ffp_notify_msg4(FFPlayer *ffp, int what, int arg1, int arg2, void *obj, int obj_len) {
    // 原始代码没有检查
    if (ffp) {
        msg_queue_put_simple4(&ffp->_msg_queue, what, arg1, arg2, obj, obj_len);
    }
}

inline static void ffp_remove_msg(FFPlayer *ffp, int what) {
    // 原始代码没有检查
    if (ffp) {
        msg_queue_remove(&ffp->_msg_queue, what);
    }
}

#endif // FFFPLAY_H
