#pragma once

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

#include "SourceManager.h"

class QNetworkAccessManager;
class QNetworkReply;

/** 搜索结果条目 / 详情页共用的一部影片 */
struct VodItem
{
    QString vodId;
    QString name;
    QString pic;
    QString remarks;
    QString year;
    QString area;
    QString typeName;
    QString sourceKey;      // 来源站点 key
    QString sourceName;     // 来源站点显示名
};

/** 单集（剧集 / 正片） */
struct VodEpisode
{
    QString name;
    QString url;
};

/** 一条播放线路（同名剧集可能有多条不同清晰度/来源的线路） */
struct VodPlayLine
{
    QString flag;                   // 线路标识，如 "ffm3u8"
    QList<VodEpisode> episodes;
};

/** 详情页数据 */
struct VodDetail
{
    QString vodId;
    QString name;
    QString pic;
    QString content;
    QString remarks;
    QString year;
    QString area;
    QString actor;
    QString director;
    QString sourceKey;
    QString sourceName;
    QList<VodPlayLine> lines;
};

/** 交给播放器的在线播放请求（含整条线路的剧集，用于自动连播） */
struct OnlinePlayRequest
{
    QString title;                  // 剧名
    QString sourceName;
    QString userAgent;
    QString referer;
    QList<VodEpisode> episodes;
    int startIndex = 0;

    bool isValid() const
    {
        return !episodes.isEmpty() && startIndex >= 0 && startIndex < episodes.size();
    }
    QString currentUrl() const
    {
        return isValid() ? episodes.at(startIndex).url : QString();
    }
    QString currentEpisodeName() const
    {
        return isValid() ? episodes.at(startIndex).name : QString();
    }
};

Q_DECLARE_METATYPE(VodItem)
Q_DECLARE_METATYPE(VodEpisode)
Q_DECLARE_METATYPE(VodPlayLine)
Q_DECLARE_METATYPE(VodDetail)
Q_DECLARE_METATYPE(OnlinePlayRequest)

/**
 * @brief 苹果 CMS（type 0 XML / type 1 JSON）接口客户端
 *
 * 只负责三件事：搜索、取详情、把 vod_play_url 拆成线路与剧集。
 * 不做脚本 Spider（jar/js/py），也不做网页嗅探 —— 那些需要额外的运行时。
 */
class CmsClient : public QObject
{
    Q_OBJECT
public:
    explicit CmsClient(QObject *parent = nullptr);
    ~CmsClient() override;

    void search(const SourceSite &site, const QString &keyword);
    void requestDetail(const SourceSite &site, const QString &vodId);

    void abortAll();
    bool isBusy() const { return m_pendingCount > 0; }

signals:
    void searchFinished(const QString &siteKey, const QList<VodItem> &items, const QString &error);
    void detailFinished(const QString &siteKey, const VodDetail &detail, const QString &error);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    enum RequestKind {
        KindSearch = 0,
        KindDetail = 1
    };

    void sendRequest(const SourceSite &site,
                     const QList<QPair<QString, QString>> &params,
                     RequestKind kind,
                     const QString &keyword,
                     const QString &vodId,
                     int attempt);

    static QUrl buildApiUrl(const SourceSite &site, const QList<QPair<QString, QString>> &params);

    QNetworkAccessManager *m_nam = nullptr;
    QHash<QString, SourceSite> m_sitesByKey;
    int m_pendingCount = 0;
};
