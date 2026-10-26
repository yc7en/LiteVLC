#include "LiveClient.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

LiveClient::LiveClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void LiveClient::fetch(const LiveSource &source)
{
    const QString key = source.key;

    // 本地文件（file:// 或磁盘路径）不联网，直接读
    if (!source.isRemote()) {
        const QString path = source.localPath();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            emit channelsFetched(key, {}, QStringLiteral("无法读取本地直播文件：%1").arg(path));
            return;
        }
        const QByteArray body = file.readAll();
        file.close();
        deliver(key, body);
        return;
    }

    QNetworkRequest request(QUrl(source.url.trimmed()));
    request.setHeader(QNetworkRequest::UserAgentHeader, source.effectiveUserAgent());
    const QString referer = source.effectiveReferer();
    if (!referer.isEmpty()) {
        request.setRawHeader("Referer", referer.toUtf8());
    }
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);

    QNetworkReply *reply = m_nam->get(request);
    m_inFlight.insert(reply, key);
    ++m_pending;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        // abortAll 已经清过表：这时的 finished 是被我们自己中断的，静默丢弃
        if (!m_inFlight.contains(reply)) {
            return;
        }
        const QString sourceKey = m_inFlight.take(reply);
        if (m_pending > 0) {
            --m_pending;
        }

        if (reply->error() != QNetworkReply::NoError) {
            emit channelsFetched(sourceKey, {},
                                 QStringLiteral("拉取失败：%1").arg(reply->errorString()));
            return;
        }
        deliver(sourceKey, reply->readAll());
    });
}

void LiveClient::deliver(const QString &sourceKey, const QByteArray &body)
{
    const QList<LiveChannel> channels = LiveParser::parse(body);
    if (channels.isEmpty()) {
        emit channelsFetched(sourceKey, {},
                             QStringLiteral("没有解析到频道（文件为空或格式不支持）"));
        return;
    }
    emit channelsFetched(sourceKey, channels, QString());
}

void LiveClient::abortAll()
{
    const QList<QNetworkReply *> replies = m_inFlight.keys();
    m_inFlight.clear();
    m_pending = 0;
    for (QNetworkReply *reply : replies) {
        reply->abort();
    }
}
