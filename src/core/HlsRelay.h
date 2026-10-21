#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class QNetworkAccessManager;

/**
 * @brief 本地 HLS 中继：只代理 m3u8 清单文本，分片由播放内核直连 CDN
 *
 * 工作方式：
 * 1. playOnlineEntry 解析出 .m3u8 后，调用 wrapUrl() 把上游地址登记成一个
 *    随机 token，播放器拿到的是 http://127.0.0.1:<port>/t/<token>；
 * 2. 播放内核来取清单时，中继按该条目保存的 UA / Referer 拉上游清单，
 *    交给 HlsFilter 处理（主清单改写子清单继续走中继；媒体清单剔除广告块、
 *    分片地址改写为绝对 URL）后回给内核；
 * 3. 结果按 token 缓存，seek / 重开不会重复拉取。
 *
 * 只绑定 127.0.0.1，token 随机生成，外部进程无法枚举。
 * 上游拉取失败时该 token 标记为失败，wrapUrl() 会退回直连地址。
 */
class HlsRelay : public QObject
{
    Q_OBJECT
public:
    explicit HlsRelay(QObject *parent = nullptr);
    ~HlsRelay() override;

    /** 启动监听（随机端口）。返回 false 表示端口绑定失败，调用方应退回直连。 */
    bool start();

    quint16 port() const { return m_port; }

    /**
     * 把上游 m3u8 地址包装成本地中继地址。
     * @return 本地地址；返回空字符串表示应直接使用原地址（启动失败或该地址
     *         此前拉取失败过）。
     */
    QString wrapUrl(const QUrl &upstream, const QString &userAgent, const QString &referer);

signals:
    /** 媒体清单里剔除了疑似广告块 */
    void adsRemoved(int blocks, double seconds);

    /** 上游清单拉取失败（该地址后续将退回直连） */
    void fetchFailed(const QString &upstreamUrl, const QString &reason);

private:
    struct Entry
    {
        QUrl upstream;
        QString userAgent;
        QString referer;
        QByteArray cached;     // 处理后的清单
        bool hasCached = false;
        bool failed = false;
    };

    void onNewConnection();
    void handleClient(QTcpSocket *socket);
    void serveToken(QTcpSocket *socket, const QString &token);
    void respond(QTcpSocket *socket, int statusCode, const QByteArray &contentType,
                 const QByteArray &body);
    void fetchUpstream(const Entry &entry, QUrl *finalUrl, QByteArray *body, bool *ok);

    QTcpServer *m_server = nullptr;
    // m_nam 走系统代理（访问真实站点）；m_namDirect 强制直连（回环地址）
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager *m_namDirect = nullptr;
    quint16 m_port = 0;
    QHash<QString, Entry> m_entries;
};
