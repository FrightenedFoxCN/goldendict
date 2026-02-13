# Silverdict - A Modern Fork of GoldenDict

## About Silverdict

**Silverdict** is a modernized and refactored dictionary lookup program based on the original [GoldenDict](http://goldendict.org) project. This repository represents a complete restructuring and modernization of the codebase with focus on:

- **Modern C++17 standards** - Eliminated deprecated exception specifications, improved type safety
- **Professional project organization** - Logical folder structure with separated resources, tools, and third-party dependencies  
- **CMake build system** - Modern build configuration replacing legacy qmake
- **Qt 6 support** - Full compatibility with Qt 6 alongside Qt 5
- **Cleaner codebase** - Comprehensive warning fixes and code quality improvements

WARNING: This is still in a very primitive state, only macOS compilation is tested.

### Acknowledgements

Silverdict is built upon the excellent work of the original **GoldenDict** project. All credit for the core dictionary functionality, format support, and UI design goes to the GoldenDict developers and contributors. This fork maintains compatibility with all original dictionary formats and features while modernizing the development infrastructure.

## Features

Silverdict retains all features from GoldenDict:

- **Multiple Dictionary Formats**: StarDict, Babylon, Lingvo, Dictd, AARD, MDict, SDict, ZIM, Slob
- **Online Dictionaries**: Integration with online dictionary services
- **Perfect Article Rendering**: Complete markup, illustrations, and content preservation
- **Flexible Search**: Accent-insensitive and case-insensitive matching
- **Cross-Platform**: Windows, Linux, macOS
- **Chinese Support**: Character conversion via OpenCC
- **Customization**: Multiple UI themes and styles
- **Multimedia**: Integrated audio players (Qt Multimedia and FFmpeg backends)

## Requirements

### Basic Requirements

- **CMake**: 3.20 or later
- **C++ Compiler**: GCC 9+, Clang 10+, or MSVC 2019+
- **Qt Framework**: 6.2+ (Qt 5.15+ also supported)
- **Git**: For version control

### Ubuntu Linux

```bash
sudo apt-get install cmake git build-essential pkg-config \
    qt6-base-dev qt6-tools-dev \
    libvorbis-dev zlib1g-dev libhunspell-dev \
    libqt6core5compat6-dev libqt6webenginewidgets6 \
    libbz2-dev liblzo2-dev libzstd-dev liblzma-dev \
    libtiff-dev libao-dev libavutil-dev libavformat-dev libavcodec-dev libswresample-dev
```

### macOS (Homebrew)

```bash
brew install cmake qt6 vorbis hunspell lzo zstd libtiff libao ffmpeg opencc
```

### Windows

All dependencies are included in `third-party/windows/` or can be installed via vcpkg.

## Building

### Quick Start

```bash
# Clone repository
git clone https://github.com/yourusername/silverdict.git
cd silverdict

# Create and configure build directory
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . -j$(nproc)  # Linux/macOS
cmake --build . -j%NUMBER_OF_PROCESSORS%  # Windows
```

### Build Options

Configure optional features during CMake setup:

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DMAKE_QTMULTIMEDIA_PLAYER=ON        # Qt Multimedia audio (default: ON)
    -DMAKE_FFMPEG_PLAYER=ON              # FFmpeg audio support (default: ON) 
    -DMAKE_ZIM_SUPPORT=ON                # ZIM/Slob formats (default: ON)
    -DMAKE_EXTRA_TIFF_HANDLER=ON         # Extra TIFF support (default: ON)
    -DMAKE_CHINESE_CONVERSION_SUPPORT=ON # Chinese conversion (default: ON)
```

### Running the Application

After building, the executable is located at:

- **macOS**: `build/GoldenDict.app/Contents/MacOS/GoldenDict`
- **Linux**: `build/GoldenDict` 
- **Windows**: `build\GoldenDict.exe`

## Project Structure

The reorganized structure improves maintainability and scalability:

```
silverdict/
├── CMakeLists.txt                 # Main build configuration
├── goldendict.rc                  # Windows resource file
├── README.md, LICENSE.txt          # Documentation and license
│
├── src/                           # Source code
│   ├── core/                      # Core application logic
│   ├── dictionary/                # Dictionary management
│   ├── formats/                   # Dictionary format handlers
│   ├── ui/                        # User interface
│   ├── audio/                     # Audio/sound handling
│   ├── text/                      # Text processing
│   ├── network/                   # Online dictionaries
│   ├── util/                      # Utility functions
│   ├── platform/                  # Platform-specific code
│   └── about/                     # About dialog
│
├── resources/                     # All application resources
│   ├── styles/                    # CSS stylesheets (article, UI themes)
│   ├── icons/                     # UI icons and graphics
│   ├── locale/                    # Translation files (.ts source)
│   ├── help/                      # Qt Help documentation
│   ├── flags/                     # Country flag icons
│   └── macos/                     # macOS-specific resources
│
├── third-party/                   # External dependencies
│   ├── qtsingleapplication/       # Qt Single Application library
│   ├── opencc/                    # Chinese conversion library
│   ├── macos/                     # Pre-built macOS dependencies
│   └── windows/                   # Windows libraries
│
├── packaging/                     # Distribution and installation
│   ├── windows/                   # NSIS installer scripts
│   ├── linux/                     # Desktop and AppStream files
│   └── macos/                     # macOS packaging config
│
├── tools/                         # Build utilities and scripts
│   ├── generators/                # Code and data generators
│   └── scripts/                   # Build helpers
│
├── docs/                          # Project documentation
├── .ci/                           # CI/CD configuration
└── .git/                          # Git repository metadata
```

## Installation

Installation is optional—the built binary can run standalone.

### Linux/macOS

```bash
cd build
make install  # Install to /usr/local by default
```

### Windows

Do not use `make install` on Windows. Either:
1. Run `GoldenDict.exe` directly from the build folder
2. Use the NSIS installer from `packaging/windows/`

## Code Quality

The project enforces strict compiler standards:

```bash
# Build with pedantic warnings
cmake .. -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wdeprecated -Wpedantic"
cmake --build .
```

### Development Standards

- **C++17 required** - No exceptions, use modern language features
- **No deprecated patterns** - Dynamic exception specs, old-style casts removed
- **Compiler warnings** - Pedantic build flags must not produce warnings
- **Consistent style** - Match existing code conventions

## Contributing

Contributions are welcome! When contributing:

1. Follow C++17 standards and avoid deprecated features
2. Build with pedantic compiler flags: `-Wall -Wextra -Wdeprecated -Wpedantic`
3. Test on multiple platforms if possible
4. Submit clear pull requests with detailed descriptions
5. Reference related issues in commit messages

## License

This project is licensed under **GNU GPLv3+**. See `LICENSE.txt` for the full license text.

This fork maintains the original license to honor the GoldenDict project's contributions.

## Support and Issues

### For Silverdict

- **Bug Reports & Features**: This repository's [issue tracker](../../issues)
- **Pull Requests**: Contributions welcome via [pull requests](../../pulls)

### For GoldenDict

- **Official Repository**: https://github.com/goldendict/goldendict
- **Issue Tracker**: https://github.com/goldendict/goldendict/issues  
- **Forum**: http://goldendict.org/forum/
- **Website**: http://goldendict.org/

## Credits

- **Original GoldenDict**: Igor Ivoylev and contributors
- **Silverdict Fork**: Modern refactoring and CMake migration
- **Qt Foundation**: Qt framework
- **Open Source Community**: All third-party libraries and components

## Related Links

- **GoldenDict Project**: http://goldendict.org
- **Official Repository**: https://github.com/goldendict/goldendict
- **Qt Project**: https://www.qt.io/
