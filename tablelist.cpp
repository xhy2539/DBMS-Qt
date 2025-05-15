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

void tableList::setDb(QString db){
    current_db = db;
}

void tableList::on_listWidget_itemDoubleClicked(QListWidgetItem *item)
{
    emit tableOpen(item->text());
}

void tableList::on_openTable_released()
{
    emit tableOpen(current_itemName);
}


void tableList::on_listWidget_itemClicked(QListWidgetItem *item)
{
    current_itemName = item->text();
}


void tableList::on_createTable_released()
{

}


void tableList::on_dropTable_released()
{
    QString sql = "DROP TABLE " + current_itemName + ";";
    emit tableDrop(current_db,sql);
}

