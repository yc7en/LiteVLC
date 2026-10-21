#include "StreamProbe.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kProbeBodyCap = 8192;   // 预检最多读 8KB，够判断是不是 m3u8 / 网页
constexpr int kProbeTimeoutMs = 4000;
constexpr int kProbeTotalBudgetMs = 12000;

QString originOf(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty()) {
        return QString();
    }
    QString origin = QStringLiteral("%1://%2").arg(url.scheme(), url.host());
    if (url.port() > 0) {
        origin += QStringLiteral(":%1").arg(url.port());
    }
    origin += QLatin1Char('/');
    return origin;
}

} // namespace

QString sanitizeMediaUrl(const QString &raw)
{
    QString s = raw;
    s.remove(QLatin1Char('\n'));
    s.remove(QLatin1Char('\r'));
    s.remove(QLatin1Char('\t'));
    s = s.trimmed();
    if (s.isEmpty()) {
        return s;
    }

    // 少数接口会把 & 转成 &amp; 又没正确解码，带上这个参数就会 403
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    // 有的源把地址用引号包起来（JSON 二次序列化后很常见）
    while (s.size() >= 2
           && ((s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"')))
               || (s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))))) {
        s = s.mid(1, s.size() - 2).trimmed();
    }

    // 协议相对地址
    if (s.startsWith(QStringLiteral("//"))) {
        s.prepend(QStringLiteral("http:"));
    }

    // 中文字幕组/中文文件名很常见，LibVLC 遇到未编码字符会直接判定为非法 URI
    const QUrl parsed(s, QUrl::TolerantMode);
    if (parsed.isValid() && !parsed.scheme().isEmpty()) {
        const QByteArray encoded = parsed.toEncoded(QUrl::FullyEncoded);
        if (!encoded.isEmpty()) {
            return QString::fromLatin1(encoded);
        }
    }

    // 去掉 URL 内部残留的空格（空格在 URL 里永远非法）
    QString noSpace = s;
    noSpace.remove(QLatin1Char(' '));
    return noSpace;
}

StreamProbe::StreamProbe(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void StreamProbe::cancel()
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_strategies.clear();
    m_strategyIndex = 0;
    m_body.clear();
}

void StreamProbe::start(const QString &url, const QString &userAgent, const QString &referer)
{
    cancel();

    m_url = sanitizeMediaUrl(url);
    m_body.clear();
    m_cappedByUs = false;
    m_lastStatus = 0;
    m_lastContentType.clear();
    m_lastError.clear();
    m_lastUserMessage.clear();
    m_sawHtml = false;

    if (m_url.isEmpty()) {
        finishFailure(QStringLiteral("empty url"), QStringLiteral("播放地址为空"));
        return;
    }

    const QUrl u(m_url);
    if (!u.isValid() || u.scheme().isEmpty()) {
        finishFailure(QStringLiteral("invalid url"), QStringLiteral("播放地址格式不正确：%1").arg(m_url));
        return;
    }

    // rtmp / rtsp / ftp 这类协议交给 LibVLC 自己处理，HTTP 预检对它们没意义
    const QString scheme = u.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        StreamProbeResult result;
        result.ok = true;
        result.url = m_url;
        result.userAgent = userAgent.trimmed();
        result.referer = referer.trimmed();
        result.error = QStringLiteral("scheme %1 passed through").arg(scheme);
        emit finished(result);
        return;
    }

    m_deadline.start();

    const QString ua = userAgent.trimmed();
    const QString ref = referer.trimmed();
    const QString mediaOrigin = originOf(u);

    // 按防盗链常见程度排序，逐组试
    const QList<Strategy> candidates = {
        { ua, ref },
        { ua, QString() },
        { QString(), QString() },
        { ua, mediaOrigin }
    };

    for (const Strategy &s : candidates) {
        bool duplicated = false;
        for (const Strategy &existing : m_strategies) {
            if (existing.userAgent == s.userAgent && existing.referer == s.referer) {
                duplicated = true;
                break;
            }
        }
        if (!duplicated) {
            m_strategies.append(s);
        }
    }

    startNext();
}

void StreamProbe::startNext()
{
    if (m_strategyIndex >= m_strategies.size()
        || (m_deadline.isValid() && m_deadline.elapsed() > kProbeTotalBudgetMs)) {
        finishFailure(m_lastError.isEmpty() ? QStringLiteral("no working header set") : m_lastError,
                      m_lastUserMessage.isEmpty() ? QStringLiteral("无法连接到该播放地址") : m_lastUserMessage);
        return;
    }

    const Strategy &strategy = m_strategies.at(m_strategyIndex);
    m_body.clear();
    m_cappedByUs = false;

    QNetworkRequest request{QUrl(m_url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kProbeTimeoutMs);
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Range", "bytes=0-8191");
    if (!strategy.userAgent.isEmpty()) {
        request.setRawHeader("User-Agent", strategy.userAgent.toUtf8());
    }
    if (!strategy.referer.isEmpty()) {
        request.setRawHeader("Referer", strategy.referer.toUtf8());
    }

    m_reply = m_nam->get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_reply) {
            return;
        }
        QByteArray chunk = m_reply->readAll();
        const int room = kProbeBodyCap - m_body.size();
        if (chunk.size() > room) {
            chunk.truncate(qMax(0, room));
            m_cappedByUs = true;
        }
        m_body.append(chunk);
        if (m_body.size() >= kProbeBodyCap) {
            m_cappedByUs = true;
            QNetworkReply *reply = m_reply;
            // 在 readyRead 里直接 abort 会重入，丢到下一个事件循环
            QTimer::singleShot(0, reply, [reply]() { reply->abort(); });
        }
    });

    connect(m_reply, &QNetworkReply::finished, this, &StreamProbe::evaluate);
}

void StreamProbe::evaluate()
{
    QNetworkReply *reply = m_reply;
    if (!reply) {
        return;
    }
    m_reply = nullptr;
    reply->disconnect(this);

    const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int status = statusVar.isValid() ? statusVar.toInt() : 0;
    const QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();

    if (!m_cappedByUs) {
        m_body.append(reply->readAll());
    }
    reply->deleteLater();

    m_lastStatus = status;
    m_lastContentType = contentType;

    const QByteArray head = m_body.left(2048);
    const QByteArray headTrimmed = head.trimmed();
    const QByteArray headLower = headTrimmed.toLower();

    const bool isM3u8 = headTrimmed.startsWith("#EXTM3U");
    const bool isHtml = contentType.contains(QStringLiteral("html"))
                        || headLower.startsWith("<!doctype")
                        || headLower.startsWith("<html")
                        || headLower.contains("<head");

    // 200/206 且拿到了内容；被我们自己截断也算拿到了
    const bool statusOk = (status == 200 || status == 206 || (status == 0 && m_cappedByUs));

    // 有些 CDN 对 Range 请求回空体但类型是对的，这类不能误判成失败；
    // 反过来，application/json / text/plain 的错误页也不能当成可播内容。
    const bool mediaContentType =
        contentType.contains(QStringLiteral("mpegurl"))
        || contentType.contains(QStringLiteral("video/"))
        || contentType.contains(QStringLiteral("audio/"))
        || contentType.contains(QStringLiteral("octet-stream"));

    const bool mediaExtension =
        m_url.contains(QStringLiteral(".m3u8"), Qt::CaseInsensitive)
        || m_url.contains(QStringLiteral(".mp4"), Qt::CaseInsensitive)
        || m_url.contains(QStringLiteral(".flv"), Qt::CaseInsensitive)
        || m_url.contains(QStringLiteral(".mkv"), Qt::CaseInsensitive)
        || m_url.contains(QStringLiteral(".m4a"), Qt::CaseInsensitive);

    const bool playable = isM3u8 || mediaContentType || mediaExtension || contentType.isEmpty();

    if (statusOk && !isHtml && playable) {
        StreamProbeResult result;
        result.ok = true;
        result.url = m_url;
        result.userAgent = m_strategies.at(m_strategyIndex).userAgent;
        result.referer = m_strategies.at(m_strategyIndex).referer;
        result.statusCode = status;
        result.contentType = contentType;
        result.isHls = isM3u8;
        emit finished(result);
        return;
    }

    // 记录这次失败的原因，继续试下一组 header
    if (isHtml) {
        m_sawHtml = true;
        m_lastError = QStringLiteral("html response");
        m_lastUserMessage = QStringLiteral("该地址返回的是网页，不是视频直链（多半需要网页解析/嗅探）");
    } else if (status == 401 || status == 403) {
        m_lastError = QStringLiteral("http %1").arg(status);
        m_lastUserMessage = QStringLiteral("源站拒绝访问（HTTP %1），一般是防盗链校验或需要登录").arg(status);
    } else if (status == 404 || status == 410) {
        m_lastError = QStringLiteral("http %1").arg(status);
        m_lastUserMessage = QStringLiteral("播放地址已失效（HTTP %1），换一条线路试试").arg(status);
    } else if (status >= 500) {
        m_lastError = QStringLiteral("http %1").arg(status);
        m_lastUserMessage = QStringLiteral("源站服务器出错（HTTP %1），稍后再试").arg(status);
    } else if (netError == QNetworkReply::TimeoutError) {
        m_lastError = QStringLiteral("timeout");
        m_lastUserMessage = QStringLiteral("连接源站超时");
    } else if (netError == QNetworkReply::SslHandshakeFailedError) {
        m_lastError = QStringLiteral("ssl handshake failed");
        m_lastUserMessage = QStringLiteral("HTTPS 证书校验失败，源站证书可能已过期或不受信任");
    } else if (netError == QNetworkReply::HostNotFoundError) {
        m_lastError = QStringLiteral("host not found");
        m_lastUserMessage = QStringLiteral("域名解析失败，源站可能已停用");
    } else if (statusOk && headTrimmed.isEmpty()) {
        m_lastError = QStringLiteral("empty body");
        m_lastUserMessage = QStringLiteral("源站返回了空内容");
    } else if (statusOk) {
        m_lastError = QStringLiteral("not media");
        m_lastUserMessage = QStringLiteral("该地址返回的不是视频内容（%1）")
                                .arg(contentType.isEmpty() ? QString::fromUtf8(headTrimmed.left(40))
                                                           : contentType);
    } else {
        m_lastError = QStringLiteral("status %1 error %2").arg(status).arg(int(netError));
        m_lastUserMessage = QStringLiteral("无法打开该地址：%1").arg(netErrorText);
    }

    ++m_strategyIndex;
    startNext();
}

void StreamProbe::finishFailure(const QString &error, const QString &userMessage)
{
    StreamProbeResult result;
    result.ok = false;
    result.url = m_url;
    result.statusCode = m_lastStatus;
    result.contentType = m_lastContentType;
    result.isHtmlPage = m_sawHtml;
    result.error = error;
    result.userMessage = userMessage;
    emit finished(result);
}
