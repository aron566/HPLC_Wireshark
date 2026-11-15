/// @file downloader_rename_probe.cpp
/// @brief M6 回归:Downloader::finished() 里 QFile::rename 失败时必须
///        发 downloadFailed,而不是无视返回值照常发 downloadFinished 并进
///        installUpdate(装的却是缺失/旧文件)。
///        本地 HTTP 服务器提供一个 1 KB 文件;失败用例在下载目录预建与目标
///        同名的目录使重命名失败;成功用例作对照(正常完成、文件落盘)。
///        离屏运行:QT_QPA_PLATFORM=offscreen ./downloader_rename_probe
/// @brief M6 转发回归:downloadFailed 必须经 Updater/QSimpleUpdater 两层
///        原样转发到主程序(QSimpleUpdater::downloadFailed),而不是只停在
///        Downloader。用本地清单 + 本地文件全链路跑一次检查+下载验证。
/// @brief M6 跟进回归:网络错误与超限两路也不能静默消失(见 Downloader::
//        finished() 注释),用户主动取消则必须保持静默。本地服务器另提供
///        /huge(声明长度 >512MB)、/slow(慢速滴答,供途中取消)、
//        /appcast-dead(download-url 指拒连端口)、/appcast-huge。
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMessageBox>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "Downloader.h"
#include "QSimpleUpdater.h"

static int g_fails = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

/// @brief 最小 HTTP 服务器:GET /small 返回 1 KB 正文后关连接;
///        GET /appcast* 返回更新清单 JSON(下载地址指回 /small)
class Server : public QTcpServer {
public:
    Server() {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [s, this]() {
                    const QByteArray req = s->readAll();
                    const QByteArray path = req.split(' ').value(1);
                    if (path == "/small") {
                        const QByteArray body(1024, 'x');
                        s->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                                 QByteArray::number(body.size()) +
                                 "\r\nConnection: close\r\n\r\n" + body);
                        s->flush();
                    } else if (path == "/huge") {
                        // 声明长度超过 512MB 上限:metaDataChanged 即拒收,
                        // 实际不发正文。
                        s->write("HTTP/1.1 200 OK\r\nContent-Length: 536870913\r\n"
                                 "Connection: close\r\n\r\n");
                        s->flush();
                    } else if (path == "/slow") {
                        // 慢速下载:先发响应头 + 1 字节,再每 50 ms 滴 1 字节,
                        // 保证取消用例能在下载途中调 cancelDownload()。
                        if (s->property("slow-started").toBool()) {
                            s->disconnectFromHost();
                        } else {
                            s->setProperty("slow-started", true);
                            const QByteArray body(1024, 'y');
                            s->write("HTTP/1.1 200 OK\r\nContent-Length: " +
                                     QByteArray::number(body.size()) +
                                     "\r\nConnection: close\r\n\r\n" +
                                     body.left(1));
                            s->flush();
                            // 定时器挂在 socket 下,socket 断开即随之销毁,不再写。
                            auto* drip = new QTimer(s);
                            int sent = 1;
                            connect(drip, &QTimer::timeout, s,
                                    [s, drip, body, sent]() mutable {
                                        if (s->state() !=
                                            QAbstractSocket::ConnectedState) {
                                            drip->stop();
                                            return;
                                        }
                                        if (sent >= body.size()) {
                                            drip->stop();
                                            s->disconnectFromHost();
                                            return;
                                        }
                                        s->write(body.mid(sent, 1));
                                        s->flush();
                                        ++sent;
                                    });
                            drip->start(50);
                            return;  // 慢滴未完,不主动断开
                        }
                    } else if (path == "/appcast-dead") {
                        // 清单里的 download-url 指拒连端口,下载即报连接被拒
                        const QByteArray json =
                            "{\"updates\": {\"linux\": {\"open-url\": \"\", "
                            "\"changelog\": \"test\", \"download-url\": "
                            "\"http://127.0.0.1:" +
                            QByteArray::number(m_dead_port) +
                            "/small\", \"latest-version\": \"9.9.9\"}}}";
                        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                 "Content-Length: " +
                                 QByteArray::number(json.size()) +
                                 "\r\nConnection: close\r\n\r\n" + json);
                        s->flush();
                    } else if (path == "/appcast-huge") {
                        // 清单里的 download-url 指 /huge(声明长度超限)
                        const QByteArray json =
                            "{\"updates\": {\"linux\": {\"open-url\": \"\", "
                            "\"changelog\": \"test\", \"download-url\": "
                            "\"http://127.0.0.1:" +
                            QByteArray::number(m_port) +
                            "/huge\", \"latest-version\": \"9.9.9\"}}}";
                        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                 "Content-Length: " +
                                 QByteArray::number(json.size()) +
                                 "\r\nConnection: close\r\n\r\n" + json);
                        s->flush();
                    } else if (path.startsWith("/appcast")) {
                        const QByteArray json =
                            "{\"updates\": {\"linux\": {\"open-url\": \"\", "
                            "\"changelog\": \"test\", \"download-url\": "
                            "\"http://127.0.0.1:" +
                            QByteArray::number(m_port) +
                            "/small\", \"latest-version\": \"9.9.9\"}}}";
                        s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                 "Content-Length: " +
                                 QByteArray::number(json.size()) +
                                 "\r\nConnection: close\r\n\r\n" + json);
                        s->flush();
                    }
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    /// @brief 清单 JSON 里的 download-url 需填真实监听端口,listen 之后再设
    void setPort(quint16 port) { m_port = port; }

    /// @brief /appcast-dead 里的 download-url 指向的拒连端口
    void setDeadPort(quint16 port) { m_dead_port = port; }

private:
    quint16 m_port = 0;
    quint16 m_dead_port = 0;
};

struct Outcome {
    bool failed = false;
    bool finished = false;
    bool cancelled = false;  ///< 取消用例:cancelDownload() 是否实际执行过
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

/// @brief 对任意 URL 跑一次下载(网络错误/超限用例);返回 Downloader 层信号
static Outcome run_download_to(const QString& dir, const QString& url) {
    Outcome out;
    auto* dl = new Downloader();
    dl->setDownloadDir(dir);
    dl->setFileName(QStringLiteral("QSU_Update.bin"));
    dl->setUseCustomInstallProcedures(true);  // 成功对照不弹安装对话框
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

/// @brief 取消用例:对 /slow 慢速下载,在 .part 出现后调 cancelDownload(),
///        再等 2 秒看有无信号(信号处理里不直接 quit,等满 2 秒才算静默)。
static Outcome run_cancel_case(const QString& dir, quint16 port) {
    Outcome out;
    auto* dl = new Downloader();
    dl->setDownloadDir(dir);
    dl->setFileName(QStringLiteral("QSU_Update.bin"));
    dl->setUseCustomInstallProcedures(true);
    const QString url = QStringLiteral("http://127.0.0.1:%1/slow").arg(port);
    dl->setUrlId(url);
    QEventLoop loop;
    QObject::connect(dl, &Downloader::downloadFailed,
                     [&](const QString&, const QString& error) {
                         out.failed = true;
                         out.error = error;
                     });
    QObject::connect(dl, &Downloader::downloadFinished,
                     [&](const QString&, const QString&) { out.finished = true; });
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (out.cancelled) return;
        if (QFile::exists(dir + QStringLiteral("/QSU_Update.bin.part"))) {
            // 走真实用户路径点“停止”按钮(cancelDownload 是 private slot,
            // 不为探针改生产接口);按钮找不到则本用例判 FAIL。
            if (QPushButton* stop =
                    dl->findChild<QPushButton*>(QStringLiteral("stopButton"))) {
                out.cancelled = true;
                stop->click();  // 弹确认框,dlg_driver 会点 Yes
                QTimer::singleShot(2000, &loop, &QEventLoop::quit);
            }
        }
    });
    poll.start(50);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);  // 兜底:不永久挂起
    dl->startDownload(QUrl(url));
    loop.exec();
    poll.stop();
    delete dl;
    return out;
}

/// @brief 经 QSimpleUpdater 全链路跑一次检查+下载;block_rename=true 时在
///        下载目录预建与目标同名("small",取自 download-url 路径)的目录,
///        使重命名失败。返回 QSimpleUpdater 层收到的信号。
static Outcome run_update_check(const QString& appcast_url, const QString& dir,
                                bool block_rename) {
    Outcome out;
    if (block_rename) QDir(dir).mkdir(QStringLiteral("small"));

    auto* su = QSimpleUpdater::getInstance();
    su->setModuleVersion(appcast_url, QStringLiteral("0.0.1"));
    su->setModuleName(appcast_url, QStringLiteral("Probe"));
    su->setNotifyOnUpdate(appcast_url, true);
    su->setNotifyOnFinish(appcast_url, false);
    su->setUseCustomInstallProcedures(appcast_url, true);  // 成功对照不弹安装对话框
    su->setDownloaderEnabled(appcast_url, true);
    su->setDownloadDir(appcast_url, dir);
    QEventLoop loop;
    QObject::connect(su, &QSimpleUpdater::downloadFailed, &loop,
                     [&](const QString& url, const QString& error) {
                         if (url != appcast_url) return;
                         out.failed = true;
                         out.error = error;
                         loop.quit();
                     });
    QObject::connect(su, &QSimpleUpdater::downloadFinished, &loop,
                     [&](const QString& url, const QString& path) {
                         if (url != appcast_url) return;
                         out.finished = true;
                         out.path = path;
                         loop.quit();
                     });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);  // 兜底:不永久挂起
    su->checkForUpdates(appcast_url);
    loop.exec();
    return out;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid()) return 2;

    Server server;
    if (!server.listen(QHostAddress::LocalHost, 0)) return 2;
    const quint16 port = server.serverPort();
    server.setPort(port);  // 清单里的 download-url 用真实端口

    // 拒连端口:临时监听取一个端口后立即关闭,连它必得 ConnectionRefusedError。
    quint16 dead = 1;
    {
        QTcpServer dead_probe;
        if (dead_probe.listen(QHostAddress::LocalHost, 0)) {
            dead = dead_probe.serverPort();
            dead_probe.close();
        }
    }
    server.setDeadPort(dead);

    // 对话框驱动:更新可用提示点 Yes 确认下载;其余模态错误框直接关掉,
    // 免得探针挂住(同 updatecheck 的做法,但 Yes 必须点,不能只关)。
    QTimer dlg_driver;
    QObject::connect(&dlg_driver, &QTimer::timeout, []() {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        if (QAbstractButton* yes = box->button(QMessageBox::Yes))
            yes->click();
        else
            box->close();
    });
    dlg_driver.start(50);

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

    // 用例 3(M6 转发):全链路重命名失败 → QSimpleUpdater::downloadFailed
    {
        const QString dir = tmp.path() + QStringLiteral("/fwd-fail");
        QDir().mkpath(dir);
        const QString appcast =
            QStringLiteral("http://127.0.0.1:%1/appcast-fail").arg(port);
        const Outcome o = run_update_check(appcast, dir, true);
        check("M6-fwd: rename failure reaches QSimpleUpdater::downloadFailed", o.failed);
        check("M6-fwd: rename failure does not emit QSimpleUpdater::downloadFinished",
              !o.finished);
        check("M6-fwd: forwarded error says rename failed",
              o.error.contains(QStringLiteral("重命名")) ||
                  o.error.contains(QStringLiteral("Rename")));
        check("M6-fwd: partial file removed after rename failure",
              !QFile::exists(dir + QStringLiteral("/small.part")));
        if (o.failed) std::printf("  error: %s\n", qPrintable(o.error));
    }

    // 用例 4(M6 转发对照):全链路正常下载 → QSimpleUpdater::downloadFinished
    {
        const QString dir = tmp.path() + QStringLiteral("/fwd-ok");
        QDir().mkpath(dir);
        const QString appcast =
            QStringLiteral("http://127.0.0.1:%1/appcast-ok").arg(port);
        const Outcome o = run_update_check(appcast, dir, false);
        check("M6-fwd: successful download reaches QSimpleUpdater::downloadFinished",
              o.finished);
        check("M6-fwd: successful download does not emit downloadFailed", !o.failed);
        check("M6-fwd: downloaded file exists with 1 KB",
              QFile::exists(o.path) && QFileInfo(o.path).size() == 1024);
    }

    // 用例 5(M6 跟进):网络错误(连接被拒)→ downloadFailed,不发 downloadFinished
    {
        const QString dir = tmp.path() + QStringLiteral("/neterr");
        QDir().mkpath(dir);
        const QString url =
            QStringLiteral("http://127.0.0.1:%1/small").arg(dead);
        const Outcome o = run_download_to(dir, url);
        check("M6-followup: network error emits downloadFailed", o.failed);
        check("M6-followup: network error does not emit downloadFinished", !o.finished);
        check("M6-followup: network error message says download failed",
              o.error.contains(QStringLiteral("下载更新包失败")));
        check("M6-followup: partial file removed after network error",
              !QFile::exists(dir + QStringLiteral("/QSU_Update.bin.part")));
        if (o.failed) std::printf("  error: %s\n", qPrintable(o.error));
    }

    // 用例 6(M6 跟进转发):全链路网络错误 → QSimpleUpdater::downloadFailed
    {
        const QString dir = tmp.path() + QStringLiteral("/fwd-neterr");
        QDir().mkpath(dir);
        const QString appcast =
            QStringLiteral("http://127.0.0.1:%1/appcast-dead").arg(port);
        const Outcome o = run_update_check(appcast, dir, false);
        check("M6-followup-fwd: network error reaches QSimpleUpdater::downloadFailed",
              o.failed);
        check("M6-followup-fwd: network error does not emit QSimpleUpdater::downloadFinished",
              !o.finished);
        check("M6-followup-fwd: forwarded network error says download failed",
              o.error.contains(QStringLiteral("下载更新包失败")));
        if (o.failed) std::printf("  error: %s\n", qPrintable(o.error));
    }

    // 用例 7(M6 跟进):用户取消 → 无信号,.part 已删
    {
        const QString dir = tmp.path() + QStringLiteral("/cancel");
        QDir().mkpath(dir);
        const Outcome o = run_cancel_case(dir, port);
        check("M6-followup: user cancel actually exercised", o.cancelled);
        check("M6-followup: user cancel emits no downloadFailed", !o.failed);
        check("M6-followup: user cancel emits no downloadFinished", !o.finished);
        check("M6-followup: partial file removed after user cancel",
              !QFile::exists(dir + QStringLiteral("/QSU_Update.bin.part")));
    }

    // 用例 8(M6 跟进):超限(声明长度 >512MB)→ downloadFailed
    {
        const QString dir = tmp.path() + QStringLiteral("/huge");
        QDir().mkpath(dir);
        const QString url =
            QStringLiteral("http://127.0.0.1:%1/huge").arg(port);
        const Outcome o = run_download_to(dir, url);
        check("M6-followup: oversize emits downloadFailed", o.failed);
        check("M6-followup: oversize does not emit downloadFinished", !o.finished);
        check("M6-followup: oversize message says size limit",
              o.error.contains(QStringLiteral("超出大小上限")));
        check("M6-followup: partial file removed after oversize",
              !QFile::exists(dir + QStringLiteral("/QSU_Update.bin.part")));
        if (o.failed) std::printf("  error: %s\n", qPrintable(o.error));
    }

    // 用例 9(M6 跟进转发):全链路超限 → QSimpleUpdater::downloadFailed
    {
        const QString dir = tmp.path() + QStringLiteral("/fwd-huge");
        QDir().mkpath(dir);
        const QString appcast =
            QStringLiteral("http://127.0.0.1:%1/appcast-huge").arg(port);
        const Outcome o = run_update_check(appcast, dir, false);
        check("M6-followup-fwd: oversize reaches QSimpleUpdater::downloadFailed",
              o.failed);
        check("M6-followup-fwd: oversize does not emit QSimpleUpdater::downloadFinished",
              !o.finished);
        check("M6-followup-fwd: forwarded oversize message says size limit",
              o.error.contains(QStringLiteral("超出大小上限")));
        if (o.failed) std::printf("  error: %s\n", qPrintable(o.error));
    }

    std::printf("\nRESULT: %s (%d failures)\n", g_fails ? "FAILURES" : "ALL PASS", g_fails);
    return g_fails ? 1 : 0;
}
