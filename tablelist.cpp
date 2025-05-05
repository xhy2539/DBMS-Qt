#include "tablelist.h"
#include "ui_tablelist.h"

tableList::tableList(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tableList)
{
    ui->setupUi(this);
}

tableList::~tableList()
{
    delete ui;
}

void tableList::clear(){
    ui->listWidget->clear();
}

void tableList::addItem(QString item){
    ui->listWidget->addItem(item);
}

void tableList::addItems(QList<QString> items){
    ui->listWidget->addItems(items);
}
