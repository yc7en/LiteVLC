---
date: 2026-10-22
abstract: 项目首次接入 GitHub Actions，从零把 Windows 与 macOS 双端构建跑到全绿，期间踩了六个坑；把每个问题的现象、根因、修法和验证方式汇总成一篇，便于以后接入新平台时对照
status: 已处理
---

# GitHub Actions 构建问题复盘（2026-10-22）

## 背景

当天完成了两件事，问题集中出现在第一件：

1. **R-261022-0005**：Windows 也进 CI（此前只有 macOS），与 macOS 并行出包；
2. **R-261022-0007**：打 tag 自动建 Release 并挂上双端产物。

从第一次运行到双端全绿，共 9 个 commit、7 次运行。每个问题单独看都不难，
难在**CI 的日志接口需要仓库管理员权限，匿名 API 只能看到
`Process completed with exit code N`**——所有排错方法都是围绕"怎么在拿不到日志的
前提下拿到真实报错"展开的。

## 问题清单（按发现顺序）

### 1. 首次推送没有触发任何 workflow

- **现象**：`git push -u origin main`（18 个提交，含 `.github/workflows/macos.yml`）后，
  `actions/runs` 的 `total_count` 一直是 0；workflow 列表里状态是 `active`，
  Actions 页面只显示「There are no workflow runs yet」，没有任何禁用提示。
- **根因**：workflow 的 `on.push` 配了 `branches: [main]` + `paths: [...]` 过滤，
  新分支首次推送时路径匹配没有命中（GitHub 对首推的 paths 评估与后续提交不同）。
- **解决**：手动触发（workflow 里本来就开了 `workflow_dispatch`）；
  后续正常提交都能触发。之后又确认了 **tag push 不评估 `paths` 过滤**，
  这成了发版流程（R-261022-0007）能成立的前提。
- **教训**：paths 过滤适合"省 runner 分钟数"，但首推和发版这类关键节点不能依赖它。

### 2. macOS：链接失败 `library not found for -llibvlc`

- **现象**：编译全部通过，`clang++: error: linker command failed with exit code 1`，
  总耗时仅 17 秒（moc + 编译 + 链接一把梭，失败在最后一步）。
- **根因**：`target_link_libraries(... libvlc)` 在 macOS 上被展开成 `-llibvlc`，
  ld 按规则去找 `liblibvlc.dylib`——而实际文件叫 `libvlc.dylib`。
  MinGW 的 ld 会退回匹配 `libvlc.lib`，所以 **Windows 本地构建从来没暴露过这个问题**。
- **解决**：APPLE 分支不再用裸库名，直接把
  `${VLC_LIB_DIR}/libvlc.dylib`、`libvlccore.dylib` 的**绝对路径**塞进
  `target_link_libraries`；`bundle.sh` 本来就会把任何指向这两个库的依赖改写成
  `@rpath/<basename>`，运行时不受影响。
- **验证**：下一轮 CI 链接通过，dmg 正常产出。

### 3. Windows：下载并解开 VLC 一步 3 秒即失败

- **现象**：`curl.exe -fL` 下载 77MB 官方 zip 后调用 `7z x` 解压，步骤 3 秒退出，
  只有 `exit code 1`，没有一句话说明原因。
- **根因**（分两层）：
  - 第一层：步骤里所有失败分支都只 `throw "中文提示"`，真实原因被吞掉；
  - 第二层：补了诊断后看到 `7z exit=7`（命令行错误）——runner 上的 7z 调用方式与本地不一致。
- **解决**：放弃 7z，改用 **Windows 10+ 自带的 `tar.exe`（bsdtar）解 zip**，
  不引入额外依赖、速度更快；下载加第二个镜像与重试；每个失败分支用
  `::error title=...::` 把真实退出码和上下文写进注解。
- **教训**：CI 步骤里的每一个 `throw` 都应该是"能自解释"的——报什么、试过什么、
  退出码是多少，一次性说清楚。

### 4. Windows：windres 编译 app.rc 失败

- **现象**：编译步骤 `FAILED: [code=1] CMakeFiles/LiteVlcPlayer.dir/resources/app.rc.obj`。
- **根因**：`resources/app.rc` 里图标写的是**本机绝对路径**
  `IDI_ICON1 ICON "E:/02Code/player/resources/app.ico"`。这不是 CI 的问题，
  而是**一直存在的可移植性 bug**——换台机器、换个克隆目录都会挂，CI 只是第一次暴露它。
- **解决**：图标改成相对文件名 `"app.ico"`，由构建系统注入搜索路径：
  CMake 用 `set(CMAKE_RC_FLAGS "... -I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")`，
  qmake 用 `RC_INCLUDEPATH += $$PWD/resources`（双构建系统必须同步改）。
- **验证**：在 `E:/tmp/ciwin`（非项目目录、删掉本地 SDK、只放官方包解出的运行库）
  从零完整构建通过，证明改动真的可移植，而不是只在原路径上碰巧能跑。

### 5. Windows：Inno Setup 秒败，缺简体中文语言包

- **现象**：编译、windeployqt、dist 组装全部通过，最后打安装包一步 1 秒退出。
- **根因**：`installer.iss` 引用 `compiler:Languages\ChineseSimplified.isl`，
  **windows-2022 镜像的 Inno Setup 不带简体中文语言包**；本地那份是 6.0.5 完整解包版、
  带语言包，所以本地 35 秒能编出来的安装包在 CI 上秒挂。
- **解决**：把 `ChineseSimplified.isl`（18 KB）随仓库分发到 `packaging/windows/`，
  `installer.iss` 的 `MessagesFile` 改用相对路径；`.gitattributes` 给 `*.isl` 固定
  CRLF（配合 BOM），避免行尾归一化后 ISCC 读不了。
- **教训**：凡是用 `compiler:` / 系统目录 / 全局安装位置的东西，都要问一句
  "GitHub 的镜像上有没有"。同样的问题在 Maven 全局仓库、npm 全局包上都会出现。

### 6. macOS：VLC dmg 下载偶发失败

- **现象**：上一轮还 30 秒下载成功的步骤，下一轮 1 秒失败。
- **根因**：`get.videolan.org` 偶发抽风（CDN 调度问题），没有备用路径。
- **解决**：镜像列表（`get.videolan.org` + `download.videolan.org`）、
  连接超时与重试；`.sha256` 校验文件拿不到时降级为跳过校验（发 warning 不拦构建）——
  完整性校验不应该因为镜像缺一个附属文件而挡住整个发布。

## 横向经验

### 日志拿不到，就用注解把错误"递"出来

GitHub 的 job 日志下载接口（`/actions/jobs/{id}/logs`）要求仓库管理员权限，
匿名调用只返回 403；而 **check-run 的 annotations 接口是公开的**。
所以在两个 workflow 里都做了：

1. 编译输出 `tee` 到 `build.log`（pwsh 用 `Tee-Object`）；
2. 失败后 grep 关键行，`Write-Host "::error title=...::<报错文本>"`；
3. 从 `check-runs/{id}/annotations` 读注解，等于免费拿到了日志里最关键的几十行。

这条经验可以复用到任何仓库：**与其反复登录网页翻日志，不如让 CI 把错误送出来**。

### 先在本地预演 CI

推送一次失败的代价是 3~5 分钟等待外加一轮猜谜。下午搭了 `E:/tmp/ciwin`：
复制源码、删掉本地 SDK、只放官方发行包解出的运行库——完整复现 CI 的目录形态，
把配置、编译、windeployqt、ISCC 全部在本地走通后才推 CI。后面几个修复
（app.rc 相对路径、语言包入库）都是先本地验证再推的。

### 失败要"能自解释"

CI 步骤里所有失败分支统一改成：打印尝试过的候选（URL / 路径）+ 真实退出码 +
关键输出尾部，再 `::error::` 成注解。宁可多输出，不要只 `throw "失败"`。

### 其他小坑

- **Git Bash 下给 Windows 原生程序传 `/D` 参数会被 MSYS 路径转换**：
  `ISCC.exe /DMyAppVersion=1.1.3` 报 `You may not specify more than one script filename`。
  本地调试要用 `MSYS2_ARG_CONV_EXCL="*"` 或写成 `//D...`；CI 里用 pwsh 无此问题。
- **双构建系统必须同步**：CMake 和 qmake 都要改的（RC 搜索路径、宏定义），
  改了一个忘了另一个不会立刻报错，等用另一套构建时才炸。
- **`set(... CACHE ...)` 不覆盖旧缓存**：改 CMake 默认值后，已配置过的 build 目录
  必须显式 `-DXXX=...` 覆盖一次，否则 exe 里还是旧值（用 `strings`/`grep` 二进制确认）。

## 最终状态

- 双端 CI 全绿：macOS 产出 `LiteVlcPlayer-1.1.3-arm64.dmg`（62 MB），
  Windows 产出 `LiteVlcPlayer-1.1.3-Setup.exe`（43 MB）+ 便携 zip（78 MB）。
- `git tag v1.1.3` 一次推送，Release 自动生成并挂上全部三个附件，
  命名与 `UpdateService::platformTag()`（`win-x64` / `macos-arm64`）对齐，
  为将来开启手动检查更新铺好了路。

## 检索关键词

GitHub Actions 排错 CI 失败 复盘 注解 annotation ::error:: check-runs 公开 API
日志 403 管理员权限 -llibvlc liblibvlc dylib 链接 windres app.rc 绝对路径 相对路径
CMAKE_RC_FLAGS RC_INCLUDEPATH ChineseSimplified.isl 语言包 windows-2022 镜像
bsdtar tar.exe 7z exit 7 get.videolan.org 备用镜像 paths 过滤 tag push
MSYS2_ARG_CONV_EXCL ISCC /D 参数 CACHE 不覆盖 双构建系统同步
