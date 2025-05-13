#ifndef POPUPWIDGET_H
#define POPUPWIDGET_H

#include <QWidget>
#include <QMouseEvent>

class popupWidget : public QWidget {
    Q_OBJECT
public:
    explicit popupWidget(QWidget *parent = nullptr);

    void showPopup();  // 显示并定位到父控件底部
    void hidePopup();  // 隐藏

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool m_isDragging = false;
    int m_startY;      // 鼠标按下时的全局Y坐标
    int m_initialHeight; // 拖动开始时的组件高度
    int m_minHeight = 50; // 最小高度
    int m_maxHeight;    // 最大高度（动态计算）
};

#endif // POPUPWIDGET_H
