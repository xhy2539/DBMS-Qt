#include "tableshow.h"
#include "ui_tableshow.h"

tableShow::tableShow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tableShow)
{
    ui->setupUi(this);
}

tableShow::~tableShow()
{
    delete ui;
}

void tableShow::setTable(xhytable table){
    ui->tableWidget->setColumnCount(table.fields().count());
    ui->tableWidget->setRowCount(table.records().count()+1);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    int column=0;
    int row = 1;
    for(xhyfield field : table.fields()){
        QTableWidgetItem* item = new QTableWidgetItem(field.name()+"\n("+field.typestring()+")");
        item->setTextAlignment(Qt::AlignTop | Qt::AlignLeft);

        ui->tableWidget->setItem(0,column,item);
        column++;
    }
    for(xhyrecord record : table.records()){
        int column = 0;

        for(int column = 0; column < table.fields().count(); ++column){
            ui->tableWidget->setItem(row,column,new QTableWidgetItem(record.value(table.fields().at(column).name())));

        }
        row++;
    }
}
