# Fliter Player0

一个使用 **FFmpeg** 和 **Qt** 实现的高性能视频播放器，参考了 ijkplayer 的架构设计。

## 📋 项目概述

Fliter Player0 是一个跨平台的视频播放器应用，使用现代 C++ 和 Qt 框架构建。它为视频播放提供了强大的基础，集成了 FFmpeg 来处理各种媒体格式。

**开发状态:** 🚧 *积极开发中*

## 🛠️ 技术栈

- **编程语言:** C++
- **UI 框架:** Qt (Qt Creator 项目)
- **媒体引擎:** FFmpeg
- **平台支持:** 跨平台 (Windows、Linux、macOS)
- **开源协议:** MIT

## 📁 项目结构

```
fliter_player0/
├── 核心组件
│   ├── fffplay.h / fffplay.cpp           # FFmpeg 播放器主接口
│   ├── fffplay_def.h / fffplay_def.cpp   # 播放器定义和配置
│   ├── ijkplayercore.h / ijkplayercore.cpp # IJK 风格的播放器核心
│   └── ff_fferror.h                      # FFmpeg 错误处理
│
├── UI 组件
│   ├── mainwindow.h / mainwindow.cpp / mainwindow.ui     # 主窗口
│   ├── ctrlbar.h / ctrlbar.cpp / ctrlbar.ui              # 控制条
│   ├── titlebar.h / titlebar.cpp / titlebar.ui           # 标题栏
│   ├── showindow.h / showindow.cpp / showindow.ui        # 视频显示窗口
│   └── playlist.h / playlist.cpp / playlist.ui            # 播放列表管理
│
├── 工具类
│   ├── ffmsg.h / ffmsg_queue.h / ffmsg_queue.cpp  # 消息队列系统
│   ├── Logger.hpp / logger.cpp                     # 日志工具
│   ├── Mutex.hpp                                   # 线程同步
│   └── icon.qrc                                    # 资源配置
│
├── 构建文件
│   ├── Fliter_player0.pro   # Qt 项目文件
│   ├── main.cpp             # 应用入口
│   └── icon/                # 图标资源
│
└── 配置文件
    ├── .gitignore
    ├── .gitattributes
    └── LICENSE
```

## 🚀 快速开始

### 系统要求

- **Qt 5.x 或 Qt 6.x**
- **FFmpeg 4.x 及以上版本**
  - Ubuntu: `sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev`
  - macOS: `brew install ffmpeg`
  - Windows: 下载 FFmpeg 二进制文件或使用包管理器

### 编译步骤

1. **克隆仓库:**
   ```bash
   git clone https://github.com/Fliter-Casual/fliter_player0.git
   cd fliter_player0
   ```

2. **用 Qt Creator 打开:**
   - 启动 Qt Creator
   - 打开 `Fliter_player0.pro` 文件
   - 选择你的 Qt 工具链进行配置

3. **配置 FFmpeg 路径 (如需要):**
   - 编辑 `Fliter_player0.pro` 文件，添加 FFmpeg 路径:
     ```qmake
     INCLUDEPATH += /path/to/ffmpeg/include
     LIBS += -L/path/to/ffmpeg/lib -lavformat -lavcodec -lavutil -lswscale
     ```

4. **编译项目:**
   - 选择 `构建` → `构建项目` 或按下 `Ctrl+B`

### 运行

```bash
./fliter_player0
```

## 🎯 功能计划

- [ ] 支持多种视频编码格式的播放
- [ ] 播放列表管理
- [ ] 播放控制 (播放、暂停、快进、音量)
- [ ] 自定义 UI (标题栏和控制条)
- [ ] 精确到帧的快进功能
- [ ] 消息队列系统实现组件解耦通信
- [ ] 完整的日志系统
- [ ] 跨平台支持

## 📝 架构亮点

- **消息队列系统:** 使用 `ffmsg_queue` 实现异步通信
- **FFmpeg 集成:** 直接调用 FFmpeg 库实现高性能媒体处理
- **IJK 风格架构:** 参考 ijkplayer 的成熟设计模式
- **线程安全:** 使用 Mutex 工具确保多线程播放安全

## 🔧 核心组件说明

### FFPlay 核心 (`fffplay.h`, `fffplay.cpp`)
视频播放和控制的主接口。

### 消息队列 (`ffmsg_queue.h`, `ffmsg_queue.cpp`)
处理播放器各组件间的异步通信。

### UI 层
- **MainWindow:** 应用主窗口
- **CtrlBar:** 播放控制条
- **TitleBar:** 窗口标题和元数据显示
- **ShowWindow:** 视频渲染区域
- **Playlist:** 媒体列表管理

## 🐛 已知问题 & TODO

- [ ] 完成音频输出功能
- [ ] 添加字幕支持
- [ ] 优化大文件播放性能
- [ ] 添加配置文件支持
- [ ] 实现硬件加速 (CUDA/VA-API)
- [ ] 完善错误处理和恢复机制

## 📄 开源协议

本项目采用 **MIT 许可证** - 详见 [LICENSE](LICENSE) 文件。

## 🤝 贡献指南

欢迎贡献代码！你可以：
- Fork 本仓库
- 创建功能分支
- 提交 Pull Request

## 📞 联系方式

如有问题、建议或 Bug 报告，欢迎在 [Issues](https://github.com/Fliter-Casual/fliter_player0/issues) 中提出。

---

# Fliter Player0

A high-performance video player implemented with **FFmpeg** and **Qt**, referencing the architectural design from ijkplayer.

## 📋 Overview

Fliter Player0 is a cross-platform video player application built with modern C++ and Qt framework. It provides a robust foundation for video playback with advanced FFmpeg integration for handling various media formats.

**Current Status:** 🚧 *In Active Development*

## 🛠️ Technology Stack

- **Language:** C++
- **GUI Framework:** Qt (Qt Creator project)
- **Media Engine:** FFmpeg
- **Platform:** Cross-platform (Windows, Linux, macOS)
- **License:** MIT

## 📁 Project Structure

```
fliter_player0/
├── Core Components
│   ├── fffplay.h / fffplay.cpp           # Main FFmpeg player interface
│   ├── fffplay_def.h / fffplay_def.cpp   # Player definitions and configurations
│   ├── ijkplayercore.h / ijkplayercore.cpp # IJK-style player core
│   └── ff_fferror.h                      # FFmpeg error handling
│
├── UI Components
│   ├── mainwindow.h / mainwindow.cpp / mainwindow.ui     # Main application window
│   ├── ctrlbar.h / ctrlbar.cpp / ctrlbar.ui              # Control bar
│   ├── titlebar.h / titlebar.cpp / titlebar.ui           # Title bar
│   ├── showindow.h / showindow.cpp / showindow.ui        # Video display window
│   └── playlist.h / playlist.cpp / playlist.ui            # Playlist management
│
├── Utilities
│   ├── ffmsg.h / ffmsg_queue.h / ffmsg_queue.cpp  # Message queue system
│   ├── Logger.hpp                                  # Logging utility
│   ├── Mutex.hpp                                   # Thread synchronization
│   └── icon.qrc                                    # Resource configuration
│
├── Build Files
│   ├── Fliter_player0.pro   # Qt project file
│   ├── main.cpp             # Application entry point
│   └── icon/                # Icon resources
│
└── Configuration
    ├── .gitignore
    ├── .gitattributes
    └── LICENSE
```

## 🚀 Getting Started

### Prerequisites

- **Qt 5.x or Qt 6.x**
- **FFmpeg 4.x or higher**
  - On Ubuntu: `sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev`
  - On macOS: `brew install ffmpeg`
  - On Windows: Download FFmpeg binaries or use package manager

### Building

1. **Clone the repository:**
   ```bash
   git clone https://github.com/Fliter-Casual/fliter_player0.git
   cd fliter_player0
   ```

2. **Open in Qt Creator:**
   - Launch Qt Creator
   - Open `Fliter_player0.pro`
   - Configure with your Qt kit

3. **Configure FFmpeg paths (if needed):**
   - Edit `Fliter_player0.pro` to include FFmpeg paths:
     ```qmake
     INCLUDEPATH += /path/to/ffmpeg/include
     LIBS += -L/path/to/ffmpeg/lib -lavformat -lavcodec -lavutil -lswscale
     ```

4. **Build:**
   - Select `Build` → `Build Project` or press `Ctrl+B`

### Running

```bash
./fliter_player0
```

## 🎯 Features (Planned/In Progress)

- [ ] Video playback with multiple codec support
- [ ] Playlist management
- [ ] Playback controls (play, pause, seek, volume)
- [ ] Custom UI with titlebar and control bar
- [ ] Frame-accurate seeking
- [ ] Message queue system for decoupled component communication
- [ ] Comprehensive logging system
- [ ] Cross-platform support

## 📝 Architecture Highlights

- **Message Queue System:** Asynchronous communication between components using `ffmsg_queue`
- **FFmpeg Integration:** Direct FFmpeg library bindings for high performance
- **IJK-style Architecture:** Reference implementation inspired by ijkplayer's proven design patterns
- **Thread-safe Operations:** Mutex utilities for safe multi-threaded playback

## 🔧 Key Components

### FFPlay Core (`fffplay.h`, `fffplay.cpp`)
Main interface for video playback and control.

### Message Queue (`ffmsg_queue.h`, `ffmsg_queue.cpp`)
Handles asynchronous communication between player components.

### UI Layers
- **MainWindow:** Application main window
- **CtrlBar:** Playback controls
- **TitleBar:** Window title and metadata
- **ShowWindow:** Video rendering area
- **Playlist:** Media list management

## 🐛 Known Issues & TODO

- [ ] Implement complete audio output
- [ ] Add subtitle support
- [ ] Optimize performance for large video files
- [ ] Add configuration file support
- [ ] Implement hardware acceleration (CUDA/VA-API)
- [ ] Complete error handling and recovery

## 📄 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

## 🤝 Contributing

Contributions are welcome! Feel free to fork, create feature branches, and submit pull requests.

## 📞 Contact & Support

For issues, questions, or suggestions, please open an [Issue](https://github.com/Fliter-Casual/fliter_player0/issues) on GitHub.

---

**Happy coding!** 🎬✨
