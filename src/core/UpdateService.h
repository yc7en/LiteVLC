#pragma once

#include <QString>

/**
 * @brief 手动「检查更新」服务（预留功能，更新源为 GitHub Releases）
 *
 * 约定：
 * - 只保留 帮助 → 检查更新 一个手动入口，程序绝不自动联网检查、绝不自动下载；
 * - 更新源不再使用自建服务器，改为仓库的 GitHub Releases：
 *   最新版本 = `releases/latest`，安装包 = 该 release 的附件（dmg / Setup.exe）；
 * - 仓库地址由 CMake 变量 APP_UPDATE_REPO（owner/repo）注入，源码里只留占位；
 * - 本版本为预留：kUpdateEnabled == false，检查更新只回显当前版本号，
 *   不发起任何网络请求。后续接入时把开关置 true，请求 latestReleaseApiUrl()
 *   拿到 tag_name 与 assets，与 currentVersion() 比较即可。
 *
 * 注意：GitHub Releases 的匿名 API 只能读**公开仓库**。仓库若是私有，
 * 客户端必须内置 token（会被反编译提取），不建议；要么公开仓库，
 * 要么单独建一个只放 release 的公开仓库。
 */
class UpdateService
{
public:
    /** 更新功能总开关（预留：后续接入时置 true 并实现网络检查逻辑） */
    static constexpr bool kUpdateEnabled = false;

    /** 当前程序版本（与 CMakeLists / installer.iss 保持一致） */
    static QString currentVersion();

    /** 平台标记，用于在 release 附件里匹配安装包 */
    static QString platformTag();

    /** 更新通道 */
    static QString channelName();

    /** 仓库 slug（owner/repo）；未配置时返回空串，界面应提示「未配置更新源」 */
    static QString repository();

    /** GitHub API：最新 release（JSON，含 tag_name / assets / html_url） */
    static QString latestReleaseApiUrl();

    /** 给人看的 release 页面地址（备用：API 不可达时让用户手动去下载） */
    static QString releasePageUrl();

private:
    UpdateService() = default;
};
