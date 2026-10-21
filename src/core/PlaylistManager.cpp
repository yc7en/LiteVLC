#include "PlaylistManager.h"
#include <QFileInfo>

PlaylistManager::PlaylistManager(QObject *parent)
    : QObject(parent)
{
}

QStringList PlaylistManager::files() const
{
    QStringList list;
    list.reserve(m_entries.size());
    for (const PlaylistEntry &entry : m_entries) {
        list.append(entry.location);
    }
    return list;
}

PlaylistEntry PlaylistManager::currentEntry() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_entries.size()) {
        return m_entries.at(m_currentIndex);
    }
    return PlaylistEntry();
}

PlaylistEntry PlaylistManager::entryAt(int index) const
{
    if (index >= 0 && index < m_entries.size()) {
        return m_entries.at(index);
    }
    return PlaylistEntry();
}

int PlaylistManager::indexOf(const QString &location) const
{
    if (location.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).location == location) {
            return i;
        }
    }
    return -1;
}

int PlaylistManager::addEntries(const QList<PlaylistEntry> &entries)
{
    int firstAddedIndex = -1;
    bool changed = false;

    for (const PlaylistEntry &raw : entries) {
        if (!raw.isValid()) {
            continue;
        }

        PlaylistEntry entry = raw;
        if (entry.kind == PlaylistEntry::Local) {
            const QString absPath = QFileInfo(entry.location).absoluteFilePath();
            if (absPath.isEmpty() || !QFileInfo::exists(absPath)) {
                continue;
            }
            entry.location = absPath;
            if (entry.title.isEmpty()) {
                entry.title = QFileInfo(absPath).fileName();
            }
        } else if (entry.title.isEmpty()) {
            entry.title = entry.location;
        }

        const int existingIdx = indexOf(entry.location);
        if (existingIdx >= 0) {
            // 同一个地址重复加入时，顺手把标题/来源刷新一下（接口数据可能变过）
            m_entries[existingIdx] = entry;
            if (firstAddedIndex < 0) {
                firstAddedIndex = existingIdx;
            }
        } else {
            m_entries.append(entry);
            changed = true;
            if (firstAddedIndex < 0) {
                firstAddedIndex = m_entries.size() - 1;
            }
        }
    }

    if (changed) {
        emit playlistChanged();
    }
    return firstAddedIndex;
}

int PlaylistManager::addFiles(const QStringList &filePaths)
{
    QList<PlaylistEntry> entries;
    entries.reserve(filePaths.size());
    for (const QString &path : filePaths) {
        PlaylistEntry entry;
        entry.kind = PlaylistEntry::Local;
        entry.location = path;
        entry.title = QFileInfo(path).fileName();
        entries.append(entry);
    }

    const int firstIdx = addEntries(entries);
    return firstIdx;
}

void PlaylistManager::removeAt(int index)
{
    if (index < 0 || index >= m_entries.size()) {
        return;
    }
    m_entries.removeAt(index);
    if (m_entries.isEmpty()) {
        m_currentIndex = -1;
    } else if (m_currentIndex >= m_entries.size()) {
        m_currentIndex = m_entries.size() - 1;
    } else if (index < m_currentIndex) {
        --m_currentIndex;
    }
    emit playlistChanged();
    emit currentIndexChanged(m_currentIndex);
}

void PlaylistManager::clear()
{
    m_entries.clear();
    m_currentIndex = -1;
    emit playlistChanged();
    emit currentIndexChanged(-1);
}

void PlaylistManager::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_entries.size()) {
        return;
    }
    m_currentIndex = index;
    emit currentIndexChanged(m_currentIndex);
}

PlaylistEntry PlaylistManager::nextEntryManual()
{
    if (m_entries.isEmpty()) {
        return PlaylistEntry();
    }
    const int nextIdx = (m_currentIndex + 1) % m_entries.size();
    setCurrentIndex(nextIdx);
    return currentEntry();
}

PlaylistEntry PlaylistManager::previousEntryManual()
{
    if (m_entries.isEmpty()) {
        return PlaylistEntry();
    }
    const int prevIdx = (m_currentIndex - 1 + m_entries.size()) % m_entries.size();
    setCurrentIndex(prevIdx);
    return currentEntry();
}

PlaylistEntry PlaylistManager::nextEntryOnEnd(ConfigManager::PlayMode mode)
{
    if (m_entries.isEmpty() || m_currentIndex < 0) {
        return PlaylistEntry();
    }

    switch (mode) {
    case ConfigManager::LoopOne:
        // 单曲循环：保持当前索引重播
        emit currentIndexChanged(m_currentIndex);
        return currentEntry();

    case ConfigManager::Sequential:
        // 顺序播放：播完最后一个即停止
        if (m_currentIndex + 1 < m_entries.size()) {
            setCurrentIndex(m_currentIndex + 1);
            return currentEntry();
        }
        return PlaylistEntry();

    case ConfigManager::LoopList:
    default:
        // 列表循环
        setCurrentIndex((m_currentIndex + 1) % m_entries.size());
        return currentEntry();
    }
}
