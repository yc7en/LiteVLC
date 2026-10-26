#include "SourceDialog.h"

#include "core/LiveClient.h"
#include "core/SourceManager.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString plainText(const QString &raw)
{
    return raw.isEmpty() ? QStringLiteral("暂无") : raw;
}

} // namespace

SourceDialog::SourceDialog(QWidget *parent)
    : QDialog(parent)
    , m_client(new CmsClient(this))
    , m_liveClient(new LiveClient(this))
    , m_importNam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("在线片源"));
    resize(980, 680);

    // 非模态：开着它也能操作主窗口（暂停、拖进度、切集），不需要先关窗
    setWindowModality(Qt::NonModal);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createSearchTab(), QStringLiteral("在线搜索"));
    m_tabs->addTab(createSourceTab(), QStringLiteral("源配置"));
    m_tabs->addTab(createLiveTab(), QStringLiteral("直播"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->addWidget(m_tabs);

    setSizeGripEnabled(true);

    connect(m_client, &CmsClient::searchFinished, this, &SourceDialog::onSearchFinished);
    connect(m_client, &CmsClient::detailFinished, this, &SourceDialog::onDetailFinished);
    connect(m_liveClient, &LiveClient::channelsFetched, this, &SourceDialog::onChannelsFetched);

    reloadSiteCombo();
    reloadSiteTable();
    reloadLiveSourceTable();
    reloadLiveCombo();

    // 还没配任何源时直接落到「源配置」页，省得用户对着空列表发呆；
    // 只有直播源没有点播源时，直接进直播页更顺手
    const bool noSites = SourceManager::instance().searchableSites().isEmpty();
    if (noSites && SourceManager::instance().lives().isEmpty()) {
        m_tabs->setCurrentIndex(1);
        m_statusLabel->setText(QStringLiteral("还没有配置片源：切到「源配置」页导入 TVBox 订阅即可"));
    } else if (noSites) {
        m_tabs->setCurrentIndex(2);
        m_liveStatusLabel->setText(QStringLiteral("已配置直播源，点「刷新频道」拉取频道列表"));
    }
}

void SourceDialog::focusSearchInput()
{
    if (m_keywordEdit) {
        m_keywordEdit->setFocus();
        m_keywordEdit->selectAll();
    }
}

void SourceDialog::focusLiveTab()
{
    if (m_tabs) {
        m_tabs->setCurrentIndex(2);
    }
    // 缓存里已有频道就直接展示，不再重复拉取（用户想重拉点「刷新频道」）
    if (!m_channelCache.isEmpty() && m_allChannels.isEmpty()) {
        rebuildLiveTable();
    }
    if (m_liveFilterEdit) {
        m_liveFilterEdit->setFocus();
    }
}

void SourceDialog::showEvent(QShowEvent *event)
{
    // 上次关窗时置过这个标志（丢弃在途回调），重新打开要放开，否则搜不出东西
    m_ignoreResults = false;
    QDialog::showEvent(event);

    if (!m_positioned) {
        m_positioned = true;
        placeBesideParent();
    }
}

void SourceDialog::placeBesideParent()
{
    QWidget *owner = parentWidget();
    if (!owner) {
        return;
    }
    QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!scr) {
        return;
    }

    const QRect avail = scr->availableGeometry();
    const QRect frame = owner->frameGeometry();
    const int gap = 8;
    const int minWidth = 760;

    const int rightSpace = avail.right() - frame.right() - gap;
    if (rightSpace >= minWidth) {
        const int w = qMin(width(), rightSpace);
        resize(w, height());
        move(frame.right() + gap,
             qBound(avail.top(), frame.top(), qMax(avail.top(), avail.bottom() - height())));
        return;
    }

    const int leftSpace = frame.left() - avail.left() - gap;
    if (leftSpace >= minWidth) {
        const int w = qMin(width(), leftSpace);
        resize(w, height());
        move(frame.left() - gap - w,
             qBound(avail.top(), frame.top(), qMax(avail.top(), avail.bottom() - height())));
        return;
    }

    // 屏幕实在放不下两个窗口：保持默认居中，用户自己拖
}

void SourceDialog::done(int result)
{
    // 关窗时中断在途请求，避免回调打在已隐藏的界面上
    m_ignoreResults = true;
    m_client->abortAll();
    m_pendingSiteKeys.clear();
    m_liveClient->abortAll();
    m_pendingLiveKeys.clear();
    if (m_searchButton) {
        m_searchButton->setEnabled(true);
    }
    QDialog::done(result);
}

// ============================================================================
// 搜索页
// ============================================================================

QWidget *SourceDialog::createSearchTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // --- 顶部：源选择 + 关键词 + 搜索 ---
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto *siteLabel = new QLabel(QStringLiteral("片源:"), tab);
    m_siteCombo = new QComboBox(tab);
    m_siteCombo->setMinimumWidth(210);

    m_keywordEdit = new QLineEdit(tab);
    m_keywordEdit->setPlaceholderText(QStringLiteral("输入片名后回车，例如：流浪地球"));
    m_keywordEdit->setClearButtonEnabled(true);

    m_searchButton = new QPushButton(QIcon(QStringLiteral(":/icons/icons/search.svg")), QStringLiteral(" 搜索"), tab);
    m_searchButton->setMinimumWidth(90);

    topRow->addWidget(siteLabel);
    topRow->addWidget(m_siteCombo);
    topRow->addWidget(m_keywordEdit, 1);
    topRow->addWidget(m_searchButton);
    layout->addLayout(topRow);

    m_statusLabel = new QLabel(QStringLiteral("选一个源或「全部源」，输入片名开始搜索"), tab);
    m_statusLabel->setObjectName(QStringLiteral("sourceStatusLabel"));
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    // --- 主体：左侧结果列表 / 右侧剧集 ---
    auto *splitter = new QSplitter(Qt::Horizontal, tab);

    m_resultTable = new QTableWidget(0, 3, splitter);
    m_resultTable->setHorizontalHeaderLabels({QStringLiteral("片名"),
                                              QStringLiteral("备注"),
                                              QStringLiteral("来源")});
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setShowGrid(false);
    m_resultTable->setAlternatingRowColors(false);
    m_resultTable->setWordWrap(false);

    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    m_detailTitle = new QLabel(QStringLiteral("在左侧选一部片子查看剧集"), rightPanel);
    m_detailTitle->setObjectName(QStringLiteral("sourceDetailTitle"));
    m_detailTitle->setWordWrap(true);

    m_detailMeta = new QLabel(rightPanel);
    m_detailMeta->setObjectName(QStringLiteral("sourceDetailMeta"));
    m_detailMeta->setWordWrap(true);

    auto *lineRow = new QHBoxLayout();
    lineRow->setSpacing(6);
    lineRow->addWidget(new QLabel(QStringLiteral("线路:"), rightPanel));
    m_lineCombo = new QComboBox(rightPanel);
    lineRow->addWidget(m_lineCombo, 1);

    m_episodeList = new QListWidget(rightPanel);
    m_episodeList->setAlternatingRowColors(false);

    m_detailInfo = new QTextBrowser(rightPanel);
    m_detailInfo->setOpenExternalLinks(false);
    m_detailInfo->setPlaceholderText(QStringLiteral("剧情简介"));

    // 底部操作区：播放 / 只加列表，两者都不关闭本窗口
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);
    m_playButton = new QPushButton(QStringLiteral("▶ 播放选中剧集"), rightPanel);
    m_playButton->setObjectName(QStringLiteral("btnPlayEpisode"));
    m_playButton->setEnabled(false);
    m_addButton = new QPushButton(QStringLiteral("＋ 加入播放列表"), rightPanel);
    m_addButton->setEnabled(false);
    actionRow->addWidget(m_playButton, 1);
    actionRow->addWidget(m_addButton, 0);

    m_actionHint = new QLabel(
        QStringLiteral("双击剧集也能直接播放；整条线路会一起进播放列表，播完自动下一集"),
        rightPanel);
    m_actionHint->setObjectName(QStringLiteral("sourceActionHint"));
    m_actionHint->setWordWrap(true);

    rightLayout->addWidget(m_detailTitle);
    rightLayout->addWidget(m_detailMeta);
    rightLayout->addLayout(lineRow);
    rightLayout->addWidget(m_episodeList, 3);
    rightLayout->addWidget(m_detailInfo, 2);
    rightLayout->addLayout(actionRow);
    rightLayout->addWidget(m_actionHint);

    splitter->addWidget(m_resultTable);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 4);
    splitter->setChildrenCollapsible(false);

    layout->addWidget(splitter, 1);

    connect(m_searchButton, &QPushButton::clicked, this, &SourceDialog::onSearchClicked);
    connect(m_keywordEdit, &QLineEdit::returnPressed, this, &SourceDialog::onSearchClicked);
    connect(m_resultTable, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { onResultCurrentRowChanged(row); });
    connect(m_lineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SourceDialog::onPlayLineChanged);
    connect(m_playButton, &QPushButton::clicked, this, &SourceDialog::onPlayClicked);
    connect(m_addButton, &QPushButton::clicked, this, &SourceDialog::onAddToPlaylistClicked);
    connect(m_episodeList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onPlayClicked();
    });

    return tab;
}

void SourceDialog::reloadSiteCombo()
{
    const QString previous = m_siteCombo->currentData().toString();

    m_siteCombo->blockSignals(true);
    m_siteCombo->clear();
    m_siteCombo->addItem(QStringLiteral("全部源（并发搜索）"), QString());

    const QList<SourceSite> sites = SourceManager::instance().searchableSites();
    for (const SourceSite &site : sites) {
        m_siteCombo->addItem(QStringLiteral("%1 [%2]").arg(site.name, site.typeLabel()), site.key);
    }

    const int index = m_siteCombo->findData(previous);
    m_siteCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_siteCombo->blockSignals(false);

    m_searchButton->setEnabled(!sites.isEmpty() || m_siteCombo->count() > 1);
}

void SourceDialog::onSearchClicked()
{
    const QString keyword = m_keywordEdit->text().trimmed();
    if (keyword.isEmpty()) {
        m_keywordEdit->setFocus();
        return;
    }

    QList<SourceSite> targets;
    const QString selectedKey = m_siteCombo->currentData().toString();
    if (selectedKey.isEmpty()) {
        targets = SourceManager::instance().searchableSites();
    } else {
        const SourceSite *site = SourceManager::instance().findSite(selectedKey);
        if (site && site->isCmsSupported()) {
            targets.append(*site);
        }
    }

    if (targets.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("还没有可用的片源。\n请切到「源配置」页导入 TVBox 订阅，"
                                                "或在 sources.json 里手动添加 type=0/1 的苹果 CMS 接口。"));
        return;
    }

    m_resultTable->setRowCount(0);
    m_rowItems.clear();
    m_pendingSiteKeys.clear();
    m_failedCount = 0;
    m_totalSites = targets.size();
    m_ignoreResults = false;
    m_detailCache.clear();
    m_detailKey.clear();
    clearDetailPane();

    for (const SourceSite &site : targets) {
        m_pendingSiteKeys.insert(site.key);
        m_client->search(site, keyword);
    }

    m_searchButton->setEnabled(false);
    updateSearchStatus();
}

void SourceDialog::onSearchFinished(const QString &siteKey,
                                    const QList<VodItem> &items,
                                    const QString &error)
{
    if (m_ignoreResults) {
        return;
    }
    m_pendingSiteKeys.remove(siteKey);

    if (items.isEmpty() && !error.isEmpty()) {
        ++m_failedCount;
    }

    for (const VodItem &item : items) {
        const int row = m_resultTable->rowCount();
        m_resultTable->insertRow(row);

        auto *nameItem = new QTableWidgetItem(item.name.isEmpty() ? item.vodId : item.name);
        nameItem->setToolTip(item.name);

        QString remark = item.remarks;
        if (remark.isEmpty()) {
            QStringList parts;
            if (!item.year.isEmpty()) {
                parts << item.year;
            }
            if (!item.area.isEmpty()) {
                parts << item.area;
            }
            remark = parts.join(QStringLiteral(" · "));
        }

        auto *remarkItem = new QTableWidgetItem(remark);
        auto *sourceItem = new QTableWidgetItem(item.sourceName);
        sourceItem->setForeground(QColor(QStringLiteral("#8AB4F8")));

        m_resultTable->setItem(row, 0, nameItem);
        m_resultTable->setItem(row, 1, remarkItem);
        m_resultTable->setItem(row, 2, sourceItem);
        m_rowItems.append(item);
    }

    updateSearchStatus();

    if (m_pendingSiteKeys.isEmpty()) {
        m_searchButton->setEnabled(true);
        if (m_resultTable->rowCount() > 0 && m_resultTable->currentRow() < 0) {
            m_resultTable->selectRow(0);
        }
    }
}

void SourceDialog::updateSearchStatus()
{
    const int done = m_totalSites - m_pendingSiteKeys.size();
    QString text;

    if (m_pendingSiteKeys.isEmpty()) {
        text = QStringLiteral("搜索完成：%1 个源，共 %2 条结果").arg(m_totalSites).arg(m_rowItems.size());
    } else {
        text = QStringLiteral("搜索中… 已完成 %1/%2 个源，当前 %3 条结果")
                   .arg(done)
                   .arg(m_totalSites)
                   .arg(m_rowItems.size());
    }

    if (m_failedCount > 0 && m_pendingSiteKeys.isEmpty()) {
        text += QStringLiteral("（%1 个源请求失败）").arg(m_failedCount);
    }
    m_statusLabel->setText(text);
}

void SourceDialog::onResultCurrentRowChanged(int row)
{
    if (row < 0 || row >= m_rowItems.size()) {
        return;
    }
    const VodItem item = m_rowItems.at(row);
    const QString cacheKey = item.sourceKey + QLatin1Char('|') + item.vodId;

    if (m_detailCache.contains(cacheKey)) {
        m_detailKey = cacheKey;
        showDetail(m_detailCache.value(cacheKey));
        return;
    }

    const SourceSite *site = SourceManager::instance().findSite(item.sourceKey);
    if (!site) {
        return;
    }

    m_detailKey = cacheKey;
    m_detailTitle->setText(QStringLiteral("正在获取《%1》的剧集…").arg(item.name));
    m_detailMeta->clear();
    m_lineCombo->blockSignals(true);
    m_lineCombo->clear();
    m_lineCombo->blockSignals(false);
    m_episodeList->clear();
    m_detailInfo->clear();
    m_playButton->setEnabled(false);
    m_addButton->setEnabled(false);

    m_client->requestDetail(*site, item.vodId);
}

void SourceDialog::onDetailFinished(const QString &siteKey, const VodDetail &detail, const QString &error)
{
    if (m_ignoreResults) {
        return;
    }
    const QString cacheKey = siteKey + QLatin1Char('|')
                             + (detail.vodId.isEmpty() ? m_detailKey.section(QLatin1Char('|'), 1) : detail.vodId);
    if (!m_detailKey.isEmpty() && cacheKey != m_detailKey) {
        return; // 过期的响应（用户已经点了别的片子）
    }

    if (!error.isEmpty() || detail.lines.isEmpty()) {
        m_detailTitle->setText(QStringLiteral("获取失败"));
        m_detailMeta->clear();
        m_episodeList->clear();
        m_detailInfo->setText(error.isEmpty()
                                  ? QStringLiteral("这个源没有返回可播放的剧集（可能源已失效）")
                                  : error);
        m_playButton->setEnabled(false);
        m_addButton->setEnabled(false);
        return;
    }

    m_detailCache.insert(m_detailKey, detail);
    showDetail(detail);
}

void SourceDialog::showDetail(const VodDetail &detail)
{
    m_detail = detail;

    QString title = detail.name;
    if (!detail.remarks.isEmpty()) {
        title += QStringLiteral("  ·  %1").arg(detail.remarks);
    }
    m_detailTitle->setText(title);

    QStringList meta;
    if (!detail.year.isEmpty()) {
        meta << detail.year;
    }
    if (!detail.area.isEmpty()) {
        meta << detail.area;
    }
    if (!detail.actor.isEmpty()) {
        meta << QStringLiteral("主演：%1").arg(detail.actor);
    }
    if (!detail.director.isEmpty()) {
        meta << QStringLiteral("导演：%1").arg(detail.director);
    }
    meta << QStringLiteral("来源：%1").arg(detail.sourceName);
    m_detailMeta->setText(meta.join(QStringLiteral("  |  ")));

    m_lineCombo->blockSignals(true);
    m_lineCombo->clear();
    for (const VodPlayLine &line : detail.lines) {
        m_lineCombo->addItem(QStringLiteral("%1 (%2 集)").arg(line.flag).arg(line.episodes.size()));
    }
    m_lineCombo->setCurrentIndex(0);
    m_lineCombo->blockSignals(false);

    m_detailInfo->setPlainText(plainText(detail.content));
    updateEpisodeList();
}

void SourceDialog::updateEpisodeList()
{
    m_episodeList->clear();

    const int lineIndex = m_lineCombo->currentIndex();
    if (lineIndex < 0 || lineIndex >= m_detail.lines.size()) {
        m_playButton->setEnabled(false);
        m_addButton->setEnabled(false);
        return;
    }

    const VodPlayLine &line = m_detail.lines.at(lineIndex);
    for (int i = 0; i < line.episodes.size(); ++i) {
        auto *item = new QListWidgetItem(line.episodes.at(i).name);
        item->setData(Qt::UserRole, i);
        m_episodeList->addItem(item);
    }

    if (m_episodeList->count() > 0) {
        m_episodeList->setCurrentRow(0);
    }
    const bool hasEpisodes = m_episodeList->count() > 0;
    m_playButton->setEnabled(hasEpisodes);
    m_addButton->setEnabled(hasEpisodes);
}

void SourceDialog::onPlayLineChanged(int)
{
    updateEpisodeList();
}

QList<PlaylistEntry> SourceDialog::buildEntriesFromCurrentLine() const
{
    QList<PlaylistEntry> entries;

    const int lineIndex = m_lineCombo->currentIndex();
    if (lineIndex < 0 || lineIndex >= m_detail.lines.size()) {
        return entries;
    }

    QString ua;
    QString referer;
    if (const SourceSite *site = SourceManager::instance().findSite(m_detail.sourceKey)) {
        ua = site->effectiveUserAgent();
        referer = site->effectiveReferer();
    }

    const VodPlayLine &line = m_detail.lines.at(lineIndex);
    entries.reserve(line.episodes.size());

    for (int i = 0; i < line.episodes.size(); ++i) {
        const VodEpisode &episode = line.episodes.at(i);
        if (episode.url.trimmed().isEmpty()) {
            continue;
        }
        const QString episodeName = episode.name.isEmpty()
                                        ? QStringLiteral("第%1集").arg(i + 1)
                                        : episode.name;
        entries.append(PlaylistEntry::makeOnline(
            episode.url.trimmed(),
            QStringLiteral("%1 · %2").arg(m_detail.name, episodeName),
            m_detail.sourceName,
            ua,
            referer));
    }
    return entries;
}

void SourceDialog::sendEntries(bool alsoPlay)
{
    const QList<PlaylistEntry> entries = buildEntriesFromCurrentLine();
    if (entries.isEmpty()) {
        m_actionHint->setText(QStringLiteral("⚠ 这条线路没有可播放的地址，换一条线路试试"));
        return;
    }

    // 剧集列表里的选中行 → 在线条目下标（跳过的空地址要扣掉）
    int wanted = m_episodeList->currentRow();
    if (wanted < 0) {
        wanted = 0;
    }
    int startIndex = 0;
    const int lineIndex = m_lineCombo->currentIndex();
    if (lineIndex >= 0 && lineIndex < m_detail.lines.size()) {
        const VodPlayLine &line = m_detail.lines.at(lineIndex);
        for (int i = 0; i < wanted && i < line.episodes.size(); ++i) {
            if (!line.episodes.at(i).url.trimmed().isEmpty()) {
                ++startIndex;
            }
        }
    }
    if (startIndex >= entries.size()) {
        startIndex = 0;
    }

    const QString episodeName = m_episodeList->currentItem()
                                    ? m_episodeList->currentItem()->text()
                                    : QString();

    if (alsoPlay) {
        emit playEntriesRequested(entries, startIndex);
        m_actionHint->setText(QStringLiteral("▶ 正在播放 %1 · %2（整条线路 %3 集已加入播放列表）")
                                  .arg(m_detail.name, episodeName)
                                  .arg(entries.size()));
    } else {
        emit addEntriesRequested(entries);
        m_actionHint->setText(QStringLiteral("✓ 已加入播放列表：%1 共 %2 集（已存在的不重复添加）")
                                  .arg(m_detail.name)
                                  .arg(entries.size()));
    }
}

void SourceDialog::onPlayClicked()
{
    sendEntries(true);
}

void SourceDialog::onAddToPlaylistClicked()
{
    sendEntries(false);
}

void SourceDialog::clearDetailPane()
{
    m_detail = VodDetail();
    m_detailTitle->setText(QStringLiteral("在左侧选一部片子查看剧集"));
    m_detailMeta->clear();
    m_lineCombo->blockSignals(true);
    m_lineCombo->clear();
    m_lineCombo->blockSignals(false);
    m_episodeList->clear();
    m_detailInfo->clear();
    m_playButton->setEnabled(false);
    m_addButton->setEnabled(false);
}

// ============================================================================
// 源配置页
// ============================================================================

QWidget *SourceDialog::createSourceTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);

    auto *btnUrl = new QPushButton(QStringLiteral("从订阅链接导入"), tab);
    auto *btnFile = new QPushButton(QStringLiteral("导入本地 JSON"), tab);
    auto *btnClip = new QPushButton(QStringLiteral("从剪贴板粘贴"), tab);
    auto *btnAddLive = new QPushButton(QStringLiteral("添加直播源"), tab);
    auto *btnOpen = new QPushButton(QStringLiteral("打开配置文件"), tab);
    auto *btnRemove = new QPushButton(QStringLiteral("删除选中"), tab);

    buttonRow->addWidget(btnUrl);
    buttonRow->addWidget(btnFile);
    buttonRow->addWidget(btnClip);
    buttonRow->addWidget(btnAddLive);
    buttonRow->addWidget(btnOpen);
    buttonRow->addStretch(1);
    buttonRow->addWidget(btnRemove);
    layout->addLayout(buttonRow);

    auto *hint = new QLabel(
        QStringLiteral("支持 TVBox / CatVod 配置文件：<b>sites</b>（点播）与 <b>lives</b>（直播）一次导入。<br>"
                       "点播只对接 <b>type=0(XML)</b> 与 <b>type=1(JSON)</b> 的苹果 CMS 接口；"
                       "直播只取带 m3u / txt 清单地址的源。jar / js / py 这类脚本 Spider 需要额外运行时，导入时自动跳过。"
                       "勾选第一列即可启用/停用，改动立即保存。"),
        tab);
    hint->setObjectName(QStringLiteral("sourceHintLabel"));
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *siteLabel = new QLabel(QStringLiteral("<b>点播源</b>（sites）— 在「在线搜索」页按片名搜索"), tab);
    siteLabel->setObjectName(QStringLiteral("sourceHintLabel"));
    layout->addWidget(siteLabel);

    m_siteTable = new QTableWidget(0, 4, tab);
    m_siteTable->setHorizontalHeaderLabels({QStringLiteral("启用"),
                                            QStringLiteral("名称"),
                                            QStringLiteral("类型"),
                                            QStringLiteral("接口地址")});
    m_siteTable->verticalHeader()->setVisible(false);
    m_siteTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_siteTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_siteTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_siteTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_siteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_siteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_siteTable->setShowGrid(false);
    layout->addWidget(m_siteTable, 3);

    auto *liveLabel = new QLabel(QStringLiteral("<b>直播源</b>（lives）— 在「直播」页刷新频道列表"), tab);
    liveLabel->setObjectName(QStringLiteral("sourceHintLabel"));
    layout->addWidget(liveLabel);

    m_liveSourceTable = new QTableWidget(0, 4, tab);
    m_liveSourceTable->setHorizontalHeaderLabels({QStringLiteral("启用"),
                                                  QStringLiteral("名称"),
                                                  QStringLiteral("类型"),
                                                  QStringLiteral("清单地址")});
    m_liveSourceTable->verticalHeader()->setVisible(false);
    m_liveSourceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_liveSourceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_liveSourceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_liveSourceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_liveSourceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_liveSourceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_liveSourceTable->setShowGrid(false);
    layout->addWidget(m_liveSourceTable, 2);

    connect(btnUrl, &QPushButton::clicked, this, &SourceDialog::onImportFromUrl);
    connect(btnFile, &QPushButton::clicked, this, &SourceDialog::onImportFromFile);
    connect(btnClip, &QPushButton::clicked, this, &SourceDialog::onImportFromClipboard);
    connect(btnAddLive, &QPushButton::clicked, this, &SourceDialog::onAddLiveSource);
    connect(btnOpen, &QPushButton::clicked, this, &SourceDialog::onOpenConfigFile);
    connect(btnRemove, &QPushButton::clicked, this, &SourceDialog::onRemoveSite);
    connect(m_siteTable, &QTableWidget::itemChanged, this, &SourceDialog::onSiteItemChanged);
    connect(m_liveSourceTable, &QTableWidget::itemChanged, this, &SourceDialog::onLiveSourceItemChanged);

    return tab;
}

void SourceDialog::reloadSiteTable()
{
    m_reloadingTable = true;
    m_siteTable->setRowCount(0);

    const QList<SourceSite> sites = SourceManager::instance().sites();
    for (const SourceSite &site : sites) {
        const int row = m_siteTable->rowCount();
        m_siteTable->insertRow(row);

        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setCheckState(site.enabled ? Qt::Checked : Qt::Unchecked);

        auto *nameItem = new QTableWidgetItem(site.name);
        nameItem->setData(Qt::UserRole, site.key);

        auto *typeItem = new QTableWidgetItem(site.typeLabel());
        auto *apiItem = new QTableWidgetItem(site.api);
        apiItem->setToolTip(site.api);

        m_siteTable->setItem(row, 0, checkItem);
        m_siteTable->setItem(row, 1, nameItem);
        m_siteTable->setItem(row, 2, typeItem);
        m_siteTable->setItem(row, 3, apiItem);
    }
    m_reloadingTable = false;
}

void SourceDialog::onSiteItemChanged(QTableWidgetItem *item)
{
    if (m_reloadingTable || !item || item->column() != 0) {
        return;
    }
    QTableWidgetItem *nameItem = m_siteTable->item(item->row(), 1);
    if (!nameItem) {
        return;
    }
    const QString key = nameItem->data(Qt::UserRole).toString();
    SourceManager::instance().setEnabled(key, item->checkState() == Qt::Checked);
    reloadSiteCombo();
    updateSearchStatus();
}

void SourceDialog::reloadLiveSourceTable()
{
    if (!m_liveSourceTable) {
        return;
    }
    m_reloadingTable = true;
    m_liveSourceTable->setRowCount(0);

    const QList<LiveSource> lives = SourceManager::instance().lives();
    for (const LiveSource &live : lives) {
        const int row = m_liveSourceTable->rowCount();
        m_liveSourceTable->insertRow(row);

        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setCheckState(live.enabled ? Qt::Checked : Qt::Unchecked);

        auto *nameItem = new QTableWidgetItem(live.name);
        nameItem->setData(Qt::UserRole, live.key);

        auto *typeItem = new QTableWidgetItem(live.typeLabel());
        auto *urlItem = new QTableWidgetItem(live.url);
        urlItem->setToolTip(live.url);

        m_liveSourceTable->setItem(row, 0, checkItem);
        m_liveSourceTable->setItem(row, 1, nameItem);
        m_liveSourceTable->setItem(row, 2, typeItem);
        m_liveSourceTable->setItem(row, 3, urlItem);
    }
    m_reloadingTable = false;
}

void SourceDialog::onLiveSourceItemChanged(QTableWidgetItem *item)
{
    if (m_reloadingTable || !item || item->column() != 0) {
        return;
    }
    QTableWidgetItem *nameItem = m_liveSourceTable->item(item->row(), 1);
    if (!nameItem) {
        return;
    }
    const QString key = nameItem->data(Qt::UserRole).toString();
    const bool enabled = item->checkState() == Qt::Checked;
    SourceManager::instance().setLiveEnabled(key, enabled);

    // 停用的源要从缓存和表格里一起摘掉，避免「明明关了还在列表里」
    if (!enabled) {
        m_channelCache.remove(key);
    }
    reloadLiveCombo();
    rebuildLiveTable();
}

void SourceDialog::onAddLiveSource()
{
    bool ok = false;
    const QString url = QInputDialog::getText(this,
                                              QStringLiteral("添加直播源"),
                                              QStringLiteral("m3u / txt 清单地址（也可填本地文件路径）："),
                                              QLineEdit::Normal,
                                              QString(),
                                              &ok);
    if (!ok || url.trimmed().isEmpty()) {
        return;
    }

    const QString suggested = QFileInfo(QUrl(url.trimmed()).fileName()).completeBaseName();
    const QString name = QInputDialog::getText(this,
                                               QStringLiteral("添加直播源"),
                                               QStringLiteral("给它起个名字："),
                                               QLineEdit::Normal,
                                               suggested,
                                               &ok);
    if (!ok) {
        return;
    }

    LiveSource live;
    live.url = url.trimmed();
    live.name = name.trimmed().isEmpty() ? suggested : name.trimmed();
    if (live.name.isEmpty()) {
        live.name = live.url;
    }
    // key 只用于本地去重，时间戳足够
    live.key = QStringLiteral("live_%1").arg(QDateTime::currentMSecsSinceEpoch());

    SourceManager::instance().mergeLives({live});

    reloadLiveSourceTable();
    reloadLiveCombo();

    m_tabs->setCurrentIndex(2);
    m_liveStatusLabel->setText(QStringLiteral("已添加直播源「%1」，点「刷新频道」拉取。").arg(live.name));
}

void SourceDialog::onImportFromUrl()
{
    bool ok = false;
    const QString url = QInputDialog::getText(this,
                                              QStringLiteral("从订阅链接导入"),
                                              QStringLiteral("订阅地址（返回 TVBox 配置 JSON）："),
                                              QLineEdit::Normal,
                                              QString(),
                                              &ok);
    if (!ok || url.trimmed().isEmpty()) {
        return;
    }
    startImportRequest(QUrl(url.trimmed()));
}

void SourceDialog::startImportRequest(const QUrl &url, int attempt)
{
    if (!url.isValid()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("订阅地址无效"));
        return;
    }

    // 配置分发站普遍按 UA 分流：桌面浏览器 UA 会被 302 回站点首页（返回 HTML），
    // 只有客户端 UA 才拿得到真正的 JSON 配置。所以默认装成客户端，
    // 万一某个站只认浏览器，第 1 次重试再换回浏览器 UA。
    const bool asClient = (attempt == 0);
    const QString userAgent =
        asClient ? QStringLiteral("okhttp/3.12.1")
                 : QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                  "AppleWebKit/537.36 (KHTML, like Gecko) "
                                  "Chrome/124.0.0.0 Safari/537.36");

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);

    QNetworkReply *reply = m_importNam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, attempt]() {
        reply->deleteLater();
        if (m_ignoreResults) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, QStringLiteral("导入失败"),
                                 QStringLiteral("拉取订阅失败：%1").arg(reply->errorString()));
            return;
        }

        const QByteArray payload = reply->readAll();

        // 拿到网页说明地址不对（多半是被重定向回首页了），换另一套 UA 再试一次，
        // 两次都只给网页才报错 —— 直接把「这是网页不是配置」讲清楚。
        if (payload.trimmed().startsWith('<')) {
            if (attempt == 0) {
                startImportRequest(url, 1);
                return;
            }
            QMessageBox::warning(
                this, QStringLiteral("导入失败"),
                QStringLiteral("这个地址返回的是网页，不是配置内容。\n\n"
                               "最终地址：%1\n\n"
                               "常见原因：\n"
                               "· 复制的是站点首页或分享页，不是「配置地址」；\n"
                               "· 配置地址已失效，或需要登录后才能访问；\n"
                               "· 站点只对特定客户端下发配置。")
                    .arg(reply->url().toString()));
            return;
        }

        importPayload(payload, QStringLiteral("订阅链接 %1").arg(url.toString()));
    });
}

void SourceDialog::onImportFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 TVBox 配置文件"),
        QString(),
        QStringLiteral("配置文件 (*.json *.txt);;所有文件 (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("无法读取文件：%1").arg(path));
        return;
    }
    importPayload(file.readAll(), path);
    file.close();
}

void SourceDialog::onImportFromClipboard()
{
    const QString text = QApplication::clipboard()->text();
    if (text.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("剪贴板里没有内容"));
        return;
    }
    importPayload(text.toUtf8(), QStringLiteral("剪贴板"));
}

void SourceDialog::importPayload(const QByteArray &payload, const QString &origin)
{
    const ConfigImportResult result = SourceManager::instance().importConfig(payload, false);

    if (result.total() <= 0) {
        const QString reason = result.error.isEmpty()
                                   ? QStringLiteral("没有解析到可用的点播源或直播源")
                                   : result.error;
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("%1\n（来源：%2）").arg(reason, origin));
        return;
    }

    reloadSiteTable();
    reloadLiveSourceTable();
    reloadSiteCombo();
    reloadLiveCombo();

    QStringList lines;
    lines << QStringLiteral("已从 %1 导入：").arg(origin);
    if (result.sitesImported > 0) {
        lines << QStringLiteral("· %1 个点播源").arg(result.sitesImported);
    }
    if (result.livesImported > 0) {
        lines << QStringLiteral("· %1 个直播源").arg(result.livesImported);
    }

    // 混合配置：可用源已导入，同时告知跳过了哪些，免得用户疑惑「少了一半」
    QStringList skipped;
    if (result.siteStats.spiderSkipped > 0) {
        skipped << QStringLiteral("%1 个点播 Spider 脚本源（type=3，需 jar / js 运行时）")
                       .arg(result.siteStats.spiderSkipped);
    }
    if (result.siteStats.otherSkipped > 0) {
        skipped << QStringLiteral("%1 个其他不支持的源").arg(result.siteStats.otherSkipped);
    }
    if (result.livesSkipped > 0) {
        skipped << QStringLiteral("%1 个脚本型直播源（只有 api，没有清单地址）").arg(result.livesSkipped);
    }
    if (!skipped.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("未导入：%1。").arg(skipped.join(QStringLiteral("，")));
    }

    lines << QString();
    lines << QStringLiteral("点播源去「在线搜索」页搜片，直播源去「直播」页点「刷新频道」。");

    QMessageBox::information(this, QStringLiteral("导入完成"), lines.join(QLatin1Char('\n')));
}

void SourceDialog::onRemoveSite()
{
    QStringList siteKeys;
    QStringList liveKeys;

    if (m_siteTable->selectionModel()) {
        const QModelIndexList rows = m_siteTable->selectionModel()->selectedRows();
        for (const QModelIndex &index : rows) {
            if (QTableWidgetItem *nameItem = m_siteTable->item(index.row(), 1)) {
                siteKeys.append(nameItem->data(Qt::UserRole).toString());
            }
        }
    }
    if (m_liveSourceTable && m_liveSourceTable->selectionModel()) {
        const QModelIndexList rows = m_liveSourceTable->selectionModel()->selectedRows();
        for (const QModelIndex &index : rows) {
            if (QTableWidgetItem *nameItem = m_liveSourceTable->item(index.row(), 1)) {
                liveKeys.append(nameItem->data(Qt::UserRole).toString());
            }
        }
    }

    const int total = siteKeys.size() + liveKeys.size();
    if (total <= 0) {
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("删除确认"),
                              QStringLiteral("确定要删除选中的 %1 个源吗？").arg(total))
        != QMessageBox::Yes) {
        return;
    }

    for (const QString &key : siteKeys) {
        SourceManager::instance().removeSite(key);
    }
    for (const QString &key : liveKeys) {
        SourceManager::instance().removeLive(key);
        m_channelCache.remove(key);
    }

    reloadSiteTable();
    reloadLiveSourceTable();
    reloadSiteCombo();
    reloadLiveCombo();
    rebuildLiveTable();
    updateSearchStatus();
}

void SourceDialog::onOpenConfigFile()
{
    const QString path = SourceManager::instance().configFilePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// ============================================================================
// 直播页
// ============================================================================

QWidget *SourceDialog::createLiveTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    topRow->addWidget(new QLabel(QStringLiteral("直播源:"), tab));
    m_liveSourceCombo = new QComboBox(tab);
    m_liveSourceCombo->setMinimumWidth(180);
    topRow->addWidget(m_liveSourceCombo);

    auto *refreshButton = new QPushButton(QIcon(QStringLiteral(":/icons/icons/search.svg")),
                                          QStringLiteral(" 刷新频道"), tab);
    topRow->addWidget(refreshButton);

    topRow->addWidget(new QLabel(QStringLiteral("分组:"), tab));
    m_liveGroupCombo = new QComboBox(tab);
    m_liveGroupCombo->setMinimumWidth(130);
    topRow->addWidget(m_liveGroupCombo);

    m_liveFilterEdit = new QLineEdit(tab);
    m_liveFilterEdit->setPlaceholderText(QStringLiteral("筛选频道名，例如：CCTV"));
    m_liveFilterEdit->setClearButtonEnabled(true);
    topRow->addWidget(m_liveFilterEdit, 1);
    layout->addLayout(topRow);

    m_liveStatusLabel = new QLabel(QStringLiteral("点「刷新频道」拉取频道列表"), tab);
    m_liveStatusLabel->setObjectName(QStringLiteral("sourceStatusLabel"));
    m_liveStatusLabel->setWordWrap(true);
    layout->addWidget(m_liveStatusLabel);

    m_channelTable = new QTableWidget(0, 3, tab);
    m_channelTable->setHorizontalHeaderLabels({QStringLiteral("频道"),
                                               QStringLiteral("分组"),
                                               QStringLiteral("来源")});
    m_channelTable->verticalHeader()->setVisible(false);
    m_channelTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_channelTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_channelTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_channelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_channelTable->setShowGrid(false);
    m_channelTable->setWordWrap(false);
    layout->addWidget(m_channelTable, 1);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);
    m_livePlayButton = new QPushButton(QStringLiteral("▶ 播放选中频道"), tab);
    m_livePlayButton->setObjectName(QStringLiteral("btnPlayEpisode"));
    m_livePlayButton->setEnabled(false);
    m_liveAddButton = new QPushButton(QStringLiteral("＋ 加入播放列表"), tab);
    m_liveAddButton->setEnabled(false);
    actionRow->addWidget(m_livePlayButton, 1);
    actionRow->addWidget(m_liveAddButton, 0);
    layout->addLayout(actionRow);

    auto *hint = new QLabel(
        QStringLiteral("双击频道直接播放。当前分组的频道会整组进播放列表，PgUp / PgDn 就是换台。"), tab);
    hint->setObjectName(QStringLiteral("sourceActionHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(refreshButton, &QPushButton::clicked, this, &SourceDialog::onRefreshLives);
    connect(m_liveGroupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SourceDialog::onLiveFilterChanged);
    connect(m_liveFilterEdit, &QLineEdit::textChanged, this, &SourceDialog::onLiveFilterChanged);
    connect(m_livePlayButton, &QPushButton::clicked, this, &SourceDialog::onPlayLiveClicked);
    connect(m_liveAddButton, &QPushButton::clicked, this, &SourceDialog::onAddLiveClicked);
    connect(m_channelTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *) { onPlayLiveClicked(); });

    return tab;
}

void SourceDialog::reloadLiveCombo()
{
    if (!m_liveSourceCombo) {
        return;
    }
    const QString previous = m_liveSourceCombo->currentData().toString();

    m_liveSourceCombo->blockSignals(true);
    m_liveSourceCombo->clear();
    m_liveSourceCombo->addItem(QStringLiteral("全部直播源"), QString());
    for (const LiveSource &live : SourceManager::instance().enabledLives()) {
        m_liveSourceCombo->addItem(QStringLiteral("%1 [%2]").arg(live.name, live.typeLabel()), live.key);
    }
    const int index = m_liveSourceCombo->findData(previous);
    m_liveSourceCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_liveSourceCombo->blockSignals(false);
}

void SourceDialog::onRefreshLives()
{
    if (!m_liveClient || !m_pendingLiveKeys.isEmpty()) {
        return;
    }

    QList<LiveSource> targets;
    const QString selectedKey = m_liveSourceCombo->currentData().toString();
    if (selectedKey.isEmpty()) {
        targets = SourceManager::instance().enabledLives();
    } else if (const LiveSource *one = SourceManager::instance().findLive(selectedKey)) {
        if (one->enabled) {
            targets.append(*one);
        }
    }

    if (targets.isEmpty()) {
        m_liveStatusLabel->setText(
            QStringLiteral("还没有启用的直播源：去「源配置」页导入 TVBox 配置，"
                           "或点「添加直播源」直接填一条 m3u / txt 地址。"));
        return;
    }

    m_liveFailedCount = 0;
    m_liveRequestedCount = targets.size();
    m_pendingLiveKeys.clear();
    for (const LiveSource &source : targets) {
        m_pendingLiveKeys.insert(source.key);
        m_channelCache.remove(source.key);   // 手动刷新就是要重拉
    }

    m_allChannels.clear();
    m_visibleChannels.clear();
    m_channelTable->setRowCount(0);
    m_livePlayButton->setEnabled(false);
    m_liveAddButton->setEnabled(false);
    updateLiveStatus();

    for (const LiveSource &source : targets) {
        m_liveClient->fetch(source);
    }
}

void SourceDialog::onChannelsFetched(const QString &sourceKey,
                                     const QList<LiveChannel> &channels,
                                     const QString &error)
{
    // 关窗后的迟到回调，或已经被下一次刷新覆盖的请求，都直接丢弃
    if (m_ignoreResults || !m_pendingLiveKeys.contains(sourceKey)) {
        return;
    }
    m_pendingLiveKeys.remove(sourceKey);

    if (!error.isEmpty() || channels.isEmpty()) {
        ++m_liveFailedCount;
    } else {
        m_channelCache.insert(sourceKey, channels);
    }

    // 每到一个源就刷新一次表格，358 个频道也不用等全部源回来
    rebuildLiveTable();
}

void SourceDialog::onLiveFilterChanged()
{
    applyLiveFilter();
}

void SourceDialog::rebuildLiveTable()
{
    m_allChannels.clear();
    for (const LiveSource &source : SourceManager::instance().lives()) {
        if (!source.enabled) {
            continue;
        }
        const auto it = m_channelCache.constFind(source.key);
        if (it == m_channelCache.constEnd()) {
            continue;
        }
        for (const LiveChannel &channel : it.value()) {
            ChannelRow row;
            row.channel = channel;
            row.sourceKey = source.key;
            row.sourceName = source.name;
            m_allChannels.append(row);
        }
    }

    // 分组下拉：尽量保住用户当前选中的分组
    const QString previousGroup = m_liveGroupCombo->currentData().toString();
    m_liveGroupCombo->blockSignals(true);
    m_liveGroupCombo->clear();
    m_liveGroupCombo->addItem(QStringLiteral("全部分组"), QString());

    QStringList groups;
    for (const ChannelRow &row : m_allChannels) {
        const QString group = row.channel.group.isEmpty() ? LiveParser::ungroupedName()
                                                          : row.channel.group;
        if (!groups.contains(group)) {
            groups.append(group);
        }
    }
    for (const QString &group : groups) {
        m_liveGroupCombo->addItem(group, group);
    }
    const int index = m_liveGroupCombo->findData(previousGroup);
    m_liveGroupCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_liveGroupCombo->blockSignals(false);

    applyLiveFilter();
}

void SourceDialog::applyLiveFilter()
{
    const QString group = m_liveGroupCombo->currentData().toString();
    const QString keyword = m_liveFilterEdit->text().trimmed();

    m_visibleChannels.clear();
    for (const ChannelRow &row : m_allChannels) {
        const QString rowGroup = row.channel.group.isEmpty() ? LiveParser::ungroupedName()
                                                             : row.channel.group;
        if (!group.isEmpty() && rowGroup != group) {
            continue;
        }
        if (!keyword.isEmpty() && !row.channel.name.contains(keyword, Qt::CaseInsensitive)) {
            continue;
        }
        m_visibleChannels.append(row);
    }

    const int currentRow = m_channelTable->currentRow();

    m_channelTable->setUpdatesEnabled(false);
    m_channelTable->setRowCount(m_visibleChannels.size());
    for (int i = 0; i < m_visibleChannels.size(); ++i) {
        const ChannelRow &row = m_visibleChannels.at(i);

        auto *nameItem = new QTableWidgetItem(row.channel.name);
        nameItem->setToolTip(row.channel.url);
        auto *groupItem = new QTableWidgetItem(row.channel.group.isEmpty()
                                                   ? LiveParser::ungroupedName()
                                                   : row.channel.group);
        auto *sourceItem = new QTableWidgetItem(row.sourceName);
        sourceItem->setForeground(QColor(QStringLiteral("#8AB4F8")));

        m_channelTable->setItem(i, 0, nameItem);
        m_channelTable->setItem(i, 1, groupItem);
        m_channelTable->setItem(i, 2, sourceItem);
    }
    m_channelTable->setUpdatesEnabled(true);

    const bool hasChannels = !m_visibleChannels.isEmpty();
    m_livePlayButton->setEnabled(hasChannels);
    m_liveAddButton->setEnabled(hasChannels);

    if (hasChannels) {
        // 筛选后尽量保住原来的选中行，避免每次输入都跳回第一行
        m_channelTable->selectRow(qBound(0, currentRow, m_visibleChannels.size() - 1));
    }

    updateLiveStatus();
}

void SourceDialog::updateLiveStatus()
{
    if (!m_liveStatusLabel) {
        return;
    }

    if (!m_pendingLiveKeys.isEmpty()) {
        m_liveStatusLabel->setText(QStringLiteral("正在加载频道… 已完成 %1/%2 个直播源，当前 %3 个频道")
                                       .arg(m_liveRequestedCount - m_pendingLiveKeys.size())
                                       .arg(m_liveRequestedCount)
                                       .arg(m_allChannels.size()));
        return;
    }

    if (m_allChannels.isEmpty()) {
        // 没有频道时保留上一条提示（例如「还没有启用的直播源」），别把它冲掉
        if (m_liveRequestedCount == 0) {
            return;
        }
        m_liveStatusLabel->setText(QStringLiteral("没有解析到频道：%1 个直播源拉取失败，可换个源试试。")
                                       .arg(qMax(1, m_liveFailedCount)));
        return;
    }

    QString text = QStringLiteral("共 %1 个频道，来自 %2 个直播源")
                       .arg(m_allChannels.size())
                       .arg(m_channelCache.size());
    if (m_visibleChannels.size() != m_allChannels.size()) {
        text += QStringLiteral("，当前筛选 %1 个").arg(m_visibleChannels.size());
    }
    if (m_liveFailedCount > 0) {
        text += QStringLiteral("（%1 个源拉取失败）").arg(m_liveFailedCount);
    }
    m_liveStatusLabel->setText(text);
}

QList<PlaylistEntry> SourceDialog::buildLiveEntries(int *startIndex) const
{
    QList<PlaylistEntry> entries;
    const int currentRow = m_channelTable->currentRow();
    int start = 0;

    for (int i = 0; i < m_visibleChannels.size(); ++i) {
        const ChannelRow &row = m_visibleChannels.at(i);
        const QString url = row.channel.url.trimmed();
        if (url.isEmpty()) {
            continue;
        }

        QString userAgent;
        QString referer;
        if (const LiveSource *source = SourceManager::instance().findLive(row.sourceKey)) {
            userAgent = source->effectiveUserAgent();
            referer = source->effectiveReferer();
        }

        entries.append(PlaylistEntry::makeOnline(url, row.channel.name, row.sourceName,
                                                 userAgent, referer, /*isLive=*/true));
        if (i == currentRow) {
            start = entries.size() - 1;
        }
    }

    if (startIndex) {
        *startIndex = entries.isEmpty() ? 0 : start;
    }
    return entries;
}

void SourceDialog::onPlayLiveClicked()
{
    const int currentRow = m_channelTable->currentRow();
    if (currentRow < 0 || currentRow >= m_visibleChannels.size()) {
        m_liveStatusLabel->setText(QStringLiteral("⚠ 先在列表里选一个频道"));
        return;
    }

    int start = 0;
    const QList<PlaylistEntry> entries = buildLiveEntries(&start);
    if (entries.isEmpty()) {
        m_liveStatusLabel->setText(QStringLiteral("⚠ 选中的频道没有可用播放地址"));
        return;
    }

    emit playEntriesRequested(entries, start);
    m_liveStatusLabel->setText(QStringLiteral("▶ 正在播放 %1（当前分组 %2 个频道已进播放列表，PgUp / PgDn 换台）")
                                   .arg(m_visibleChannels.at(currentRow).channel.name)
                                   .arg(entries.size()));
}

void SourceDialog::onAddLiveClicked()
{
    int start = 0;
    const QList<PlaylistEntry> entries = buildLiveEntries(&start);
    if (entries.isEmpty()) {
        m_liveStatusLabel->setText(QStringLiteral("⚠ 当前分组没有可用频道"));
        return;
    }
    emit addEntriesRequested(entries);
    m_liveStatusLabel->setText(QStringLiteral("✓ 已把 %1 个频道加入播放列表").arg(entries.size()));
}

void SourceDialog::onLiveChannelActivated()
{
    onPlayLiveClicked();
}
