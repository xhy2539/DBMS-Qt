#ifndef TABLESHOW_H
#define TABLESHOW_H

#include <QWidget>
#include "xhytable.h"

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

private:
    Ui::tableShow *ui;
};

#endif // TABLESHOW_H
