// UpdateMirrorResolver 探针:按 argv[1] 写入 config.ini 的 mirror_mode,
// 再对真实 update.json 跑一次解析,打印最终生效地址与前缀。
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <cstdio>
#include "appconfig.h"
#include "updatemirror.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("auto");
    {
        QSettings s(appcfg::ini_path(), QSettings::IniFormat);
        s.setValue(QStringLiteral("general/mirror_mode"), mode);
        s.setValue(QStringLiteral("general/mirror_list"), QString());
        s.sync();
    }
    const QString url = appcfg::update_url();
    std::printf("mode=%s url=%s\n", qPrintable(mode), qPrintable(url));
    auto* r = new UpdateMirrorResolver(&app);
    QObject::connect(r, &UpdateMirrorResolver::resolved, &app,
                     [](const QString& eff, const QString& prefix) {
        std::printf("RESOLVED effective=%s prefix=%s\n",
                    qPrintable(eff), prefix.isEmpty() ? "(native)" : qPrintable(prefix));
        QCoreApplication::exit(0);
    });
    QTimer::singleShot(30000, &app, []() {
        std::printf("TIMEOUT\n");
        QCoreApplication::exit(2);
    });
    QTimer::singleShot(0, r, [r, url]() { r->resolve(url); });
    return app.exec();
}
