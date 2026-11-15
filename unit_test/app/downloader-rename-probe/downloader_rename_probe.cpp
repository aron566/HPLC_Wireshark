/// @file downloader_rename_probe.cpp
/// @brief M6 回归:Downloader::finished() 里 QFile::rename 失败时必须
///        发 downloadFailed,而不是无视返回值照常发 downloadFinished 并进
///        installUpdate(装的却是缺失/旧文件)。
///        本地 HTTP 服务器提供一个 1 KB 文件;失败用例在下载目录预建与目标
///        同名的目录使重命名失败;成功用例作对照(正常完成、文件落盘)。
///        离屏运行:QT_QPA_PLATFORM=offscreen ./downloader_rename_probe
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMessageBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "Downloader.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

/// @brief 最小 HTTP 服务器:GET /small 返回 1 KB 正文后关连接
class Server : public QTcpServer {
public:
    Server() {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [s]() {
                    const QByteArray req = s->readAll();
                    if (req.split(' ').value(1) == "/small") {
                        const QByteArray body(1024, 'x');
                        s->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                                 QByteArray::number(body.size()) +
                                 "\r\nConnection: close\r\n\r\n" + body);
                        s->flush();
                    }
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
};

struct Outcome {
    bool failed = false;
    bool finished = false;
    QString error;
    QString path;
};

/// @brief 跑一次下载;block_rename=true 时在下载目录预建与目标同名的目录,
///        使 QFile::rename 失败
static Outcome run_download(const QString& dir, bool block_rename, quint16 port) {
    Outcome out;
    const QString file_name = QStringLiteral("QSU_Update.bin");
    if (block_rename) QDir(dir).mkdir(file_name);

    auto* dl = new Downloader();
    dl->setDownloadDir(dir);
    dl->setFileName(file_name);
    dl->setUseCustomInstallProcedures(true);  // 成功对照不弹安装对话框
    const QString url = QStringLiteral("http://127.0.0.1:%1/small").arg(port);
    dl->setUrlId(url);
    QEventLoop loop;
    QObject::connect(dl, &Downloader::downloadFailed,
                     [&](const QString&, const QString& error) {
                         out.failed = true;
                         out.error = error;
                         loop.quit();
                     });
    QObject::connect(dl, &Downloader::downloadFinished,
                     [&](const QString&, const QString& path) {
                         out.finished = true;
                         out.path = path;
                         loop.quit();
                     });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);  // 兜底:不永久挂起
    dl->startDownload(QUrl(url));
    loop.exec();
    delete dl;
    return out;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid()) return 2;

    Server server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
    const quint16 port = server.serverPort();

    // 失败路径弹 QMessageBox(模态):定时关掉,免得探针挂住(同 updatecheck 的做法)
    QTimer auto_close;
    QObject::connect(&auto_close, &QTimer::timeout, []() {
        if (QWidget* m = QApplication::activeModalWidget()) m->close();
    });
    auto_close.start(50);

    // 用例 1:重命名失败 → downloadFailed,不发 downloadFinished
    {
        const QString dir = tmp.path() + QStringLiteral("/fail");
        QDir().mkpath(dir);
        const Outcome o = run_download(dir, true, port);
        check("M6: rename failure emits downloadFailed", o.failed);
        check("M6: rename failure does not emit downloadFinished", !o.finished);
        check("M6: failure message says rename failed",
              o.error.contains(QStringLiteral("重命名")) ||
                  o.error.contains(QStringLiteral("Rename")));
        check("M6: partial file removed after rename failure",
              !QFile::exists(dir + QStringLiteral("/QSU_Update.bin.part")));
        if (o.failed) std::printf("  error: %s\n", qPrintable(o.error));
    }

    // 用例 2:对照,正常下载 → downloadFinished,文件落盘 1 KB
    {
        const QString dir = tmp.path() + QStringLiteral("/ok");
        QDir().mkpath(dir);
        const Outcome o = run_download(dir, false, port);
        check("M6: successful download emits downloadFinished", o.finished);
        check("M6: successful download does not emit downloadFailed", !o.failed);
        check("M6: downloaded file exists with 1 KB",
              QFile::exists(o.path) && QFileInfo(o.path).size() == 1024);
    }

    std::printf("\nRESULT: %s (%d failures)\n", g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
