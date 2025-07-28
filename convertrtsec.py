import sys
import re
import base64
import requests
import time
import hmac
import hashlib
import json

from pathlib import Path
from urllib.parse import unquote, urlparse
from PyQt5.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QSplitter, QTextEdit, QLabel,
    QProgressDialog, QMessageBox
)
from PyQt5.QtGui import (
    QGuiApplication, QClipboard, QTextCharFormat,
    QColor, QSyntaxHighlighter,
    QImage, QPixmap
)
from PyQt5.QtCore import (
    Qt,
    QBuffer,
    QByteArray,
    QIODevice,
    QUrl
)
import dotenv
import os

env_path = Path(__file__).parent / 'api.env'
if env_path.exists():
    dotenv.load_dotenv(env_path)
    OSS_UPLOAD_URL = os.getenv('OSS_UPLOAD_URL')
    OSS_BASE_URL = os.getenv('OSS_BASE_URL')
    STS_URL = os.getenv('STS_URL')
else:
    raise RuntimeError("Environment file 'api.env' not found. Please create it with the required variables.")

class ImageMarkerHighlighter(QSyntaxHighlighter):
    def __init__(self, document):
        super().__init__(document)
        self.pattern = re.compile(r'\[Image omitted #\d+\]')
        fmt = QTextCharFormat()
        fmt.setBackground(QColor('#ffff99'))
        self.fmt = fmt

    def highlightBlock(self, text):
        for m in self.pattern.finditer(text):
            self.setFormat(m.start(), m.end() - m.start(), self.fmt)

class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Word-to-HTML/RTF Converter")
        self.full_imgs = []
        self.full_html = ""
        self._syncing = False

        splitter = QSplitter(Qt.Horizontal)

        # Left pane: raw HTML with placeholders
        left = QWidget()
        ll = QVBoxLayout(left)
        ll.setContentsMargins(0,0,0,0)
        ll.addWidget(QLabel("HTML Source (plain text):"))
        self.src = QTextEdit()
        self.src.setAcceptRichText(False)
        ll.addWidget(self.src)
        ImageMarkerHighlighter(self.src.document())

        # Right pane: rendered HTML
        right = QWidget()
        rl = QVBoxLayout(right)
        rl.setContentsMargins(0,0,0,0)
        rl.addWidget(QLabel("Rendered Preview:"))
        self.preview = QTextEdit()
        rl.addWidget(self.preview)

        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setSizes([500,500])

        # Controls
        btns = QHBoxLayout()
        self.paste_btn     = QPushButton("Paste from Word")
        self.copy_html_btn = QPushButton("Copy as HTML")
        self.copy_rtf_btn  = QPushButton("Copy as Rich Text")
        self.confirm_btn   = QPushButton("Confirm")
        btns.addWidget(self.paste_btn)
        btns.addWidget(self.copy_html_btn)
        btns.addWidget(self.copy_rtf_btn)
        btns.addWidget(self.confirm_btn)
        btns.addStretch()

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)
        layout.addLayout(btns)

        # Signals
        self.paste_btn.clicked.connect(self.paste_from_word)
        self.copy_html_btn.clicked.connect(self.copy_html)
        self.copy_rtf_btn.clicked.connect(self.copy_rtf)
        self.confirm_btn.clicked.connect(self.confirm_and_upload)
        self.src.textChanged.connect(self.sync_from_source)
        self.preview.textChanged.connect(self.sync_from_preview)
    
    def load_external_images(self, *args):
        """
        Fetch any <img src='http(s)://…'> in self.preview and register them
        so QTextEdit will render them.
        """
        doc = self.preview.document()
        html = self.preview.toHtml()
        # find every remote URL in an <img> tag
        urls = re.findall(r'<img[^>]+src="(https?://[^"]+)"', html)
        for url in set(urls):
            try:
                resp = requests.get(url, timeout=10)
                resp.raise_for_status()
                img = QImage.fromData(resp.content)
                if not img.isNull():
                    doc.addResource(doc.ImageResource, QUrl(url), img)
            except Exception as e:
                print(f"Failed to load {url}: {e}")
        # re-apply the HTML so the images show up
        self.preview.setHtml(html)

    def convert_and_number(self, html: str):
        def inline_img(m):
            pre, src, suf = m.group(1), m.group(2), m.group(3)
            if src.lower().startswith('data:'):
                return m.group(0)

            parsed = urlparse(src)
            if parsed.scheme == 'file':
                # URL-decode and strip ALL leading slashes...
                raw = unquote(parsed.path).lstrip('/')
                # But on macOS, Word sometimes emits four slashes (file:////Users/…),
                # so ensure we always end up with a single leading slash
                if sys.platform == 'darwin':
                    raw = '/' + raw.lstrip('/')
                # On Windows, drive letter may live in netloc
                if parsed.netloc and re.match(r'^[A-Za-z]:', raw):
                    raw = parsed.netloc + raw
            else:
                # Bare Windows paths or other URIs
                raw = unquote(src).replace('\\\\', '/')
            
            print(f"Processing image source: '{raw}'")

            fp = Path(raw)
            if fp.is_file():
                print(f"Found local file: {fp}")
                data = fp.read_bytes()
                b64  = base64.b64encode(data).decode('ascii')
                ext  = fp.suffix.lstrip('.').lower() or 'png'
                return f'{pre}data:image/{ext};base64,{b64}{suf}'

            return m.group(0)

        inlined = re.sub(
            r'(<img\b[^>]+src=[\'"])([^\'"]+)([\'"][^>]*>)',
            inline_img, html, flags=re.IGNORECASE
        )
        self.full_imgs = re.findall(r'<img\b[^>]*>', inlined, flags=re.IGNORECASE)

        counter = 0
        def ph(m):
            nonlocal counter
            counter += 1
            return f'\n[Image omitted #{counter}]\n'

        masked = re.sub(r'<img\b[^>]*>', ph, inlined, flags=re.IGNORECASE)
        return inlined, masked

    def paste_from_word(self):
        cb = QGuiApplication.clipboard().mimeData()
        raw = cb.html() if cb.hasHtml() else (cb.text() if cb.hasText() else "")
        if not raw:
            return
        inlined, masked = self.convert_and_number(raw)
        self.full_html = inlined
        self._syncing = True
        self.src.setPlainText(masked)
        self.preview.setHtml(inlined)
        self.load_external_images()
        self._syncing = False

    def sync_from_source(self):
        if self._syncing: return
        self._syncing = True
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
        self.load_external_images()
        self._syncing = False

    def sync_from_preview(self):
        if self._syncing: return
        self._syncing = True
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

    def confirm_and_upload(self):
        matches = re.findall(r'(data:image/[^;]+;base64,[^"\']+)', self.full_html)
        unique = list(dict.fromkeys(matches))
        total = len(unique)
        if total == 0:
            return

        progress = QProgressDialog("Uploading images…", "Cancel", 0, total, self)
        progress.setWindowModality(Qt.WindowModal)
        progress.show()

        url_map = {}
        success_count = 0

        for i, data_uri in enumerate(unique, start=1):
            if progress.wasCanceled():
                break
            header, b64data = data_uri.split(",", 1)
            binary = base64.b64decode(b64data)
            try:
                url = self.upload_to_oss(header, binary)
                url_map[data_uri] = url
                success_count += 1
            except Exception as e:
                print(f"Upload failed for image {i}: {e}")

            progress.setValue(i)
            progress.setLabelText(f"Uploading image {i}/{total} — {success_count} succeeded")
            QApplication.processEvents()

        progress.close()

        new_html = self.full_html
        for data_uri, url in url_map.items():
            new_html = new_html.replace(data_uri, url)

        self.full_html = new_html
        self._syncing = True
        self.src.setPlainText(new_html)
        self.preview.setHtml(new_html)
        self.load_external_images()
        self._syncing = False

        QMessageBox.information(
            self, "Upload Complete",
            f"Uploaded {success_count} of {total} images successfully."
        )

    def fetch_sts(self):
        """Get fresh STS creds from your backend, or raise on error."""
        resp = requests.get(STS_URL, timeout=10)
        resp.raise_for_status()
        data = resp.json().get('data')
        if not data:
            raise RuntimeError("No STS data in response")
        return data

    def upload_to_oss(self, data_uri_header: str, binary_data: bytes) -> str:
        """Try upload, refresh credentials on 403, retry once."""
        # Inner function to build form & post
        def do_upload(creds):
            # build policy + signature exactly as before...
            expire = int(time.time()) + 3600
            policy = {
                "expiration": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(expire)),
                "conditions":[["content-length-range", 0, 1024*1024*1024]]
            }
            policy_b64 = base64.b64encode(json.dumps(policy).encode()).decode()
            sig = base64.b64encode(
                hmac.new(creds['accessKeySecret'].encode(),
                         policy_b64.encode(), hashlib.sha1).digest()
            ).decode()

            mime = data_uri_header.split(";",1)[0].split(":",1)[1]
            ext  = mime.split("/",1)[1].lower()
            # if ext == "jpeg": ext = "jpg"
            key = f"pc/course/dev/{int(time.time()*1000)}.{ext}"

            form = {
                "key":                  key,
                "policy":               policy_b64,
                "OSSAccessKeyId":       creds['accessKeyId'],
                "signature":            sig,
                "x-oss-security-token": creds['securityToken'],
                "success_action_status":"200",
            }
            files = { "file": (f"image.{ext}", binary_data, mime) }
            # r = requests.post(OSS_UPLOAD_URL, data=form, files=files, timeout=30)
            r = requests.post(
                OSS_UPLOAD_URL,
                data=form,
                files=files,
                timeout=30,
                verify=False,
                allow_redirects=True
            )
            print(f"status: {r.status_code}")
            print(f"response: {r.text}")
            return r, key

        # 1) initial creds
        creds = self.fetch_sts()
        r, key = do_upload(creds)
        # 2) if 403, refresh once and retry
        if r.status_code == 403:
            creds = self.fetch_sts()
            r, key = do_upload(creds)

        # 3) final check
        if r.status_code != 200 and r.status_code != 201:
            raise RuntimeError(f"OSS upload failed [{r.status_code}]: {r.text}")

        return f"{OSS_BASE_URL}/{key}"

if __name__ == "__main__":
    app = QApplication(sys.argv)
    w = MainWindow()
    w.resize(1000, 700)
    w.show()
    sys.exit(app.exec_())