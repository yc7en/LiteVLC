#include "LiveParser.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QUrl>

namespace {

/** m3u 里 `key="value"` 形式的属性 */
QString attrValue(const QString &line, const QString &key)
{
    const QRegularExpression re(
        QStringLiteral("%1\\s*=\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch m = re.match(line);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

/** 一行内容看着像不像播放地址（http/rtmp/rtsp/本地绝对路径） */
bool looksLikeUrl(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty() || t.startsWith(QLatin1Char('#'))) {
        return false;
    }
    if (t.contains(QStringLiteral("://"))) {
        return true;
    }
    // 本地绝对路径：/data/xxx.m3u8 或 C:\xxx\list.txt
    return t.startsWith(QLatin1Char('/'))
           || (t.size() > 2 && t.at(1) == QLatin1Char(':'));
}

/** 多线路写法 `频道,url1#url2` 取第一条；地址里真带 # 的情况极少，收益大于风险 */
QString firstUrl(const QString &raw)
{
    QString url = raw.trimmed();
    const int hash = url.indexOf(QLatin1Char('#'));
    if (hash > 0) {
        url = url.left(hash).trimmed();
    }
    return url;
}

QList<LiveChannel> dedupe(const QList<LiveChannel> &channels)
{
    QList<LiveChannel> out;
    QSet<QString> seen;
    out.reserve(channels.size());
    for (const LiveChannel &ch : channels) {
        if (!ch.isValid()) {
            continue;
        }
        const QString fingerprint = ch.name + QLatin1Char('|') + ch.url;
        if (seen.contains(fingerprint)) {
            continue;
        }
        seen.insert(fingerprint);
        out.append(ch);
    }
    return out;
}

} // namespace

QString LiveParser::ungroupedName()
{
    return QStringLiteral("未分组");
}

QString LiveParser::decode(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QString();
    }

    QString text = QString::fromUtf8(data);
    // 出现 U+FFFD 说明不是 UTF-8；老直播源（尤其 txt 列表）多为 GBK
    if (text.contains(QChar(0xFFFD))) {
        auto decoder = QStringDecoder(QStringDecoder::System);
        const QString fallback = decoder.decode(data);
        if (!fallback.isEmpty() && !fallback.contains(QChar(0xFFFD))) {
            return fallback;
        }
    }
    return text;
}

QList<LiveChannel> LiveParser::parseM3u(const QString &text)
{
    QList<LiveChannel> out;

    LiveChannel pending;
    QString currentGroup;
    const QStringList lines = text.split(QLatin1Char('\n'));
    out.reserve(lines.size() / 2 + 1);

    for (const QString &raw : lines) {
        const QString line = QString(raw).remove(QLatin1Char('\r')).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        if (line.startsWith(QStringLiteral("#EXTINF"), Qt::CaseInsensitive)) {
            pending = LiveChannel();

            // 频道名在最后一个逗号之后；属性里也可能有逗号，所以从右往左找
            const int comma = line.lastIndexOf(QLatin1Char(','));
            QString display = comma >= 0 ? line.mid(comma + 1).trimmed() : QString();
            if (display.isEmpty()) {
                display = attrValue(line, QStringLiteral("tvg-name"));
            }
            if (display.isEmpty()) {
                display = attrValue(line, QStringLiteral("tvg-id"));
            }
            pending.name = display;
            pending.logo = attrValue(line, QStringLiteral("tvg-logo"));

            const QString groupTitle = attrValue(line, QStringLiteral("group-title"));
            if (!groupTitle.isEmpty()) {
                currentGroup = groupTitle;
            }
            pending.group = currentGroup;
            continue;
        }

        if (line.startsWith(QStringLiteral("#EXTGRP:"), Qt::CaseInsensitive)) {
            const QString g = line.mid(8).trimmed();
            if (!g.isEmpty()) {
                currentGroup = g;
                pending.group = g;
            }
            continue;
        }

        if (line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (!looksLikeUrl(line)) {
            continue;
        }

        LiveChannel ch = pending;
        ch.url = firstUrl(line);
        if (ch.name.isEmpty()) {
            ch.name = QUrl(ch.url).fileName();
        }
        if (ch.name.isEmpty()) {
            ch.name = ch.url;
        }
        if (ch.group.isEmpty()) {
            ch.group = ungroupedName();
        }
        out.append(ch);

        // 一条 EXTINF 只对应一个地址，用掉即清空（分组沿用 currentGroup）
        pending = LiveChannel();
    }

    return out;
}

QList<LiveChannel> LiveParser::parseTxt(const QString &text)
{
    QList<LiveChannel> out;

    QString currentGroup;
    const QStringList lines = text.split(QLatin1Char('\n'));
    out.reserve(lines.size());

    for (const QString &raw : lines) {
        const QString line = QString(raw).remove(QLatin1Char('\r')).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        // 分组行：`央视,#genre#`（逗号两侧可能有空格，也可能没有）
        const int genre = line.indexOf(QStringLiteral("#genre#"), 0, Qt::CaseInsensitive);
        if (genre >= 0) {
            QString group = line.left(genre).trimmed();
            if (group.endsWith(QLatin1Char(','))) {
                group.chop(1);
            }
            currentGroup = group.trimmed();
            continue;
        }

        const int comma = line.indexOf(QLatin1Char(','));
        if (comma <= 0) {
            // 没有逗号但整行是地址：当成无名频道收下，总比丢掉强
            if (looksLikeUrl(line)) {
                LiveChannel ch;
                ch.url = firstUrl(line);
                ch.name = QUrl(ch.url).fileName();
                ch.group = currentGroup.isEmpty() ? ungroupedName() : currentGroup;
                out.append(ch);
            }
            continue;
        }

        LiveChannel ch;
        ch.name = line.left(comma).trimmed();
        ch.url = firstUrl(line.mid(comma + 1));
        ch.group = currentGroup.isEmpty() ? ungroupedName() : currentGroup;
        if (ch.name.isEmpty()) {
            ch.name = ch.url;
        }
        if (ch.isValid()) {
            out.append(ch);
        }
    }

    return out;
}

QList<LiveChannel> LiveParser::parseBareList(const QString &text)
{
    QList<LiveChannel> out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = QString(raw).remove(QLatin1Char('\r')).trimmed();
        if (!looksLikeUrl(line)) {
            continue;
        }
        LiveChannel ch;
        ch.url = firstUrl(line);
        ch.name = QStringLiteral("频道 %1").arg(out.size() + 1);
        ch.group = ungroupedName();
        out.append(ch);
    }
    return out;
}

QList<LiveChannel> LiveParser::parse(const QByteArray &data)
{
    const QString text = decode(data);
    if (text.trimmed().isEmpty()) {
        return {};
    }

    const QString head = text.left(8192);
    if (head.contains(QStringLiteral("#EXTM3U"), Qt::CaseInsensitive)
        || head.contains(QStringLiteral("#EXTINF"), Qt::CaseInsensitive)) {
        return dedupe(parseM3u(text));
    }

    if (text.contains(QStringLiteral("#genre#"), Qt::CaseInsensitive)) {
        return dedupe(parseTxt(text));
    }

    // 有 `名称,地址` 结构才按 txt 解析；纯地址、一行一个的按裸列表处理，
    // 否则会被当成「频道名 = 文件名」的 txt，名字全是 1.m3u8 这种。
    bool hasNamedLine = false;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = QString(raw).remove(QLatin1Char('\r')).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const int comma = line.indexOf(QLatin1Char(','));
        if (comma > 0 && looksLikeUrl(line.mid(comma + 1))) {
            hasNamedLine = true;
            break;
        }
    }

    if (hasNamedLine) {
        return dedupe(parseTxt(text));
    }
    return dedupe(parseBareList(text));
}

QStringList LiveParser::groups(const QList<LiveChannel> &channels)
{
    QStringList result;
    QSet<QString> seen;
    for (const LiveChannel &ch : channels) {
        const QString group = ch.group.isEmpty() ? ungroupedName() : ch.group;
        if (seen.contains(group)) {
            continue;
        }
        seen.insert(group);
        result.append(group);
    }
    return result;
}
