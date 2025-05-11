#include "querylist.h"
#include "ui_querylist.h"

queryList::queryList(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::queryList)
{
    ui->setupUi(this);
}

queryList::~queryList()
{
    delete ui;
}

void queryList::clear(){
    ui->listWidget->clear();
}

void queryList::addItem(QString item){
    ui->listWidget->addItem(item);
}

void queryList::addItems(QList<QString> items){
    ui->listWidget->addItems(items);
}
