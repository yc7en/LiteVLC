#pragma once

#include <QSlider>

/**
 * @brief 支持点击直接跳转 (Click-to-Seek) 与平滑拖拽防抖的进度条控件
 */
class ClickableSlider : public QSlider
{
    Q_OBJECT
public:
    explicit ClickableSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

    bool isUserDragging() const { return m_isDragging; }

signals:
    // 用户点击或松开拖拽后发出的精确跳转信号
    void seekRequested(int value);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int pixelPosToRangeValue(int pos) const;
    bool m_isDragging = false;
};
