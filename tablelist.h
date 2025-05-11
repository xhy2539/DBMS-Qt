#ifndef TABLELIST_H
#define TABLELIST_H

#include <QWidget>

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

private:
    Ui::tableList *ui;
};

#endif // TABLELIST_H
