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
