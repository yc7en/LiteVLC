[Eng](README.md) | **中文**

# LiteVLC Player

![Version](https://img.shields.io/badge/version-1.1.6-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20%2F%20macOS-6a0dad)
![Stars](https://img.shields.io/github/stars/yc7en/LiteVLC)
![Forks](https://img.shields.io/github/forks/yc7en/LiteVLC)

> 基于 **Qt 6 (C++ / QWidget)** 与 **LibVLC** 打造的极简、高性能本地视频播放器，并支持导入 **TVBox 兼容的在线片源**做搜索与追剧。

<p align="center">
  <img src="docs/images/screenshot-main.png" width="640" alt="LiteVLC Player 主界面" /><br/>
  <img src="docs/images/screenshot-online-search.png" width="640" alt="在线片源搜索" />
</p>

## 概述

LiteVLC Player 剥离了原版 VLC 臃肿的流媒体、媒体库与皮肤界面，只保留核心高频看片功能。它支持本地视频硬件加速播放、播放进度记忆，并可导入标准 TVBox / CatVod 订阅，搜索与观看在线剧集与直播频道。支持 **Windows** 与 **macOS**（Apple Silicon + Intel）。

## 功能特性

### 核心播放

- **播放控制**：播放 / 暂停 / 停止、自定义"指哪跳哪"进度条（`ClickableSlider`）、拖拽防抖跳转、`Left` / `Right` 快退/快进 5 秒。
- **音量与声轨**：鼠标滚轮或 `Up` / `Down` 调节音量、`M` 一键静音、多语言声轨自动识别与实时切换。
- **画面与显示**：双击或 `Enter` / `F` 全屏、宽高比预设（`原比例` / `16:9` / `4:3` / `21:9` / `1:1`）、基于 Win32 `SetWindowPos` 的零闪烁窗口置顶（`T`）。
- **加载与续播**：将视频或字幕拖进窗口即可播放；自动记录每个视频的播放进度，下次打开跳回原位置并显示 OSD 提示。

### 高级功能

- **外挂字幕与时间轴微调**：自动挂载同名（或带语言后缀如 `.zh.srt`）的 `.srt` / `.ass` / `.ssa` 字幕；`[` / `]` 以 ±0.5 秒步进微调。
- **倍速播放（0.5× – 3.0×）**：下拉框或 `C` / `V` 调节，声音保持原调。
- **轻量播放列表**：右侧可折叠 `QListWidget` 抽屉，顺序 / 列表循环 / 单曲循环；本地文件与在线剧集共用一张队列。
- **GPU 硬解**：设置中可切换（`Auto` / `D3D11VA` / `DXVA2` / `软解`），4K 播放极低 CPU 占用。
- **自动跳过片头片尾**：设定秒数后，连播自动跳片头并在片尾切下一集。

### 在线片源（兼容 TVBox 订阅）

- **自定义源**：导入 TVBox / CatVod 标准订阅 JSON（订阅链接 / 本地文件 / 剪贴板）；按内容形态而非声明类型识别（原样 JSON、带注释 JSON、整份 base64、藏在 JPEG 里的配置）。
- **只对接苹果 CMS 接口**：`type=0`（XML）与 `type=1`（JSON），与 TVBox 逐字段对齐；`jar` / `js` / `py` 脚本 Spider 需要额外运行时，导入时自动跳过。
- **多源并发搜索**：指定单源或全部源并发搜索，结果按来源分组实时返回。
- **选集与多线路**：自动解析 `vod_play_from` / `vod_play_url`，拆成多条线路与剧集列表。
- **统一播放列表**："▶ 播放选中剧集"整条入列并起播；搜索窗为非模态；连播带保护（连续失败自动停，不会跳完整季）。
- **防盗链**：按源配置注入 `User-Agent` / `Referer`（Referer 缺省取接口 origin）。
- **HLS 插播广告过滤**：在线 m3u8 走本地中继，按 `#EXT-X-DISCONTINUITY` 剔除广告短块；分片仍直连 CDN。
- **直播频道（TVBox `lives`）**：同一份配置一并导入直播源（`m3u` / `txt`），「直播」页支持分组与筛选，整组进列表、`PgUp` / `PgDn` 换台；直播条目刻意绕开 HLS 中继。

### 离线缓存（整片下载）

- 播放列表右键 →「缓存本集到本地」；只做单集，不支持直播。
- **地址过期也能接着下**：缓存身份是「站点 + 影片 id + 线路 + 集数」，分片按累计播放时间轴建索引；遇 403/404/410 自动回源重新取地址，重新对齐后**只补缺口**。
- **缓存列表**（`Ctrl+Shift+D`）：进度 / 大小 / 速度 / 真实失败原因；可暂停、重试、删除（可选连文件）、打开目录。
- **命中本地就播**：完整缓存的条目直接走本地，不走网络、不经中继。
- **导出单文件**：合并为 `.ts` / `.mp4`（纯字节拼接，不转码）；`ffmpeg` 在 PATH 时才额外提供无损封装为标准 MP4 的选项。
- **限流**：任务数与分片连接数可调，支持全局 KB/s 限速；**有视频在播时自动让路**。
- **不支持**：加密源（`#EXT-X-KEY` / AES-128）明确拒绝。

### 桌面集成与分发

- **应用图标**：程序 / 快捷方式 / 任务栏统一使用内置图标，exe 内嵌版本信息。
- **手动检查更新（预留）**：「帮助 → 检查更新」只回显当前版本，不后台自动检查/下载；更新源为 GitHub Releases（`yc7en/LiteVLC`，由 `-DAPP_UPDATE_REPO` 注入）。
- **Windows 安装包**：`package.bat` 一键完成 编译 → `windeployqt` → 复制 LibVLC → Inno Setup，产出 `LiteVlcPlayer-<版本>-Setup.exe`。
- **配置目录自适应**：绿色版配置就地存放；安装到 `Program Files` 后自动回退到 `%APPDATA%\LiteVlcPlayer`。

## 快捷键一览

| 快捷键 | 功能说明 |
| :--- | :--- |
| `Space` | 播放 / 暂停 |
| `Enter` / `F` / 双击画面 | 进入 / 退出全屏（`Esc` 退出） |
| `Left` / `Right` | 快退 5 秒 / 快进 5 秒 |
| `Up` / `Down` / 鼠标滚轮 | 音量 +5% / −5% |
| `M` | 静音 / 取消静音 |
| `[` / `]` | 字幕时间轴提前 0.5 秒 / 延后 0.5 秒 |
| `C` / `V` | 播放倍速 −0.25× / +0.25×（`0.5×` – `3.0×`） |
| `T` | 窗口置顶开关 |
| `L` | 显示 / 隐藏右侧播放列表 |
| `Ctrl+F` | 打开在线片源搜索 |
| `Ctrl+Shift+D` | 打开缓存下载列表 |
| `PgUp` / `PgDn` | 上一集 / 下一集（在线模式） |

## 编译与运行

环境要求：

- **Qt 6**（64 位，MSVC 或 MinGW）
- **CMake**（或 Qt Creator）
- **Inno Setup 6** —— 仅用于 Windows 安装包（`package.bat` 依次找 `E:\tools\innosetup\ISCC.exe`、标准安装路径，或环境变量 `INNO_SETUP`）

仓库已内置裁剪精简后的 **LibVLC 3.0.21 (win64) SDK 与运行库**（`vlc/` 目录），无需配置环境变量。

```bash
# 配置 + 编译（任意平台）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 一键打包 Windows 11 安装包（需 Inno Setup 6）
package.bat
```

- CMake 会在编译后自动把 `libvlc.dll` / `libvlccore.dll` 拷贝到可执行文件旁，并定位 `vlc/plugins`。
- macOS 通过 `macdeployqt` + `packaging/macos/bundle.sh` 产出 arm64（及 x64）`.app` / `.dmg`。
- CI（`windows.yml` / `macos.yml`）在推 `main` 或 `workflow_dispatch` 时构建；推 `v*` tag 会把 Setup.exe 与 dmg 挂到同一个 Release。版本号统一从 `CMakeLists.txt` 读取。
- 发布：在 `CMakeLists.txt`、`resources/app.rc`、`installer.iss`、`package.bat` 中统一提升版本号，然后 `git tag v<版本> && git push origin v<版本>`。

## 配置

在线源与播放器解耦：播放器只负责「拿到一个 http(s) 直链 + UA/Referer 就播」，源的可信度与可用性由你导入的订阅决定。

**导入方式**（底部控制栏「在线」按钮或 `Ctrl+F` →「源配置」）：

1. **从订阅链接导入** —— 填 TVBox 配置地址，直接拉取；
2. **导入本地 JSON** —— 选一个 TVBox 配置 / `sources.json`；
3. **从剪贴板粘贴** —— 复制一段订阅 JSON 后点一下。

**配置文件**：程序目录下的 `sources.json`（首次运行自动生成模板）。关键字段：`type`（`0` = 苹果 CMS XML，`1` = JSON）、`api`、`searchable`、`enabled`、`userAgent` / `referer`、`timeout`。

> 只支持可直接请求的苹果 CMS 接口。`jar` / `js` / `py` 脚本 Spider 需要 JVM / V8 / CPython 运行时，导入时会被跳过。

**播不了的时候看什么**：播放前先做地址预检，失败直接给出原因——源站 5xx、网关空页、403 防盗链、链接失效（404/410）、地址藏在加密脚本、或由页面 JS 运行时计算。播放器也会自动解析播放页（正文 m3u8、`player_aaaa`、`<video src>`、`<iframe>`、base64）并逐个验证候选。「另存响应内容」按钮可导出诊断文件。

## 项目结构

```
player/
├── src/            # 应用源码
├── resources/      # 图标、app.rc（版本信息）
├── packaging/      # macOS 打包脚本、Windows 安装配置
├── thirdparty/     # 内置的 VLC C 头文件（LGPL-2.1+）
├── vlc/            # LibVLC 3.0.21 win64 SDK/运行库（未纳入版本库，约 108MB）
├── docs/           # BugFix-YYMMDD-00XX.md / R-YYMMDD-00XX.md / 截图
├── .github/        # CI 工作流（windows.yml / macos.yml）
├── CMakeLists.txt  # 构建配置 + 版本号
├── player.pro      # qmake 工程（备选）
├── installer.iss   # Inno Setup 脚本
├── package.bat     # 一键 Windows 安装包
└── build.bat / dist/
```

## 变更记录

约定：**Bug 修复**写入 `docs/BugFix-YYMMDD-00XX.md`，**新功能迭代**写入 `docs/R-YYMMDD-00XX.md`；每次改动单独提交一次 git，commit message 带上对应文档文件名。完整历史见 `docs/` 各文件，重点摘录：

| 编号 | 类型 | 摘要 |
|---|---|---|
| `R-261026-0002` | 新功能 | 整片缓存（离线下载）：按累计时间轴索引分片、地址过期续下、缓存列表、"命中本地就播"、单文件导出 |
| `R-261026-0001` | 新功能 | 接入 TVBox 直播源（lives）：新增「直播」页（分组 / 筛选 / 整组换台），直播条目绕开 HLS 中继 |
| `R-261022-0009` | 新功能 | 支持 Intel Mac：macOS CI 矩阵化（x86_64 + arm64），dmg 名带平台标签 |
| `R-261022-0008` | 新功能 | 自动字幕 + 自动翻译（计划）：本地 ASR、llama.cpp 翻译、自绘字幕层 |
| `R-261021-0005` | 新功能 | HLS 插播广告过滤：本地中继改写 m3u8，按 DISCONTINUITY 剔除广告 |
| `R-261021-0001` | 新功能 | 在线片源：自定义源 / 搜索 / 在线播放闭环 |

## 贡献

欢迎提交 Issue 与 Pull Request。涉及功能或行为变更时，请保持 `docs/` 同步：每次改动单独提交一次 git，commit message 带上对应的 `BugFix-YYMMDD-00XX.md` / `R-YYMMDD-00XX.md` 文件名。

## 安全

播放器不内置任何自建更新服务器，更新源仅指向 GitHub Releases，且不在后台自动下载。发现安全漏洞请通过 GitHub Issue 私下反馈，请勿公开讨论尚未修复的细节。

## 许可证

本仓库当前未提供 `LICENSE` 文件，许可证尚未声明；内置的 VLC C 头文件为 LGPL-2.1+。发布前请先确定并补充许可证。

## 作者

[yc7en](https://github.com/yc7en)
