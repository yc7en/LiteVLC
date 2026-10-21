#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief 网页内容识别与地址嗅探
 *
 * 只做纯文本分析，不联网、不执行 JS。所有方法都是静态的，便于单独测试。
 *
 * 之所以要区分「网页」的几种情况：源站返回 HTML 时的对策完全不同——
 *   A 回源错误页（502/403/404 的 nginx/Cloudflare 页）→ 换线路，嗅探无意义
 *   B 防盗链/签名缺失（302 回首页或提示 auth_key）→ 回 CMS 重新取地址
 *   C 真播放页（HTML 内嵌 m3u8 / iframe）→ 嗅探有效
 *   D JS 运行时计算地址（eval/atob/document.write）→ 需要浏览器内核
 *   E 地址编码不匹配（中文路径按 GBK 解释）→ 换编码重试
 */
class PageSniffer
{
public:
    /** 一个候选播放地址 */
    struct Candidate
    {
        QString url;
        QString rule;    // 命中的规则（literal/key/attr/base64），便于排查
        int score = 0;   // 越大越可能是真正的播放地址
    };

    /** 从响应体解码文本：UTF-8 优先，出替换字符时回退系统编码（中文 Windows 即 GBK） */
    static QString decodeText(const QByteArray &body);

    /** 提取 <title>，失败返回空 */
    static QString titleOf(const QByteArray &body);

    /** 是否含 <iframe>（播放页最常见的嵌套方式） */
    static bool hasIframe(const QByteArray &body);

    /** 是否像「地址由 JS 运行时拼出来」——这类必须执行 JS，纯文本解析无解 */
    static bool looksJsComputed(const QByteArray &body);

    /** 文本里是否有明显的播放清单特征（#EXTM3U、.m3u8、player_aaaa 等） */
    static bool containsPlaylistMark(const QByteArray &body);

    /** 页面上出现 <iframe> 的地址（绝对化、去重，最多 maxCount 个） */
    static QStringList iframeUrls(const QString &pageUrl, const QByteArray &body, int maxCount = 2);

    /**
     * @brief 从页面里提取候选播放地址
     *
     * 规则按命中率排序：地址字面量 → 常见键（player_aaaa / hls 配置）→ 媒体标签属性
     * → base64 编码串。相对路径按 pageUrl 解析，结果按 score 降序、去重、最多 20 条。
     */
    static QList<Candidate> extract(const QString &pageUrl, const QByteArray &body);
};
