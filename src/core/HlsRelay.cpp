#include "HlsRelay.h"

#include "HlsFilter.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QDateTime>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>

namespace {

QByteArray makeToken()
{
    const quint32 a = QRandomGenerator::global()->generate();
    const quint32 b = QRandomGenerator::global()->generate();
    return QString::asprintf("%08x%08x", a, b).toLatin1();
}

QByteArray httpDate()
{
    return QLocale::c().toString(QDateTime::currentDateTimeUtc(),
                                 QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT'")).toLatin1();
}

} // namespace

HlsRelay::HlsRelay(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_namDirect = new QNetworkAccessManager(this);
    m_namDirect->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
}

HlsRelay::~HlsRelay() = default;

bool HlsRelay::start()
{
    if (m_server) {
        return m_port != 0;
    }
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &HlsRelay::onNewConnection);
    // 只绑定回环地址：本地进程可用，外部机器不可达
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    m_port = m_server->serverPort();
    return true;
}

QString HlsRelay::wrapUrl(const QUrl &upstream, const QString &userAgent, const QString &referer)
{
    if (!m_server || m_port == 0 || !upstream.isValid()) {
        return QString();
    }

    // 已登记过的地址直接复用 token（同一集重试 / seek 不会重复登记）
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (it->upstream == upstream && !it->failed) {
            return QStringLiteral("http://127.0.0.1:%1/t/%2").arg(m_port).arg(it.key());
        }
    }
    // 此前拉取失败过的地址退回直连，避免反复走一条死路
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (it->upstream == upstream && it->failed) {
            return QString();
        }
    }

    const QString token = QString::fromLatin1(makeToken());
    Entry e;
    e.upstream = upstream;
    e.userAgent = userAgent;
    e.referer = referer;
    m_entries.insert(token, e);
    return QStringLiteral("http://127.0.0.1:%1/t/%2").arg(m_port).arg(token);
}

void HlsRelay::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        handleClient(socket);
    }
}

void HlsRelay::handleClient(QTcpSocket *socket)
{
    if (!socket->waitForReadyRead(3000)) {
        socket->abort();
        return;
    }
    const QByteArray request = socket->readAll();
    const int headerEnd = request.indexOf("\r\n\r\n");
    const QByteArray head = headerEnd >= 0 ? request.left(headerEnd) : request;
    const QList<QByteArray> reqLines = head.split('\n');
    if (reqLines.isEmpty()) {
        socket->abort();
        return;
    }
    // "GET /t/<token> HTTP/1.1"
    const QByteArray first = reqLines.first().trimmed();
    const QList<QByteArray> parts = first.split(' ');
    if (parts.size() < 2 || parts.first() != "GET") {
        respond(socket, 405, "text/plain", "method not allowed");
        return;
    }
    QString path = QString::fromLatin1(parts.at(1));
    const int q = path.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        path.truncate(q);
    }

    static const QString kPrefix = QStringLiteral("/t/");
    if (!path.startsWith(kPrefix)) {
        respond(socket, 404, "text/plain", "not found");
        return;
    }
    const QString token = path.mid(kPrefix.length());
    serveToken(socket, token);
}

void HlsRelay::fetchUpstream(const Entry &entry, QUrl *finalUrl, QByteArray *body, bool *ok)
{
    *ok = false;
    QNetworkRequest request(entry.upstream);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    // 回环地址绕过系统代理：本机源站（测试 / 本地中继链）不应被代理拦截
    const QString host = entry.upstream.host();
    QNetworkAccessManager *nam = m_nam;
    if (host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost")
        || host == QStringLiteral("::1")) {
        nam = m_namDirect;
    }
    if (!entry.userAgent.isEmpty()) {
        request.setHeader(QNetworkRequest::UserAgentHeader, entry.userAgent);
    }
    if (!entry.referer.isEmpty()) {
        request.setRawHeader("Referer", entry.referer.toUtf8());
    }

    QNetworkReply *reply = nam->get(request);
    QEventLoop loop;
    QTimer::singleShot(16000, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        const QString reason = reply->errorString();
        reply->deleteLater();
        emit fetchFailed(entry.upstream.toString(), reason);
        return;
    }
    *finalUrl = reply->url();          // 重定向后的最终地址，作为相对 URI 的基准
    *body = reply->readAll();
    reply->deleteLater();
    *ok = !body->isEmpty();
    if (!*ok) {
        emit fetchFailed(entry.upstream.toString(), QStringLiteral("响应为空"));
    }
}

void HlsRelay::serveToken(QTcpSocket *socket, const QString &token)
{
    if (!m_entries.contains(token)) {
        respond(socket, 404, "text/plain", "unknown token");
        return;
    }

    Entry &entry = m_entries[token];
    if (entry.hasCached) {
        respond(socket, 200, "application/vnd.apple.mpegurl", entry.cached);
        return;
    }
    if (entry.failed) {
        respond(socket, 502, "text/plain", "upstream unavailable");
        return;
    }

    QUrl finalUrl;
    QByteArray body;
    bool ok = false;
    fetchUpstream(entry, &finalUrl, &body, &ok);
    if (!ok) {
        entry.failed = true;
        respond(socket, 502, "text/plain", "upstream unavailable");
        return;
    }

    const HlsFilter::Result result = HlsFilter::process(
        QString::fromUtf8(body), finalUrl,
        [this, &entry](const QUrl &child) {
            return wrapUrl(child, entry.userAgent, entry.referer);
        });

    const QByteArray payload = result.output.toUtf8();
    entry.cached = payload;
    entry.hasCached = true;

    if (result.modified) {
        emit adsRemoved(result.removedBlocks, result.removedSeconds);
    }
    respond(socket, 200, "application/vnd.apple.mpegurl", payload);
}

void HlsRelay::respond(QTcpSocket *socket, int statusCode, const QByteArray &contentType,
                       const QByteArray &body)
{
    const QByteArray status = (statusCode == 200) ? QByteArrayLiteral("OK")
                              : (statusCode == 404) ? QByteArrayLiteral("Not Found")
                              : QByteArrayLiteral("Bad Gateway");
    QByteArray resp;
    resp += QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode) + ' ' + status + "\r\n";
    resp += QByteArrayLiteral("Server: LiteVlcPlayer-Relay\r\n");
    resp += QByteArrayLiteral("Date: ") + httpDate() + "\r\n";
    resp += QByteArrayLiteral("Content-Type: ") + contentType + "\r\n";
    resp += QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) + "\r\n";
    resp += QByteArrayLiteral("Connection: close\r\n\r\n");
    resp += body;

    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
    if (socket->state() != QAbstractSocket::UnconnectedState) {
        socket->waitForBytesWritten(3000);
    }
}
