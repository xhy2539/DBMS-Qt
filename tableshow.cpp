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
    ui->tableWidget->setRowCount(table.records().count());

    int row = 0;
    QStringList headers;
    ui->tableWidget->horizontalHeader()->setStyleSheet(
        "QHeaderView::section {"
        "    padding: 4px;"
        "    qproperty-wordWrap: true;"  // 允许换行
        "    text-align: top-left;"       // 对齐方式
        "}"
    );
    for(xhyfield field : table.fields()){
        headers << (field.name()+"\n("+field.typestring()+")");

    }
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    ui->tableWidget->horizontalHeader()->setMinimumSectionSize(40);
    for(xhyrecord record : table.records()){
        int column = 0;

        for(int column = 0; column < table.fields().count(); ++column){
            ui->tableWidget->setItem(row,column,new QTableWidgetItem(record.value(table.fields().at(column).name())));

        }
        row++;
    }
}

void tableShow::on_tableWidget_itemChanged(QTableWidgetItem *item)
{
    qDebug()<<item->column();
}

