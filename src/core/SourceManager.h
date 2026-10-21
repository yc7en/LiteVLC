#pragma once

#include <QList>
#include <QString>

/**
 * @brief 单个在线片源站点
 *
 * 字段命名与 TVBox / CatVod 订阅格式（标准 JSON：顶层 sites 数组）逐字段对齐，
 * 因此可以直接把网上现成的 TVBox 配置粘进来导入。
 *
 * 支持的 only 两种接口形态（与 TVBox 的 type 语义一致）：
 *   type = 0  → 苹果 CMS XML 接口 (xml.php/provide/vod)
 *   type = 1  → 苹果 CMS JSON 接口 (api.php/provide/vod)
 *   type = 3  → Spider（jar / js / py），本播放器不内置脚本运行时，标记为不支持
 */
struct SourceSite
{
    QString key;                            // 唯一标识（缺省时由名称派生）
    QString name;                           // 显示名称
    QString api;                            // 接口地址
    QString ext;                            // 站点扩展参数（原样保存，部分 CMS 需要）
    int     type = 1;                       // 0=XML  1=JSON  3=Spider
    bool    searchable = true;
    bool    enabled = true;
    bool    quickSearch = true;
    QString userAgent;                      // 留空则用内置浏览器 UA
    QString referer;                        // 留空则用接口地址的 origin
    int     timeoutSec = 15;

    /** 是否为可直接请求的苹果 CMS 接口（type 0/1 且 api 是 http 地址） */
    bool isCmsSupported() const;
    /** 中文类型标签，用于源列表展示 */
    QString typeLabel() const;
    QString effectiveUserAgent() const;
    QString effectiveReferer() const;
};

/**
 * @brief 在线源配置管理（sources.json，与 player_config.json 同目录）
 *
 * 文件格式：
 * {
 *   "_comment": "...",
 *   "sites": [ { "key": "...", "name": "...", "type": 1, "api": "...", ... } ]
 * }
 * 也接受直接写成裸数组，或粘贴整份 TVBox 订阅（多余字段忽略）。
 */
class SourceManager
{
public:
    static SourceManager &instance();

    void load();
    bool save();

    const QList<SourceSite> &sites() const { return m_sites; }
    QList<SourceSite> enabledSites() const;
    QList<SourceSite> searchableSites() const;

    void setSites(const QList<SourceSite> &sites);
    /** 按 key 合并：已存在的覆盖，不存在的新增；返回新增数量 */
    int mergeSites(const QList<SourceSite> &sites);
    void setEnabled(const QString &key, bool enabled);
    void removeSite(const QString &key);
    const SourceSite *findSite(const QString &key) const;

    QString configFilePath() const;

    /** 解析订阅 JSON（顶层对象含 sites，或直接是数组）；失败返回空并写 error */
    static QList<SourceSite> parseSubscription(const QByteArray &json, QString *error);

    /** 导入订阅；replaceExisting=true 表示全量替换。返回导入条数 */
    int importSubscription(const QByteArray &json, bool replaceExisting, QString *error);

private:
    SourceManager();

    QList<SourceSite> m_sites;
};
