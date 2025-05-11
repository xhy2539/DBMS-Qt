#ifndef TABLEDESIGN_H
#define TABLEDESIGN_H

#include <QWidget>

namespace Ui {
class tableDesign;
}

class tableDesign : public QWidget
{
    Q_OBJECT

public:
    explicit tableDesign(QWidget *parent = nullptr);
    ~tableDesign();

private:
    Ui::tableDesign *ui;
};

#endif // TABLEDESIGN_H
