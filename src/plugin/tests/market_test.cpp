/// @file market_test.cpp
/// @brief 插件市场后端单元测试(feed 解析/版本比较/sha256/解包/安装/启停/卸载)
/// @details 用法(仓库根):
///   cd src/plugin/tests && mkdir -p build_market && cd build_market \
///     && /home/hatch/qt/6.5.3/gcc_64/bin/qmake ../market_test.pro && make \
///     && QT_QPA_PLATFORM=offscreen ./market_test
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include "plugin_market.h"

static int g_fail = 0;
#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            qWarning("FAIL: %s (line %d)", msg, __LINE__);           \
            ++g_fail;                                               \
        } else {                                                    \
            qInfo("ok: %s", msg);                                   \
        }                                                           \
    } while (0)

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("BPLC_MarketTest");
    QCoreApplication::setApplicationVersion("1.3.0");

    // feed 解析:用真实市场仓库的 market.json
    const QString feed_path =
        QString::fromLocal8Bit(qgetenv("MARKET_JSON"));
    QFile ff(feed_path.isEmpty()
                 ? QStringLiteral("/home/hatch/workspace/BPLC_Plugin_Market/"
                                  "market.json")
                 : feed_path);
    CHECK(ff.open(QIODevice::ReadOnly), "open market.json");
    QString perr;
    const QList<MarketPlugin> feed =
        PluginMarket::parse_feed(ff.readAll(), &perr);
    CHECK(perr.isEmpty() && feed.size() == 3, "parse 3 plugins");
    const MarketPlugin& topo = feed[0];
    CHECK(topo.name == "js-topo", "topo name");
    CHECK(topo.versions.size() == 2, "topo 2 versions");
    CHECK(topo.latest()->version == "1.1.0", "topo latest 1.1.0");
    CHECK(!topo.display_name_en.isEmpty(), "topo en name");

    // 版本比较
    CHECK(PluginMarket::compare_version("1.1.0", "1.0.0") > 0, "ver gt");
    CHECK(PluginMarket::compare_version("1.0.0", "1.0.0") == 0, "ver eq");
    CHECK(PluginMarket::compare_version("1.0.0", "1.10.0") < 0, "ver lt");
    CHECK(PluginMarket::app_version_ok("1.3.0"), "app ver ok");
    CHECK(!PluginMarket::app_version_ok("9.9.9"), "app ver reject");
    CHECK(PluginMarket::app_version_ok(""), "app ver empty ok");

    // sha256 校验:与 market.json 记录值比对
    const QString zip_path =
        QStringLiteral("/home/hatch/workspace/BPLC_Plugin_Market/plugins/"
                       "js-topo/js-topo-1.1.0.zip");
    CHECK(PluginMarket::verify_sha256(zip_path, topo.latest()->sha256),
          "sha256 match feed");
    CHECK(!PluginMarket::verify_sha256(
              zip_path, QString(64, QLatin1Char('0'))),
          "sha256 mismatch detected");

    // 解包
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "tmp dir");
    const QString staged = tmp.path() + "/staged";
    QString uerr;
    CHECK(PluginMarket::unzip_to_dir(zip_path, staged, &uerr),
          "unzip ok");
    const PluginManifest m = read_plugin_manifest(staged);
    CHECK(m.valid && m.name == "js-topo" && m.version == "1.1.0",
          "staged manifest valid");
    // zip-slip 防护
    CHECK(!PluginMarket::unzip_to_dir(QStringLiteral("/nope.zip"), staged,
                                      &uerr),
          "bad zip rejected");

    // 安装/启停/卸载(走默认安装目录,应用名为 BPLC_MarketTest,隔离)
    PluginMarket mk;
    bool fin_ok = false;
    QString fin_err, fin_name;
    QObject::connect(&mk, &PluginMarket::install_finished,
                     [&](bool ok, const QString& err, const QString& name) {
                         fin_ok = ok;
                         fin_err = err;
                         fin_name = name;
                     });
    mk.install_from_file(zip_path);
    CHECK(fin_ok && fin_name == "js-topo", "install_from_file ok");

    QList<InstalledPlugin> inst = mk.installed_plugins();
    CHECK(inst.size() == 1 && inst[0].manifest.version == "1.1.0" &&
              inst[0].enabled,
          "installed listed, enabled");

    // 更新检查:已装 1.1.0=最新 → 无更新;伪造 1.0.0 → 有更新
    CHECK(PluginMarket::update_for(inst[0], feed) == nullptr,
          "no update at latest");
    InstalledPlugin old = inst[0];
    old.manifest.version = "1.0.0";
    const MarketVersion* upd = PluginMarket::update_for(old, feed);
    CHECK(upd && upd->version == "1.1.0", "update found");

    CHECK(mk.set_enabled("js-topo", false), "disable ok");
    CHECK(!PluginMarket::plugin_dir_enabled(
              inst[0].dir),
          "dir disabled");
    inst = mk.installed_plugins();
    CHECK(inst.size() == 1 && !inst[0].enabled, "listed disabled");
    CHECK(mk.set_enabled("js-topo", true), "re-enable ok");

    CHECK(mk.uninstall("js-topo"), "uninstall ok");
    CHECK(mk.installed_plugins().isEmpty(), "uninstalled gone");
    CHECK(!mk.uninstall("js-topo"), "uninstall missing fails");
    CHECK(!mk.uninstall(".."), "uninstall traversal guard");

    // 坏包拒绝:非 zip
    fin_ok = true;
    mk.install_from_file(QStringLiteral("/tmp"));
    CHECK(!fin_ok, "non-zip rejected");

    if (g_fail == 0) qInfo("ALL MARKET TESTS PASSED");
    return g_fail == 0 ? 0 : 1;
}
