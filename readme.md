# Convertrt: Word-to-HTML/RTF Converter

<!-- badge -->
![PyQt5](https://img.shields.io/badge/PyQt5-5.15.4-blue.svg)
![Qt6](https://img.shields.io/badge/Qt-6.2.4-blue.svg)
![CMake](https://img.shields.io/badge/CMake-3.16.3-blue.svg)

A cross-platform desktop application for converting rich text (with images) from Word or other sources into clean, inlined HTML or RTF.

## Features

- **Paste rich text** from Word (or any app), including embedded images.
- **Inline local `file://` images** as Base64 data URLs—no broken links.
- **Mask images** in the source view with numbered `[Image omitted #n]` placeholders (highlighted in yellow).
- **Two-way editing** and real-time sync between raw HTML and rendered preview.
- **Copy fully inlined HTML or RTF** to the clipboard.
- **Syntax highlighting** for `[Image omitted #n]` placeholders in the plain-text editor.
- **Convenient buttons**: Paste, Copy as HTML, Copy as Rich Text.

## Implementation Stacks

- **C++**: Qt6 + CMake
- **Python**: PyQt5

---

## C++ Implementation (Qt6 + CMake)

### Prerequisites

- Qt6 (Widgets module)
- CMake (≥3.16)
- C++17 compiler (gcc, clang, MSVC)

### Directory Structure

```
convertrt/
├── CMakeLists.txt       # Build script
├── src/
│   └── main.cpp         # Application code
└── .gitignore
```

### Build & Run

```sh
# 1. Clone
git clone https://github.com/Eric-xin/convertrt
cd convertrt

# 2. Configure
mkdir build && cd build
cmake ..

# 3. Compile
cmake --build .

# 4. Launch
./convertrt
```

---

## Python Implementation (PyQt5)

### Prerequisites

- Python 3.7+
- PyQt5

```sh
pip install PyQt5
```

### Directory Structure

```
convertrt/
├── convertrt.py    # Standalone Python GUI script
└── requirements.txt     # dependencies
```

### Run

```sh
python word_paste_gui.py
```

---

## Usage (Both Versions)

1. **Launch** the application (C++ or Python).
2. **Paste from Word** to import clipboard HTML/text.
3. **Left pane**: Raw HTML with `[Image omitted #n]` placeholders (highlighted in yellow).
4. **Right pane**: Rendered HTML with embedded (Base64 inlined) images.
5. **Edit either pane**; changes sync automatically.
6. **Copy as HTML** or **Copy as Rich Text** to copy the full inlined content to your clipboard.
