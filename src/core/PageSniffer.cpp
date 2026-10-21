#include "PageSniffer.h"

#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>
#include <QUrl>
#include <algorithm>

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

QList<PageSniffer::Candidate> PageSniffer::extract(const QString &pageUrl, const QByteArray &body)
{
    QList<Candidate> out;
    if (body.isEmpty()) {
        return out;
    }

    QString text = decodeText(body);
    // JSON / JS 里地址常被转义成 https:\/\/host\/a.m3u8，先还原
    text.replace(QStringLiteral("\\/"), QStringLiteral("/"));

    const QUrl pageUrlObj(pageUrl, QUrl::TolerantMode);
    const QString pageHost = pageUrlObj.host();
    QStringList seen;

    auto add = [&](const QString &raw, const QString &rule, int baseScore) {
        QString c = raw.trimmed();
        // 抠出来时常带尾部的引号、括号、分号
        while (!c.isEmpty() && (c.endsWith(QLatin1Char('"')) || c.endsWith(QLatin1Char('\''))
                                || c.endsWith(QLatin1Char(')')) || c.endsWith(QLatin1Char(';')))) {
            c.chop(1);
        }
        if (c.isEmpty()) {
            return;
        }
        c = absolutize(pageUrl, c);
        if (c.isEmpty()) {
            return;
        }
        const QUrl u(c, QUrl::TolerantMode);
        if (!u.isValid() || (u.scheme() != QStringLiteral("http")
                             && u.scheme() != QStringLiteral("https"))) {
            return;
        }
        if (seen.contains(c)) {
            return;
        }
        seen.append(c);

        Candidate cand;
        cand.url = c;
        cand.rule = rule;
        cand.score = baseScore;
        if (!pageHost.isEmpty() && u.host() == pageHost) {
            cand.score += 20;   // 同域优先：跨域多半是广告或统计
        }
        if (c.contains(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) {
            cand.score += 50;
        } else if (c.contains(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
            cand.score += 30;
        } else if (c.contains(QStringLiteral(".flv"), Qt::CaseInsensitive)) {
            cand.score += 15;
        }
        out.append(cand);
    };

    // 规则 1：正文里的完整媒体地址（不含 .ts，避免把分片当播放地址）
    static const QRegularExpression reMedia(
        QStringLiteral(R"(https?://[^\s"'<>\\()\[\]]+?\.(?:m3u8|mp4|flv|mkv|m4a)(?:\?[^\s"'<>\\()\[\]]*)?)"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = reMedia.globalMatch(text);
    while (it.hasNext()) {
        add(it.next().captured(0), QStringLiteral("literal"), 40);
    }

    // 规则 2：常见键名（苹果 CMS 的 player_aaaa、hls.js 的 sources、各类 JSON 接口）
    static const QRegularExpression reKey(
        QStringLiteral(R"((?:url|play_url|playUrl|video_url|videoUrl|source|src|file|link|m3u8|m3u8_url)\s*["']?\s*[:=]\s*["']([^"'\r\n]{8,2048})["'])"),
        QRegularExpression::CaseInsensitiveOption);
    it = reKey.globalMatch(text);
    while (it.hasNext()) {
        add(it.next().captured(1), QStringLiteral("key"), 35);
    }

    // 规则 3：媒体标签属性
    static const QRegularExpression reAttr(
        QStringLiteral(R"(<(?:video|source|embed)[^>]*\b(?:src|data-src|data-url|data-video)\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    it = reAttr.globalMatch(text);
    while (it.hasNext()) {
        add(it.next().captured(1), QStringLiteral("attr"), 30);
    }

    // 规则 4：base64 编码的地址（部分站点为了绕开正则，直接把地址编码后塞进 JS）
    static const QRegularExpression reB64(QStringLiteral(R"(([A-Za-z0-9+/]{40,2000}={0,2}))"));
    it = reB64.globalMatch(text);
    int checked = 0;
    while (it.hasNext() && checked < 8) {
        const QByteArray decoded = QByteArray::fromBase64(it.next().captured(1).toLatin1());
        ++checked;
        if (decoded.isEmpty()) {
            continue;
        }
        const QString d = QString::fromUtf8(decoded).trimmed();
        // 解出来必须是地址；纯文本（比如内联清单）交给上层用页面地址本身
        if (!d.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
            continue;
        }
        int cut = d.size();
        for (int i = 0; i < d.size(); ++i) {
            const QChar ch = d.at(i);
            if (ch.isSpace() || ch == QLatin1Char('"') || ch == QLatin1Char('\'')
                || ch == QLatin1Char('<') || ch == QLatin1Char('>')) {
                cut = i;
                break;
            }
        }
        add(d.left(cut), QStringLiteral("base64"), 25);
    }

    std::sort(out.begin(), out.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    if (out.size() > 20) {
        out = out.mid(0, 20);
    }
    return out;
}

