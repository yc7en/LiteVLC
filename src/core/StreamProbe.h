#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief 清理用户/接口给出的播放地址
 *
 * 真实源站里经常混进来的脏东西：
 *   - 前后空白、换行（从 HTML/JSON 里抠出来时最容易带）
 *   - HTML 实体残留的 &amp;（XML 未完整解码时）
 *   - 中文/空格未做百分号编码（LibVLC 会当成非法 URI 直接失败）
 */
QString sanitizeMediaUrl(const QString &raw);

/** 预检结论的类型。区分它是因为每种情况的对策完全不同。 */
enum class ProbeKind
{
    Media,          // 拿到了媒体内容 / 播放清单
    PassThrough,    // 非 http(s) 协议，直接交给 LibVLC
    NeedsSniff,     // HTML 文档，页面里可能有可解析的线索
    NeedsBrowser,   // HTML 且像「地址由 JS 运行时算出来」，纯文本解析无解
    HtmlError,      // HTML 但更像回源错误页（状态码非 2xx / 体积过小）
    HttpError,      // 401/403/404/5xx
    Timeout,        // 连接超时
    TlsFailed,      // 证书问题（Qt 与 LibVLC 结论可能不同，不作为拦截依据）
    EmptyBody,      // 200 但空内容
    NotMedia,       // 2xx 但不是媒体（json/plain 等）
    InvalidUrl,     // 地址非法
};

/** 一次地址预检的结果 */
struct StreamProbeResult
{
    bool ok = false;
    ProbeKind kind = ProbeKind::InvalidUrl;
    QString url;            // 预检通过的地址（跟随重定向后的地址不强制替换，仍用原始地址）
    QString userAgent;      // 这组 header 是实测能拿到流的
    QString referer;
    int statusCode = 0;
    QString contentType;
    int bodySize = 0;
    bool isHls = false;
    bool isHtmlPage = false;

    // —— 诊断信息（失败时给用户看，也便于以后统计失败分布）——
    QString pageTitle;      // <title>
    bool hasIframe = false;
    bool hasEval = false;
    QByteArray body;        // HTML 文档正文（仅 NeedsSniff / NeedsBrowser 时保留，上限 128KB）

    QString error;          // 失败原因（给程序看）
    QString userMessage;    // 失败原因（给用户看）

    /** 转成 A~E 分类字母，便于用户对照 README */
    QString category() const;
    /** 分类的中文说明 */
    QString categoryText() const;
};

/**
 * @brief 播放前的地址预检
 *
 * 解决的是「LibVLC 拿不到流但界面上一点提示都没有」这个问题：
 * 先用和播放器几乎一致的请求方式探一次，把结果分成几类——
 *   1. 能拿到媒体内容 → 把「能用的那组 UA/Referer」交给 LibVLC
 *   2. 返回 HTML → 再细分：可能可解析的播放页 / 错误页 / JS 计算页
 *   3. 403 / 404 / 超时 / 证书 → 把状态码翻成人话提示出来
 *
 * 探测顺序就是防盗链的常见破解顺序：原样 → 去 Referer → 全裸 → 换成媒体域名做 Referer。
 */
class StreamProbe : public QObject
{
    Q_OBJECT
public:
    explicit StreamProbe(QObject *parent = nullptr);

    /**
     * @brief 开始探测；会先取消上一次未完成的探测
     * @param quickMode 候选地址复检用：超时 3s、总预算 6s、只试前两组 header
     */
    void start(const QString &url, const QString &userAgent, const QString &referer,
               bool quickMode = false);
    void cancel();
    bool isRunning() const { return m_reply != nullptr; }

signals:
    void finished(const StreamProbeResult &result);

private:
    struct Strategy
    {
        QString userAgent;
        QString referer;
    };

    void startNext();
    void evaluate();
    void finishFailure(const QString &error, const QString &userMessage);
    ProbeKind classifyFailure() const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;

    QList<Strategy> m_strategies;
    int m_strategyIndex = 0;

    QString m_url;
    QByteArray m_body;
    bool m_cappedByUs = false;
    bool m_capDecided = false;
    int m_bodyCap = 8192;

    int m_lastStatus = 0;
    QString m_lastContentType;
    QString m_lastError;        // 最近一次失败原因
    QString m_lastUserMessage;
    ProbeKind m_lastKind = ProbeKind::InvalidUrl;
    int m_lastBodySize = 0;
    bool m_sawHtml = false;
    bool m_quickMode = false;

    // 见过的最像「播放页」的那份 HTML（分组 header 可能有的返回错误页、有的返回播放页）
    QByteArray m_bestHtmlBody;
    int m_bestHtmlStatus = 0;
    QString m_bestHtmlContentType;

    QElapsedTimer m_deadline;   // 整体预算，避免 4 组 header 串行超时拖太久
};
