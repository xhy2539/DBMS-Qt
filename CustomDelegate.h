#ifndef CUSTOMDELEGATE_H
#define CUSTOMDELEGATE_H

#include <QStyledItemDelegate>
#include <QLineEdit>

class CustomDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit CustomDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    // 创建编辑器时保存旧值
    QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (QLineEdit *lineEdit = qobject_cast<QLineEdit*>(editor)) {
            oldValue = index.data(Qt::DisplayRole).toString(); // 保存旧值
        }
        return editor;
    }

    // 提交数据时触发信号
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        QStyledItemDelegate::setModelData(editor, model, index);
        QString newValue = model->data(index, Qt::DisplayRole).toString();
        emit dataChanged(index.row(), index.column(), oldValue, newValue); // 发射信号
    }

signals:
    void dataChanged(int row, int column, const QString &oldValue, const QString &newValue) const;

private:
    mutable QString oldValue; // 临时存储旧值
};

#endif // CUSTOMDELEGATE_H
