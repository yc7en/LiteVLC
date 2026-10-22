#!/bin/bash
# macOS 打包：把 VLC 的 lib 与 plugins 塞进 .app，修 install_name，做 ad-hoc 签名，出 dmg
#
# 用法：
#   packaging/macos/bundle.sh <LiteVlcPlayer.app> <VLC.app 的 Contents/MacOS 目录> [输出版本号]
#
# 为什么保持 VLC.app 内部的相对布局（MacOS/lib、MacOS/plugins）：
#   这样 libvlc / 插件的相对路径假设与它们在 VLC.app 里完全一致，不用逐个 guessing 改写。
#   我们只在主程序上设置 @executable_path/lib 这条 rpath，进程内所有 @rpath 引用都能命中。

set -euo pipefail

APP="${1:?用法: bundle.sh <LiteVlcPlayer.app> <VLC.app/Contents/MacOS> [version]}"
VLC_MACOS="${2:?缺少 VLC.app/Contents/MacOS 路径}"
VERSION="${3:-0.0.0}"

MACOS_DIR="$APP/Contents/MacOS"
LIB_DST="$MACOS_DIR/lib"
PLUGIN_DST="$MACOS_DIR/plugins"

echo "==> app:    $APP"
echo "==> vlc:    $VLC_MACOS"
echo "==> 版本:   $VERSION"

[ -d "$APP" ] || { echo "找不到 bundle: $APP"; exit 1; }
[ -f "$VLC_MACOS/lib/libvlc.dylib" ] || { echo "找不到 libvlc.dylib: $VLC_MACOS/lib"; exit 1; }

# ---------- 1. 拷贝 lib 与 plugins（保持 VLC.app 相对布局） ----------
echo "==> 拷贝 libvlc / plugins"
rm -rf "$LIB_DST" "$PLUGIN_DST"
mkdir -p "$LIB_DST"
cp -a "$VLC_MACOS/lib/"*.dylib "$LIB_DST/"
cp -a "$VLC_MACOS/plugins" "$PLUGIN_DST"

# ---------- 2. 修 install_name ----------
rewrite_refs() {
    local bin="$1"
    # 把任何指向 libvlc / libvlccore 的绝对路径或无版本名引用，统一改成 @rpath/<实际文件名>
    while IFS= read -r dep; do
        case "$(basename "$dep")" in
            libvlccore*.dylib|libvlc.*.dylib|libvlc.dylib|libvlccore.dylib)
                install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$bin" 2>/dev/null || true
                ;;
        esac
    done < <(otool -L "$bin" | tail -n +2 | awk '{print $1}')
}

echo "==> 修 lib 的 install_name"
for dylib in "$LIB_DST"/*.dylib; do
    install_name_tool -id "@rpath/$(basename "$dylib")" "$dylib" 2>/dev/null || true
    rewrite_refs "$dylib"
done

echo "==> 修主程序依赖与 rpath"
EXE="$MACOS_DIR/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist")"
[ -f "$EXE" ] || { echo "找不到可执行文件: $EXE"; exit 1; }
rewrite_refs "$EXE"
install_name_tool -add_rpath "@executable_path/lib" "$EXE" 2>/dev/null || true

echo "==> 修插件依赖（逐个加 rpath）"
PLUGIN_COUNT=0
while IFS= read -r -d '' plugin; do
    rewrite_refs "$plugin"
    install_name_tool -add_rpath "@executable_path/lib" "$plugin" 2>/dev/null || true
    PLUGIN_COUNT=$((PLUGIN_COUNT + 1))
done < <(find "$PLUGIN_DST" -type f -name '*.dylib' -print0)
echo "    插件数: $PLUGIN_COUNT"

# ---------- 3. 架构自检：只允许 arm64 ----------
echo "==> 架构自检（arm64）"
BAD_ARCH=0
while IFS= read -r -d '' bin; do
    archs="$(lipo -archs "$bin" 2>/dev/null || echo unknown)"
    case "$archs" in
        *arm64*) ;;
        *) echo "    非 arm64: $bin ($archs)"; BAD_ARCH=1 ;;
    esac
done < <(find "$APP/Contents" -type f \( -name '*.dylib' -o -perm -100 \) -print0)

if [ "$BAD_ARCH" -ne 0 ]; then
    echo "存在非 arm64 二进制，已中止（Apple Silicon 版本必须全部 arm64）"
    exit 2
fi
echo "    通过"

# ---------- 4. ad-hoc 签名（arm64 必须签名，否则系统直接 kill） ----------
echo "==> ad-hoc 签名"
while IFS= read -r -d '' bin; do
    codesign --force --sign - --timestamp=none "$bin" >/dev/null 2>&1 || true
done < <(find "$APP/Contents" -type f \( -name '*.dylib' -o -perm -100 \) -print0)
codesign --force --sign - --timestamp=none "$APP" >/dev/null

echo "==> 校验签名"
codesign --verify --deep --strict "$APP" && echo "    签名有效"

# ---------- 5. dmg ----------
if [ "${MAKE_DMG:-1}" = "1" ]; then
    OUT_DIR="$(cd "$(dirname "$APP")" && pwd)"
    DMG="$OUT_DIR/LiteVlcPlayer-${VERSION}-arm64.dmg"
    echo "==> 生成 dmg: $DMG"
    rm -rf "$OUT_DIR/dmg-stage"
    mkdir -p "$OUT_DIR/dmg-stage"
    cp -a "$APP" "$OUT_DIR/dmg-stage/"
    ln -s /Applications "$OUT_DIR/dmg-stage/Applications"
    cat > "$OUT_DIR/dmg-stage/首次打开说明.txt" <<'TXT'
如果 macOS 提示「无法验证开发者」：
  1. 在 Finder 里右键点 LiteVlcPlayer.app，选「打开」，再点「打开」；
  2. 或执行：xattr -dr com.apple.quarantine /Applications/LiteVlcPlayer.app

本版本为 ad-hoc 签名（没有 Apple 开发者证书），不影响功能。
TXT
    hdiutil create -volname "LiteVlcPlayer ${VERSION}" -srcfolder "$OUT_DIR/dmg-stage" \
        -ov -format UDZO "$DMG" >/dev/null
    rm -rf "$OUT_DIR/dmg-stage"
    echo "==> 完成: $DMG"
fi

echo "==> 打包完成"
