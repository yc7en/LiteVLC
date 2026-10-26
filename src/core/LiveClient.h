#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "LiveParser.h"
#include "SourceManager.h"

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief 直播源拉取器
 *
 * 一个源一次请求：拿回 m3u / txt 文本，交给 LiveParser 解析成频道列表。
 * 只负责「取文本 + 解析」，不缓存、不重试 —— 换台是高频操作，重试留给用户手动刷新。
 *
 * 与 CmsClient 的分工一致：网络与解析在 core，界面只消费信号。
 */
class LiveClient : public QObject
{
    Q_OBJECT
public:
    explicit LiveClient(QObject *parent = nullptr);

    /** 拉取一个直播源；本地文件（file:// 或磁盘路径）直接读盘 */
    void fetch(const LiveSource &source);

    /** 中断全部在途请求（关窗时用），被中断的源不会再发信号 */
    void abortAll();

    bool isBusy() const { return m_pending > 0; }

signals:
    /** error 为空表示成功；失败时 channels 为空 */
    void channelsFetched(const QString &sourceKey, const QList<LiveChannel> &channels, const QString &error);

private:
    void deliver(const QString &sourceKey, const QByteArray &body);

    QNetworkAccessManager *m_nam = nullptr;
    QHash<QNetworkReply *, QString> m_inFlight;   // reply → sourceKey
    int m_pending = 0;
};
