#include "popupwidget.h"
#include <QDebug>

popupWidget::popupWidget(QWidget *parent)
    : QWidget(parent) {
    // 初始隐藏，不参与布局
    hide();
    setWindowFlags(Qt::SubWindow); // 设为子窗口
    setStyleSheet("background: #F0F0F0; border-top: 2px solid #808080;");
}

void popupWidget::showPopup() {
    // 初始化尺寸和位置
    m_maxHeight = parentWidget()->height() * 0.8; // 最大高度为父控件高度的80%
    setFixedWidth(parentWidget()->width());
    setFixedHeight(m_minHeight);
    move(0, parentWidget()->height() - height());
    show();
    raise(); // 提升到最上层
}

void popupWidget::hidePopup() {
    hide();
}

// 鼠标按下：开始拖动
void popupWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_startY = event->globalPosition().toPoint().y();
        m_initialHeight = height();
    }
    QWidget::mousePressEvent(event);
}

// 鼠标移动：调整高度
void popupWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDragging) {
        // 计算高度变化
        int deltaY = m_startY - event->globalPosition().toPoint().y();
        int newHeight = m_initialHeight + deltaY;
        newHeight = qBound(m_minHeight, newHeight, m_maxHeight);
        // 更新组件高度和位置
        setFixedHeight(newHeight);
        move(0, parentWidget()->height() - newHeight);
    }
    QWidget::mouseMoveEvent(event);
}

// 鼠标释放：结束拖动
void popupWidget::mouseReleaseEvent(QMouseEvent *event) {
    m_isDragging = false;
    QWidget::mouseReleaseEvent(event);
}

// 父控件大小变化时，调整宽度和位置
void popupWidget::resizeEvent(QResizeEvent *event) {
    setFixedWidth(parentWidget()->width());
    move(0, parentWidget()->height() - height());
    QWidget::resizeEvent(event);
}
