/// @file plugin_market.h
/// @brief 插件市场后端:远端 feed 拉取、下载安装、离线安装、启停、卸载
/// @details
///   Feed 格式见 BPLC_Plugin_Market/market.json:
///     { "version": 1, "updated": "...",
///       "plugins": [ { "name", "display_name", "display_name_en",
///                       "description", "description_en", "category",
///                       "author", "versions": [
///                         { "version", "url", "sha256", "size",
///                           "min_app_version" } ] } ] }
///   插件包为 zip,内含 plugin.json(清单,见 plugin_api/plugin_manifest.h)
///   与入口脚本。安装目录:
///     QStandardPaths::AppDataLocation + "/plugins/<name>/"
///   每个已安装插件目录下有 meta.json: { "enabled": bool, "installed_at": ... }。
///   校验链(当前):feed 提供的 sha256 校验下载包 + 清单合法性 +
///   min_app_version 兼容性。Ed25519 官方签名为后续步骤,见 docs/MARKET.md。
#ifndef BPLC_PLUGIN_MARKET_H
#define BPLC_PLUGIN_MARKET_H

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "../plugin_api/plugin_manifest.h"

/// @brief 市场 feed 中的单个版本
struct MarketVersion {
    QString version;
    QString url;
    QString sha256;          ///< 包 sha256 hex(小写)
    qint64  size = 0;
    QString min_app_version; ///< 要求宿主最低版本
};

/// @brief 市场 feed 中的一个插件
struct MarketPlugin {
    QString name;
    QString display_name;
    QString display_name_en;
    QString description;
    QString description_en;
    QString category;        ///< diagnosis | report | graphics | protocol ...
    QString author;
    QList<MarketVersion> versions;

    const MarketVersion* latest() const {
        return versions.isEmpty() ? nullptr : &versions.last();
    }
    QString localized_name(bool english) const {
        if (english && !display_name_en.isEmpty()) return display_name_en;
        return display_name.isEmpty() ? name : display_name;
    }
    QString localized_desc(bool english) const {
        if (english && !description_en.isEmpty()) return description_en;
        return description;
    }
};

/// @brief 本地已安装插件
struct InstalledPlugin {
    PluginManifest manifest;
    bool    enabled = true;
    QString dir;             ///< 插件目录绝对路径
};

/// @brief 插件市场后端(网络/文件 IO 均在本类,GUI 线程使用)
class PluginMarket : public QObject {
    Q_OBJECT
public:
    explicit PluginMarket(QObject* parent = nullptr);
    ~PluginMarket() override;

    /// @brief 用户插件安装根目录
    static QString default_install_dir();
    /// @brief 默认远端 feed 地址(BPLC_Plugin_Market/market.json)
    static QString default_feed_url();

    /// @brief 拉取远端 feed(异步,经 feed_ready/feed_error 回传)
    void fetch_feed(const QString& url);
    /// @brief 解析 feed JSON(静态,便于测试)
    static QList<MarketPlugin> parse_feed(const QByteArray& json, QString* err);

    /// @brief 从市场安装指定版本(异步,经 install_* 信号回传)
    void install_market_plugin(const MarketPlugin& plugin, int version_index);
    /// @brief 离线安装本地 zip 包(异步,经 install_* 信号回传)
    void install_from_file(const QString& zip_path);

    /// @brief 扫描安装目录,返回已安装插件
    QList<InstalledPlugin> installed_plugins() const;
    /// @brief 启用/禁用(写 meta.json,下次加载生效)
    bool set_enabled(const QString& name, bool enabled);
    /// @brief 卸载(删除插件目录)
    bool uninstall(const QString& name);
    /// @brief 该插件在 feed 中是否有更新(返回新版本,无则 nullptr)
    static const MarketVersion* update_for(const InstalledPlugin& inst,
                                           const QList<MarketPlugin>& feed);

    // ---- 静态工具(可独立测试) ----
    static bool verify_sha256(const QString& file_path,
                              const QString& expected_hex);
    static bool unzip_to_dir(const QString& zip_path, const QString& dest_dir,
                             QString* err);
    /// @brief 语义化版本比较: <0(a<b) 0(==) >0(a>b)
    static int compare_version(const QString& a, const QString& b);
    /// @brief 当前宿主版本是否满足 min_app_version
    static bool app_version_ok(const QString& min_app_version);
    /// @brief 插件目录是否被禁用(meta.json enabled==false)
    static bool plugin_dir_enabled(const QString& plugin_dir);

signals:
    void feed_ready(const QList<MarketPlugin>& plugins);
    void feed_error(const QString& error);
    void install_progress(const QString& text);
    void install_finished(bool ok, const QString& error, const QString& name);

private:
    void finish_install_from_zip(const QString& zip_path,
                                 const QString& expected_sha256);
    bool deploy_staged(const QString& staged_dir, const QString& plugin_name,
                       QString* err);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_pending_name;       ///< 本次安装中的插件名
    QString m_pending_sha256;     ///< 本次安装期望的 sha256(离线安装为空)
};

#endif // BPLC_PLUGIN_MARKET_H
