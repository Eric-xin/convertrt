#!/bin/bash
# macOS deployment script for convertrt
# Usage: ./deploy-macos.sh [build_dir] [qt_dir]

set -e

BUILD_DIR="${1:-build}"
QT_DIR="${2}"

echo "=== macOS Deployment Script for convertrt ==="

# Try to find Qt installation
if [ -z "$QT_DIR" ]; then
    # Try common Qt installation paths
    QT_PATHS=(
        "/opt/homebrew/opt/qt@6"
        "/usr/local/opt/qt@6"
        "/usr/local/Qt"
        "$HOME/Qt"
        "/Applications/Qt"
    )
    
    for path in "${QT_PATHS[@]}"; do
        if [ -d "$path" ]; then
            # Find the latest version
            QT_DIR=$(find "$path" -name "clang_64" -type d | head -1 | xargs dirname 2>/dev/null || echo "")
            if [ -n "$QT_DIR" ]; then
                QT_DIR="$QT_DIR/clang_64"
                break
            fi
        fi
    done
    
    if [ -z "$QT_DIR" ]; then
        echo "Error: Qt installation not found. Please specify Qt directory as second argument."
        echo "Usage: ./deploy-macos.sh [build_dir] [qt_dir]"
        echo "Example: ./deploy-macos.sh build /opt/homebrew/opt/qt@6/clang_64"
        exit 1
    fi
fi

echo "Using Qt from: $QT_DIR"

# Check if executable exists
EXECUTABLE_PATH=""
if [ -f "$BUILD_DIR/convertrt" ]; then
    EXECUTABLE_PATH="$BUILD_DIR/convertrt"
elif [ -f "$BUILD_DIR/bin/convertrt" ]; then
    EXECUTABLE_PATH="$BUILD_DIR/bin/convertrt"
elif [ -f "$BUILD_DIR/convertrt.app/Contents/MacOS/convertrt" ]; then
    echo "Found existing app bundle, will recreate it..."
    EXECUTABLE_PATH="$BUILD_DIR/convertrt.app/Contents/MacOS/convertrt"
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
mkdir -p deploy

# Create app bundle structure
APP_NAME="ConvertRT.app"
APP_PATH="deploy/$APP_NAME"
mkdir -p "$APP_PATH/Contents/MacOS"
mkdir -p "$APP_PATH/Contents/Resources"
mkdir -p "$APP_PATH/Contents/Frameworks"

# Copy the executable
cp "$EXECUTABLE_PATH" "$APP_PATH/Contents/MacOS/ConvertRT"
chmod +x "$APP_PATH/Contents/MacOS/ConvertRT"
echo "✓ Copied executable to app bundle"

# Copy config file if it exists
if [ -f "config.ini" ]; then
    cp config.ini "$APP_PATH/Contents/Resources/"
    echo "✓ Copied config.ini"
fi

# Create Info.plist
cat > "$APP_PATH/Contents/Info.plist" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>ConvertRT</string>
    <key>CFBundleIdentifier</key>
    <string>com.convertrt.app</string>
    <key>CFBundleName</key>
    <string>ConvertRT</string>
    <key>CFBundleDisplayName</key>
    <string>ConvertRT</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.15</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSRequiresAquaSystemAppearance</key>
    <false/>
    <key>NSAppTransportSecurity</key>
    <dict>
        <key>NSAllowsArbitraryLoads</key>
        <true/>
    </dict>
</dict>
</plist>
EOF

echo "✓ Created Info.plist"

# Use macdeployqt to bundle Qt frameworks
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
if [ ! -f "$MACDEPLOYQT" ]; then
    echo "Error: macdeployqt not found at $MACDEPLOYQT"
    echo "Make sure Qt is properly installed with development tools"
    exit 1
fi

echo "Running macdeployqt..."
"$MACDEPLOYQT" "$APP_PATH" -verbose=2

if [ $? -eq 0 ]; then
    echo "✓ Successfully deployed Qt frameworks"
else
    echo "⚠ macdeployqt completed with warnings, but continuing..."
fi

# Fix any code signing issues for local testing
echo "Fixing code signing for local testing..."
find "$APP_PATH" -name "*.dylib" -exec codesign --force --sign - {} \; 2>/dev/null || true
codesign --force --sign - "$APP_PATH/Contents/MacOS/ConvertRT" 2>/dev/null || true

# Create DMG directory structure
mkdir -p deploy/dmg
cp -R "$APP_PATH" deploy/dmg/

# Create a README for the DMG
cat > deploy/dmg/README.txt << 'EOF'
ConvertRT - Word to HTML/RTF Converter for macOS

INSTALLATION:
1. Drag ConvertRT.app to your Applications folder
2. Double-click to run
3. If macOS shows security warning, go to System Preferences > Security & Privacy and click "Open Anyway"

CONFIGURATION:
The configuration file is located inside the app bundle:
- Right-click ConvertRT.app > Show Package Contents
- Navigate to Contents/Resources/config.ini
- Edit with any text editor

USAGE:
1. Copy rich text from Microsoft Word
2. Paste into ConvertRT
3. Use "Confirm" to upload images to cloud storage
4. Copy the result as HTML or Rich Text

SYSTEM REQUIREMENTS:
- macOS 10.15 (Catalina) or later
- Intel or Apple Silicon Mac
- Internet connection for image uploads

TROUBLESHOOTING:
- If app won't open: Check System Preferences > Security & Privacy
- If images won't upload: Check config.ini settings
- For support: Check the project repository
EOF

# Create a symbolic link to Applications folder (for DMG)
ln -sf /Applications deploy/dmg/Applications

echo "✓ Created DMG-ready structure"

# Create installation script
cat > deploy/install.sh << 'EOF'
#!/bin/bash
echo "ConvertRT macOS Installation"
echo "============================"

# Check if we're running on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "Error: This installer is for macOS only"
    exit 1
fi

# Copy to Applications
if [ -w "/Applications" ]; then
    echo "Installing to /Applications..."
    cp -R "ConvertRT.app" "/Applications/"
    echo "✓ ConvertRT installed to /Applications"
else
    echo "Installing to ~/Applications..."
    mkdir -p "$HOME/Applications"
    cp -R "ConvertRT.app" "$HOME/Applications/"
    echo "✓ ConvertRT installed to ~/Applications"
fi

echo ""
echo "Installation complete!"
echo "You can now:"
echo "1. Find ConvertRT in your Applications folder"
echo "2. Add it to your Dock by dragging from Applications"
echo "3. Run it by double-clicking"
EOF

chmod +x deploy/install.sh
echo "✓ Created installation script"

# Check bundle validity
echo "Verifying app bundle..."
if [ -f "$APP_PATH/Contents/MacOS/ConvertRT" ] && [ -f "$APP_PATH/Contents/Info.plist" ]; then
    echo "✓ App bundle structure is valid"
    
    # Try to get bundle info
    BUNDLE_ID=$(defaults read "$(pwd)/$APP_PATH/Contents/Info.plist" CFBundleIdentifier 2>/dev/null || echo "unknown")
    BUNDLE_VERSION=$(defaults read "$(pwd)/$APP_PATH/Contents/Info.plist" CFBundleVersion 2>/dev/null || echo "unknown")
    echo "  Bundle ID: $BUNDLE_ID"
    echo "  Version: $BUNDLE_VERSION"
else
    echo "⚠ App bundle may be incomplete"
fi

# Show framework dependencies
echo ""
echo "Qt frameworks included:"
find "$APP_PATH/Contents/Frameworks" -name "*.framework" -exec basename {} .framework \; 2>/dev/null | sort || echo "None found"

echo ""
echo "=== Deployment Complete! ==="
echo "Created: $APP_PATH"
echo ""
echo "Distribution options:"
echo "1. DMG-ready folder: deploy/dmg/"
echo "2. Standalone app: deploy/$APP_NAME"
echo "3. Installation script: deploy/install.sh"
echo ""
echo "To create a DMG (requires additional tools):"
echo "  hdiutil create -srcfolder deploy/dmg -volname ConvertRT ConvertRT.dmg"
echo ""
echo "To test the app:"
echo "  open '$APP_PATH'"

# Optional: Create DMG if hdiutil is available
if command -v hdiutil >/dev/null 2>&1; then
    read -p "Create DMG file? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        DMG_NAME="ConvertRT-$(date +%Y%m%d-%H%M).dmg"
        hdiutil create -srcfolder deploy/dmg -volname "ConvertRT" "$DMG_NAME"
        echo "✓ Created $DMG_NAME"
    fi
fi
