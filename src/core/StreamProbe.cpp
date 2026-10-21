#include "StreamProbe.h"
#include "PageSniffer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kCapMedia = 8192;          // 确认是音视频：读 8KB 足够判断
constexpr int kCapDoc = 128 * 1024;      // 网页：留足篇幅给后续嗅探
constexpr int kCapDocQuick = 64 * 1024;  // 候选复检时减半
constexpr int kProbeTimeoutMs = 4000;
constexpr int kProbeTimeoutQuickMs = 3000;
constexpr int kProbeTotalBudgetMs = 12000;
constexpr int kProbeTotalBudgetQuickMs = 6000;

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

/** 把「返回了网页」再细分成可行动的几类（只在 2xx 时调用） */
ProbeKind classifyHtml(const QByteArray &body)
{
    // 页面里已经有播放清单线索（.m3u8 / player_aaaa / <video>）→ 值得解析
    if (PageSniffer::containsPlaylistMark(body)) {
        return ProbeKind::NeedsSniff;
    }
    // 没有线索，但用了 eval/atob/document.write → 地址是运行时算的
    if (PageSniffer::looksJsComputed(body)) {
        return ProbeKind::NeedsBrowser;
    }
    // 有 iframe 嵌套，说明真正的播放器在下一层
    if (PageSniffer::hasIframe(body)) {
        return ProbeKind::NeedsSniff;
    }
    // 篇幅极短：网关错误页 / "not found" 那种。
    // 但带 <script> 的短页面多是「地址藏在脚本里」的播放页，不能一律判死。
    if (body.size() < 512) {
        const bool hasScript = PageSniffer::decodeText(body).contains(QStringLiteral("<script"),
                                                                     Qt::CaseInsensitive);
        return hasScript ? ProbeKind::NeedsSniff : ProbeKind::HtmlError;
    }
    // 篇幅像正经页面，虽然没看到线索也还有机会
    if (body.size() > 4096) {
        return ProbeKind::NeedsSniff;
    }
    return ProbeKind::HtmlError;
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

QString StreamProbeResult::category() const
{
    switch (kind) {
    case ProbeKind::HtmlError:
        return QStringLiteral("A");
    case ProbeKind::HttpError:
        return (statusCode == 401 || statusCode == 403) ? QStringLiteral("B") : QStringLiteral("A");
    case ProbeKind::EmptyBody:
        return QStringLiteral("A");
    case ProbeKind::NeedsSniff:
        return QStringLiteral("C");
    case ProbeKind::NotMedia:
        return QStringLiteral("C");
    case ProbeKind::NeedsBrowser:
        return QStringLiteral("D");
    case ProbeKind::Timeout:
    case ProbeKind::TlsFailed:
        return QStringLiteral("E");
    default:
        return QStringLiteral("E");
    }
}

QString StreamProbeResult::categoryText() const
{
    switch (kind) {
    case ProbeKind::HtmlError:
        return QStringLiteral("回源错误页（源站或 CDN 出了故障，与解析无关）");
    case ProbeKind::HttpError:
        return (statusCode == 401 || statusCode == 403)
                   ? QStringLiteral("防盗链校验或需要登录（缺 Referer / Cookie / 签名）")
                   : QStringLiteral("播放地址已失效");
    case ProbeKind::EmptyBody:
        return QStringLiteral("源站返回空内容");
    case ProbeKind::NeedsSniff:
        return QStringLiteral("播放页（页面里可能有可解析的播放地址）");
    case ProbeKind::NotMedia:
        return QStringLiteral("返回的不是视频内容");
    case ProbeKind::NeedsBrowser:
        return QStringLiteral("地址由网页 JS 运行时计算（纯文本解析无解）");
    case ProbeKind::Timeout:
        return QStringLiteral("连接源站超时");
    case ProbeKind::TlsFailed:
        return QStringLiteral("HTTPS 证书校验失败");
    default:
        return QStringLiteral("其他错误");
    }
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

void StreamProbe::start(const QString &url, const QString &userAgent, const QString &referer,
                        bool quickMode)
{
    cancel();

    m_url = sanitizeMediaUrl(url);
    m_body.clear();
    m_cappedByUs = false;
    m_capDecided = false;
    m_bodyCap = kCapMedia;
    m_lastStatus = 0;
    m_lastContentType.clear();
    m_lastError.clear();
    m_lastUserMessage.clear();
    m_lastKind = ProbeKind::InvalidUrl;
    m_lastBodySize = 0;
    m_sawHtml = false;
    m_quickMode = quickMode;
    m_bestHtmlBody.clear();
    m_bestHtmlStatus = 0;
    m_bestHtmlContentType.clear();

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
        result.kind = ProbeKind::PassThrough;
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
    QList<Strategy> candidates = {
        { ua, ref },
        { ua, QString() },
        { QString(), QString() },
        { ua, mediaOrigin }
    };
    if (quickMode) {
        // 复检只关心「这组 header 能不能拿到流」，多试的收益低于多花的等待时间
        candidates = { { ua, ref }, { QString(), QString() } };
    }

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
    const int budget = m_quickMode ? kProbeTotalBudgetQuickMs : kProbeTotalBudgetMs;
    if (m_strategyIndex >= m_strategies.size()
        || (m_deadline.isValid() && m_deadline.elapsed() > budget)) {
        finishFailure(m_lastError.isEmpty() ? QStringLiteral("no working header set") : m_lastError,
                      m_lastUserMessage.isEmpty() ? QStringLiteral("无法连接到该播放地址") : m_lastUserMessage);
        return;
    }

    const Strategy &strategy = m_strategies.at(m_strategyIndex);
    m_body.clear();
    m_cappedByUs = false;
    m_capDecided = false;
    m_bodyCap = kCapMedia;

    QNetworkRequest request{QUrl(m_url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_quickMode ? kProbeTimeoutQuickMs : kProbeTimeoutMs);
    request.setRawHeader("Accept", "*/*");
    // 申请 128KB：若是网页就有足够篇幅供嗅探；若是音视频，下面按类型提前截断，不会真读这么多
    request.setRawHeader("Range", "bytes=0-131071");
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

        if (!m_capDecided) {
            const QString ct = m_reply->header(QNetworkRequest::ContentTypeHeader)
                                   .toString().toLower();
            const bool docLike = ct.contains(QStringLiteral("text/"))
                                 || ct.contains(QStringLiteral("html"))
                                 || ct.contains(QStringLiteral("xml"))
                                 || ct.contains(QStringLiteral("json"))
                                 || ct.isEmpty();   // 没给类型时按网页对待，宁可多读
            m_bodyCap = docLike ? (m_quickMode ? kCapDocQuick : kCapDoc) : kCapMedia;
            m_capDecided = true;
        }

        QByteArray chunk = m_reply->readAll();
        const int room = m_bodyCap - m_body.size();
        if (chunk.size() > room) {
            chunk.truncate(qMax(0, room));
            m_cappedByUs = true;
        }
        m_body.append(chunk);

        // 播放清单在开头几行就能确认，不必等读满
        bool isPlaylist = false;
        if (m_body.size() >= 7) {
            const QByteArray head = m_body.left(16);
            isPlaylist = head.contains("#EXTM3U");
        }

        if (m_body.size() >= m_bodyCap || isPlaylist) {
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
        const int room = m_bodyCap - m_body.size();
        QByteArray rest = reply->readAll();
        if (rest.size() > room) {
            rest.truncate(qMax(0, room));
        }
        m_body.append(rest);
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

    const bool playable = mediaContentType || mediaExtension || contentType.isEmpty();

    const bool success = statusOk && (isM3u8 || (!isHtml && playable));

    // 注意：有的站把 m3u8 以 text/html 下发，所以 isM3u8 要优先于 isHtml 判定
    if (success) {
        StreamProbeResult result;
        result.ok = true;
        result.kind = ProbeKind::Media;
        result.url = m_url;
        result.userAgent = m_strategies.at(m_strategyIndex).userAgent;
        result.referer = m_strategies.at(m_strategyIndex).referer;
        result.statusCode = status;
        result.contentType = contentType;
        result.bodySize = m_body.size();
        result.isHls = isM3u8;
        emit finished(result);
        return;
    }

    m_lastBodySize = m_body.size();

    // 网页体先留存：即使这次是 403，页面里也可能写着原因，对诊断有用；
    // 也可能某组 header 拿到错误页、另一组拿到播放页，所以取「内容最多的那份」。
    if (isHtml) {
        m_sawHtml = true;
        if (m_body.size() > m_bestHtmlBody.size()) {
            m_bestHtmlBody = m_body;
            m_bestHtmlStatus = status;
            m_bestHtmlContentType = contentType;
        }
    }

    // 归因顺序很关键：403 的 HTML 页属于「防盗链」而不是「错误页」，所以要判在 isHtml 之前
    if (status == 401 || status == 403) {
        m_lastKind = ProbeKind::HttpError;
        m_lastError = QStringLiteral("http %1").arg(status);
        m_lastUserMessage = QStringLiteral("源站拒绝访问（HTTP %1），一般是防盗链校验或需要登录").arg(status);
    } else if (status == 404 || status == 410) {
        m_lastKind = ProbeKind::HttpError;
        m_lastError = QStringLiteral("http %1").arg(status);
        m_lastUserMessage = QStringLiteral("播放地址已失效（HTTP %1），换一条线路试试").arg(status);
    } else if (status >= 500) {
        m_lastKind = ProbeKind::HttpError;
        m_lastError = QStringLiteral("http %1").arg(status);
        m_lastUserMessage = QStringLiteral("源站服务器出错（HTTP %1），稍后再试").arg(status);
    } else if (netError == QNetworkReply::TimeoutError) {
        m_lastKind = ProbeKind::Timeout;
        m_lastError = QStringLiteral("timeout");
        m_lastUserMessage = QStringLiteral("连接源站超时");
    } else if (netError == QNetworkReply::SslHandshakeFailedError) {
        m_lastKind = ProbeKind::TlsFailed;
        m_lastError = QStringLiteral("ssl handshake failed");
        m_lastUserMessage = QStringLiteral("HTTPS 证书校验失败，源站证书可能已过期或不受信任");
    } else if (netError == QNetworkReply::HostNotFoundError) {
        m_lastKind = ProbeKind::Timeout;
        m_lastError = QStringLiteral("host not found");
        m_lastUserMessage = QStringLiteral("域名解析失败，源站可能已停用");
    } else if (isHtml) {
        m_lastKind = classifyHtml(m_body);
        m_lastError = QStringLiteral("html response");
        switch (m_lastKind) {
        case ProbeKind::NeedsBrowser:
            m_lastUserMessage = QStringLiteral("该地址返回的是网页，且播放地址由 JS 运行时计算，纯文本解析不了");
            break;
        case ProbeKind::NeedsSniff:
            m_lastUserMessage = QStringLiteral("该地址返回的是播放页，页面里可能有可解析的播放地址");
            break;
        default:
            m_lastUserMessage = QStringLiteral("该地址返回的是一个内容极少（%1 字节）的网页，不像播放页")
                                    .arg(m_body.size());
            break;
        }
    } else if (statusOk && headTrimmed.isEmpty()) {
        m_lastKind = ProbeKind::EmptyBody;
        m_lastError = QStringLiteral("empty body");
        m_lastUserMessage = QStringLiteral("源站返回了空内容");
    } else if (statusOk) {
        m_lastKind = ProbeKind::NotMedia;
        m_lastError = QStringLiteral("not media");
        m_lastUserMessage = QStringLiteral("该地址返回的不是视频内容（%1）")
                                .arg(contentType.isEmpty() ? QString::fromUtf8(headTrimmed.left(40))
                                                           : contentType);
    } else {
        m_lastKind = ProbeKind::HttpError;
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
    result.statusCode = m_bestHtmlStatus > 0 ? m_bestHtmlStatus : m_lastStatus;
    result.contentType = m_bestHtmlBody.isEmpty() ? m_lastContentType : m_bestHtmlContentType;
    result.isHtmlPage = m_sawHtml;

    result.kind = m_lastKind;
    if (result.kind == ProbeKind::InvalidUrl && error.contains(QStringLiteral("url"))) {
        result.kind = ProbeKind::InvalidUrl;
    }

    // HTML 类失败一律带上诊断信息，让用户和我们都能看清到底返回了什么
    if (m_sawHtml && !m_bestHtmlBody.isEmpty()) {
        // 只在「结论本来就是网页相关」时重新分类，别把 403/502 这类归因覆盖掉
        if (m_lastKind == ProbeKind::HtmlError || m_lastKind == ProbeKind::NeedsSniff
            || m_lastKind == ProbeKind::NeedsBrowser) {
            result.kind = classifyHtml(m_bestHtmlBody);
        }
        result.bodySize = m_bestHtmlBody.size();
        result.pageTitle = PageSniffer::titleOf(m_bestHtmlBody);
        result.hasIframe = PageSniffer::hasIframe(m_bestHtmlBody);
        result.hasEval = PageSniffer::looksJsComputed(m_bestHtmlBody);
        result.body = m_bestHtmlBody;
    } else {
        result.bodySize = m_lastBodySize;
    }

    result.error = error;
    result.userMessage = userMessage;
    emit finished(result);
}
