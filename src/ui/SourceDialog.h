#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>

#include "core/CmsClient.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTextBrowser;
class QNetworkReply;

/**
 * @brief 在线片源对话框
 *
 * 两个页签：
 *  1. 在线搜索 —— 选源 / 输入关键词 / 多源并发搜索 / 选片 / 选线路与剧集 / 播放
 *  2. 源配置   —— 导入 TVBox 订阅（链接 / 文件 / 剪贴板）、启用停用、删除
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

signals:
    /** 用户确认播放某一集（episodes 为该线路全部剧集，便于自动连播） */
    void playRequested(const OnlinePlayRequest &request);

private slots:
    void onSearchClicked();
    void onSearchFinished(const QString &siteKey, const QList<VodItem> &items, const QString &error);
    void onResultCurrentRowChanged(int row);
    void onDetailFinished(const QString &siteKey, const VodDetail &detail, const QString &error);
    void onPlayLineChanged(int index);
    void onPlayClicked();

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
    void importPayload(const QByteArray &payload, const QString &origin);
    void startImportRequest(const QUrl &url);

    CmsClient *m_client = nullptr;
    QNetworkAccessManager *m_importNam = nullptr;

    // --- 搜索页 ---
    QComboBox *m_siteCombo = nullptr;
    QLineEdit *m_keywordEdit = nullptr;
    QPushButton *m_searchButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTableWidget *m_resultTable = nullptr;
    QList<VodItem> m_rowItems;

    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailMeta = nullptr;
    QComboBox *m_lineCombo = nullptr;
    QListWidget *m_episodeList = nullptr;
    QTextBrowser *m_detailInfo = nullptr;
    QPushButton *m_playButton = nullptr;

    VodDetail m_detail;
    QString m_detailKey;                 // siteKey|vodId，避免重复请求
    QHash<QString, VodDetail> m_detailCache;

    QSet<QString> m_pendingSiteKeys;
    int m_failedCount = 0;
    int m_totalSites = 0;
    /** 对话框已关闭时丢弃迟到的回调（关闭时会 abort 掉在途请求） */
    bool m_ignoreResults = false;

    // --- 源配置页 ---
    QTableWidget *m_siteTable = nullptr;
    bool m_reloadingTable = false;
};
