#include "mainwindow.h"

#include <QApplication>
#include "Logger.hpp"

using namespace LogModule;

/* ==================== 优化改动说明 ====================
 * 原始代码:
 *   #undef main
 *   int main(int argc, char *argv[])
 *   {
 *       QApplication a(argc, argv);
 *       MainWindow w;
 *       w.show();
 *       return a.exec();
 *   }
 * 
 * 问题:
 *   1. 没有日志系统初始化
 *   2. 没有错误处理
 *   3. 没有程序启动/退出日志
 * 
 * 优化方案:
 *   1. 初始化日志系统
 *   2. 添加异常处理
 *   3. 添加程序生命周期日志
 *   4. 使用 try-catch 捕获异常
 * ====================================================== */

#undef main

int main(int argc, char *argv[])
{
    /* ==================== 优化改动：日志系统初始化 ====================
     * 原始代码: 没有初始化
     * 优化: 在应用启动前初始化日志系统
     * 优势:
     *   1. 能够记录应用启动过程
     *   2. 便于调试
     *   3. 更好的问题追踪
     * ================================================================ */
    ENABLE_CONSOLE_LOG_STRATEGY();
    LOG(LogLevel::INFO) << "========================================";
    LOG(LogLevel::INFO) << "Fliter Player0 Application Starting";
    LOG(LogLevel::INFO) << "========================================";
    
    try {
        /* 原始代码:
         * QApplication a(argc, argv);
         * MainWindow w;
         * w.show();
         * return a.exec();
         */
        QApplication a(argc, argv);
        
        LOG(LogLevel::DEBUG) << "QApplication created successfully";
        
        MainWindow w;
        LOG(LogLevel::DEBUG) << "MainWindow created successfully";
        
        w.show();
        LOG(LogLevel::DEBUG) << "MainWindow displayed";
        
        int result = a.exec();
        
        LOG(LogLevel::INFO) << "========================================";
        LOG(LogLevel::INFO) << "Fliter Player0 Application Exiting";
        LOG(LogLevel::INFO) << "Exit code: " << result;
        LOG(LogLevel::INFO) << "========================================";
        
        return result;
    }
    /* ==================== 优化改动：异常处理 ====================
     * 原始代码: 完全没有异常处理
     * 优化: 添加 try-catch 块
     * 优势:
     *   1. 捕获运行时异常
     *   2. 优雅地退出程序
     *   3. 记录错误信息便于调试
     * ================================================================ */
    catch (const std::exception& e) {
        LOG(LogLevel::ERROR) << "Standard exception caught: " << e.what();
        return -1;
    }
    catch (...) {
        LOG(LogLevel::FATAL) << "Unknown exception caught";
        return -1;
    }
}
