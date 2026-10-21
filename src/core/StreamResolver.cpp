#include "StreamResolver.h"
#include "PageSniffer.h"

#include <QUrl>
#include <algorithm>

namespace {

constexpr int kMaxProbes = 8;          // 整条链最多发出的请求数（含首探）
constexpr int kMaxDocs = 2;            // 最多嗅探 2 个页面（原页 + 一层 iframe）
constexpr int kTotalBudgetMs = 15000;  // 整条链的总预算

} // namespace

StreamResolver::StreamResolver(QObject *parent)
    : QObject(parent)
    , m_probe(new StreamProbe(this))
{
    connect(m_probe, &StreamProbe::finished, this, &StreamResolver::onProbeFinished);
}

void StreamResolver::cancel()
{
    if (m_probe) {
        m_probe->cancel();
    }
    m_phase = Phase::Idle;
    m_originResult = StreamProbeResult();
    m_docs.clear();
    m_iframeQueue.clear();
    m_iframeIndex = 0;
    m_tried.clear();
    m_triedOrder.clear();
    m_probeCount = 0;
}

bool StreamResolver::budgetLeft() const
{
    return !m_deadline.isValid() || m_deadline.elapsed() < kTotalBudgetMs;
}

void StreamResolver::resolve(const QString &url, const QString &userAgent, const QString &referer)
{
    cancel();

    m_url = url;
    m_ua = userAgent.trimmed();
    m_ref = referer.trimmed();
    m_deadline.start();

    m_phase = Phase::Origin;
    ++m_probeCount;
    m_tried.insert(sanitizeMediaUrl(url));

    emit progress(QStringLiteral("正在连接源站…"));
    m_probe->start(url, m_ua, m_ref, /*quickMode=*/false);
}

void StreamResolver::onProbeFinished(const StreamProbeResult &result)
{
    switch (m_phase) {
    case Phase::Origin: {
        m_originResult = result;

        if (result.ok) {
            ResolvedStream stream;
            stream.url = result.url.isEmpty() ? m_url : result.url;
            stream.userAgent = result.userAgent;
            stream.referer = result.referer;
            stream.via = result.kind == ProbeKind::PassThrough ? QStringLiteral("passthrough")
                                                               : QStringLiteral("direct");
            m_phase = Phase::Idle;
            emit resolved(stream);
            return;
        }

        if ((result.kind == ProbeKind::NeedsSniff || result.kind == ProbeKind::NeedsBrowser)
            && !result.body.isEmpty()) {
            const QString pageUrl = result.url.isEmpty() ? m_url : result.url;
            m_docs.append({ pageUrl, result.body });
            m_iframeQueue = PageSniffer::iframeUrls(pageUrl, result.body, 2);

            emit progress(QStringLiteral("返回的是网页，正在解析播放地址…"));
            sniffAndAdvance();
            return;
        }

        finishWithFailure(failureFromProbe(result));
        return;
    }

    case Phase::Candidate: {
        if (result.ok) {
            ResolvedStream stream;
            stream.url = result.url;
            stream.userAgent = result.userAgent;
            stream.referer = result.referer;
            stream.via = QStringLiteral("sniff");
            m_phase = Phase::Idle;
            emit resolved(stream);
            return;
        }
        // 候选本身也可能是播放页：页面里 `<iframe src=...>` 抠出来的地址就是这种。
        // 不把它当文档再嗅一层，iframe 型播放页会永远解析不成功。
        if ((result.kind == ProbeKind::NeedsSniff || result.kind == ProbeKind::NeedsBrowser)
            && !result.body.isEmpty() && m_docs.size() < kMaxDocs) {
            m_docs.append({ result.url, result.body });
            const QStringList nested = PageSniffer::iframeUrls(result.url, result.body, 1);
            for (const QString &u : nested) {
                if (!m_iframeQueue.contains(u)) {
                    m_iframeQueue.append(u);
                }
            }
        }
        sniffAndAdvance();
        return;
    }

    case Phase::Doc: {
        if (result.ok) {
            // iframe 直接指向媒体文件
            ResolvedStream stream;
            stream.url = result.url;
            stream.userAgent = result.userAgent;
            stream.referer = result.referer;
            stream.via = QStringLiteral("iframe");
            m_phase = Phase::Idle;
            emit resolved(stream);
            return;
        }
        // 这一层也是网页：继续嗅探（深度限制在 m_docs 的数量上）
        if ((result.kind == ProbeKind::NeedsSniff || result.kind == ProbeKind::NeedsBrowser)
            && !result.body.isEmpty() && m_docs.size() < kMaxDocs) {
            m_docs.append({ result.url, result.body });
            const QStringList nested = PageSniffer::iframeUrls(result.url, result.body, 1);
            for (const QString &u : nested) {
                if (!m_iframeQueue.contains(u)) {
                    m_iframeQueue.append(u);
                }
            }
        }
        sniffAndAdvance();
        return;
    }

    case Phase::Idle:
        return;
    }
}

QStringList StreamResolver::collectCandidateUrls() const
{
    QList<PageSniffer::Candidate> all;
    for (const SniffedDoc &doc : m_docs) {
        all.append(PageSniffer::extract(doc.url, doc.body));
    }
    std::sort(all.begin(), all.end(),
              [](const PageSniffer::Candidate &a, const PageSniffer::Candidate &b) {
                  return a.score > b.score;
              });

    QStringList urls;
    for (const PageSniffer::Candidate &c : all) {
        if (!urls.contains(c.url)) {
            urls.append(c.url);
        }
    }
    return urls;
}

void StreamResolver::sniffAndAdvance()
{
    if (!budgetLeft() || m_probeCount >= kMaxProbes) {
        finishFromVerdict();
        return;
    }

    // 1. 先试页面里直接给出的候选地址
    const QStringList candidates = collectCandidateUrls();
    for (const QString &url : candidates) {
        if (m_tried.contains(url)) {
            continue;
        }
        m_tried.insert(url);
        m_triedOrder.append(url);
        m_phase = Phase::Candidate;
        ++m_probeCount;
        emit progress(QStringLiteral("正在验证解析出的地址…"));
        m_probe->start(url, m_ua, m_ref, /*quickMode=*/true);
        return;
    }

    // 2. 再看内嵌页面（一层）
    while (m_iframeIndex < m_iframeQueue.size()) {
        const QString url = m_iframeQueue.at(m_iframeIndex++);
        if (m_tried.contains(url)) {
            continue;
        }
        m_tried.insert(url);
        m_triedOrder.append(url);
        m_phase = Phase::Doc;
        ++m_probeCount;
        emit progress(QStringLiteral("正在打开内嵌页面…"));
        m_probe->start(url, m_ua, m_ref, /*quickMode=*/true);
        return;
    }

    finishFromVerdict();
}

void StreamResolver::finishFromVerdict()
{
    ResolveFailure failure = failureFromProbe(m_originResult);

    bool jsComputed = false;
    for (const SniffedDoc &doc : m_docs) {
        if (PageSniffer::looksJsComputed(doc.body)) {
            jsComputed = true;
            break;
        }
    }

    if (jsComputed) {
        failure.kind = ResolveFailureKind::NeedsBrowser;
        failure.category = QStringLiteral("D");
        failure.userMessage =
            QStringLiteral("播放地址是网页 JS 运行时算出来的，纯文本解析拿不到（这类需要浏览器内核）");
    } else {
        failure.kind = ResolveFailureKind::NoCandidate;
        failure.category = QStringLiteral("C");
        failure.userMessage =
            QStringLiteral("返回的是播放页，但页面里没找到可播放的地址（可能藏在加密脚本里，或者需要登录）");
    }

    finishWithFailure(failure);
}

ResolveFailure StreamResolver::failureFromProbe(const StreamProbeResult &result) const
{
    ResolveFailure f;
    f.url = result.url.isEmpty() ? m_url : result.url;
    f.statusCode = result.statusCode;
    f.contentType = result.contentType;
    f.bodySize = result.bodySize;
    f.pageTitle = result.pageTitle;
    f.hasIframe = result.hasIframe;
    f.hasEval = result.hasEval;
    f.body = result.body;
    f.error = result.error;
    f.userMessage = result.userMessage;
    f.category = result.category();
    f.tried = m_triedOrder;

    for (const QString &u : m_iframeQueue) {
        f.iframes.append(u);
    }

    switch (result.kind) {
    case ProbeKind::NeedsSniff:
        f.kind = ResolveFailureKind::NoCandidate;
        break;
    case ProbeKind::NeedsBrowser:
        f.kind = ResolveFailureKind::NeedsBrowser;
        break;
    case ProbeKind::HtmlError:
        f.kind = ResolveFailureKind::HtmlError;
        break;
    case ProbeKind::Timeout:
        f.kind = ResolveFailureKind::Timeout;
        break;
    case ProbeKind::TlsFailed:
        f.kind = ResolveFailureKind::TlsFailed;
        break;
    default:
        f.kind = ResolveFailureKind::HttpError;
        break;
    }
    return f;
}

void StreamResolver::finishWithFailure(const ResolveFailure &failure)
{
    m_phase = Phase::Idle;
    emit failed(failure);
}
