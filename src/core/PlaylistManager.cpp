#include "PlaylistManager.h"
#include <QFileInfo>

PlaylistManager::PlaylistManager(QObject *parent)
    : QObject(parent)
{
}

QString PlaylistManager::currentFile() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
        return m_files.at(m_currentIndex);
    }
    return QString();
}

int PlaylistManager::addFiles(const QStringList &filePaths)
{
    int firstAddedIndex = -1;
    bool changed = false;

    for (const QString &rawPath : filePaths) {
        const QString absPath = QFileInfo(rawPath).absoluteFilePath();
        if (absPath.isEmpty() || !QFileInfo::exists(absPath)) {
            continue;
        }
        int existingIdx = -1;
        for (int i = 0; i < m_files.size(); ++i) {
            if (m_files.at(i).compare(absPath, Qt::CaseInsensitive) == 0) {
                existingIdx = i;
                break;
            }
        }
        if (existingIdx >= 0) {
            if (firstAddedIndex < 0) {
                firstAddedIndex = existingIdx;
            }
        } else {
            m_files.append(absPath);
            changed = true;
            if (firstAddedIndex < 0) {
                firstAddedIndex = m_files.size() - 1;
            }
        }
    }

    if (changed) {
        emit playlistChanged();
    }
    return firstAddedIndex;
}

void PlaylistManager::removeAt(int index)
{
    if (index < 0 || index >= m_files.size()) {
        return;
    }
    m_files.removeAt(index);
    if (m_files.isEmpty()) {
        m_currentIndex = -1;
    } else if (m_currentIndex >= m_files.size()) {
        m_currentIndex = m_files.size() - 1;
    }
    emit playlistChanged();
    emit currentIndexChanged(m_currentIndex);
}

void PlaylistManager::clear()
{
    m_files.clear();
    m_currentIndex = -1;
    emit playlistChanged();
    emit currentIndexChanged(-1);
}

void PlaylistManager::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_files.size()) {
        return;
    }
    m_currentIndex = index;
    emit currentIndexChanged(m_currentIndex);
}

QString PlaylistManager::nextFileManual()
{
    if (m_files.isEmpty()) {
        return QString();
    }
    int nextIdx = (m_currentIndex + 1) % m_files.size();
    setCurrentIndex(nextIdx);
    return currentFile();
}

QString PlaylistManager::previousFileManual()
{
    if (m_files.isEmpty()) {
        return QString();
    }
    int prevIdx = (m_currentIndex - 1 + m_files.size()) % m_files.size();
    setCurrentIndex(prevIdx);
    return currentFile();
}

QString PlaylistManager::nextFileOnEnd(ConfigManager::PlayMode mode)
{
    if (m_files.isEmpty() || m_currentIndex < 0) {
        return QString();
    }

    switch (mode) {
    case ConfigManager::LoopOne:
        // 单曲循环：保持当前索引重播
        emit currentIndexChanged(m_currentIndex);
        return currentFile();

    case ConfigManager::Sequential:
        // 顺序播放：播完最后一首即停止
        if (m_currentIndex + 1 < m_files.size()) {
            setCurrentIndex(m_currentIndex + 1);
            return currentFile();
        }
        return QString();

    case ConfigManager::LoopList:
    default:
        // 列表循环
        setCurrentIndex((m_currentIndex + 1) % m_files.size());
        return currentFile();
    }
}
