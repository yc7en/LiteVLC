#include "HlsFilter.h"

#include <QRegularExpression>
#include <algorithm>
#include <functional>
#include <numeric>
#include <vector>

namespace {

// 一「块」= 一次 EXT-X-DISCONTINUITY 之后的连续内容
struct Segment
{
    double duration = 0.0;
    bool isUri = false;      // true = 分片地址行（需要绝对化）
};

struct Block
{
    bool startsWithDiscontinuity = false;
    QStringList lines;       // 原样保留的标签行（EXTINF 在内，分片行已替换为标记？不——直接存原始行，输出时再改写）
    double duration = 0.0;
    int segmentCount = 0;
};

bool isDiscontinuityLine(const QString &line)
{
    return line.startsWith(QStringLiteral("#EXT-X-DISCONTINUITY"));
}

bool isHeaderLine(const QString &line)
{
    static const QStringList prefixes = {
        QStringLiteral("#EXTM3U"),
        QStringLiteral("#EXT-X-VERSION"),
        QStringLiteral("#EXT-X-PLAYLIST-TYPE"),
        QStringLiteral("#EXT-X-MEDIA-SEQUENCE"),
        QStringLiteral("#EXT-X-TARGETDURATION"),
        QStringLiteral("#EXT-X-ALLOW-CACHE"),
        QStringLiteral("#EXT-X-INDEPENDENT-SEGMENTS"),
    };
    for (const QString &p : prefixes) {
        if (line.startsWith(p)) {
            return true;
        }
    }
    return false;
}

double parseExtinf(const QString &line, bool *ok)
{
    // #EXTINF:4.171,  （逗号后的标题可能缺失）
    static const QRegularExpression re(
        QStringLiteral("^#EXTINF:\\s*([0-9]+(?:\\.[0-9]+)?)"));
    const auto m = re.match(line);
    if (m.hasMatch()) {
        if (ok) *ok = true;
        return m.captured(1).toDouble();
    }
    if (ok) *ok = false;
    return 0.0;
}

double medianOf(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

// 媒体清单：解析 -> 过滤 -> 重组
HlsFilter::Result processMedia(const QStringList &lines, const QUrl &base,
                               const std::function<QString(const QUrl &)> &wrapChild)
{
    Q_UNUSED(wrapChild);

    QStringList header;          // 头部标签（EXTM3U / VERSION / ...）
    QVector<Block> blocks;       // 内容块（头部之后的每一块）
    bool endList = false;
    bool seenSegment = false;    // 是否已进入内容区

    Block cur;
    bool curHasDiscontinuity = false;
    auto flush = [&]() {
        if (cur.segmentCount > 0 || !cur.lines.isEmpty()) {
            cur.startsWithDiscontinuity = curHasDiscontinuity;
            blocks.append(cur);
        }
        cur = Block();
        curHasDiscontinuity = false;
    };

    for (QString raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (isDiscontinuityLine(line)) {
            flush();
            curHasDiscontinuity = true;
            continue;
        }
        if (!seenSegment) {
            bool ok = false;
            parseExtinf(line, &ok);
            if (ok || (!line.startsWith(QLatin1Char('#')))) {
                seenSegment = true;
            } else if (isHeaderLine(line)) {
                header.append(line);
                continue;
            }
        }
        if (line == QStringLiteral("#EXT-X-ENDLIST")) {
            endList = true;
            continue;
        }
        cur.lines.append(line);
        if (line.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const double d = parseExtinf(line, &ok);
            if (ok) {
                cur.duration += d;
            }
        } else {
            ++cur.segmentCount;
        }
    }
    flush();

    HlsFilter::Result result;
    result.totalBlocks = static_cast<int>(blocks.size());

    // 分片地址绝对化（无论是否过滤都要做，方便中继只代理清单）
    const auto absolutize = [base](QStringList &blockLines) {
        for (QString &l : blockLines) {
            if (!l.startsWith(QLatin1Char('#'))) {
                l = base.resolved(QUrl(l)).toString(QUrl::FullyEncoded);
            }
        }
    };

    QVector<Block> content;      // 有分片的块
    QVector<int> contentIndex;   // 对应 blocks 下标（保持顺序）
    for (int i = 0; i < blocks.size(); ++i) {
        if (blocks[i].segmentCount > 0) {
            content.append(blocks[i]);
            contentIndex.append(i);
        } else {
            // 纯标签块（如 #EXT-X-MAP 初始化段），原样保留在头部之后
            header.append(blocks[i].lines);
        }
    }

    auto assemble = [&](const QVector<Block> &kept) -> QString {
        QString out;
        for (const QString &l : header) {
            out += l + QLatin1Char('\n');
        }
        bool firstContent = true;
        for (const Block &b : kept) {
            // 块与块之间原本就有 DISCONTINUITY，剔除广告块后保留其余边界
            if (!firstContent) {
                out += QStringLiteral("#EXT-X-DISCONTINUITY\n");
            }
            firstContent = false;
            QStringList ls = b.lines;
            absolutize(ls);
            for (const QString &l : ls) {
                out += l + QLatin1Char('\n');
            }
        }
        if (endList) {
            out += QStringLiteral("#EXT-X-ENDLIST\n");
        }
        return out;
    };

    // 块太少 / 全部块时长一致 -> 没有可用的区分信号，原样透传（只绝对化）
    if (content.size() < 5) {
        result.output = assemble(content);
        result.modified = false;
        return result;
    }

    std::vector<double> durs;
    durs.reserve(content.size());
    for (const Block &b : content) {
        durs.push_back(b.duration);
    }
    const double med = medianOf(durs);
    if (med <= 0.0) {
        result.output = assemble(content);
        result.modified = false;
        return result;
    }

    const double threshold = 0.65 * med;
    const double totalDur = std::accumulate(durs.begin(), durs.end(), 0.0);

    QVector<Block> kept;
    double removedDur = 0.0;
    int removedCount = 0;
    for (const Block &b : content) {
        if (b.duration < threshold) {
            ++removedCount;
            removedDur += b.duration;
        } else {
            kept.append(b);
        }
    }

    // 剔除量占比过高说明判定不可信（例如整部片本来就是碎片化编码），放弃过滤
    if (totalDur > 0.0 && removedDur / totalDur > 0.25) {
        result.output = assemble(content);
        result.modified = false;
        return result;
    }

    result.output = assemble(kept);
    result.modified = (removedCount > 0);
    result.removedBlocks = removedCount;
    result.removedSeconds = removedDur;
    return result;
}

// 主清单：子清单地址改写为继续走中继
HlsFilter::Result processMaster(const QStringList &lines, const QUrl &base,
                                const std::function<QString(const QUrl &)> &wrapChild)
{
    HlsFilter::Result result;
    result.isMaster = true;

    QString out;
    static const QRegularExpression uriAttr(
        QStringLiteral("URI=\"([^\"]+)\""));

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QString l = line;
        if (!line.startsWith(QLatin1Char('#'))) {
            // STREAM-INF 下一行的子清单地址
            if (wrapChild) {
                const QUrl child = base.resolved(QUrl(line));
                l = wrapChild(child);
            } else {
                l = base.resolved(QUrl(line)).toString(QUrl::FullyEncoded);
            }
        } else if (line.contains(QStringLiteral("URI=\""))) {
            // EXT-X-I-FRAME-STREAM-INF 等标签内嵌的 URI 属性
            auto it = uriAttr.globalMatch(l);
            QString replaced = l;
            int offset = 0;
            for (const auto &m : it) {
                const QUrl child = base.resolved(QUrl(m.captured(1)));
                const QString wrapped = wrapChild ? wrapChild(child)
                                                  : child.toString(QUrl::FullyEncoded);
                const int start = m.capturedStart(1) + offset;
                replaced.replace(start, m.capturedLength(1), wrapped);
                offset += wrapped.length() - m.capturedLength(1);
            }
            l = replaced;
        }
        out += l + QLatin1Char('\n');
    }
    result.output = out;
    result.modified = false;
    return result;
}

} // namespace

namespace HlsFilter {

Result process(const QString &body, const QUrl &upstreamUrl,
               const std::function<QString(const QUrl &)> &wrapChild)
{
    const QStringList lines = body.split(QLatin1Char('\n'));
    const QString trimmedBody = body.trimmed();

    if (trimmedBody.contains(QStringLiteral("#EXT-X-STREAM-INF"))) {
        return processMaster(lines, upstreamUrl, wrapChild);
    }
    if (trimmedBody.contains(QStringLiteral("#EXTINF"))) {
        return processMedia(lines, upstreamUrl, wrapChild);
    }
    // 不是标准 m3u8（可能是纯文本/错误页），原样交回
    Result r;
    r.output = body;
    r.modified = false;
    return r;
}

bool looksLikeHls(const QString &url)
{
    QString clean = url;
    const int q = clean.indexOf(QLatin1Char('?'));
    if (q >= 0) {
        clean.truncate(q);
    }
    return clean.endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
}

} // namespace HlsFilter
