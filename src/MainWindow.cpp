#include "MainWindow.h"
#include <QTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QSplitter>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QtNetwork/QHttpMultiPart>
#include <QtNetwork/QNetworkReply>
#include <QDateTime>
#include <QCryptographicHash>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QJsonArray>
#include <QJsonValue>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QtNetwork/QHttpPart>
#include <QDebug>
#include <QTimer>
#include <QSet>
#include <QRandomGenerator>

#include <QSettings>

// Load endpoints from config file (e.g., config.ini in the application directory)
static QSettings settings(QStringLiteral("config.ini"), QSettings::IniFormat);

static const QString STS_URL        = settings.value("oss/sts_url").toString();
static const QString OSS_UPLOAD_URL = settings.value("oss/oss_upload_url").toString();
static const QString OSS_BASE_URL   = settings.value("oss/oss_base_url").toString();

ImageMarkerHighlighter::ImageMarkerHighlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc),
      _pattern(QStringLiteral("\\[Image omitted #\\d+\\]"))
{
    _format.setBackground(Qt::yellow);
}

void ImageMarkerHighlighter::highlightBlock(const QString &text) {
    auto it = _pattern.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        setFormat(m.capturedStart(), m.capturedLength(), _format);
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("Word-to-HTML/RTF Converter");

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Left pane
    _srcEdit = new QTextEdit;
    _srcEdit->setAcceptRichText(false);
    new ImageMarkerHighlighter(_srcEdit->document());
    auto *leftW = new QWidget;
    auto *lLayout = new QVBoxLayout(leftW);
    lLayout->setContentsMargins(0,0,0,0);
    lLayout->addWidget(new QLabel("HTML Source (plain text):"));
    lLayout->addWidget(_srcEdit);

    // Right pane
    _preview = new QTextBrowser;
    _preview->setOpenExternalLinks(true);
    _preview->setReadOnly(false);
    auto *rightW = new QWidget;
    auto *rLayout = new QVBoxLayout(rightW);
    rLayout->setContentsMargins(0,0,0,0);
    rLayout->addWidget(new QLabel("Rendered Preview:"));
    rLayout->addWidget(_preview);

    splitter->addWidget(leftW);
    splitter->addWidget(rightW);

    // Buttons
    auto *buttonLayout = new QHBoxLayout;
    QPushButton *pasteBtn     = new QPushButton("Paste from Word");
    QPushButton *copyHtmlBtn  = new QPushButton("Copy as HTML");
    QPushButton *copyRtfBtn   = new QPushButton("Copy as Rich Text");
    QPushButton *confirmBtn   = new QPushButton("Confirm");
    buttonLayout->addWidget(pasteBtn);
    buttonLayout->addWidget(copyHtmlBtn);
    buttonLayout->addWidget(copyRtfBtn);
    buttonLayout->addWidget(confirmBtn);
    buttonLayout->addStretch();

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter);
    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(pasteBtn,    &QPushButton::clicked, this, &MainWindow::pasteFromWord);
    connect(copyHtmlBtn, &QPushButton::clicked, this, &MainWindow::copyHtml);
    connect(copyRtfBtn,  &QPushButton::clicked, this, &MainWindow::copyRtf);
    connect(confirmBtn,  &QPushButton::clicked, this, &MainWindow::confirmAndUpload);
    connect(_srcEdit,    &QTextEdit::textChanged, this, &MainWindow::syncFromSource);
    connect(_preview,    &QTextBrowser::textChanged, this, &MainWindow::syncFromPreview);
}

std::pair<QString, QString> MainWindow::inlineAndMask(const QString &html) {
    QString inl = html;
    QRegularExpression reImg(R"(<img\b[^>]+src=['"]([^'"]+)['"][^>]*>)",
                             QRegularExpression::CaseInsensitiveOption);
    int pos = 0;
    QRegularExpressionMatch m;
    while ((m = reImg.match(inl, pos)).hasMatch()) {
        QString header;
        QByteArray data = fetchLocalImage(m.captured(1), header);
        if (!data.isEmpty()) {
            QString tag = m.captured(0);
            tag.replace(m.captured(1), header + QString::fromLatin1(data.toBase64()));
            inl.replace(m.capturedStart(), m.capturedLength(), tag);
            pos = m.capturedStart() + tag.length();
        } else {
            pos = m.capturedEnd();
        }
    }

    _imgTags = inl.split(QRegularExpression(R"(<img\b[^>]*>)"), Qt::SkipEmptyParts);

    QString masked;
    int count = 0;
    QRegularExpression reAll(R"(<img\b[^>]*>)",
                             QRegularExpression::CaseInsensitiveOption);
    pos = 0;
    while ((m = reAll.match(inl, pos)).hasMatch()) {
        masked += inl.mid(pos, m.capturedStart() - pos);
        masked += QString("\n[Image omitted #%1]\n").arg(++count);
        pos = m.capturedEnd();
    }
    masked += inl.mid(pos);
    return { inl, masked };
}

QByteArray MainWindow::fetchLocalImage(const QString &src, QString &header) {
    QUrl url(src);
    QString raw = url.isLocalFile() ? url.toLocalFile() : QString(src).replace('\\','/');
    QFileInfo fi(raw);
    if (!fi.exists()) return {};
    QFile f(fi.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray bytes = f.readAll();
    QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
    header = QString("data:%1;base64,").arg(mime);
    return bytes;
}

void MainWindow::pasteFromWord() {
    auto *cb = QGuiApplication::clipboard();
    QString raw = cb->mimeData()->hasHtml()
                ? cb->mimeData()->html()
                : cb->mimeData()->hasText()
                  ? cb->mimeData()->text()
                  : QString();
    if (raw.isEmpty()) return;
    auto [inl, masked] = inlineAndMask(raw);
    _fullHtml = inl;
    _syncing = true;
    _srcEdit->setPlainText(masked);
    _preview->setHtml(inl);
    _syncing = false;
    // Load external images after syncing is done
    loadExternalImages();
}

void MainWindow::syncFromSource() {
    if (_syncing) return;
    _syncing = true;
    QString text = _srcEdit->toPlainText();
    QString out;
    auto parts = text.split(QRegularExpression(R"(\[Image omitted #\d+\])"),
                            Qt::KeepEmptyParts);
    for (const auto &p : parts) {
        QRegularExpression re(R"(\[Image omitted #(\d+)\])");
        auto m = re.match(p);
        if (m.hasMatch()) {
            int idx = m.captured(1).toInt() - 1;
            if (idx >= 0 && idx < _imgTags.size())
                out += _imgTags[idx];
        } else {
            out += p;
        }
    }
    _preview->setHtml(out);
    _syncing = false;
    // Load external images after syncing is done
    loadExternalImages();
}

void MainWindow::loadExternalImages() {
    if (_syncing) {
        qDebug() << "Skipping loadExternalImages() - syncing in progress";
        return;
    }
    
    qDebug() << "loadExternalImages() called";
    auto *doc = _preview->document();
    const QString html = _preview->toHtml();

    // Regex to find all distinct http(s) image URLs
    QRegularExpression re(
        R"(<img\b[^>]*\bsrc=['"](https?://[^'"]+)['"][^>]*>)",
        QRegularExpression::CaseInsensitiveOption
    );

    QSet<QString> seen;
    auto it = re.globalMatch(html);
    int imageCount = 0;
    int loadedCount = 0;
    while (it.hasNext()) {
        QString url = it.next().captured(1);
        if (seen.contains(url)) continue;
        seen.insert(url);
        imageCount++;

        qDebug() << "Loading external image:" << url;

        // Synchronous fetch with timeout and error handling
        QNetworkRequest req((QUrl(url)));
        req.setRawHeader("User-Agent", "convertrt/1.0");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        
        QNetworkReply *reply = _networkManager.get(req);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        timer.setInterval(10000); // 10 second timeout
        
        // Set up connections
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, [&loop, reply]() {
            reply->abort(); // Abort the request on timeout
            loop.quit();
        });
        
        timer.start();
        loop.exec();
        timer.stop();

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QImage img;
            if (img.loadFromData(data)) {
                doc->addResource(QTextDocument::ImageResource, QUrl(url), img);
                qDebug() << "Successfully loaded image:" << url;
                loadedCount++;
            } else {
                qDebug() << "Failed to decode image data for:" << url;
            }
        } else if (reply->error() == QNetworkReply::OperationCanceledError) {
            qDebug() << "Timeout loading image:" << url;
        } else {
            qDebug() << "Network error loading image:" << url << "Error:" << reply->errorString();
        }
        reply->deleteLater();
    }

    qDebug() << "Found" << imageCount << "external images, loaded" << loadedCount << "successfully";
    
    // Only re-apply HTML if we loaded any images
    if (loadedCount > 0) {
        _syncing = true;
        _preview->setHtml(html);
        _syncing = false;
    }
    qDebug() << "loadExternalImages() completed";
}

void MainWindow::syncFromPreview() {
    if (_syncing) return;
    _syncing = true;
    auto [inl, masked] = inlineAndMask(_preview->toHtml());
    _fullHtml = inl;
    _srcEdit->setPlainText(masked);
    _syncing = false;
    // Load external images after syncing is done
    loadExternalImages();
}

void MainWindow::copyHtml() {
    QGuiApplication::clipboard()->setText(_fullHtml);
}

void MainWindow::copyRtf() {
    QTextEdit tmp;
    tmp.setHtml(_fullHtml);
    tmp.selectAll();
    tmp.copy();
}

void MainWindow::confirmAndUpload() {
    QRegularExpression re(R"(data:image/[^;]+;base64,[^"']+)");
    auto it = re.globalMatch(_fullHtml);
    
    // Store each image instance separately, even if they're identical
    QList<QString> allDataUris;
    QList<QString> uploadedUrls;
    
    while (it.hasNext()) {
        auto match = it.next();
        allDataUris.append(match.captured(0));
    }
    
    int total = allDataUris.size();
    if (!total) return;

    qDebug() << "Found" << total << "images to upload";

    QProgressDialog pd("Uploading images…", "Cancel", 0, total, this);
    pd.setWindowModality(Qt::WindowModal);
    pd.show();

    int success = 0;
    uploadedUrls.resize(total); // Pre-allocate to match allDataUris size
    
    // Upload each image individually, creating unique URLs for each instance
    for (int i = 0; i < allDataUris.size(); ++i) {
        if (pd.wasCanceled()) break;
        
        QString uri = allDataUris[i];
        QString header = uri.section(',', 0, 0) + ",";
        QByteArray data = QByteArray::fromBase64(uri.section(',', 1).toLatin1());
        
        qDebug() << "Uploading image" << (i+1) << "of" << total << "- size:" << data.size() << "bytes";
        
        try {
            QString uploadedUrl = uploadToOss(header, data);
            uploadedUrls[i] = uploadedUrl;
            ++success;
            qDebug() << "Successfully uploaded image" << (i+1) << "to:" << uploadedUrl;
        } catch (const QString &error) {
            qDebug() << "Failed to upload image" << (i+1) << ":" << error;
            uploadedUrls[i] = QString(); // Mark as failed
        } catch (...) {
            qDebug() << "Failed to upload image" << (i+1) << ": Unknown error";
            uploadedUrls[i] = QString(); // Mark as failed
        }
        
        pd.setValue(i + 1);
        pd.setLabelText(QString("%1/%2 — %3 succeeded")
                        .arg(i+1).arg(total).arg(success));
        QCoreApplication::processEvents();
    }
    pd.close();
    
    // Replace data URIs one by one, in order
    QString newHtml = _fullHtml;
    for (int i = 0; i < allDataUris.size(); ++i) {
        if (!uploadedUrls[i].isEmpty()) {
            QString dataUri = allDataUris[i];
            QString uploadedUrl = uploadedUrls[i];
            
            // Replace the first occurrence of this data URI
            int pos = newHtml.indexOf(dataUri);
            if (pos != -1) {
                newHtml.replace(pos, dataUri.length(), uploadedUrl);
                qDebug() << "Replaced image" << (i+1) << "with" << uploadedUrl;
            }
        }
    }

    _fullHtml = newHtml;
    _syncing = true;
    _srcEdit->setPlainText(newHtml);
    _preview->setHtml(newHtml);
    _syncing = false;
    // Load external images after syncing is done
    loadExternalImages();

    QMessageBox::information(this, "Upload Complete",
        QString("Uploaded %1 of %2 images successfully.").arg(success).arg(total));
}

QJsonObject MainWindow::fetchSts() {
    QNetworkRequest req((QUrl(STS_URL)));
    // auto *reply = _networkManager.get(req);
    QNetworkReply *reply = _networkManager.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError)
        throw QString(reply->errorString());
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    return doc.object().value("data").toObject();
}

QString MainWindow::uploadToOss(const QString &header, const QByteArray &data) {
    // Debug: Check if URLs are loaded correctly
    qDebug() << "STS_URL:" << STS_URL;
    qDebug() << "OSS_UPLOAD_URL:" << OSS_UPLOAD_URL;
    qDebug() << "OSS_BASE_URL:" << OSS_BASE_URL;
    
    if (STS_URL.isEmpty() || OSS_UPLOAD_URL.isEmpty() || OSS_BASE_URL.isEmpty()) {
        throw QString("Configuration not loaded properly. Check config.ini file.");
    }

    auto creds = fetchSts();
    qDebug() << "STS credentials fetched successfully";
    
    qint64 expire = QDateTime::currentSecsSinceEpoch() + 3600;
    
    QJsonObject policy;
    // Use UTC time format like in Python version
    policy["expiration"] = QDateTime::fromSecsSinceEpoch(expire).toUTC().toString("yyyy-MM-ddThh:mm:ssZ");
    QJsonArray inner;
    inner << QJsonValue(QStringLiteral("content-length-range"))
          << QJsonValue(0)
          << QJsonValue(1024*1024*1024);
    QJsonArray conditions;
    conditions << QJsonValue(inner);
    policy["conditions"] = conditions;
    QByteArray p64 = QJsonDocument(policy).toJson(QJsonDocument::Compact).toBase64();
    
    // Use HMAC-SHA1 for signature (proper HMAC implementation)
    QByteArray key = creds["accessKeySecret"].toString().toUtf8();
    QByteArray sig = QMessageAuthenticationCode::hash(p64, key, QCryptographicHash::Sha1).toBase64();

    qDebug() << "Policy (base64):" << p64;
    qDebug() << "Signature:" << sig;
    qDebug() << "AccessKeyId:" << creds["accessKeyId"].toString();

    QString mime = header.section(';',0,0).section(':',1,1);
    QString ext  = mime.section('/',1,1).toLower();
    // if (ext == "jpeg") ext = "jpg";
    
    // Compute SHA1 of the first few bytes of the document for uniqueness
    QByteArray shaInput = data.left(128); // first 128 bytes
    QByteArray sha1 = QCryptographicHash::hash(shaInput, QCryptographicHash::Sha1).toHex();

    // Add microsecond precision and random component to ensure uniqueness for each upload
    static int uploadCounter = 0;
    uploadCounter++;
    
    QString objectKey = QString("pc/course/dev/%1.%2.%3.%4.%5")
        .arg(QString::fromUtf8(sha1.left(8))) // first 8 hex chars of sha1
        .arg(QString::number(QDateTime::currentMSecsSinceEpoch())) // millisecond precision
        .arg(uploadCounter) // incremental counter
        .arg(QRandomGenerator::global()->bounded(10000)) // random component
        .arg(ext);

    qDebug() << "Uploading to key:" << objectKey;
    qDebug() << "MIME type:" << mime;

    // Try manual multipart construction to match Python version exactly
    QByteArray boundary = "----formdata-qt-" + QString::number(QDateTime::currentMSecsSinceEpoch()).toUtf8();
    QByteArray multipartData;
    
    auto addFormField = [&](const QByteArray &name, const QByteArray &value) {
        multipartData += "--" + boundary + "\r\n";
        multipartData += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
        multipartData += value + "\r\n";
    };
    
    // Add form fields in exact order as Python
    addFormField("key", objectKey.toUtf8());
    addFormField("policy", p64);
    addFormField("OSSAccessKeyId", creds["accessKeyId"].toString().toUtf8());
    addFormField("signature", sig);
    addFormField("x-oss-security-token", creds["securityToken"].toString().toUtf8());
    addFormField("success_action_status", "200");
    
    // Add file part
    multipartData += "--" + boundary + "\r\n";
    multipartData += "Content-Disposition: form-data; name=\"file\"; filename=\"image." + ext.toUtf8() + "\"\r\n";
    multipartData += "Content-Type: " + mime.toUtf8() + "\r\n\r\n";
    multipartData += data;
    multipartData += "\r\n--" + boundary + "--\r\n";

    QNetworkRequest req((QUrl(OSS_UPLOAD_URL)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);
    req.setHeader(QNetworkRequest::ContentLengthHeader, multipartData.size());
    
    qDebug() << "Content-Type:" << req.header(QNetworkRequest::ContentTypeHeader).toString();
    qDebug() << "Content-Length:" << multipartData.size();
    
    QNetworkReply *reply = _networkManager.post(req, multipartData);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // Debug output
    qDebug() << "Upload response status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug() << "Upload response:" << reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("Upload failed: %1 (HTTP %2)")
                          .arg(reply->errorString())
                          .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        throw errorMsg;
    }

    return OSS_BASE_URL + "/" + objectKey;
}