#ifndef QUERYLIST_H
#define QUERYLIST_H

#include <QWidget>

namespace Ui {
class queryList;
}

class queryList : public QWidget
{
    Q_OBJECT

public:
    explicit queryList(QWidget *parent = nullptr);
    ~queryList();
    void clear();
    void addItem(QString item);
    void addItems(QList<QString> items);

private:
    Ui::queryList *ui;
};

#endif // QUERYLIST_H
