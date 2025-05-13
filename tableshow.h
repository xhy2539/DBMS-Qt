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
    explicit tableShow(QWidget *parent = nullptr);
    ~tableShow();
    void setTable(xhytable table);

private slots:
    void on_tableWidget_itemChanged(QTableWidgetItem *item);

private:
    Ui::tableShow *ui;
};

#endif // TABLESHOW_H
