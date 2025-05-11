#ifndef TABLESHOW_H
#define TABLESHOW_H

#include <QWidget>
#include "xhytable.h"
#include <QTableWidgetItem>

namespace Ui {
class tableShow;
}

class tableShow : public QWidget
{
    Q_OBJECT

public:
    explicit tableShow(QWidget *parent = nullptr , QString dbName = nullptr );
    ~tableShow();
    void setTable(xhytable table);
signals:
    void dataChanged(QString database,QString sql);
private slots:
    void on_tableWidget_itemChanged(QTableWidgetItem *item);

    void on_addRecord_released();

    void on_deleteRecord_released();

    void on_comfirm_released();

    void on_cancle_released();

private:
    Ui::tableShow *ui;
    xhytable m_table ;
    QString dbName;
    bool adding = false;
};

#endif // TABLESHOW_H
