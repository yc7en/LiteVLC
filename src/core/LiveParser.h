#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

/**
 * @brief 一个直播频道
 *
 * 由 m3u / txt 直播源解析而来，字段与 TVBox 直播源的概念对齐：
 * 名称、播放地址、分组（频道分类）。logo 只用于展示，取不到就留空。
 */
struct LiveChannel
{
    QString name;
    QString url;
    QString group;      // 分组名；解析不到时统一记为「未分组」
    QString logo;       // tvg-logo，可为空

    bool isValid() const { return !url.trimmed().isEmpty(); }
};

/**
 * @brief 直播源文本解析（m3u / txt）
 *
 * 纯函数：不联网、不依赖 GUI，方便单测与复用。
 *
 * 支持的三种形态：
 *  1. m3u / m3u8 —— `#EXTINF:-1 tvg-logo="..." group-title="央视",CCTV-1` 后跟地址行
 *  2. txt        —— `央视,#genre#` 之后每行 `频道名,播放地址`（TVBox 最常见的直播源格式）
 *  3. 裸地址列表 —— 一行一个地址，频道名按序号生成
 */
class LiveParser
{
public:
    /** 自动识别格式后解析；同一条目（名称+地址相同）自动去重 */
    static QList<LiveChannel> parse(const QByteArray &data);

    static QList<LiveChannel> parseM3u(const QString &text);
    static QList<LiveChannel> parseTxt(const QString &text);
    /** 裸地址列表：没有名称也没有分组，按出现顺序命名为「频道 1」「频道 2」 */
    static QList<LiveChannel> parseBareList(const QString &text);

    /** 响应体解码：UTF-8 优先，出现替换字符时回退系统编码（中文 Windows 即 GBK） */
    static QString decode(const QByteArray &data);

    /** 去重后的分组列表，保持首次出现顺序；空分组记作「未分组」 */
    static QStringList groups(const QList<LiveChannel> &channels);

    static QString ungroupedName();
};

Q_DECLARE_METATYPE(LiveChannel)
Q_DECLARE_METATYPE(QList<LiveChannel>)
