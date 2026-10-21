#include "UpdateService.h"

#include <QByteArray>

#ifndef APP_VERSION
#define APP_VERSION "1.1.0"
#endif

namespace {

// 更新服务器地址的混淆存放：明文 -> 逐字节 XOR 0x5A -> Base64 -> 切三段乱序存放。
// kChunks 按乱序摆放，kOrder 记录还原时取片段的先后；明文只存在于运行时内存，
// 不写日志、不进界面、不进任何文件。
constexpr char kXorKey = 0x5A;
const char *const kChunks[] = { "pobHQ7KTM7", "YGtoamhs", "bnQjOW0/aG" };
constexpr int kOrder[] = { 2, 0, 1 };

} // namespace

QString UpdateService::currentVersion()
{
    return QStringLiteral(APP_VERSION);
}

QString UpdateService::platformTag()
{
    return QStringLiteral("win-x64");
}

QString UpdateService::channelName()
{
    return QStringLiteral("stable");
}

QString UpdateService::releaseEndpoint()
{
    QByteArray b64;
    for (int idx : kOrder) {
        b64 += kChunks[idx];
    }
    QByteArray raw = QByteArray::fromBase64(b64);
    for (char &c : raw) {
        c = static_cast<char>(c ^ kXorKey);
    }
    return QString::fromLatin1(raw);
}

QString UpdateService::manifestUrl()
{
    return QStringLiteral("http://%1/update/%2/manifest.json")
        .arg(releaseEndpoint(), platformTag());
}
