#ifndef TABLESHOW_H
#define TABLESHOW_H

#include <QWidget>

namespace Ui {
class tableShow;
}

class tableShow : public QWidget
{
    Q_OBJECT

public:
    explicit tableShow(QWidget *parent = nullptr);
    ~tableShow();

private:
    Ui::tableShow *ui;
};

#endif // TABLESHOW_H
