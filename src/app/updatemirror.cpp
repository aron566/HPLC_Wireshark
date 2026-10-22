/// @file updatemirror.cpp
/// @brief UpdateMirrorResolver 实现(见 updatemirror.h 的决策流程说明)
#include "updatemirror.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "appconfig.h"

namespace {
constexpr int kRegionTimeoutMs  = 3500;   // 出口区域识别(每源)
constexpr int kProbeTimeoutMs   = 4500;   // 清单探活(原生/每个镜像)
const char* kTraceUrl  = "https://www.cloudflare.com/cdn-cgi/trace";
const char* kIpapiUrl  = "https://ipapi.co/country/";
} // namespace

UpdateMirrorResolver::UpdateMirrorResolver(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)) {}

void UpdateMirrorResolver::get(const QString& url, int timeoutMs,
                               const std::function<void(QNetworkReply*)>& done) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("BPLC-STA-Monitor update-mirror probe"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(timeoutMs);
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        done(reply);
        reply->deleteLater();
    });
}

bool UpdateMirrorResolver::looksLikeManifest(const QByteArray& body) {
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    return doc.isObject() && doc.object().contains(QStringLiteral("updates"));
}

void UpdateMirrorResolver::finish(const QString& effectiveUrl, const QString& prefix) {
    if (m_done) return;
    m_done = true;
    emit resolved(effectiveUrl, prefix);
}

void UpdateMirrorResolver::resolve(const QString& updateUrl) {
    m_url = updateUrl;
    m_done = false;
    m_mirrors = appcfg::mirror_list();
    const QString mode = appcfg::mirror_mode();
    if (mode == QStringLiteral("native")) {
        qInfo("[update-mirror] mode=native, using native URL");
        finish(m_url, QString());
    } else if (mode == QStringLiteral("proxy")) {
        qInfo("[update-mirror] mode=proxy, probing mirrors");
        probeMirror(0);
    } else {
        detectRegion();
    }
}

void UpdateMirrorResolver::detectRegion() {
    get(QString::fromLatin1(kTraceUrl), kRegionTimeoutMs, [this](QNetworkReply* r) {
        QString loc;
        if (r->error() == QNetworkReply::NoError) {
            const QString body = QString::fromUtf8(r->readAll());
            for (const QString& line : body.split(QLatin1Char('\n'))) {
                if (line.startsWith(QStringLiteral("loc=")))
                    loc = line.mid(4).trimmed().toUpper();
            }
        }
        if (!loc.isEmpty()) { onRegion(loc); return; }
        // Cloudflare trace 无结果 → ipapi 兜底
        get(QString::fromLatin1(kIpapiUrl), kRegionTimeoutMs, [this](QNetworkReply* r2) {
            QString cc;
            if (r2->error() == QNetworkReply::NoError)
                cc = QString::fromUtf8(r2->readAll()).trimmed().toUpper();
            onRegion(cc.size() == 2 ? cc : QString());
        });
    });
}

void UpdateMirrorResolver::onRegion(const QString& countryCode) {
    if (countryCode == QStringLiteral("CN")) {
        qInfo("[update-mirror] egress region=CN, probing mirrors");
        probeMirror(0);
    } else if (!countryCode.isEmpty()) {
        qInfo("[update-mirror] egress region=%s, using native URL",
              qPrintable(countryCode));
        finish(m_url, QString());
    } else {
        qInfo("[update-mirror] egress region unknown, probing native URL first");
        probeNative();
    }
}

void UpdateMirrorResolver::probeNative() {
    get(m_url, kProbeTimeoutMs, [this](QNetworkReply* r) {
        const bool ok = r->error() == QNetworkReply::NoError
            && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && looksLikeManifest(r->readAll());
        if (ok) {
            qInfo("[update-mirror] native URL reachable, using native");
            finish(m_url, QString());
        } else {
            qWarning("[update-mirror] native URL probe failed, probing mirrors");
            probeMirror(0);
        }
    });
}

void UpdateMirrorResolver::probeMirror(int index) {
    if (index >= m_mirrors.size()) {
        qWarning("[update-mirror] all mirrors unavailable, falling back to native URL");
        finish(m_url, QString());
        return;
    }
    const QString prefix = m_mirrors.at(index);
    get(prefix + m_url, kProbeTimeoutMs, [this, index, prefix](QNetworkReply* r) {
        const bool ok = r->error() == QNetworkReply::NoError
            && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200
            && looksLikeManifest(r->readAll());
        if (ok) {
            qInfo("[update-mirror] mirror selected: %s", qPrintable(prefix));
            finish(prefix + m_url, prefix);
        } else {
            qWarning("[update-mirror] mirror probe failed: %s", qPrintable(prefix));
            probeMirror(index + 1);
        }
    });
}
