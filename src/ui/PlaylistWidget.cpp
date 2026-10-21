#include "PlaylistWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QFont>

PlaylistWidget::PlaylistWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(250);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // 顶部标题与循环模式选择
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(QStringLiteral("播放列表"), this);
    QFont boldFont = titleLabel->font();
    boldFont.setBold(true);
    titleLabel->setFont(boldFont);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("顺序播放"), static_cast<int>(ConfigManager::Sequential));
    m_modeCombo->addItem(QStringLiteral("列表循环"), static_cast<int>(ConfigManager::LoopList));
    m_modeCombo->addItem(QStringLiteral("单曲循环"), static_cast<int>(ConfigManager::LoopOne));

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_modeCombo);

    // 中间 QListWidget 列表
    m_listWidget = new QListWidget(this);
    m_listWidget->setAlternatingRowColors(true);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    // 底部操作按钮行
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    m_btnAdd    = new QPushButton(QStringLiteral("+ 添加"), this);
    m_btnRemove = new QPushButton(QStringLiteral("- 移除"), this);
    m_btnClear  = new QPushButton(QStringLiteral("清空"), this);

    btnLayout->addWidget(m_btnAdd);
    btnLayout->addWidget(m_btnRemove);
    btnLayout->addWidget(m_btnClear);

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(m_listWidget, 1);
    mainLayout->addLayout(btnLayout);

    // 信号连接
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        int row = m_listWidget->row(item);
        if (row >= 0) {
            emit itemDoubleClicked(row);
        }
    });

    connect(m_btnAdd, &QPushButton::clicked, this, &PlaylistWidget::addFilesRequested);
    connect(m_btnRemove, &QPushButton::clicked, this, [this]() {
        int row = m_listWidget->currentRow();
        if (row >= 0) {
            emit removeIndexRequested(row);
        }
    });
    connect(m_btnClear, &QPushButton::clicked, this, &PlaylistWidget::clearRequested);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx >= 0) {
            auto mode = static_cast<ConfigManager::PlayMode>(m_modeCombo->itemData(idx).toInt());
            emit playModeChanged(mode);
        }
    });
}

void PlaylistWidget::updateList(const QStringList &files, int currentIndex)
{
    m_listWidget->clear();
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo fi(files.at(i));
        const QString display = QStringLiteral("%1. %2")
            .arg(i + 1, 2, 10, QLatin1Char('0'))
            .arg(fi.fileName());

        auto *item = new QListWidgetItem(display, m_listWidget);
        item->setToolTip(fi.absoluteFilePath());
    }
    setCurrentIndex(currentIndex);
}

void PlaylistWidget::setCurrentIndex(int currentIndex)
{
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        QFont f = item->font();
        const bool isCurrent = (i == currentIndex);
        f.setBold(isCurrent);
        item->setFont(f);
        if (isCurrent) {
            m_listWidget->setCurrentRow(i);
        }
    }
}

void PlaylistWidget::setPlayMode(ConfigManager::PlayMode mode)
{
    m_modeCombo->blockSignals(true);
    for (int i = 0; i < m_modeCombo->count(); ++i) {
        if (m_modeCombo->itemData(i).toInt() == static_cast<int>(mode)) {
            m_modeCombo->setCurrentIndex(i);
            break;
        }
    }
    m_modeCombo->blockSignals(false);
}
