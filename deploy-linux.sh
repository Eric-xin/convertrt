#!/bin/bash
# Linux deployment script for convertrt
# Usage: ./deploy-linux.sh [build_dir] [qt_dir]

set -e

BUILD_DIR="${1:-build}"
QT_DIR="${2}"

echo "=== Linux Deployment Script for convertrt ==="

# Check if executable exists
EXECUTABLE_PATH=""
if [ -f "$BUILD_DIR/convertrt" ]; then
    EXECUTABLE_PATH="$BUILD_DIR/convertrt"
elif [ -f "$BUILD_DIR/bin/convertrt" ]; then
    EXECUTABLE_PATH="$BUILD_DIR/bin/convertrt"
else
    echo "Error: convertrt executable not found in $BUILD_DIR"
    echo "Make sure to build the project first with: cmake --build build --config Release"
    exit 1
fi

echo "Using executable from: $EXECUTABLE_PATH"

# Create deployment directory
if [ -d "deploy" ]; then
    rm -rf deploy
fi
mkdir -p deploy/convertrt

# Copy the executable
cp "$EXECUTABLE_PATH" deploy/convertrt/
echo "✓ Copied convertrt executable"

# Copy config file if it exists
if [ -f "config.ini" ]; then
    cp config.ini deploy/convertrt/
    echo "✓ Copied config.ini"
fi

# Check if we can find Qt libraries
QT_LIBS_FOUND=false
if [ -n "$QT_DIR" ] && [ -d "$QT_DIR/lib" ]; then
    echo "Using Qt from: $QT_DIR"
    QT_LIBS_FOUND=true
elif [ -n "$Qt6_DIR" ] && [ -d "$Qt6_DIR/../.." ]; then
    QT_DIR="$(realpath "$Qt6_DIR/../..")"
    echo "Found Qt from Qt6_DIR: $QT_DIR"
    QT_LIBS_FOUND=true
elif command -v qmake6 >/dev/null 2>&1; then
    QT_DIR="$(qmake6 -query QT_INSTALL_PREFIX)"
    echo "Found Qt from qmake6: $QT_DIR"
    QT_LIBS_FOUND=true
elif command -v qmake >/dev/null 2>&1; then
    QT_DIR="$(qmake -query QT_INSTALL_PREFIX)"
    echo "Found Qt from qmake: $QT_DIR"
    QT_LIBS_FOUND=true
fi

# Copy Qt libraries if found
if [ "$QT_LIBS_FOUND" = true ] && [ -d "$QT_DIR/lib" ]; then
    echo "Copying essential Qt libraries..."
    
    # List of essential Qt libraries
    QT_LIBS=(
        "libQt6Core.so.6"
        "libQt6Gui.so.6"
        "libQt6Widgets.so.6"
        "libQt6Network.so.6"
        "libQt6DBus.so.6"
        "libQt6XcbQpa.so.6"
    )
    
    for lib in "${QT_LIBS[@]}"; do
        if [ -f "$QT_DIR/lib/$lib" ]; then
            cp "$QT_DIR/lib/$lib" deploy/convertrt/
            echo "✓ Copied $lib"
        fi
    done
    
    # Copy Qt plugins
    if [ -d "$QT_DIR/plugins" ]; then
        mkdir -p deploy/convertrt/platforms
        mkdir -p deploy/convertrt/imageformats
        
        # Platform plugins
        if [ -f "$QT_DIR/plugins/platforms/libqxcb.so" ]; then
            cp "$QT_DIR/plugins/platforms/libqxcb.so" deploy/convertrt/platforms/
            echo "✓ Copied platform plugin: libqxcb.so"
        fi
        
        if [ -f "$QT_DIR/plugins/platforms/libqwayland-egl.so" ]; then
            cp "$QT_DIR/plugins/platforms/libqwayland-egl.so" deploy/convertrt/platforms/
            echo "✓ Copied platform plugin: libqwayland-egl.so"
        fi
        
        # Image format plugins
        for plugin in "$QT_DIR/plugins/imageformats/"*.so; do
            if [ -f "$plugin" ]; then
                cp "$plugin" deploy/convertrt/imageformats/
                echo "✓ Copied image format plugin: $(basename "$plugin")"
            fi
        done
    fi
else
    echo "⚠ Qt libraries not found or not copying - target system must have Qt6 installed"
fi

# Create launcher script
cat > deploy/convertrt/run.sh << 'EOF'
#!/bin/bash
# Launcher script for convertrt

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Set up library path to include local Qt libraries
export LD_LIBRARY_PATH=".:$LD_LIBRARY_PATH"

# Set Qt plugin path if we have local plugins
if [ -d "platforms" ] || [ -d "imageformats" ]; then
    export QT_PLUGIN_PATH=".:$QT_PLUGIN_PATH"
fi

# Set Qt platform if needed
if [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
    echo "Warning: No display detected. Make sure you're running in a GUI environment."
fi

# Run the application
exec ./convertrt "$@"
EOF

chmod +x deploy/convertrt/run.sh
echo "✓ Created launcher script"

# Create AppDir structure for potential AppImage creation
mkdir -p deploy/convertrt.AppDir
cp deploy/convertrt/convertrt deploy/convertrt.AppDir/
if [ -f deploy/convertrt/config.ini ]; then
    cp deploy/convertrt/config.ini deploy/convertrt.AppDir/
fi

# Create .desktop file
cat > deploy/convertrt.AppDir/convertrt.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=ConvertRT
Comment=Word to HTML/RTF Converter
Exec=convertrt
Icon=convertrt
Categories=Office;WordProcessor;
EOF

# Create a simple icon (you should replace this with a real icon)
cat > deploy/convertrt.AppDir/convertrt.svg << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<svg width="64" height="64" xmlns="http://www.w3.org/2000/svg">
  <rect width="64" height="64" fill="#4a90e2"/>
  <text x="32" y="40" font-family="Arial" font-size="24" fill="white" text-anchor="middle">RT</text>
</svg>
EOF

# Create AppRun script
cat > deploy/convertrt.AppDir/AppRun << 'EOF'
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}:${PATH}"
export LD_LIBRARY_PATH="${HERE}:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}:${QT_PLUGIN_PATH}"
cd "${HERE}"
exec ./convertrt "$@"
EOF

chmod +x deploy/convertrt.AppDir/AppRun
echo "✓ Created AppDir structure"

# Create installation script
cat > deploy/install.sh << 'EOF'
#!/bin/bash
echo "ConvertRT Installation Script"
echo "=============================="

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    INSTALL_DIR="/opt/convertrt"
    BIN_DIR="/usr/local/bin"
    DESKTOP_DIR="/usr/share/applications"
    echo "Installing system-wide to $INSTALL_DIR"
else
    INSTALL_DIR="$HOME/.local/share/convertrt"
    BIN_DIR="$HOME/.local/bin"
    DESKTOP_DIR="$HOME/.local/share/applications"
    echo "Installing to user directory $INSTALL_DIR"
fi

# Create directories
mkdir -p "$INSTALL_DIR"
mkdir -p "$BIN_DIR"
mkdir -p "$DESKTOP_DIR"

# Copy files
cp -r convertrt/* "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/convertrt"
chmod +x "$INSTALL_DIR/run.sh"

# Create symlink
ln -sf "$INSTALL_DIR/run.sh" "$BIN_DIR/convertrt"

# Create desktop entry
cat > "$DESKTOP_DIR/convertrt.desktop" << DESKTOP_EOF
[Desktop Entry]
Type=Application
Name=ConvertRT
Comment=Word to HTML/RTF Converter
Exec=$INSTALL_DIR/run.sh
Icon=$INSTALL_DIR/convertrt.svg
Categories=Office;WordProcessor;
Terminal=false
DESKTOP_EOF

echo "✓ Installation completed!"
echo ""
echo "You can now run ConvertRT by:"
echo "1. Command line: convertrt"
echo "2. Applications menu: ConvertRT"
echo "3. Directly: $INSTALL_DIR/run.sh"
EOF

chmod +x deploy/install.sh
echo "✓ Created installation script"

# Create README
cat > deploy/README.md << 'EOF'
# ConvertRT - Linux Distribution

## Quick Start

### Option 1: Portable Usage
```bash
cd convertrt
./run.sh
```

### Option 2: System Installation
```bash
# For system-wide installation (requires sudo)
sudo ./install.sh

# For user installation
./install.sh
```

### Option 3: Direct Execution
```bash
cd convertrt
./convertrt
```

## Requirements

### System Requirements
- Linux (Ubuntu 20.04+ or equivalent)
- X11 or Wayland display server
- 4 GB RAM (recommended)
- 100 MB disk space

### Dependencies
If you don't have Qt6 bundled in this package:
```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-tools-dev libqt6network6

# Fedora
sudo dnf install qt6-qtbase-devel qt6-qttools-devel

# Arch Linux
sudo pacman -S qt6-base qt6-tools
```

## Configuration

Edit `config.ini` to set up your OSS endpoints:
```ini
[oss]
sts_url=https://your-backend/getSts
oss_upload_url=https://bucket.oss-region.aliyuncs.com
oss_base_url=https://bucket.oss-region.aliyuncs.com
```

## Troubleshooting

### Application won't start
1. Check you're in a GUI environment: `echo $DISPLAY`
2. Install Qt6: See dependencies above
3. Run with debug: `QT_DEBUG_PLUGINS=1 ./convertrt`

### Images won't upload
1. Check `config.ini` settings
2. Verify network connectivity
3. Check console output for error messages

### Missing libraries
```bash
# Check missing dependencies
ldd ./convertrt

# Install missing Qt libraries
sudo apt install qt6-base-dev
```

## Building from Source

```bash
# Install build dependencies
sudo apt install cmake build-essential qt6-base-dev qt6-tools-dev

# Build
cmake -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build

# Deploy
./deploy-linux.sh
```
EOF

echo ""
echo "=== Deployment Complete! ==="
echo "Ready-to-use application is in the 'deploy' folder."
echo ""
echo "Distribution options:"
echo "1. Portable: Distribute the 'deploy/convertrt' folder"
echo "2. Installer: Run 'deploy/install.sh' on target system"
echo "3. AppImage: Use 'deploy/convertrt.AppDir' with appimagetool"
echo ""
echo "Package contents:"
find deploy -type f | sort

# Optional: Create a tarball
read -p "Create a tarball for distribution? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    TARNAME="convertrt-linux-$(date +%Y%m%d-%H%M).tar.gz"
    tar -czf "$TARNAME" -C deploy .
    echo "✓ Created $TARNAME"
fi
