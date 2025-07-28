import sys, re, base64
from pathlib import Path
from urllib.parse import unquote, urlparse
from PyQt5.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QSplitter, QTextEdit, QLabel
)
from PyQt5.QtGui import (
    QGuiApplication, QClipboard, QTextCharFormat,
    QColor, QSyntaxHighlighter
)
from PyQt5.QtCore import Qt

class ImageMarkerHighlighter(QSyntaxHighlighter):
    def __init__(self, document):
        super().__init__(document)
        # match [Image omitted #n]
        self.pattern = re.compile(r'\[Image omitted #\d+\]')
        fmt = QTextCharFormat()
        fmt.setBackground(QColor('#ffff99'))
        self.highlight_fmt = fmt

    def highlightBlock(self, text):
        for m in self.pattern.finditer(text):
            start, length = m.start(), m.end() - m.start()
            self.setFormat(start, length, self.highlight_fmt)

class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Word-to-HTML/RTF Converter")
        self.full_imgs = []    # stores the inlined <img> tags in order
        self.full_html = ""    
        self._syncing = False

        splitter = QSplitter(Qt.Horizontal)

        # ─── Left pane: editable plain-text HTML with placeholders ───
        left = QWidget()
        ll = QVBoxLayout(left); ll.setContentsMargins(0,0,0,0)
        ll.addWidget(QLabel("HTML Source (plain text):"))
        self.src = QTextEdit()
        self.src.setAcceptRichText(False)         # disable HTML rendering
        ll.addWidget(self.src)
        # attach highlighter to the QTextDocument
        ImageMarkerHighlighter(self.src.document())

        # ─── Right pane: editable rendered HTML ───
        right = QWidget()
        rl = QVBoxLayout(right); rl.setContentsMargins(0,0,0,0)
        rl.addWidget(QLabel("Rendered Preview:"))
        self.preview = QTextEdit()
        rl.addWidget(self.preview)

        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setSizes([500,500])

        # ─── Controls ───
        btns = QHBoxLayout()
        self.paste_btn     = QPushButton("Paste from Word")
        self.copy_html_btn = QPushButton("Copy as HTML")
        self.copy_rtf_btn  = QPushButton("Copy as Rich Text")
        btns.addWidget(self.paste_btn)
        btns.addWidget(self.copy_html_btn)
        btns.addWidget(self.copy_rtf_btn)
        btns.addStretch()

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)
        layout.addLayout(btns)

        # ─── Signals ───
        self.paste_btn.clicked.connect(self.paste_from_word)
        self.copy_html_btn.clicked.connect(self.copy_html)
        self.copy_rtf_btn.clicked.connect(self.copy_rtf)
        self.src.textChanged.connect(self.sync_from_source)
        self.preview.textChanged.connect(self.sync_from_preview)

    def convert_and_number(self, html: str):
        # 1) inline file:// images → data:image
        def inline_img(m):
            pre, src, suf = m.group(1), m.group(2), m.group(3)
            if src.lower().startswith('data:'):
                return m.group(0)

            # 1) URL-style file:///
            parsed = urlparse(src)
            if parsed.scheme == 'file':
                # parsed.path might be '/C:/…' on Windows; strip leading '/'
                raw = unquote(parsed.path)
                if raw.startswith('/') and raw[2] == ':':
                    raw = raw.lstrip('/')
                # handle file://C:/… too
                if parsed.netloc and raw[1] == ':':
                    raw = parsed.netloc + raw
            else:
                # 2) Unescaped Windows path “C:\…” or “C:/…” or URL-encoded in href
                raw = unquote(src)
                # If it looks like a Windows path without file://
                if re.match(r'^[A-Za-z]:[\\/]', raw):
                    # normalize backslashes
                    raw = raw.replace('\\', '/')
            
            # 3) Now raw should be something like 'C:/Users/...'
            fp = Path(raw)
            if fp.is_file():
                data = fp.read_bytes()
                b64 = base64.b64encode(data).decode('ascii')
                ext = fp.suffix.lstrip('.').lower() or 'png'
                return f'{pre}data:image/{ext};base64,{b64}{suf}'
            
            # fallback: leave original tag untouched
            return m.group(0)

        inlined = re.sub(
            r'(<img\b[^>]+src=[\'"])([^\'"]+)([\'"][^>]*>)',
            inline_img, html, flags=re.IGNORECASE
        )

        # 2) extract and store the full <img> tags
        self.full_imgs = re.findall(r'<img\b[^>]*>', inlined, flags=re.IGNORECASE)

        # 3) replace each <img> with numbered placeholder on own lines
        def ph(m):
            idx = ph.counter; ph.counter += 1
            return f'\n[Image omitted #{idx}]\n'
        ph.counter = 1

        masked = re.sub(r'<img\b[^>]*>', ph, inlined, flags=re.IGNORECASE)
        return inlined, masked

    def paste_from_word(self):
        cb = QGuiApplication.clipboard().mimeData()
        raw = cb.html() if cb.hasHtml() else (cb.text() if cb.hasText() else "")
        if not raw: return
        inlined, masked = self.convert_and_number(raw)
        self.full_html = inlined

        self._syncing = True
        self.src.setPlainText(masked)
        self.preview.setHtml(inlined)
        self._syncing = False

    def sync_from_source(self):
        if self._syncing: return
        self._syncing = True

        # reconstruct preview by swapping placeholders back to <img> tags
        parts = re.split(r'(\[Image omitted #\d+\])', self.src.toPlainText())
        out = []
        for part in parts:
            m = re.match(r'\[Image omitted #(\d+)\]', part)
            if m:
                idx = int(m.group(1)) - 1
                out.append(self.full_imgs[idx] if 0 <= idx < len(self.full_imgs) else '')
            else:
                out.append(part)
        self.preview.setHtml("".join(out))

        self._syncing = False

    def sync_from_preview(self):
        if self._syncing: return
        self._syncing = True

        # when preview edited, re-mask
        new_full = self.preview.toHtml()
        inlined, masked = self.convert_and_number(new_full)
        self.full_html = inlined
        self.src.setPlainText(masked)

        self._syncing = False

    def copy_html(self):
        QGuiApplication.clipboard().setText(self.full_html, QClipboard.Clipboard)

    def copy_rtf(self):
        tmp = QTextEdit()
        tmp.setHtml(self.full_html)
        tmp.selectAll()
        tmp.copy()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    w = MainWindow()
    w.resize(1000,700)
    w.show()
    sys.exit(app.exec_())