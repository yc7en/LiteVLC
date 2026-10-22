#include "SourceDialog.h"

#include "core/SourceManager.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
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
    , m_importNam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("在线片源"));
    resize(980, 680);

    // 非模态：开着它也能操作主窗口（暂停、拖进度、切集），不需要先关窗
    setWindowModality(Qt::NonModal);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(createSearchTab(), QStringLiteral("在线搜索"));
    tabs->addTab(createSourceTab(), QStringLiteral("源配置"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->addWidget(tabs);

    setSizeGripEnabled(true);

    connect(m_client, &CmsClient::searchFinished, this, &SourceDialog::onSearchFinished);
    connect(m_client, &CmsClient::detailFinished, this, &SourceDialog::onDetailFinished);

    reloadSiteCombo();
    reloadSiteTable();

    // 还没配任何源时直接落到「源配置」页，省得用户对着空列表发呆
    if (SourceManager::instance().searchableSites().isEmpty()) {
        tabs->setCurrentIndex(1);
        m_statusLabel->setText(QStringLiteral("还没有配置片源：切到「源配置」页导入 TVBox 订阅即可"));
    }
}

void SourceDialog::focusSearchInput()
{
    if (m_keywordEdit) {
        m_keywordEdit->setFocus();
        m_keywordEdit->selectAll();
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
    auto *btnOpen = new QPushButton(QStringLiteral("打开配置文件"), tab);
    auto *btnRemove = new QPushButton(QStringLiteral("删除选中"), tab);

    buttonRow->addWidget(btnUrl);
    buttonRow->addWidget(btnFile);
    buttonRow->addWidget(btnClip);
    buttonRow->addWidget(btnOpen);
    buttonRow->addStretch(1);
    buttonRow->addWidget(btnRemove);
    layout->addLayout(buttonRow);

    auto *hint = new QLabel(
        QStringLiteral("支持 TVBox / CatVod 订阅里的 <b>sites</b> 数组，只对接 <b>type=0(XML)</b> 和 "
                       "<b>type=1(JSON)</b> 的苹果 CMS 接口。<br>"
                       "jar / js / py 这类脚本 Spider 源需要额外运行时，导入时会自动跳过。"
                       "勾选第一列即可启用/停用某个源，改动立即保存。"),
        tab);
    hint->setObjectName(QStringLiteral("sourceHintLabel"));
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    layout->addWidget(hint);

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
    layout->addWidget(m_siteTable, 1);

    connect(btnUrl, &QPushButton::clicked, this, &SourceDialog::onImportFromUrl);
    connect(btnFile, &QPushButton::clicked, this, &SourceDialog::onImportFromFile);
    connect(btnClip, &QPushButton::clicked, this, &SourceDialog::onImportFromClipboard);
    connect(btnOpen, &QPushButton::clicked, this, &SourceDialog::onOpenConfigFile);
    connect(btnRemove, &QPushButton::clicked, this, &SourceDialog::onRemoveSite);
    connect(m_siteTable, &QTableWidget::itemChanged, this, &SourceDialog::onSiteItemChanged);

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

void SourceDialog::startImportRequest(const QUrl &url)
{
    if (!url.isValid()) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), QStringLiteral("订阅地址无效"));
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                                     "Chrome/124.0.0.0 Safari/537.36"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);

    QNetworkReply *reply = m_importNam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, QStringLiteral("导入失败"),
                                 QStringLiteral("拉取订阅失败：%1").arg(reply->errorString()));
            return;
        }
        importPayload(reply->readAll(), QStringLiteral("订阅链接 %1").arg(url.toString()));
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
    QString error;
    ParseStats stats;
    const int count = SourceManager::instance().importSubscription(payload, false, &error, &stats);

    if (count <= 0) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             error.isEmpty()
                                 ? QStringLiteral("没有解析到可用的接口源（来源：%1）").arg(origin)
                                 : QStringLiteral("%1\n（来源：%2）").arg(error, origin));
        return;
    }

    reloadSiteTable();
    reloadSiteCombo();

    // 混合配置：可用源已导入，同时告知跳过了哪些，免得用户疑惑「少了一半」
    QString extra;
    if (stats.spiderSkipped > 0 || stats.otherSkipped > 0) {
        QStringList skipped;
        if (stats.spiderSkipped > 0) {
            skipped << QStringLiteral("%1 个 Spider 脚本源（type=3，需 jar 运行时）").arg(stats.spiderSkipped);
        }
        if (stats.otherSkipped > 0) {
            skipped << QStringLiteral("%1 个其他不支持源").arg(stats.otherSkipped);
        }
        extra = QStringLiteral("\n\n另有 %1 个源未导入：%2。")
                    .arg(stats.spiderSkipped + stats.otherSkipped)
                    .arg(skipped.join(QStringLiteral("，")));
    }

    QMessageBox::information(
        this,
        QStringLiteral("导入完成"),
        QStringLiteral("已从 %1 导入 %2 个可用片源。%3\n\n"
                       "现在回到「在线搜索」页即可搜索播放。")
            .arg(origin)
            .arg(count)
            .arg(extra));
}

void SourceDialog::onRemoveSite()
{
    const QModelIndexList rows = m_siteTable->selectionModel()
                                     ? m_siteTable->selectionModel()->selectedRows()
                                     : QModelIndexList();
    if (rows.isEmpty()) {
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("删除确认"),
                              QStringLiteral("确定要删除选中的 %1 个片源吗？").arg(rows.size()))
        != QMessageBox::Yes) {
        return;
    }

    QStringList keys;
    for (const QModelIndex &index : rows) {
        if (QTableWidgetItem *nameItem = m_siteTable->item(index.row(), 1)) {
            keys.append(nameItem->data(Qt::UserRole).toString());
        }
    }
    for (const QString &key : keys) {
        SourceManager::instance().removeSite(key);
    }

    reloadSiteTable();
    reloadSiteCombo();
    updateSearchStatus();
}

void SourceDialog::onOpenConfigFile()
{
    const QString path = SourceManager::instance().configFilePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
