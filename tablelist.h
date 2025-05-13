#ifndef TABLELIST_H
#define TABLELIST_H

#include <QWidget>
#include <QListWidget>

namespace Ui {
class tableList;
}

class tableList : public QWidget
{
    Q_OBJECT

public:
    explicit tableList(QWidget *parent = nullptr);
    ~tableList();
    void clear();
    void addItem(QString item);
    void addItems(QList<QString> items);
signals:
    void tableOpen(QString tableName);

private slots:
    void on_listWidget_itemDoubleClicked(QListWidgetItem *item);

    void on_openTable_released();

    void on_listWidget_itemClicked(QListWidgetItem *item);

private:
    Ui::tableList *ui;
    QString current_itemName;
};

#endif // TABLELIST_H
