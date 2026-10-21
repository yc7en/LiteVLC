#include "ClickableSlider.h"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

ClickableSlider::ClickableSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent)
{
    setCursor(Qt::PointingHandCursor);
}

int ClickableSlider::pixelPosToRangeValue(int pos) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect gr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    int sliderMin = 0;
    int sliderMax = 0;
    int sliderLength = 0;

    if (orientation() == Qt::Horizontal) {
        sliderLength = sr.width();
        sliderMin = gr.x();
        sliderMax = gr.right() - sliderLength + 1;
    } else {
        sliderLength = sr.height();
        sliderMin = gr.y();
        sliderMax = gr.bottom() - sliderLength + 1;
    }

    return QStyle::sliderValueFromPosition(
        minimum(),
        maximum(),
        pos - sliderMin,
        sliderMax - sliderMin,
        opt.upsideDown
    );
}

void ClickableSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        int val = pixelPosToRangeValue(orientation() == Qt::Horizontal
                                           ? event->pos().x()
                                           : event->pos().y());
        setValue(val);
        emit seekRequested(val);
        event->accept();
        return;
    }
    QSlider::mousePressEvent(event);
}

void ClickableSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        int val = pixelPosToRangeValue(orientation() == Qt::Horizontal
                                           ? event->pos().x()
                                           : event->pos().y());
        setValue(val);
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void ClickableSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        int val = pixelPosToRangeValue(orientation() == Qt::Horizontal
                                           ? event->pos().x()
                                           : event->pos().y());
        setValue(val);
        emit seekRequested(val);
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}
