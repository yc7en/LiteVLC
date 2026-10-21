#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/StreamProbe.h"

/** 解析链路最终交给播放器的东西 */
struct ResolvedStream
{
    QString url;
    QString userAgent;
    QString referer;
    QByteArray cookie;          // 目前永远为空：Cookie 只能靠中继或浏览器拿到，留字段占位
    bool needRelay = false;     // 分片级 header，必须本地中继才能播
    bool needCookie = false;
    QString via;                // direct / passthrough / sniff / iframe
};

enum class ResolveFailureKind
{
    InvalidUrl,
    HttpError,
    Timeout,
    TlsFailed,
    HtmlError,
    NoCandidate,    // 播放页里没找到候选地址
    NeedsBrowser,   // 地址由 JS 运行时计算
    Canceled,
};

/** 解析失败的全部信息（含诊断字段，供弹窗展示与另存响应） */
struct ResolveFailure
{
    ResolveFailureKind kind = ResolveFailureKind::InvalidUrl;
    QString url;
    QString error;
    QString userMessage;
    QString category;       // A~E，与 README 的分类一致
    int statusCode = 0;
    QString contentType;
    int bodySize = 0;
    QString pageTitle;
    bool hasIframe = false;
    bool hasEval = false;
    QByteArray body;        // 原始 HTML，供「另存响应内容」
    QStringList tried;      // 试过的候选地址
    QStringList iframes;    // 试过的内嵌页面
};

/**
 * @brief 播放地址解析链（direct → 页面嗅探 → 候选复检）
 *
 * 上游只跟它打交道，不需要知道「这次是直连还是嗅探出来的」：
 *   1. 直连预检通过 → 直接给出地址与实测可用的 UA/Referer
 *   2. 返回网页 → 提取候选地址（含 iframe 一层）→ 逐个复检 → 第一个能播的胜出
 *   3. 都不行 → 给出带分类和诊断信息的失败结果
 *
 * 后续如果要做本地中继或浏览器嗅探，只需在 `sniffAndAdvance()` 的末尾挂一环，
 * 上层不用改。
 */
class StreamResolver : public QObject
{
    Q_OBJECT
public:
    explicit StreamResolver(QObject *parent = nullptr);

    void resolve(const QString &url, const QString &userAgent, const QString &referer);
    void cancel();
    bool isRunning() const { return m_phase != Phase::Idle; }

signals:
    void resolved(const ResolvedStream &stream);
    void failed(const ResolveFailure &failure);
    void progress(const QString &text);

private:
    enum class Phase
    {
        Idle,
        Origin,     // 首探原始地址
        Candidate,  // 复检嗅探出的候选
        Doc,        // 抓取 iframe 页面
    };

    void onProbeFinished(const StreamProbeResult &result);
    void sniffAndAdvance();
    void finishFromVerdict();
    void finishWithFailure(const ResolveFailure &failure);
    ResolveFailure failureFromProbe(const StreamProbeResult &result) const;
    QStringList collectCandidateUrls() const;
    bool budgetLeft() const;

    struct SniffedDoc
    {
        QString url;
        QByteArray body;
    };

    StreamProbe *m_probe = nullptr;
    Phase m_phase = Phase::Idle;
    QElapsedTimer m_deadline;

    QString m_url;
    QString m_ua;
    QString m_ref;

    StreamProbeResult m_originResult;
    QList<SniffedDoc> m_docs;       // 已嗅探的页面，最多 2 个
    QStringList m_iframeQueue;
    int m_iframeIndex = 0;
    QSet<QString> m_tried;          // 已试过的地址（候选 + iframe）
    QStringList m_triedOrder;
    int m_probeCount = 0;
};
