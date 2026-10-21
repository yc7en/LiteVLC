#include "CmsClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringConverter>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {

/** 把响应体统一转成 UTF-8：多数 CMS 是 UTF-8，老站可能是 GBK（中文 Windows 的系统代码页） */
QByteArray ensureUtf8(const QByteArray &raw)
{
    const QString asUtf8 = QString::fromUtf8(raw);
    if (!asUtf8.contains(QChar(0xFFFD))) {
        return raw; // 本来就是合法 UTF-8
    }
    QStringDecoder fallback(QStringConverter::System);
    const QString decoded = fallback(raw);
    return decoded.toUtf8();
}

QString stripHtml(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"),  QStringLiteral("'"));
    return text.trimmed();
}

/** "第1集$http://...#第2集$http://..." → 剧集列表 */
QList<VodEpisode> parseEpisodes(const QString &text)
{
    QList<VodEpisode> episodes;
    if (text.trimmed().isEmpty()) {
        return episodes;
    }

    const QStringList parts = text.split(QLatin1Char('#'), Qt::SkipEmptyParts);
    int index = 0;
    for (const QString &part : parts) {
        ++index;
        const QString item = part.trimmed();
        if (item.isEmpty()) {
            continue;
        }

        QString name;
        QString url;
        const int dollar = item.indexOf(QLatin1Char('$'));
        if (dollar > 0) {
            name = item.left(dollar).trimmed();
            url  = item.mid(dollar + 1).trimmed();
        } else {
            url = item;
        }

        // 少数源会把整条线路写成 JSON 片段，跳过非 URL 项
        if (!url.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
            continue;
        }
        if (name.isEmpty()) {
            name = QStringLiteral("第%1集").arg(index);
        }
        episodes.append({name, url});
    }
    return episodes;
}

VodPlayLine makeLine(const QString &flag, const QString &rawText, int index)
{
    VodPlayLine line;
    line.flag = flag.trimmed().isEmpty() ? QStringLiteral("线路%1").arg(index) : flag.trimmed();
    line.episodes = parseEpisodes(rawText);
    return line;
}

/** vod_play_from / vod_play_url 按 $$$ 拆成多条线路 */
QList<VodPlayLine> parsePlayLines(const QString &from, const QString &urls)
{
    QList<VodPlayLine> lines;
    if (urls.trimmed().isEmpty()) {
        return lines;
    }
    const QStringList flags = from.split(QStringLiteral("$$$"));
    const QStringList urlGroups = urls.split(QStringLiteral("$$$"));

    for (int i = 0; i < urlGroups.size(); ++i) {
        VodPlayLine line = makeLine(flags.value(i), urlGroups.at(i), i + 1);
        if (!line.episodes.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

QString jsonVodId(const QJsonValue &v)
{
    if (v.isString()) {
        return v.toString();
    }
    if (v.isDouble()) {
        return QString::number(static_cast<qint64>(v.toDouble()));
    }
    return QString();
}

bool looksLikeHtmlPage(const QByteArray &body)
{
    const QByteArray head = body.left(1024).toLower();
    return head.contains("<!doctype html") || head.contains("<html");
}

// ---------------------------------------------------------------------------
// XML（苹果 CMS type=0）
// ---------------------------------------------------------------------------

struct XmlVideo
{
    QString id, name, pic, remarks, year, area, content, actor, director;
    QList<VodPlayLine> lines;
};

QList<XmlVideo> parseXmlVideos(const QString &xml)
{
    QList<XmlVideo> result;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("video")) {
            continue;
        }

        XmlVideo video;
        QString playFrom;
        QString playUrl;

        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isEndElement() && reader.name() == QLatin1String("video")) {
                break;
            }
            if (!reader.isStartElement()) {
                continue;
            }

            const auto tag = reader.name();
            if (tag == QLatin1String("dl")) {
                int lineIndex = 0;
                while (!reader.atEnd()) {
                    reader.readNext();
                    if (reader.isEndElement() && reader.name() == QLatin1String("dl")) {
                        break;
                    }
                    if (reader.isStartElement() && reader.name() == QLatin1String("dd")) {
                        ++lineIndex;
                        const QString flag = reader.attributes().value(QStringLiteral("flag")).toString();
                        const QString text = reader.readElementText();
                        VodPlayLine line = makeLine(flag, text, lineIndex);
                        if (!line.episodes.isEmpty()) {
                            video.lines.append(line);
                        }
                    }
                }
            } else if (tag == QLatin1String("play_from") || tag == QLatin1String("vod_play_from")) {
                playFrom = reader.readElementText();
            } else if (tag == QLatin1String("play_url") || tag == QLatin1String("vod_play_url")) {
                playUrl = reader.readElementText();
            } else {
                const QString text = reader.readElementText();
                if (tag == QLatin1String("id")) {
                    video.id = text.trimmed();
                } else if (tag == QLatin1String("name")) {
                    video.name = text.trimmed();
                } else if (tag == QLatin1String("pic")) {
                    video.pic = text.trimmed();
                } else if (tag == QLatin1String("remarks")) {
                    video.remarks = text.trimmed();
                } else if (tag == QLatin1String("year")) {
                    video.year = text.trimmed();
                } else if (tag == QLatin1String("area")) {
                    video.area = text.trimmed();
                } else if (tag == QLatin1String("actor")) {
                    video.actor = text.trimmed();
                } else if (tag == QLatin1String("director")) {
                    video.director = text.trimmed();
                } else if (tag == QLatin1String("des") || tag == QLatin1String("content")
                           || tag == QLatin1String("vod_content")) {
                    video.content = text.trimmed();
                }
            }
        }

        if (video.lines.isEmpty() && !playUrl.isEmpty()) {
            video.lines = parsePlayLines(playFrom, playUrl);
        }
        if (video.id.isEmpty()) {
            video.id = video.name;
        }
        if (!video.name.isEmpty() || !video.id.isEmpty()) {
            result.append(video);
        }
    }
    return result;
}

VodItem toVodItem(const XmlVideo &video, const SourceSite &site)
{
    VodItem item;
    item.vodId = video.id;
    item.name = video.name;
    item.pic = video.pic;
    item.remarks = video.remarks;
    item.year = video.year;
    item.area = video.area;
    item.sourceKey = site.key;
    item.sourceName = site.name;
    return item;
}

// ---------------------------------------------------------------------------
// JSON（苹果 CMS type=1）
// ---------------------------------------------------------------------------

QJsonArray listArrayOf(const QJsonObject &root, bool *found)
{
    if (found) {
        *found = false;
    }
    if (root.value(QStringLiteral("list")).isArray()) {
        if (found) {
            *found = true;
        }
        return root.value(QStringLiteral("list")).toArray();
    }
    if (root.value(QStringLiteral("data")).isArray()) {
        if (found) {
            *found = true;
        }
        return root.value(QStringLiteral("data")).toArray();
    }
    if (root.value(QStringLiteral("data")).isObject()) {
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        if (data.value(QStringLiteral("list")).isArray()) {
            if (found) {
                *found = true;
            }
            return data.value(QStringLiteral("list")).toArray();
        }
    }
    return QJsonArray();
}

VodItem itemFromJsonObject(const QJsonObject &obj, const SourceSite &site)
{
    VodItem item;
    item.vodId      = jsonVodId(obj.value(QStringLiteral("vod_id")));
    item.name       = obj.value(QStringLiteral("vod_name")).toString().trimmed();
    item.pic        = obj.value(QStringLiteral("vod_pic")).toString().trimmed();
    item.remarks    = obj.value(QStringLiteral("vod_remarks")).toString().trimmed();
    item.year       = obj.value(QStringLiteral("vod_year")).toString().trimmed();
    item.area       = obj.value(QStringLiteral("vod_area")).toString().trimmed();
    item.typeName   = obj.value(QStringLiteral("type_name")).toString().trimmed();
    item.sourceKey  = site.key;
    item.sourceName = site.name;
    if (item.vodId.isEmpty()) {
        item.vodId = item.name;
    }
    return item;
}

VodDetail detailFromJsonObject(const QJsonObject &obj, const SourceSite &site, const QString &fallbackId)
{
    VodDetail detail;
    detail.vodId      = jsonVodId(obj.value(QStringLiteral("vod_id")));
    if (detail.vodId.isEmpty()) {
        detail.vodId = fallbackId;
    }
    detail.name       = obj.value(QStringLiteral("vod_name")).toString().trimmed();
    detail.pic        = obj.value(QStringLiteral("vod_pic")).toString().trimmed();
    detail.remarks    = obj.value(QStringLiteral("vod_remarks")).toString().trimmed();
    detail.year       = obj.value(QStringLiteral("vod_year")).toString().trimmed();
    detail.area       = obj.value(QStringLiteral("vod_area")).toString().trimmed();
    detail.actor      = obj.value(QStringLiteral("vod_actor")).toString().trimmed();
    detail.director   = obj.value(QStringLiteral("vod_director")).toString().trimmed();
    detail.content    = stripHtml(obj.value(QStringLiteral("vod_content")).toString());
    detail.sourceKey  = site.key;
    detail.sourceName = site.name;

    const QString playFrom = obj.value(QStringLiteral("vod_play_from")).toString();
    const QString playUrl  = obj.value(QStringLiteral("vod_play_url")).toString();
    detail.lines = parsePlayLines(playFrom, playUrl);
    return detail;
}

} // namespace

// ============================================================================

CmsClient::CmsClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    qRegisterMetaType<VodItem>("VodItem");
    qRegisterMetaType<VodEpisode>("VodEpisode");
    qRegisterMetaType<VodPlayLine>("VodPlayLine");
    qRegisterMetaType<VodDetail>("VodDetail");
    qRegisterMetaType<OnlinePlayRequest>("OnlinePlayRequest");
    qRegisterMetaType<QList<VodItem>>("QList<VodItem>");
}

CmsClient::~CmsClient() = default;

QUrl CmsClient::buildApiUrl(const SourceSite &site, const QList<QPair<QString, QString>> &params)
{
    QUrl url(site.api.trimmed());
    if (!url.isValid()) {
        return QUrl();
    }

    // 站点自带的默认参数（常见 api 结尾是 ?ac=list）先清掉同名项，避免 ac 重复导致接口只认第一个
    QUrlQuery query(url);
    const QStringList replaceable = {
        QStringLiteral("ac"), QStringLiteral("wd"), QStringLiteral("ids"),
        QStringLiteral("pg"), QStringLiteral("at"), QStringLiteral("t")
    };
    for (const QString &name : replaceable) {
        query.removeAllQueryItems(name);
    }
    for (const auto &param : params) {
        query.addQueryItem(param.first, param.second);
    }
    url.setQuery(query);
    return url;
}

void CmsClient::search(const SourceSite &site, const QString &keyword)
{
    m_sitesByKey.insert(site.key, site);
    const QList<QPair<QString, QString>> params = {
        {QStringLiteral("ac"), QStringLiteral("videolist")},
        {QStringLiteral("wd"), keyword},
        {QStringLiteral("pg"), QStringLiteral("1")}
    };
    sendRequest(site, params, KindSearch, keyword, QString(), 0);
}

void CmsClient::requestDetail(const SourceSite &site, const QString &vodId)
{
    m_sitesByKey.insert(site.key, site);
    const QList<QPair<QString, QString>> params = {
        {QStringLiteral("ac"), QStringLiteral("videolist")},
        {QStringLiteral("ids"), vodId}
    };
    sendRequest(site, params, KindDetail, QString(), vodId, 0);
}

void CmsClient::sendRequest(const SourceSite &site,
                            const QList<QPair<QString, QString>> &params,
                            RequestKind kind,
                            const QString &keyword,
                            const QString &vodId,
                            int attempt)
{
    const QUrl url = buildApiUrl(site, params);
    if (!url.isValid()) {
        const QString error = QStringLiteral("接口地址无效：%1").arg(site.api);
        if (kind == KindDetail) {
            emit detailFinished(site.key, VodDetail(), error);
        } else {
            emit searchFinished(site.key, QList<VodItem>(), error);
        }
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, site.effectiveUserAgent());
    request.setRawHeader("Accept", "application/json, text/xml, application/xml, */*");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    const QString referer = site.effectiveReferer();
    if (!referer.isEmpty()) {
        request.setRawHeader("Referer", referer.toUtf8());
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(site.timeoutSec * 1000);

    QNetworkReply *reply = m_nam->get(request);
    reply->setProperty("siteKey", site.key);
    reply->setProperty("kind", static_cast<int>(kind));
    reply->setProperty("attempt", attempt);
    reply->setProperty("keyword", keyword);
    reply->setProperty("vodId", vodId);

    ++m_pendingCount;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
}

void CmsClient::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    m_pendingCount = qMax(0, m_pendingCount - 1);

    const QString siteKey = reply->property("siteKey").toString();
    const auto kind = static_cast<RequestKind>(reply->property("kind").toInt());
    const int attempt = reply->property("attempt").toInt();
    const QString keyword = reply->property("keyword").toString();
    const QString vodId = reply->property("vodId").toString();
    const SourceSite site = m_sitesByKey.value(siteKey);

    auto fail = [&](const QString &error) {
        if (kind == KindDetail) {
            emit detailFinished(siteKey, VodDetail(), error);
        } else {
            emit searchFinished(siteKey, QList<VodItem>(), error);
        }
    };

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = reply->errorString();
        if (status == 403) {
            error = QStringLiteral("HTTP 403 拒绝访问（源站可能有防盗链或需要代理）");
        } else if (status > 0) {
            error = QStringLiteral("HTTP %1：%2").arg(status).arg(error);
        }
        fail(error);
        return;
    }

    const QByteArray body = ensureUtf8(reply->readAll());
    if (body.trimmed().isEmpty()) {
        fail(QStringLiteral("接口返回了空内容"));
        return;
    }
    if (looksLikeHtmlPage(body)) {
        fail(QStringLiteral("接口返回的是网页而不是数据（接口地址可能已失效）"));
        return;
    }

    const bool isXml = body.trimmed().startsWith('<');

    if (kind == KindSearch) {
        QList<VodItem> items;

        if (isXml) {
            const QList<XmlVideo> videos = parseXmlVideos(QString::fromUtf8(body));
            for (const XmlVideo &video : videos) {
                items.append(toVodItem(video, site));
            }
        } else {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
            if (doc.isObject()) {
                bool found = false;
                const QJsonArray list = listArrayOf(doc.object(), &found);
                for (const QJsonValue &value : list) {
                    if (!value.isObject()) {
                        continue;
                    }
                    const VodItem item = itemFromJsonObject(value.toObject(), site);
                    if (!item.name.isEmpty() || !item.vodId.isEmpty()) {
                        items.append(item);
                    }
                }
                if (!found) {
                    fail(QStringLiteral("接口返回的 JSON 里没有 list 字段（可能不是苹果 CMS 接口）"));
                    return;
                }
            } else if (!doc.isNull()) {
                fail(QStringLiteral("接口返回的 JSON 结构不是对象"));
                return;
            } else {
                fail(QStringLiteral("接口返回内容无法解析：%1").arg(parseError.errorString()));
                return;
            }
        }

        // 少数 CMS 只认 ac=detail，videolist 拿不到东西时换一种再试一次
        if (items.isEmpty() && attempt == 0) {
            const QList<QPair<QString, QString>> params = {
                {QStringLiteral("ac"), QStringLiteral("detail")},
                {QStringLiteral("wd"), keyword},
                {QStringLiteral("pg"), QStringLiteral("1")}
            };
            sendRequest(site, params, KindSearch, keyword, QString(), 1);
            return;
        }

        emit searchFinished(siteKey, items,
                            items.isEmpty() ? QStringLiteral("没有搜索到结果") : QString());
        return;
    }

    // ---- 详情 ----
    VodDetail detail;
    if (isXml) {
        const QList<XmlVideo> videos = parseXmlVideos(QString::fromUtf8(body));
        if (!videos.isEmpty()) {
            const XmlVideo &video = videos.first();
            detail.vodId      = video.id.isEmpty() ? vodId : video.id;
            detail.name       = video.name;
            detail.pic        = video.pic;
            detail.remarks    = video.remarks;
            detail.year       = video.year;
            detail.area       = video.area;
            detail.actor      = video.actor;
            detail.director   = video.director;
            detail.content    = stripHtml(video.content);
            detail.lines      = video.lines;
            detail.sourceKey  = site.key;
            detail.sourceName = site.name;
        }
    } else {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (doc.isObject()) {
            bool found = false;
            const QJsonArray list = listArrayOf(doc.object(), &found);
            if (!list.isEmpty() && list.first().isObject()) {
                detail = detailFromJsonObject(list.first().toObject(), site, vodId);
            }
        } else {
            fail(QStringLiteral("详情返回内容无法解析：%1").arg(parseError.errorString()));
            return;
        }
    }

    if (detail.lines.isEmpty() && attempt == 0) {
        const QList<QPair<QString, QString>> params = {
            {QStringLiteral("ac"), QStringLiteral("detail")},
            {QStringLiteral("ids"), vodId}
        };
        sendRequest(site, params, KindDetail, QString(), vodId, 1);
        return;
    }

    if (detail.name.isEmpty() && detail.lines.isEmpty()) {
        fail(QStringLiteral("没有取到详情数据（该源可能已失效）"));
        return;
    }

    emit detailFinished(siteKey, detail, QString());
}

void CmsClient::abortAll()
{
    const QList<QNetworkReply *> replies = m_nam->findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
        reply->abort();
    }
    m_pendingCount = 0;
}
