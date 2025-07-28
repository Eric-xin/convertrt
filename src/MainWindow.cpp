#include "MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QByteArray>
#include <QIODevice>
#include <QBuffer>

// Highlighter for “[Image omitted #n]”
class ImageMarkerHighlighter : public QSyntaxHighlighter {
public:
    ImageMarkerHighlighter(QTextDocument *doc)
        : QSyntaxHighlighter(doc),
          pattern_(R"(\[Image omitted #\d+\])")
    {
        fmt_.setBackground(Qt::yellow);
    }

protected:
    void highlightBlock(const QString &text) override {
        QRegularExpression re(pattern_);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), fmt_);
        }
    }

private:
    QRegularExpression pattern_;
    QTextCharFormat fmt_;
};

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Left pane
    auto *leftW = new QWidget;
    auto *leftL = new QVBoxLayout(leftW);
    leftL->setContentsMargins(0,0,0,0);
    leftL->addWidget(new QLabel("HTML Source (plain text):"));
    srcEdit_ = new QPlainTextEdit;
    leftL->addWidget(srcEdit_);
    new ImageMarkerHighlighter(srcEdit_->document());

    // Right pane
    auto *rightW = new QWidget;
    auto *rightL = new QVBoxLayout(rightW);
    rightL->setContentsMargins(0,0,0,0);
    rightL->addWidget(new QLabel("Rendered Preview:"));
    previewEdit_ = new QTextEdit;
    rightL->addWidget(previewEdit_);

    splitter->addWidget(leftW);
    splitter->addWidget(rightW);
    splitter->setStretchFactor(0,1);
    splitter->setStretchFactor(1,1);

    // Buttons
    auto *btnL = new QHBoxLayout;
    auto *pasteBtn = new QPushButton("Paste from Word");
    auto *copyHtmlBtn = new QPushButton("Copy as HTML");
    auto *copyRtfBtn  = new QPushButton("Copy as Rich Text");
    btnL->addWidget(pasteBtn);
    btnL->addWidget(copyHtmlBtn);
    btnL->addWidget(copyRtfBtn);
    btnL->addStretch();

    auto *mainL = new QVBoxLayout(this);
    mainL->addWidget(splitter);
    mainL->addLayout(btnL);

    connect(pasteBtn,    &QPushButton::clicked, this, &MainWindow::pasteFromWord);
    connect(copyHtmlBtn, &QPushButton::clicked, this, &MainWindow::copyHtml);
    connect(copyRtfBtn,  &QPushButton::clicked, this, &MainWindow::copyRtf);
    connect(srcEdit_,    &QPlainTextEdit::textChanged, this, &MainWindow::syncFromSource);
    connect(previewEdit_,&QTextEdit::textChanged,      this, &MainWindow::syncFromPreview);
}

void MainWindow::pasteFromWord() {
    const QMimeData *md = QApplication::clipboard()->mimeData();
    QString raw = md->hasHtml() ? md->html() : (md->hasText() ? md->text() : QString());
    if (raw.isEmpty()) return;

    fullHtml_ = inlineAndNumber(raw);
    srcEdit_->blockSignals(true);
    srcEdit_->setPlainText(maskedHtml_);
    srcEdit_->blockSignals(false);

    previewEdit_->blockSignals(true);
    previewEdit_->setHtml(fullHtml_);
    previewEdit_->blockSignals(false);
    syncing_ = false;
}

void MainWindow::syncFromSource() {
    if (syncing_) return;
    syncing_ = true;

    QString text = srcEdit_->toPlainText();
    QRegularExpression re(R"(\[Image omitted #(\d+)\])");
    auto it = re.globalMatch(text);
    QString out;
    int lastPos = 0;
    while (it.hasNext()) {
        auto m = it.next();
        out += text.mid(lastPos, m.capturedStart() - lastPos);
        int idx = m.captured(1).toInt() - 1;
        if (idx >= 0 && idx < imgTags_.size())
            out += imgTags_[idx];
        lastPos = m.capturedEnd();
    }
    out += text.mid(lastPos);

    previewEdit_->blockSignals(true);
    previewEdit_->setHtml(out);
    previewEdit_->blockSignals(false);

    syncing_ = false;
}

void MainWindow::syncFromPreview() {
    if (syncing_) return;
    syncing_ = true;
    QString raw = previewEdit_->toHtml();
    fullHtml_ = inlineAndNumber(raw);
    srcEdit_->blockSignals(true);
    srcEdit_->setPlainText(maskedHtml_);
    srcEdit_->blockSignals(false);
    syncing_ = false;
}

void MainWindow::copyHtml() {
    QApplication::clipboard()->setText(fullHtml_, QClipboard::Clipboard);
}

void MainWindow::copyRtf() {
    QTextEdit temp;
    temp.setHtml(fullHtml_);
    temp.selectAll();
    temp.copy();
}

QString MainWindow::makeMasked(const QString &inlined) {
    QString masked;
    int lastPos = 0;
    int counter = 0;
    QRegularExpression reImg(R"(<img\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
    auto it = reImg.globalMatch(inlined);
    while (it.hasNext()) {
        auto m = it.next();
        masked += inlined.mid(lastPos, m.capturedStart() - lastPos);
        counter++;
        masked += QString("\n[Image omitted #%1]\n").arg(counter);
        lastPos = m.capturedEnd();
    }
    masked += inlined.mid(lastPos);
    return masked;
}

QString MainWindow::inlineAndNumber(const QString &html) {
    imgTags_.clear();
    QString inlined = html;
    QRegularExpression re(R"(<img\b[^>]+src=['"]([^'"]+)['"][^>]*>)", QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(inlined);
    while (it.hasNext()) {
        auto m = it.next();
        QString tag = m.captured(0);
        QString src = m.captured(1);
        if (!src.startsWith("data:")) {
            QUrl url(src);
            QString path = url.isLocalFile() ? url.toLocalFile() : QUrl::fromPercentEncoding(src.toUtf8());
            QFileInfo fi(path);
            if (fi.exists() && fi.isFile()) {
                QFile f(path);
                f.open(QIODevice::ReadOnly);
                QByteArray data = f.readAll().toBase64();
                QString ext = fi.suffix().toLower();
                tag.replace(src, QString("data:image/%1;base64,%2").arg(ext, QString(data)));
            }
        }
        imgTags_ << tag;
    }
    maskedHtml_ = makeMasked(inlined);
    return inlined;
}