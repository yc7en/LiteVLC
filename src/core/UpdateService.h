#pragma once

#include <QString>

/**
 * @brief 手动「检查更新」服务（预留功能）
 *
 * 约定：
 * - 只保留 帮助 → 检查更新 一个手动入口，程序绝不自动联网检查、绝不自动下载；
 * - 更新服务器地址不落明文：源码里只保存 XOR + Base64 后乱序切分的三个片段，
 *   运行时在内存中还原；不写入日志、界面、配置文件与文档；
 * - 本版本为预留：kUpdateEnabled == false，检查更新只回显当前版本号，
 *   不发起任何网络请求。后续接入时把开关置 true，在 checkNow() 里请求
 *   manifestUrl() 拉取版本清单即可（服务端只需要提供一份 JSON 清单与安装包）。
 */
class UpdateService
{
public:
    /** 更新功能总开关（预留：后续接入时置 true 并实现网络检查逻辑） */
    static constexpr bool kUpdateEnabled = false;

    /** 当前程序版本（与 CMakeLists / installer.iss 保持一致） */
    static QString currentVersion();

    /** 平台标记，用于更新清单里的安装包匹配 */
    static QString platformTag();

    /** 更新通道 */
    static QString channelName();

    /** 运行时还原更新服务器地址（host:port，仅存在于内存，禁止写日志 / 界面） */
    static QString releaseEndpoint();

    /** 版本清单地址（后续接入更新时使用） */
    static QString manifestUrl();

private:
    UpdateService() = default;
};
