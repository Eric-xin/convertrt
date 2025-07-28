#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QVector>

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void pasteFromWord();
    void syncFromSource();
    void syncFromPreview();
    void copyHtml();
    void copyRtf();

private:
    QString makeMasked(const QString &inlined);
    QString inlineAndNumber(const QString &html);

    QPlainTextEdit *srcEdit_;
    QTextEdit      *previewEdit_;
    QString         fullHtml_, maskedHtml_;
    QVector<QString> imgTags_;
    bool            syncing_ = false;
};