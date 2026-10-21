#include "PageSniffer.h"

#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QUrl>

namespace {

/** 把 <iframe src="..."> 里的地址绝对化 */
QString absolutize(const QString &base, const QString &candidate)
{
    QString c = candidate.trimmed();
    if (c.isEmpty() || c.startsWith(QStringLiteral("about:"))
        || c.startsWith(QStringLiteral("javascript:")) || c.startsWith(QStringLiteral("data:"))) {
        return QString();
    }
    const QUrl u(c);
    if (u.isValid() && !u.scheme().isEmpty()) {
        return c;
    }
    const QUrl baseUrl(base, QUrl::TolerantMode);
    if (!baseUrl.isValid()) {
        return QString();
    }
    if (c.startsWith(QStringLiteral("//"))) {
        return baseUrl.scheme() + QLatin1Char(':') + c;
    }
    return baseUrl.resolved(QUrl(c, QUrl::TolerantMode)).toString();
}

} // namespace

QString PageSniffer::decodeText(const QByteArray &body)
{
    if (body.isEmpty()) {
        return QString();
    }

    QString text = QString::fromUtf8(body);
    // 出现 U+FFFD 说明不是 UTF-8；老 CMS / 老站点多为 GBK（CP936）
    if (text.contains(QChar(0xFFFD))) {
        auto decoder = QStringDecoder(QStringDecoder::System);
        const QString fallback = decoder.decode(body);
        if (!fallback.isEmpty() && !fallback.contains(QChar(0xFFFD))) {
            return fallback;
        }
        // 两种都不干净时，退回 UTF-8 版本（至少 ASCII 部分是好的）
    }
    return text;
}

QString PageSniffer::titleOf(const QByteArray &body)
{
    if (body.isEmpty()) {
        return QString();
    }
    static const QRegularExpression re(
        QStringLiteral("<title[^>]*>(.*?)</title>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    const QString text = decodeText(body.left(64 * 1024));
    const auto m = re.match(text);
    if (!m.hasMatch()) {
        return QString();
    }
    QString t = m.captured(1).simplified();
    if (t.size() > 120) {
        t = t.left(120) + QStringLiteral("…");
    }
    return t;
}

bool PageSniffer::hasIframe(const QByteArray &body)
{
    if (body.isEmpty()) {
        return false;
    }
    const QString text = decodeText(body).toLower();
    return text.contains(QStringLiteral("<iframe"));
}

bool PageSniffer::looksJsComputed(const QByteArray &body)
{
    if (body.isEmpty()) {
        return false;
    }
    const QString text = decodeText(body);

    // 这些是「地址不在文本里，而是运行时算出来」的典型特征
    static const QRegularExpression re(
        QStringLiteral(R"((?:^|[^A-Za-z0-9_$])(?:eval\s*\(|atob\s*\(|document\.write(?:ln)?\s*\(|unescape\s*\(|fromCharCode\s*\())"),
        QRegularExpression::CaseInsensitiveOption);
    if (re.match(text).hasMatch()) {
        return true;
    }

    // 大量 \x41\x42 形式的十六进制转义，也是混淆特征
    static const QRegularExpression reHex(QStringLiteral(R"(\\x[0-9a-fA-F]{2})"));
    int hexCount = 0;
    auto it = reHex.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        if (++hexCount >= 8) {
            return true;
        }
    }
    return false;
}

bool PageSniffer::containsPlaylistMark(const QByteArray &body)
{
    if (body.isEmpty()) {
        return false;
    }
    const QString text = decodeText(body);
    const QString lower = text.toLower();
    return lower.contains(QStringLiteral(".m3u8"))
           || lower.contains(QStringLiteral("#extm3u"))
           || lower.contains(QStringLiteral(".mp4"))
           || lower.contains(QStringLiteral("player_aaaa"))
           || lower.contains(QStringLiteral("<video"))
           || lower.contains(QStringLiteral("mac_url"));
}

QStringList PageSniffer::iframeUrls(const QString &pageUrl, const QByteArray &body, int maxCount)
{
    QStringList result;
    if (body.isEmpty() || maxCount <= 0) {
        return result;
    }

    const QString text = decodeText(body);
    static const QRegularExpression re(
        QStringLiteral(R"(<iframe[^>]*\bsrc\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);

    auto it = re.globalMatch(text);
    while (it.hasNext() && result.size() < maxCount) {
        const QString url = absolutize(pageUrl, it.next().captured(1));
        if (!url.isEmpty() && !result.contains(url)) {
            result.append(url);
        }
    }
    return result;
}
