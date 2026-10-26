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
 * @brief 单个直播源
 *
 * 对应 TVBox / CatVod 配置里的 lives 数组条目：一个源就是一个频道清单
 * （m3u / txt），拉下来才有频道列表，所以这里只存「清单在哪」。
 *
 * 只接受「有地址」的形态：url 为 http(s) 地址或本地文件。
 * lives 里靠 api（csp_xxx 脚本）出频道的条目需要脚本运行时，导入时跳过。
 */
struct LiveSource
{
    QString key;        // 唯一标识（缺省时由序号派生）
    QString name;       // 显示名称，如「IPTV 合集」
    QString url;        // m3u / txt 地址，或本地文件路径
    QString userAgent;  // 留空则用内置浏览器 UA
    QString referer;    // 留空则用清单地址的 origin
    bool    enabled = true;

    /** 是否为 http(s) 远程清单（否则按本地文件处理） */
    bool isRemote() const;
    /** 本地文件路径（file:// 会转成本地路径） */
    QString localPath() const;
    /** 展示用类型标签：M3U / TXT / 本地 */
    QString typeLabel() const;
    QString effectiveUserAgent() const;
    QString effectiveReferer() const;
};

/** 订阅解析统计：让界面能把「为什么没导入 / 少导入了」讲清楚 */
struct ParseStats
{
    int totalSites = 0;      // sites 数组里的对象条目数
    int spiderSkipped = 0;   // type=3（csp_*/js/py，需要 jar 等运行时）
    int otherSkipped = 0;    // 其他不支持的形态（type 非 0/1/3、api 无效等）
    int imported = 0;        // 解析为可用 CMS 接口的条数
    bool isEmpty() const { return totalSites == 0; }
};

/**
 * @brief 一次配置导入的结果
 *
 * TVBox 配置里点播源（sites）和直播源（lives）混在一起，导入要一次拿全，
 * 否则用户得点两遍、还容易以为「少导入了一半」。
 */
struct ConfigImportResult
{
    int sitesImported = 0;
    int livesImported = 0;
    int livesSkipped = 0;    // 没有可用清单地址的直播源（脚本 api / 本机回环地址）
    bool hasSites = false;   // 配置里是否出现过 sites 数组
    bool hasLives = false;   // 配置里是否出现过 lives 数组
    ParseStats siteStats;
    QString error;           // 两条都没导入成功时的原因

    int total() const { return sitesImported + livesImported; }
};

/**
 * @brief 在线源配置管理（sources.json，与 player_config.json 同目录）
 *
 * 文件格式：
 * {
 *   "_comment": "...",
 *   "sites": [ { "key": "...", "name": "...", "type": 1, "api": "...", ... } ],
 *   "lives": [ { "key": "...", "name": "...", "url": "...m3u", "enabled": true } ]
 * }
 * 也接受直接写成裸数组，或粘贴整份 TVBox 订阅（sites + lives 一起导入，多余字段忽略）。
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

    // --- 直播源（lives）---
    const QList<LiveSource> &lives() const { return m_lives; }
    QList<LiveSource> enabledLives() const;
    /** 按 key 合并直播源；返回新增数量 */
    int mergeLives(const QList<LiveSource> &lives);
    void setLiveEnabled(const QString &key, bool enabled);
    void removeLive(const QString &key);
    const LiveSource *findLive(const QString &key) const;

    QString configFilePath() const;

    /** 解析订阅 JSON（顶层对象含 sites，或直接是数组）；失败返回空并写 error。
     *  stats 可选：回填跳过/导入统计，供界面提示「为什么没导入」。 */
    static QList<SourceSite> parseSubscription(const QByteArray &json, QString *error,
                                               ParseStats *stats = nullptr);

    /** 从同一份配置里解析直播源；skipped 回填「没有可用清单地址」的条数 */
    static QList<LiveSource> parseLives(const QByteArray &json, int *skipped = nullptr);

    /** 导入订阅；replaceExisting=true 表示全量替换。返回导入条数 */
    int importSubscription(const QByteArray &json, bool replaceExisting, QString *error,
                           ParseStats *stats = nullptr);

    /** 一站式导入：同一份配置里能用的点播源和直播源一起收下 */
    ConfigImportResult importConfig(const QByteArray &json, bool replaceExisting);

private:
    SourceManager();

    QList<SourceSite> m_sites;
    QList<LiveSource> m_lives;
};
