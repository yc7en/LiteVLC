#include "SettingsDialog.h"
#include "core/ConfigManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QLabel>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("播放器设置"));
    setMinimumWidth(380);

    auto &cfg = ConfigManager::instance();
    auto *mainLayout = new QVBoxLayout(this);

    // 1. 硬件加速设置分组
    auto *hwGroup = new QGroupBox(QStringLiteral("GPU 硬件加速 (解码 4K 高清视频大幅降低 CPU 占用)"), this);
    auto *hwLayout = new QFormLayout(hwGroup);

    m_chkHwAccel = new QCheckBox(QStringLiteral("启用 GPU 硬件加速解码"), this);
    m_chkHwAccel->setChecked(cfg.hwAccelEnabled());

    m_comboHwBackend = new QComboBox(this);
    m_comboHwBackend->addItem(QStringLiteral("自动选择 (Auto - 推荐)"), QStringLiteral("any"));
    m_comboHwBackend->addItem(QStringLiteral("Direct3D 11 (d3d11va - Win10/11 推荐)"), QStringLiteral("d3d11va"));
    m_comboHwBackend->addItem(QStringLiteral("DXVA2 (dxva2 - 兼容性好)"), QStringLiteral("dxva2"));

    for (int i = 0; i < m_comboHwBackend->count(); ++i) {
        if (m_comboHwBackend->itemData(i).toString() == cfg.hwAccelBackend()) {
            m_comboHwBackend->setCurrentIndex(i);
            break;
        }
    }
    m_comboHwBackend->setEnabled(m_chkHwAccel->isChecked());
    connect(m_chkHwAccel, &QCheckBox::toggled, m_comboHwBackend, &QWidget::setEnabled);

    hwLayout->addRow(m_chkHwAccel);
    hwLayout->addRow(QStringLiteral("硬解后端:"), m_comboHwBackend);
    auto *hwHint = new QLabel(QStringLiteral("* 修改硬件加速设置将在下一次加载视频时生效"), this);
    hwHint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    hwLayout->addRow(hwHint);

    // 2. 跳过片头片尾分组
    auto *skipGroup = new QGroupBox(QStringLiteral("跳过片头 / 片尾 (适合追剧连播)"), this);
    auto *skipLayout = new QFormLayout(skipGroup);

    m_chkSkipIntro = new QCheckBox(QStringLiteral("自动跳过片头"), this);
    m_chkSkipIntro->setChecked(cfg.skipIntroEnabled());

    m_spinSkipIntro = new QSpinBox(this);
    m_spinSkipIntro->setRange(1, 600);
    m_spinSkipIntro->setSuffix(QStringLiteral(" 秒"));
    m_spinSkipIntro->setValue(cfg.skipIntroSeconds());
    m_spinSkipIntro->setEnabled(m_chkSkipIntro->isChecked());
    connect(m_chkSkipIntro, &QCheckBox::toggled, m_spinSkipIntro, &QWidget::setEnabled);

    m_chkSkipOutro = new QCheckBox(QStringLiteral("自动跳过片尾 (触发下一集)"), this);
    m_chkSkipOutro->setChecked(cfg.skipOutroEnabled());

    m_spinSkipOutro = new QSpinBox(this);
    m_spinSkipOutro->setRange(1, 600);
    m_spinSkipOutro->setSuffix(QStringLiteral(" 秒"));
    m_spinSkipOutro->setValue(cfg.skipOutroSeconds());
    m_spinSkipOutro->setEnabled(m_chkSkipOutro->isChecked());
    connect(m_chkSkipOutro, &QCheckBox::toggled, m_spinSkipOutro, &QWidget::setEnabled);

    skipLayout->addRow(m_chkSkipIntro, m_spinSkipIntro);
    skipLayout->addRow(m_chkSkipOutro, m_spinSkipOutro);

    // 3. 在线播放分组
    auto *onlineGroup = new QGroupBox(QStringLiteral("在线播放"), this);
    auto *onlineLayout = new QVBoxLayout(onlineGroup);

    m_chkHlsAdFilter = new QCheckBox(QStringLiteral("过滤 HLS 插播广告 (跳过清单里的短广告块)"), this);
    m_chkHlsAdFilter->setChecked(cfg.hlsAdFilterEnabled());
    auto *hlsHint = new QLabel(QStringLiteral("* 播放在线 m3u8 时在本地过滤掉混入的插播广告，只影响在线播放"), this);
    hlsHint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    onlineLayout->addWidget(m_chkHlsAdFilter);
    onlineLayout->addWidget(hlsHint);

    // 4. 历史记录与自动续播分组
    auto *histGroup = new QGroupBox(QStringLiteral("历史记录与记忆续播"), this);
    auto *histLayout = new QVBoxLayout(histGroup);

    m_chkAutoResume = new QCheckBox(QStringLiteral("下次打开同一视频时，自动跳转到上次观看位置"), this);
    m_chkAutoResume->setChecked(cfg.autoResumeEnabled());

    m_btnClearHistory = new QPushButton(QStringLiteral("清除所有历史播放进度记录"), this);
    connect(m_btnClearHistory, &QPushButton::clicked, this, [this]() {
        ConfigManager::instance().clearHistory();
        emit historyCleared();
        QMessageBox::information(this, QStringLiteral("已清除"), QStringLiteral("历史播放进度记录已清空。"));
    });

    histLayout->addWidget(m_chkAutoResume);
    histLayout->addWidget(m_btnClearHistory);

    // 底部确认/取消按钮
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(hwGroup);
    mainLayout->addWidget(skipGroup);
    mainLayout->addWidget(onlineGroup);
    mainLayout->addWidget(histGroup);
    mainLayout->addWidget(btnBox);
}

void SettingsDialog::onAccept()
{
    auto &cfg = ConfigManager::instance();
    cfg.setHwAccelEnabled(m_chkHwAccel->isChecked());
    cfg.setHwAccelBackend(m_comboHwBackend->currentData().toString());

    cfg.setSkipIntroEnabled(m_chkSkipIntro->isChecked());
    cfg.setSkipIntroSeconds(m_spinSkipIntro->value());

    cfg.setSkipOutroEnabled(m_chkSkipOutro->isChecked());
    cfg.setSkipOutroSeconds(m_spinSkipOutro->value());

    cfg.setAutoResumeEnabled(m_chkAutoResume->isChecked());
    cfg.setHlsAdFilterEnabled(m_chkHlsAdFilter->isChecked());
    accept();
}
