#include "fffplay.h"
#include <iostream>
#include <string.h>
#include <cstring>
#include "ffmsg.h"

// FFPlayer类的构造函数
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   FFPlayer::FFPlayer()
 *   {
 *   }
 * 
 * 问题:
 *   1. 没有初始化成员变量
 *   2. 没有日志输出
 * 
 * 优化方案:
 *   1. 在初始化列表中初始化所有成员变量
 *   2. 添加日志输出便于调试
 *   3. 避免未定义行为
 * ====================================================== */
FFPlayer::FFPlayer()
    : _input_filename(nullptr)
    , _read_thread(nullptr)
    , _abort_request(false)
{
    LOG(LogLevel::DEBUG) << "FFPlayer created";  // 新增日志
    // 原始代码: 成员变量未初始化
}

// FFPlayer类的析构函数
/* ==================== 优化改动说明 ====================
 * 原始代码: 没有析构函数，无法正确释放资源
 * 
 * 问题:
 *   1. _input_filename 使用 strdup() 申请的内存未释放 -> 内存泄漏
 *   2. _read_thread 没有被 join() -> 资源泄漏
 *   3. 消息队列没有销毁
 * 
 * 优化方案:
 *   1. 添加析构函数
 *   2. 调用 stop_read_thread() 优雅停止线程
 *   3. 释放 _input_filename 内存
 *   4. 销毁消息队列
 *   5. 添加日志记录
 * ====================================================== */
FFPlayer::~FFPlayer()
{
    LOG(LogLevel::DEBUG) << "FFPlayer destructor called";  // 新增日志
    
    // 停止读取线程
    stop_read_thread();
    
    // 原始代码完全缺少的: 释放输入文件名内存
    if (_input_filename) {
        free(_input_filename);
        _input_filename = nullptr;
        LOG(LogLevel::DEBUG) << "Released input filename memory";
    }
    
    // 原始代码缺少的: 销毁消息队列
    msg_queue_destroy(&_msg_queue);
    
    LOG(LogLevel::DEBUG) << "FFPlayer destroyed";
}

// 创建播放器
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   int FFPlayer::ffplayer_create()
 *   {
 *       std::cout << "ffp_create\n";
 *       msg_queue_init(&_msg_queue);
 *       return 0;
 *   }
 * 
 * 问题:
 *   1. 没有检查 msg_queue_init 的返回值
 *   2. 使用 std::cout，没有统一日志系统
 *   3. 没有初始化 _abort_request 标志
 * 
 * 优化方案:
 *   1. 检查函数返回值
 *   2. 使用 Logger 系统
 *   3. 初始化退出标志
 *   4. 添加错误处理
 * ====================================================== */
int FFPlayer::ffplayer_create()
{
    LOG(LogLevel::INFO) << "Creating FFPlayer";  // 原始: std::cout << "ffp_create\n";
    
    // 原始代码没有检查返回值
    if (msg_queue_init(&_msg_queue) < 0) {
        LOG(LogLevel::ERROR) << "Failed to initialize message queue";
        return -1;  // 新增
    }
    
    _abort_request = false;  // 新增初始化
    LOG(LogLevel::DEBUG) << "FFPlayer created successfully";  // 新增日志
    return 0;
}

// 播放器异步准备，准备完成后会发送消息通知UI线程,UI线程收到消息后可以调用ijkmp_start()开始播放
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   int FFPlayer::ffplayer_prepare_async_1(char *file_name)
 *   {
 *       _input_filename = strdup(file_name);
 *       int reval = stream_open(file_name);
 *       return reval;
 *   }
 * 
 * 问题:
 *   1. 没有检查 file_name 是否为 nullptr
 *   2. 没有检查 strdup 是否失败
 *   3. 如果旧的 _input_filename 存在，会造成内存泄漏
 *   4. 没有日志输出
 *   5. 错误处理不完善
 * 
 * 优化方案:
 *   1. 添加参数有效性检查
 *   2. 释放旧的文件名
 *   3. 检查内存申请是否成功
 *   4. 添加日志和错误处理
 * ====================================================== */
int FFPlayer::ffplayer_prepare_async_1(char *file_name)
{
    // 原始代码没有的检查
    if (!file_name) {
        LOG(LogLevel::ERROR) << "Invalid file name";
        return -1;
    }
    
    LOG(LogLevel::INFO) << "Preparing async for file: " << file_name;  // 新增日志
    
    // 原始代码没有的: 先释放之前的文件名
    if (_input_filename) {
        free(_input_filename);
        LOG(LogLevel::DEBUG) << "Released previous filename";
    }
    
    // 保存文件名（申请内存+拷贝字符串）
    _input_filename = strdup(file_name);
    
    // 原始代码没有的: 检查申请是否成功
    if (!_input_filename) {
        LOG(LogLevel::ERROR) << "Failed to allocate memory for filename";
        return -1;
    }
    
    int retval = stream_open(file_name);
    
    // 原始代码没有的: 错误处理
    if (retval < 0) {
        LOG(LogLevel::ERROR) << "Failed to open stream";
        free(_input_filename);  // 释放已申请的内存
        _input_filename = nullptr;
        return -1;
    }
    
    return 0;
}

// 打开流
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   int FFPlayer::stream_open(const char *file_name)
 *   {
 *       // 初始化Frame帧队列
 *       // 初始化Packet包队列
 *       // 初始化时钟
 *
 *       // 插件read_thread
 *
 *       _read_thread = new std::thread(&FFPlayer::read_thread,this);
 *   }
 * 
 * 问题:
 *   1. 没有参数检查
 *   2. 没有返回值
 *   3. 使用 new 创建线程，容易内存泄漏
 *   4. 没有异常处理
 *   5. 没有日志输出
 * 
 * 优化方案:
 *   1. 添加参数有效性检查
 *   2. 添加返回值
 *   3. 使用 make_unique 替代 new
 *   4. 使用 try-catch 异常处理
 *   5. 添加详细日志
 * ====================================================== */
int FFPlayer::stream_open(const char *file_name)
{
    // 原始代码没有的检查
    if (!file_name) {
        LOG(LogLevel::ERROR) << "Invalid file name for stream_open";
        return -1;  // 原始代码没有返回值
    }
    
    LOG(LogLevel::INFO) << "Opening stream: " << file_name;  // 新增日志
    
    try {
        // TODO: 初始化Frame帧队列
        // TODO: 初始化Packet包队列
        // TODO: 初始化时钟

        // 创建read_thread
        /* 原始代码: _read_thread = new std::thread(&FFPlayer::read_thread,this);
         * 问题: 使用 new 手动管理内存，容易泄漏
         */
        _abort_request = false;  // 新增初始化标志
        _read_thread = std::make_unique<std::thread>(&FFPlayer::read_thread, this);  // 优化: 使用 make_unique
        
        LOG(LogLevel::INFO) << "Read thread created successfully";  // 新增日志
        return 0;  // 原始代码缺少返回值
    } 
    catch (const std::exception& e) {
        LOG(LogLevel::ERROR) << "Exception in stream_open: " << e.what();  // 新增异常处理和日志
        return -1;
    }
}

// 停止读取线程
/* ==================== 优化改动：新增函数 ====================
 * 原始代码: 完全缺少这个函数
 * 
 * 功能:
 *   1. 设置线程退出标志
 *   2. 等待线程优雅退出
 *   3. 错误处理
 *   4. 日志记录
 * ====================================================== */
void FFPlayer::stop_read_thread()
{
    LOG(LogLevel::DEBUG) << "Stopping read thread";  // 新增日志
    
    // 设置退出标志
    _abort_request = true;
    
    // 等待线程退出
    if (_read_thread && _read_thread->joinable()) {
        try {
            _read_thread->join();
            LOG(LogLevel::DEBUG) << "Read thread joined successfully";  // 新增日志
        } 
        catch (const std::exception& e) {
            LOG(LogLevel::ERROR) << "Exception when joining read thread: " << e.what();  // 新增异常处理
        }
    }
    
    // 释放线程对象 (unique_ptr 自动管理，但显式调用更清晰)
    _read_thread.reset();
}

// 读取线程, 这个线程的主要功能是读取输入流，解封装，解码等，最后把解码后的数据发送给UI线程进行渲染
/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   int FFPlayer::read_thread()
 *   {
 *       ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
 *       std::cout << "read_thread FFP_MSG_OPEN_INPUT " << this << std::endl;
 *       ...
 *       while (1) {
 *           std::this_thread::sleep_for(std::chrono::milliseconds(1000));
 *       }
 *   }
 * 
 * 问题:
 *   1. 无限循环，没有退出条件
 *   2. 使用 std::cout，没有统一日志
 *   3. 没有异常处理
 *   4. 休眠时间过长 (1000ms)
 * 
 * 优化方案:
 *   1. 添加 _abort_request 检查，实现优雅退出
 *   2. 使用 Logger 系统
 *   3. 添加异常处理
 *   4. 改进休眠时间 (100ms)
 * ====================================================== */
int FFPlayer::read_thread()
{
    LOG(LogLevel::INFO) << "Read thread started";  // 新增日志
    
    try {
        ffp_notify_msg1(this, FFP_MSG_OPEN_INPUT);
        LOG(LogLevel::DEBUG) << "Sent FFP_MSG_OPEN_INPUT";  // 优化: 使用 Logger 替代 std::cout
        
        ffp_notify_msg1(this, FFP_MSG_FIND_STREAM_INFO);
        LOG(LogLevel::DEBUG) << "Sent FFP_MSG_FIND_STREAM_INFO";  // 优化: 使用 Logger
        
        ffp_notify_msg1(this, FFP_MSG_COMPONENT_OPEN);
        LOG(LogLevel::DEBUG) << "Sent FFP_MSG_COMPONENT_OPEN";  // 优化: 使用 Logger
        
        ffp_notify_msg1(this, FFP_MSG_PREPARED);
        LOG(LogLevel::DEBUG) << "Sent FFP_MSG_PREPARED";  // 优化: 使用 Logger
        
        // 原始代码: while (1) { ... } 无限循环
        // 优化: 添加 _abort_request 检查
        while (!_abort_request) {
            // 原始代码注释: std::cout << "read_thread sleep, mp:" << this << std::endl;
            // 先模拟线程运行
            
            /* 原始代码: std::this_thread::sleep_for(std::chrono::milliseconds(1000));
             * 优化: 改为 100ms，提高响应速度
             */
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG(LogLevel::INFO) << "Read thread exiting normally";  // 新增日志
    } 
    catch (const std::exception& e) {
        LOG(LogLevel::ERROR) << "Exception in read_thread: " << e.what();  // 新增异常处理和日志
    }
    
    return 0;
}
