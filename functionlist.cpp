#include "functionlist.h"
#include "ui_functionlist.h"

functionList::functionList(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::functionList)
{
    ui->setupUi(this);
}

functionList::~functionList()
{
    delete ui;
}

void functionList::clear(){
    ui->listWidget->clear();
}

void functionList::addItem(QString item){
    ui->listWidget->addItem(item);
}

void functionList::addItems(QList<QString> items){
    ui->listWidget->addItems(items);
}
