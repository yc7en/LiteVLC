**Eng** | [中文](README.zh-CN.md)

# LiteVLC Player

![Version](https://img.shields.io/badge/version-1.1.6-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20%2F%20macOS-6a0dad)
![Stars](https://img.shields.io/github/stars/yc7en/LiteVLC)
![Forks](https://img.shields.io/github/forks/yc7en/LiteVLC)

> A minimal, high-performance local video player built on **Qt 6 (C++ / QWidget)** and **LibVLC**, with **TVBox-compatible online sources** for search and binge-watching.

<p align="center">
  <img src="docs/images/screenshot-main.png" width="640" alt="LiteVLC Player main window" /><br/>
  <img src="docs/images/screenshot-online-search.png" width="640" alt="Online source search" />
</p>

## Overview

LiteVLC Player strips the bloat of upstream VLC — streaming services, media library, skins — down to the core day-to-day playback experience. It plays local video files with GPU hardware acceleration, remembers playback position, and imports standard TVBox / CatVod subscriptions to search and watch online episodes and live channels. Available on **Windows** and **macOS** (Apple Silicon + Intel).

## Features

### Core playback

- **Transport controls** — play / pause / stop, a click-anywhere-to-seek progress bar (`ClickableSlider`), debounced drag seeking, and `Left` / `Right` arrows for ±5 s.
- **Volume and audio tracks** — mouse wheel or `Up` / `Down` for volume, `M` to mute, automatic detection and live switching of multiple audio tracks.
- **Picture and display** — full screen by double-click or `Enter` / `F`, aspect-ratio presets (`Native` / `16:9` / `4:3` / `21:9` / `1:1`), flicker-free always-on-top via Win32 `SetWindowPos` (`T`).
- **Loading and resume** — drag a video or subtitle onto the window to play it; playback position is remembered per file and restored with an on-screen OSD hint.

### Advanced

- **External subtitles and timing offset** — auto-loads a sibling `.srt` / `.ass` / `.ssa` file (language suffixes like `.zh.srt` recognised); `[` / `]` nudge the subtitle timeline by ±0.5 s.
- **Playback speed (0.5× – 3.0×)** — via the combo box or `C` / `V`, with pitch preserved.
- **Lightweight playlist** — a collapsible `QListWidget` drawer with sequential / list-loop / single-loop modes; local files and online episodes share one queue.
- **GPU acceleration** — hardware decoding switchable in Settings (`Auto` / `D3D11VA` / `DXVA2` / software); low CPU load even at 4K.
- **Auto-skip intro and outro** — set lengths in seconds; binge playback skips the intro and rolls to the next episode.

### Online sources (TVBox subscription compatible)

- **Custom sources** — import a standard TVBox / CatVod subscription JSON (URL, local file, or clipboard); dispatchers are matched on shape rather than declared type (plain JSON, commented JSON, whole-body base64, JPEG-embedded configs).
- **Apple CMS interfaces only** — `type=0` (XML) and `type=1` (JSON), aligned one-to-one with TVBox. `jar` / `js` / `py` script spiders are skipped on import (they need an extra runtime).
- **Concurrent multi-source search** — one source or all at once; results stream in grouped by source.
- **Episodes and lines** — `vod_play_from` / `vod_play_url` parsed into play lines + episode lists.
- **Unified playlist** — "play selected" appends the whole line and starts; non-modal search window; continuous playback with an advance guard.
- **Hotlink protection** — per-source `User-Agent` / `Referer` injection (Referer defaults to the API origin).
- **HLS mid-roll ad filtering** — online m3u8 routed through a local relay that drops short ad blocks on `#EXT-X-DISCONTINUITY`; segments still come straight from the CDN.
- **Live TV (TVBox `lives`)** — the same import picks up live sources (`m3u` / `txt`); a Live tab with grouping and filtering; channels queue as a group so `PgUp` / `PgDn` zaps. Live entries deliberately bypass the HLS relay.

### Offline cache (whole-movie download)

- Right-click an entry in the playlist → "Cache this episode for offline" (single episode, no live channels).
- **Expired URLs do not kill the cache** — identity is `source + vod id + line + episode index`, segments indexed by cumulative playback timeline; on 403/404/410 the task re-resolves, re-aligns and fetches only the gaps.
- **Cache list** (`Ctrl+Shift+D`) — progress, sizes, speed, real failure reason; pause, retry, delete (with files), open folder.
- **Cache hit plays local** — fully cached episodes play straight from disk (no network, no relay).
- **Export to a single file** — concatenate into `.ts` / `.mp4` (pure byte concat, no transcoding); optional ffmpeg remux appears only if `ffmpeg` is on `PATH`.
- **Throttling** — concurrent-task and connection limits, optional global KB/s limit, auto-yield while a video is playing.
- **Not supported** — encrypted sources (`#EXT-X-KEY` / AES-128) are rejected explicitly.

### Desktop integration and distribution

- **App icon** — program, shortcut and taskbar use the built-in icon; version info embedded in the exe.
- **Manual update check (reserved)** — "Help → Check for updates" only echoes the current version; update source is GitHub Releases (`yc7en/LiteVLC`, injected via `-DAPP_UPDATE_REPO`).
- **Windows installer** — `package.bat` chains compile → `windeployqt` → copy LibVLC → Inno Setup, producing `LiteVlcPlayer-<version>-Setup.exe`.
- **Adaptive config location** — portable builds keep config in place; installed under `Program Files` it falls back to `%APPDATA%\LiteVlcPlayer`.

## Keyboard shortcuts

| Shortcut | Action |
| :--- | :--- |
| `Space` | Play / pause |
| `Enter` / `F` / double-click | Enter / leave full screen (`Esc` also leaves) |
| `Left` / `Right` | Seek back / forward 5 s |
| `Up` / `Down` / mouse wheel | Volume +5% / −5% |
| `M` | Mute / unmute |
| `[` / `]` | Subtitle timing −0.5 s / +0.5 s |
| `C` / `V` | Playback speed −0.25× / +0.25× (`0.5×` – `3.0×`) |
| `T` | Toggle always-on-top |
| `L` | Show / hide the playlist drawer |
| `Ctrl+F` | Open online source search |
| `Ctrl+Shift+D` | Open the cache / download list |
| `PgUp` / `PgDn` | Previous / next episode (online mode) |

## Building

Prerequisites:

- **Qt 6** (64-bit, MSVC or MinGW)
- **CMake** (or Qt Creator)
- **Inno Setup 6** — only for the Windows installer (`package.bat` looks for `E:\tools\innosetup\ISCC.exe`, then the standard install path, or honours `INNO_SETUP`)

The repository ships a trimmed **LibVLC 3.0.21 (win64) SDK and runtime** under `vlc/` — no environment variables needed.

```bash
# Configure + build (any platform)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build

# One-command Windows 11 installer (requires Inno Setup 6)
package.bat
```

- CMake copies `libvlc.dll` / `libvlccore.dll` next to the executable and locates `vlc/plugins` on startup.
- macOS builds produce an arm64 (and x64) `.app` / `.dmg` via `macdeployqt` + `packaging/macos/bundle.sh`.
- CI (`windows.yml` / `macos.yml`) builds on push to `main` or `workflow_dispatch`; pushing a `v*` tag attaches Setup.exe + dmg to one Release. Version is read from `CMakeLists.txt`.
- Release: bump the version in `CMakeLists.txt`, `resources/app.rc`, `installer.iss`, `package.bat`, then `git tag v<version> && git push origin v<version>`.

## Configuration

Online sources are decoupled from the player: it only needs "an http(s) direct link plus UA/Referer"; reliability is up to the subscription you import.

**Import** — the "Online" button or `Ctrl+F` → "Sources":

1. **From a subscription URL** — paste a TVBox config address and fetch it.
2. **From a local JSON file** — pick a TVBox config or a `sources.json`.
3. **From the clipboard** — copy a subscription JSON and click once.

**Config file**: `sources.json` in the program directory (a template is generated on first run). Key fields: `type` (`0` = Apple CMS XML, `1` = JSON), `api`, `searchable`, `enabled`, `userAgent` / `referer`, `timeout`.

> Only directly requestable Apple CMS endpoints are supported. `jar` / `js` / `py` spiders need a JVM / V8 / CPython runtime and are skipped on import.

**When a video will not play**: every address is pre-checked before playback and the reason is reported directly — origin server error (5xx), near-empty gateway page, 403 hotlink, expired URL (404/410), URL hidden in encrypted script, or JS-computed address. The player also auto-resolves play pages (m3u8 in the body, `player_aaaa`, `<video src>`, `<iframe>`, base64) and verifies candidates. A "Save response body" button writes a diagnostic dump.

## Project structure

```
player/
├── src/            # application source
├── resources/      # icons, app.rc (version info)
├── packaging/      # macOS bundle scripts, Windows installer config
├── thirdparty/     # vendored VLC C headers (LGPL-2.1+)
├── vlc/            # LibVLC 3.0.21 win64 SDK/runtime (untracked, ~108 MB)
├── docs/           # BugFix-YYMMDD-00XX.md / R-YYMMDD-00XX.md / images
├── .github/        # CI workflows (windows.yml / macos.yml)
├── CMakeLists.txt  # build config + version
├── player.pro      # qmake project (alternative)
├── installer.iss   # Inno Setup script
├── package.bat     # one-command Windows installer
└── build.bat / dist/
```


## Contributing

Contributions are welcome. Please open an Issue or Pull Request. For feature or behaviour changes, keep `docs/` in sync: one git commit per change, with the commit message naming the corresponding `BugFix-YYMMDD-00XX.md` / `R-YYMMDD-00XX.md`.

## Security

The player does not embed any self-hosted update server: the update source is GitHub Releases only, and nothing is downloaded in the background. Report vulnerabilities privately via a GitHub Issue rather than discussing unreleased details in public.

## License

This repository does not currently ship a `LICENSE` file; the license is not yet declared. Vendored VLC C headers are LGPL-2.1+. Decide and add a license before publishing.

## Author

[yc7en](https://github.com/yc7en)
