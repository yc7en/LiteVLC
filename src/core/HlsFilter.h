#pragma once

#include <QString>
#include <QUrl>
#include <functional>

/**
 * @brief HLS (m3u8) 清单解析与广告块过滤（纯逻辑，不联网，可独立单测）
 *
 * 站点把广告混进点播清单的常见手法：用 #EXT-X-DISCONTINUITY 把正片切成
 * 一段段「块」，在正片块之间插入时长很短的广告块。同一份正片里正片块的
 * 时长分布稳定（本例 286 块里正片块全部 >= 16.7s），广告块显著偏短
 * （全部 < 15s），且同一批广告会以完全相同的分片时长反复出现。
 *
 * 过滤判据（保守优先，宁漏勿误删）：
 * - 块数 >= 5 才启用（普通无广告清单只有 0~2 个 DISCONTINUITY，不碰）；
 * - 计算所有块时长的中位数 M，块时长 < 0.65 * M 判为广告块剔除；
 * - 剔除总量超过清单总时长的 25% 时放弃过滤（判定不可信，原样透传）。
 *
 * 分片本身不改写代理：改写为绝对 URL，播放内核直连 CDN；
 * 只有清单文本经过我们（见 HlsRelay）。
 */
namespace HlsFilter {

struct Result
{
    bool isMaster = false;       // 是否主清单（含 EXT-X-STREAM-INF）
    bool modified = false;       // 是否做了广告剔除（false = 原样透传）
    int removedBlocks = 0;       // 剔除的广告块数量
    double removedSeconds = 0.0; // 剔除的广告总时长
    int totalBlocks = 0;         // 媒体清单的分块总数
    QString output;              // 处理后的清单文本
};

/**
 * 处理一份 m3u8 文本。
 * @param body        上游返回的清单原文
 * @param upstreamUrl 上游清单地址（用于把相对 URI 解析成绝对地址）
 * @param wrapChild   主清单子清单地址的改写函数（返回继续走中继的本地地址）；
 *                    媒体清单不使用它。传空函数表示不改写（用于单测）。
 */
Result process(const QString &body, const QUrl &upstreamUrl,
               const std::function<QString(const QUrl &)> &wrapChild);

/** 是否疑似 HLS 地址（后缀判断，用于决定要不要走中继） */
bool looksLikeHls(const QString &url);

} // namespace HlsFilter
