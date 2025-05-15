#ifndef FUNCTIONLIST_H
#define FUNCTIONLIST_H

#include <QWidget>

namespace Ui {
class functionList;
}

class functionList : public QWidget
{
    Q_OBJECT

public:
    explicit functionList(QWidget *parent = nullptr);
    ~functionList();
    void clear();
    void addItem(QString item);
    void addItems(QList<QString> items);

private:
    Ui::functionList *ui;
};

#endif // FUNCTIONLIST_H
