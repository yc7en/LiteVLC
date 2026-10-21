#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

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

/** 一次地址预检的结果 */
struct StreamProbeResult
{
    bool ok = false;
    QString url;            // 预检通过的地址（跟随重定向后的地址不强制替换，仍用原始地址）
    QString userAgent;      // 这组 header 是实测能拿到流的
    QString referer;
    int statusCode = 0;
    QString contentType;
    bool isHls = false;
    bool isHtmlPage = false;
    QString error;          // 失败原因（给程序看）
    QString userMessage;    // 失败原因（给用户看）
};

/**
 * @brief 播放前的地址预检
 *
 * 解决的是「LibVLC 拿不到流但界面上一点提示都没有」这个问题：
 * 先用和播放器几乎一致的请求方式探一次，把结果分成三类——
 *   1. 能拿到媒体内容 → 把「能用的那组 UA/Referer」交给 LibVLC
 *   2. 返回 HTML 页面 → 说明这不是直链（多半是解析页），直接告诉用户
 *   3. 403 / 404 / 超时 → 把状态码翻成人话提示出来
 *
 * 探测顺序就是防盗链的常见破解顺序：原样 → 去 Referer → 全裸 → 换成媒体域名做 Referer。
 */
class StreamProbe : public QObject
{
    Q_OBJECT
public:
    explicit StreamProbe(QObject *parent = nullptr);

    /** 开始探测；会先取消上一次未完成的探测 */
    void start(const QString &url, const QString &userAgent, const QString &referer);
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

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;

    QList<Strategy> m_strategies;
    int m_strategyIndex = 0;

    QString m_url;
    QByteArray m_body;
    bool m_cappedByUs = false;

    int m_lastStatus = 0;
    QString m_lastContentType;
    QString m_lastError;        // 最近一次失败原因
    QString m_lastUserMessage;
    bool m_sawHtml = false;
    QElapsedTimer m_deadline;   // 整体预算，避免 4 组 header 串行超时拖太久
};
