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
    void setDb(QString db);
signals:
    void tableOpen(QString tableName);
    void tableCreate();
    void tableDrop(QString dbName,QString sql);
private slots:
    void on_listWidget_itemDoubleClicked(QListWidgetItem *item);

    void on_openTable_released();

    void on_listWidget_itemClicked(QListWidgetItem *item);

    void on_createTable_released();

    void on_dropTable_released();

private:
    Ui::tableList *ui;
    QString current_itemName;
    QString current_db;
};

#endif // TABLELIST_H
