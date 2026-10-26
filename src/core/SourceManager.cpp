#include "SourceManager.h"

#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUrl>

namespace {

const char *kDefaultUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

/** 兼容 TVBox 配置里 0/1 数字、"0"/"1" 字符串、true/false 三种写法 */
bool boolFlag(const QJsonObject &obj, const QString &key, bool defaultValue)
{
    if (!obj.contains(key)) {
        return defaultValue;
    }
    const QJsonValue v = obj.value(key);
    if (v.isBool()) {
        return v.toBool();
    }
    if (v.isDouble()) {
        return v.toInt() != 0;
    }
    if (v.isString()) {
        const QString s = v.toString().trimmed().toLower();
        return !(s == QStringLiteral("0") || s == QStringLiteral("false") || s == QStringLiteral("off"));
    }
    return defaultValue;
}

QString extToString(const QJsonValue &v)
{
    if (v.isString()) {
        return v.toString();
    }
    if (v.isObject()) {
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    }
    if (v.isArray()) {
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

/** key 归一化：TVBox 用 key 做唯一标识，缺失时按序号 + 前缀派生 */
QString normalizeKey(const QString &raw, int index, const QString &prefix = QStringLiteral("site"))
{
    QString key = raw.trimmed();
    if (key.isEmpty()) {
        return QStringLiteral("%1_%2").arg(prefix).arg(index);
    }
    return key;
}

/** 解析配置 JSON：直连失败时再试一次 base64（TVBox 订阅常把整份配置 base64 过） */
QJsonDocument parseConfigJson(const QByteArray &payload, QJsonParseError *outError)
{
    const QByteArray body = payload.trimmed();

    QJsonParseError error{};
    QJsonDocument doc = QJsonDocument::fromJson(body, &error);

    if (doc.isNull()) {
        QByteArray compact = body;
        compact.replace("\r", "").replace("\n", "").replace(" ", "");
        const QByteArray decoded = QByteArray::fromBase64(compact);
        if (!decoded.isEmpty()) {
            QJsonParseError innerError{};
            const QJsonDocument innerDoc = QJsonDocument::fromJson(decoded, &innerError);
            if (!innerDoc.isNull()) {
                doc = innerDoc;
                error = innerError;
            }
        }
    }

    if (outError) {
        *outError = error;
    }
    return doc;
}

/** 从配置里取数组：支持 root[key] 与 root.data[key] 两种包法 */
QJsonArray extractArray(const QJsonObject &root, const QString &key)
{
    if (root.value(key).isArray()) {
        return root.value(key).toArray();
    }
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    if (data.value(key).isArray()) {
        return data.value(key).toArray();
    }
    return QJsonArray();
}

} // namespace

// ============================================================================
// SourceSite
// ============================================================================

bool SourceSite::isCmsSupported() const
{
    if (type != 0 && type != 1) {
        return false;
    }
    const QString trimmed = api.trimmed();
    return trimmed.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

QString SourceSite::typeLabel() const
{
    if (!isCmsSupported()) {
        return QStringLiteral("不支持");
    }
    return type == 0 ? QStringLiteral("XML") : QStringLiteral("JSON");
}

QString SourceSite::effectiveUserAgent() const
{
    const QString ua = userAgent.trimmed();
    return ua.isEmpty() ? QString::fromLatin1(kDefaultUserAgent) : ua;
}

QString SourceSite::effectiveReferer() const
{
    const QString ref = referer.trimmed();
    if (!ref.isEmpty()) {
        return ref;
    }
    // 未显式指定时，用接口地址的 origin 作为 Referer —— 多数 CMS 站只校验站点域名，
    // 这条默认值能兜住大部分「403 防盗链」情况，又不需要用户手填。
    // 注意用 authority 而不是 host：自定义端口的站（:8080）带 host 会丢掉端口，Referer 直接对不上。
    const QUrl url(api.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1://%2/").arg(url.scheme(), url.authority());
}

// ============================================================================
// LiveSource
// ============================================================================

bool LiveSource::isRemote() const
{
    const QString u = url.trimmed();
    return u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

QString LiveSource::localPath() const
{
    const QString u = url.trimmed();
    if (u.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        return QUrl(u).toLocalFile();
    }
    return u;
}

QString LiveSource::typeLabel() const
{
    const QString probe = (isRemote() ? QUrl(url.trimmed()).path() : localPath()).toLower();
    if (probe.endsWith(QStringLiteral(".m3u")) || probe.endsWith(QStringLiteral(".m3u8"))) {
        return QStringLiteral("M3U");
    }
    if (probe.endsWith(QStringLiteral(".txt"))) {
        return QStringLiteral("TXT");
    }
    return isRemote() ? QStringLiteral("链接") : QStringLiteral("本地");
}

QString LiveSource::effectiveUserAgent() const
{
    const QString ua = userAgent.trimmed();
    return ua.isEmpty() ? QString::fromLatin1(kDefaultUserAgent) : ua;
}

QString LiveSource::effectiveReferer() const
{
    const QString ref = referer.trimmed();
    if (!ref.isEmpty()) {
        return ref;
    }
    if (!isRemote()) {
        return QString();
    }
    // 不少直播清单也做防盗链，默认带上清单所在域名（authority 才能保住端口）
    const QUrl target(this->url.trimmed());
    if (!target.isValid() || target.host().isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1://%2/").arg(target.scheme(), target.authority());
}

// ============================================================================
// SourceManager
// ============================================================================

SourceManager::SourceManager()
{
    load();
}

SourceManager &SourceManager::instance()
{
    static SourceManager inst;
    return inst;
}

QString SourceManager::configFilePath() const
{
    // 与 ConfigManager 同一套策略：绿色版就地存，安装版回退到 %APPDATA%
    return AppPaths::filePath(QStringLiteral("sources.json"));
}

void SourceManager::load()
{
    m_sites.clear();
    m_lives.clear();

    const QString path = configFilePath();
    QFile file(path);
    if (!file.exists()) {
        // 首次运行生成带注释的模板，方便用户直接手改
        QJsonObject root;
        root[QStringLiteral("_comment")] = QStringLiteral(
            "在线片源配置。sites 数组的字段与 TVBox/CatVod 订阅格式一致，"
            "支持 type=0(XML)/type=1(JSON) 的苹果 CMS 接口；lives 数组是直播源"
            "（m3u / txt 清单地址）。也可以把整份 TVBox 订阅 JSON 粘贴进「源配置」页导入。");
        root[QStringLiteral("sites")] = QJsonArray();
        root[QStringLiteral("lives")] = QJsonArray();
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        if (file.isOpen()) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
        }
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QString error;
    m_sites = parseSubscription(raw, &error);
    m_lives = parseLives(raw);
}

bool SourceManager::save()
{
    QJsonArray sitesArray;
    for (const SourceSite &site : m_sites) {
        QJsonObject obj;
        obj[QStringLiteral("key")]         = site.key;
        obj[QStringLiteral("name")]        = site.name;
        obj[QStringLiteral("type")]        = site.type;
        obj[QStringLiteral("api")]         = site.api;
        if (!site.ext.isEmpty()) {
            obj[QStringLiteral("ext")]     = site.ext;
        }
        obj[QStringLiteral("searchable")]  = site.searchable ? 1 : 0;
        obj[QStringLiteral("quickSearch")] = site.quickSearch ? 1 : 0;
        obj[QStringLiteral("enabled")]     = site.enabled;
        if (!site.userAgent.isEmpty()) {
            obj[QStringLiteral("userAgent")] = site.userAgent;
        }
        if (!site.referer.isEmpty()) {
            obj[QStringLiteral("referer")]   = site.referer;
        }
        obj[QStringLiteral("timeout")]     = site.timeoutSec;
        sitesArray.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("sites")] = sitesArray;

    QJsonArray livesArray;
    for (const LiveSource &live : m_lives) {
        QJsonObject obj;
        obj[QStringLiteral("key")]     = live.key;
        obj[QStringLiteral("name")]    = live.name;
        obj[QStringLiteral("url")]     = live.url;
        obj[QStringLiteral("enabled")] = live.enabled;
        if (!live.userAgent.isEmpty()) {
            obj[QStringLiteral("userAgent")] = live.userAgent;
        }
        if (!live.referer.isEmpty()) {
            obj[QStringLiteral("referer")]   = live.referer;
        }
        livesArray.append(obj);
    }
    root[QStringLiteral("lives")] = livesArray;

    QFile file(configFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QList<SourceSite> SourceManager::enabledSites() const
{
    QList<SourceSite> result;
    for (const SourceSite &site : m_sites) {
        if (site.enabled) {
            result.append(site);
        }
    }
    return result;
}

QList<SourceSite> SourceManager::searchableSites() const
{
    QList<SourceSite> result;
    for (const SourceSite &site : m_sites) {
        if (site.enabled && site.searchable && site.isCmsSupported()) {
            result.append(site);
        }
    }
    return result;
}

void SourceManager::setSites(const QList<SourceSite> &sites)
{
    m_sites = sites;
    save();
}

int SourceManager::mergeSites(const QList<SourceSite> &sites)
{
    int added = 0;
    for (const SourceSite &incoming : sites) {
        bool replaced = false;
        for (SourceSite &existing : m_sites) {
            if (existing.key == incoming.key) {
                existing = incoming;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_sites.append(incoming);
            ++added;
        }
    }
    if (!sites.isEmpty()) {
        save();
    }
    return added;
}

void SourceManager::setEnabled(const QString &key, bool enabled)
{
    for (SourceSite &site : m_sites) {
        if (site.key == key) {
            site.enabled = enabled;
            break;
        }
    }
    save();
}

void SourceManager::removeSite(const QString &key)
{
    for (int i = 0; i < m_sites.size(); ++i) {
        if (m_sites.at(i).key == key) {
            m_sites.removeAt(i);
            break;
        }
    }
    save();
}

const SourceSite *SourceManager::findSite(const QString &key) const
{
    for (const SourceSite &site : m_sites) {
        if (site.key == key) {
            return &site;
        }
    }
    return nullptr;
}

QList<LiveSource> SourceManager::enabledLives() const
{
    QList<LiveSource> result;
    for (const LiveSource &live : m_lives) {
        if (live.enabled) {
            result.append(live);
        }
    }
    return result;
}

int SourceManager::mergeLives(const QList<LiveSource> &lives)
{
    int added = 0;
    for (const LiveSource &incoming : lives) {
        bool replaced = false;
        for (LiveSource &existing : m_lives) {
            if (existing.key == incoming.key) {
                existing = incoming;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            m_lives.append(incoming);
            ++added;
        }
    }
    if (!lives.isEmpty()) {
        save();
    }
    return added;
}

void SourceManager::setLiveEnabled(const QString &key, bool enabled)
{
    for (LiveSource &live : m_lives) {
        if (live.key == key) {
            live.enabled = enabled;
            break;
        }
    }
    save();
}

void SourceManager::removeLive(const QString &key)
{
    for (int i = 0; i < m_lives.size(); ++i) {
        if (m_lives.at(i).key == key) {
            m_lives.removeAt(i);
            break;
        }
    }
    save();
}

const LiveSource *SourceManager::findLive(const QString &key) const
{
    for (const LiveSource &live : m_lives) {
        if (live.key == key) {
            return &live;
        }
    }
    return nullptr;
}

QList<SourceSite> SourceManager::parseSubscription(const QByteArray &json, QString *error,
                                                   ParseStats *stats)
{
    QList<SourceSite> result;
    ParseStats localStats;
    ParseStats &st = stats ? *stats : localStats;
    st = ParseStats();

    QJsonParseError parseError{};
    const QJsonDocument doc = parseConfigJson(json, &parseError);

    if (doc.isNull()) {
        if (error) {
            *error = QStringLiteral("JSON 解析失败：%1（偏移 %2）")
                         .arg(parseError.errorString())
                         .arg(parseError.offset);
        }
        return result;
    }

    QJsonArray sitesArray;
    if (doc.isArray()) {
        sitesArray = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        // 部分订阅把内容包在 data 里，两种都认
        const bool hasSites = root.value(QStringLiteral("sites")).isArray()
                              || root.value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("sites")).isArray();
        if (!hasSites) {
            if (error) {
                *error = QStringLiteral("没有找到 sites 数组，请确认这是 TVBox 订阅配置");
            }
            return result;
        }
        sitesArray = extractArray(root, QStringLiteral("sites"));
    }

    QSet<QString> usedKeys;
    int index = 0;
    for (const QJsonValue &value : sitesArray) {
        ++index;
        if (!value.isObject()) {
            continue;
        }
        ++st.totalSites;
        const QJsonObject obj = value.toObject();

        SourceSite site;
        site.name = obj.value(QStringLiteral("name")).toString().trimmed();
        site.key  = normalizeKey(obj.value(QStringLiteral("key")).toString(), index);
        site.api  = obj.value(QStringLiteral("api")).toString().trimmed();
        site.ext  = extToString(obj.value(QStringLiteral("ext")));
        site.type = obj.value(QStringLiteral("type")).toVariant().toInt();

        if (site.name.isEmpty() && site.api.isEmpty()) {
            continue;
        }
        if (site.name.isEmpty()) {
            site.name = site.key;
        }

        // 不带协议的 api 一般是 csp_xxx / 本地 js 之类的脚本源，本播放器没有脚本运行时
        if (!site.isCmsSupported()) {
            if (site.type == 3) {
                ++st.spiderSkipped;
            } else {
                ++st.otherSkipped;
            }
            continue;
        }
        ++st.imported;

        site.searchable  = boolFlag(obj, QStringLiteral("searchable"), true);
        site.quickSearch = boolFlag(obj, QStringLiteral("quickSearch"), true);
        site.enabled     = boolFlag(obj, QStringLiteral("enabled"), true);
        site.userAgent   = obj.value(QStringLiteral("userAgent")).toString().trimmed();
        site.referer     = obj.value(QStringLiteral("referer")).toString().trimmed();
        const int timeout = obj.value(QStringLiteral("timeout")).toInt(0);
        if (timeout > 0) {
            site.timeoutSec = qBound(3, timeout, 60);
        }

        // key 去重：同名站点加后缀，避免搜索结果串源
        QString uniqueKey = site.key;
        int suffix = 1;
        while (usedKeys.contains(uniqueKey)) {
            uniqueKey = QStringLiteral("%1_%2").arg(site.key).arg(++suffix);
        }
        site.key = uniqueKey;
        usedKeys.insert(uniqueKey);

        result.append(site);
    }

    if (result.isEmpty() && error && error->isEmpty()) {
        // 把统计讲清楚：用户才知道是配置里压根没有 type=0/1 接口，不是软件坏了
        QStringList lines;
        lines << QStringLiteral("该配置共有 %1 个源，没有一个是可用的 type=0/1 苹果 CMS 接口：")
                     .arg(st.totalSites);
        if (st.spiderSkipped > 0) {
            lines << QStringLiteral("· %1 个 type=3 Spider 源（csp_* 脚本，需要 TVBox 的 jar 运行时，本播放器未内置）")
                         .arg(st.spiderSkipped);
        }
        if (st.otherSkipped > 0) {
            lines << QStringLiteral("· %1 个其他不支持形态的源").arg(st.otherSkipped);
        }
        lines << QStringLiteral("建议换用提供 type=0（XML）/ type=1（JSON）接口的订阅配置。");
        *error = lines.join(QLatin1Char('\n'));
    }
    return result;
}

int SourceManager::importSubscription(const QByteArray &json, bool replaceExisting, QString *error,
                                      ParseStats *stats)
{
    const QList<SourceSite> parsed = parseSubscription(json, error, stats);
    if (parsed.isEmpty()) {
        return 0;
    }
    if (replaceExisting) {
        m_sites = parsed;
        save();
    } else {
        mergeSites(parsed);
    }
    return parsed.size();
}

QList<LiveSource> SourceManager::parseLives(const QByteArray &json, int *skipped)
{
    QList<LiveSource> result;
    if (skipped) {
        *skipped = 0;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = parseConfigJson(json, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        return result;
    }

    const QJsonArray livesArray = extractArray(doc.object(), QStringLiteral("lives"));
    if (livesArray.isEmpty()) {
        return result;
    }

    QSet<QString> usedKeys;
    int index = 0;
    for (const QJsonValue &value : livesArray) {
        ++index;
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();

        LiveSource live;
        live.name = obj.value(QStringLiteral("name")).toString().trimmed();
        live.url  = obj.value(QStringLiteral("url")).toString().trimmed();
        if (live.url.isEmpty()) {
            // 少数配置把清单地址写在 api 里；csp_xxx / js 这类脚本形态没有地址，下面会被跳过
            live.url = obj.value(QStringLiteral("api")).toString().trimmed();
        }
        if (live.name.isEmpty() && live.url.isEmpty()) {
            continue;
        }

        // 只收有清单地址的：脚本形态（csp_*）本播放器不跑脚本
        if (!live.isRemote() && !QFileInfo::exists(live.localPath())) {
            if (skipped) {
                ++*skipped;
            }
            continue;
        }

        live.key = normalizeKey(obj.value(QStringLiteral("key")).toString(), index,
                                QStringLiteral("live"));
        if (live.name.isEmpty()) {
            live.name = live.key;
        }
        live.enabled   = boolFlag(obj, QStringLiteral("enabled"), true);
        live.userAgent = obj.value(QStringLiteral("ua")).toString().trimmed();
        if (live.userAgent.isEmpty()) {
            live.userAgent = obj.value(QStringLiteral("userAgent")).toString().trimmed();
        }
        live.referer = obj.value(QStringLiteral("referer")).toString().trimmed();

        // 直播源的 key 与站点 key 各自独立，但同一份配置内要去重
        QString uniqueKey = live.key;
        int suffix = 1;
        while (usedKeys.contains(uniqueKey)) {
            uniqueKey = QStringLiteral("%1_%2").arg(live.key).arg(++suffix);
        }
        live.key = uniqueKey;
        usedKeys.insert(uniqueKey);

        result.append(live);
    }

    return result;
}

ConfigImportResult SourceManager::importConfig(const QByteArray &json, bool replaceExisting)
{
    ConfigImportResult result;

    // 先看配置里到底出现过哪些字段：全空时才能给出「这压根不是 TVBox 配置」的判断
    QJsonParseError parseError{};
    const QJsonDocument doc = parseConfigJson(json, &parseError);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        result.hasSites = root.value(QStringLiteral("sites")).isArray()
                          || data.value(QStringLiteral("sites")).isArray();
        result.hasLives = root.value(QStringLiteral("lives")).isArray()
                          || data.value(QStringLiteral("lives")).isArray();
    }

    QString siteError;
    const QList<SourceSite> sites = parseSubscription(json, &siteError, &result.siteStats);
    const QList<LiveSource> lives = parseLives(json, &result.livesSkipped);

    result.sitesImported = sites.size();
    result.livesImported = lives.size();

    if (replaceExisting) {
        if (!sites.isEmpty()) {
            m_sites = sites;
        }
        if (!lives.isEmpty()) {
            m_lives = lives;
        }
        if (!sites.isEmpty() || !lives.isEmpty()) {
            save();
        }
    } else {
        mergeSites(sites);
        mergeLives(lives);
    }

    if (result.total() == 0) {
        if (!siteError.isEmpty()) {
            result.error = siteError;
        } else if (result.hasLives && result.livesSkipped > 0) {
            result.error = QStringLiteral(
                "这份配置有 %1 个直播源，但都是脚本（api）形态，没有可直接拉取的 m3u / txt 清单地址。")
                               .arg(result.livesSkipped);
        } else {
            result.error = QStringLiteral("没有解析到可用的 sites / lives 配置");
        }
    }
    return result;
}
