#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>

#include "core/CmsClient.h"
#include "core/PlaylistEntry.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QPushButton;
class QShowEvent;
class QTableWidget;
class QTableWidgetItem;
class QTextBrowser;
class QNetworkReply;

/**
 * @brief 在线片源对话框
 *
 * 两个页签：
 *  1. 在线搜索 —— 选源 / 输入关键词 / 多源并发搜索 / 选片 / 选线路与剧集 / 播放或加入播放列表
 *  2. 源配置   —— 导入 TVBox 订阅（链接 / 文件 / 剪贴板）、启用停用、删除
 *
 * 窗口是**非模态**的：开着它也可以照常操作主窗口（暂停、拖进度、切下一集）。
 * 选好剧集后点「播放」只把条目交给主窗口，自身不关闭，方便连着挑好几部。
 *
 * 只对接苹果 CMS 的 type 0/1 接口；脚本 Spider（jar/js/py）不在本轮范围内。
 */
class SourceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SourceDialog(QWidget *parent = nullptr);

    /** 打开时把焦点放到关键词输入框 */
    void focusSearchInput();

protected:
    void done(int result) override;
    void showEvent(QShowEvent *event) override;

signals:
    /** 加入播放列表并立即播放 startIndex 这一集（entries 为该线路全部剧集） */
    void playEntriesRequested(const QList<PlaylistEntry> &entries, int startIndex);
    /** 只加入播放列表，不改变当前播放 */
    void addEntriesRequested(const QList<PlaylistEntry> &entries);

private slots:
    void onSearchClicked();
    void onSearchFinished(const QString &siteKey, const QList<VodItem> &items, const QString &error);
    void onResultCurrentRowChanged(int row);
    void onDetailFinished(const QString &siteKey, const VodDetail &detail, const QString &error);
    void onPlayLineChanged(int index);
    void onPlayClicked();
    void onAddToPlaylistClicked();

    void onImportFromUrl();
    void onImportFromFile();
    void onImportFromClipboard();
    void onRemoveSite();
    void onOpenConfigFile();
    void onSiteItemChanged(QTableWidgetItem *item);

private:
    QWidget *createSearchTab();
    QWidget *createSourceTab();

    void reloadSiteCombo();
    void reloadSiteTable();
    void updateSearchStatus();
    void clearDetailPane();
    void showDetail(const VodDetail &detail);
    void updateEpisodeList();

    /** 把当前线路的全部剧集转成播放列表条目（带上该源的 UA / Referer） */
    QList<PlaylistEntry> buildEntriesFromCurrentLine() const;
    void sendEntries(bool alsoPlay);

    /** 首次打开时尽量贴到主窗口旁边，两个窗口并排互不遮挡 */
    void placeBesideParent();

    void importPayload(const QByteArray &payload, const QString &origin);
    void startImportRequest(const QUrl &url);

    CmsClient *m_client = nullptr;
    QNetworkAccessManager *m_importNam = nullptr;

    // --- 搜索页 ---
    QComboBox *m_siteCombo = nullptr;
    QLineEdit *m_keywordEdit = nullptr;
    QPushButton *m_searchButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_actionHint = nullptr;
    QTableWidget *m_resultTable = nullptr;
    QList<VodItem> m_rowItems;

    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailMeta = nullptr;
    QComboBox *m_lineCombo = nullptr;
    QListWidget *m_episodeList = nullptr;
    QTextBrowser *m_detailInfo = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_addButton = nullptr;

    VodDetail m_detail;
    QString m_detailKey;                 // siteKey|vodId，避免重复请求
    QHash<QString, VodDetail> m_detailCache;

    QSet<QString> m_pendingSiteKeys;
    int m_failedCount = 0;
    int m_totalSites = 0;
    /** 窗口已关闭时丢弃迟到的回调（关闭时会 abort 掉在途请求） */
    bool m_ignoreResults = false;
    /** 位置只自动摆一次，之后用户拖到哪就留在哪 */
    bool m_positioned = false;

    // --- 源配置页 ---
    QTableWidget *m_siteTable = nullptr;
    bool m_reloadingTable = false;
};
