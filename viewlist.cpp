#include "viewlist.h"
#include "ui_viewlist.h"

viewList::viewList(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::viewList)
{
    ui->setupUi(this);
}

viewList::~viewList()
{
    delete ui;
}

void viewList::clear(){
    ui->listWidget->clear();
}

void viewList::addItem(QString item){
    ui->listWidget->addItem(item);
}

void viewList::addItems(QList<QString> items){
    ui->listWidget->addItems(items);
}
