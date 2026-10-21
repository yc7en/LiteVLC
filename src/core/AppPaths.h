#pragma once

#include <QString>

/**
 * @brief 数据文件存放位置（配置 / 片源 / 进度历史）
 *
 * 绿色版（解压即用）：程序目录可写时，配置就近放在 exe 旁边，整个文件夹拷走即迁移；
 * 安装版（Program Files）：目录受系统保护，普通用户写不进去，
 * 自动回退到 %APPDATA%/LiteVlcPlayer，避免设置静默丢失。
 */
namespace AppPaths {

/** 可写数据目录（结果缓存，进程内只算一次） */
QString dataDir();

/** 数据目录下的文件完整路径 */
QString filePath(const QString &fileName);

} // namespace AppPaths
