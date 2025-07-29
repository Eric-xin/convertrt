#pragma once

#include <QWidget>
#include <QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextEdit;
class QTextBrowser;
class QProgressDialog;

class ImageMarkerHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit ImageMarkerHighlighter(QTextDocument *doc);
protected:
    void highlightBlock(const QString &text) override;
private:
    QRegularExpression _pattern;
    QTextCharFormat   _format;
};

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void pasteFromWord();
    void copyHtml();
    void copyRtf();
    void confirmAndUpload();
    void syncFromSource();
    void syncFromPreview();

private:
    std::pair<QString, QString> inlineAndMask(const QString &html);
    QByteArray fetchLocalImage(const QString &src, QString &header);
    QJsonObject fetchSts();
    QString uploadToOss(const QString &header, const QByteArray &data);
    void loadExternalImages();

    QTextEdit          *_srcEdit;
    QTextBrowser       *_preview;
    QString             _fullHtml;
    QStringList         _imgTags;
    bool                _syncing = false;
    QNetworkAccessManager _networkManager;
};