#include "SourceManager.h"

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

/** 站点 key 归一化：TVBox 用 key 做唯一标识，缺失时由名称派生 */
QString normalizeKey(const QString &raw, int index)
{
    QString key = raw.trimmed();
    if (key.isEmpty()) {
        return QStringLiteral("site_%1").arg(index);
    }
    return key;
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
    const QUrl url(api.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        return QString();
    }
    return QStringLiteral("%1://%2/").arg(url.scheme(), url.host());
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
    QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty()) {
        appDir = QDir::currentPath();
    }
    return QDir(appDir).filePath(QStringLiteral("sources.json"));
}

void SourceManager::load()
{
    m_sites.clear();

    const QString path = configFilePath();
    QFile file(path);
    if (!file.exists()) {
        // 首次运行生成带注释的模板，方便用户直接手改
        QJsonObject root;
        root[QStringLiteral("_comment")] = QStringLiteral(
            "在线片源配置。sites 数组的字段与 TVBox/CatVod 订阅格式一致，"
            "支持 type=0(XML)/type=1(JSON) 的苹果 CMS 接口。"
            "也可以把整份 TVBox 订阅 JSON 粘贴进「源配置」页导入。");
        root[QStringLiteral("sites")] = QJsonArray();
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

QList<SourceSite> SourceManager::parseSubscription(const QByteArray &json, QString *error)
{
    QList<SourceSite> result;
    QByteArray payload = json.trimmed();

    QJsonParseError parseError{};
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);

    // TVBox 订阅地址常返回 base64 编码的配置，这里顺手兜一层
    if (doc.isNull()) {
        QByteArray compact = payload;
        compact.replace("\r", "").replace("\n", "").replace(" ", "");
        const QByteArray decoded = QByteArray::fromBase64(compact);
        if (!decoded.isEmpty()) {
            QJsonParseError innerError{};
            const QJsonDocument innerDoc = QJsonDocument::fromJson(decoded, &innerError);
            if (!innerDoc.isNull()) {
                doc = innerDoc;
                parseError = innerError;
            }
        }
    }

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
        if (root.value(QStringLiteral("sites")).isArray()) {
            sitesArray = root.value(QStringLiteral("sites")).toArray();
        } else if (root.value(QStringLiteral("data")).isObject()
                   && root.value(QStringLiteral("data")).toObject().value(QStringLiteral("sites")).isArray()) {
            // 部分订阅把内容包在 data 里
            sitesArray = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("sites")).toArray();
        } else {
            if (error) {
                *error = QStringLiteral("没有找到 sites 数组，请确认这是 TVBox 订阅配置");
            }
            return result;
        }
    }

    QSet<QString> usedKeys;
    int index = 0;
    for (const QJsonValue &value : sitesArray) {
        ++index;
        if (!value.isObject()) {
            continue;
        }
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
            continue;
        }

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
        *error = QStringLiteral("配置里没有可用的接口源（只支持 type=0/1 的苹果 CMS 接口，"
                                "jar/js/py 脚本 Spider 源需要额外运行时，本播放器未内置）");
    }
    return result;
}

int SourceManager::importSubscription(const QByteArray &json, bool replaceExisting, QString *error)
{
    const QList<SourceSite> parsed = parseSubscription(json, error);
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
