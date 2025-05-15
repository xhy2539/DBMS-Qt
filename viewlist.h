#ifndef VIEWLIST_H
#define VIEWLIST_H

#include <QWidget>

namespace Ui {
class viewList;
}

class viewList : public QWidget
{
    Q_OBJECT

public:
    explicit viewList(QWidget *parent = nullptr);
    ~viewList();
    void clear();
    void addItem(QString item);
    void addItems(QList<QString> items);

private:
    Ui::viewList *ui;
};

#endif // VIEWLIST_H
