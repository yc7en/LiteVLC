#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>

/**
 * @brief 极简设置对话框
 * 包含：
 * 1. GPU 硬件加速开关与解码器选择 (Auto / D3D11VA / DXVA2 / 软解)
 * 2. 跳过片头 / 跳过片尾开关与秒数设定
 * 3. 自动记忆续播开关与清除历史记录按钮
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void historyCleared();

private slots:
    void onAccept();

private:
    QCheckBox *m_chkHwAccel = nullptr;
    QComboBox *m_comboHwBackend = nullptr;

    QCheckBox *m_chkSkipIntro = nullptr;
    QSpinBox  *m_spinSkipIntro = nullptr;

    QCheckBox *m_chkSkipOutro = nullptr;
    QSpinBox  *m_spinSkipOutro = nullptr;

    QCheckBox *m_chkAutoResume = nullptr;
    QCheckBox *m_chkHlsAdFilter = nullptr;
    QPushButton *m_btnClearHistory = nullptr;
};
