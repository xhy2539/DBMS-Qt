#include "tableshow.h"
#include "ui_tableshow.h"
#include "CustomDelegate.h"

tableShow::tableShow(QWidget *parent, QString dbName )
    : QWidget(parent)
    , ui(new Ui::tableShow)
    , dbName(dbName)
    , m_table(NULL)
{

    ui->setupUi(this);
    ui->comfirm->setEnabled(false);
    ui->cancle->setEnabled(false);
    CustomDelegate *delegate = new CustomDelegate(ui->tableWidget);
    ui->tableWidget->setItemDelegate(delegate); // 设置委托

    // 连接自定义信号到槽函数
    connect(delegate, &CustomDelegate::dataChanged,[=](int row, int col, const QString &oldVal, const QString &newVal) {
        if(row < m_table.records().count()){
            // qDebug() << "行:" << row << "列:" << col << "旧值:" << oldVal << "新值:" << newVal <<m_table.records().count();

        }
    });

}

tableShow::~tableShow()
{
    delete ui;
}

void tableShow::setTable(xhytable table){
    m_table = table;
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
    // for(QString primaryKey : table.primaryKeys()){
    //     qDebug()<<primaryKey;
    // }
}

void tableShow::on_tableWidget_itemChanged(QTableWidgetItem *item)
{
    // qDebug()<<item->column();
}


void tableShow::on_addRecord_released()
{
    ui->tableWidget->insertRow(ui->tableWidget->rowCount());
    ui->addRecord->setEnabled(false);
    ui->deleteRecord->setEnabled(false);
    ui->comfirm->setEnabled(true);
    ui->cancle->setEnabled(true);
}


void tableShow::on_deleteRecord_released()
{

}


void tableShow::on_comfirm_released()
{

}


void tableShow::on_cancle_released()
{
    ui->tableWidget->removeRow(ui->tableWidget->rowCount()-1);

    ui->addRecord->setEnabled(true);
    ui->deleteRecord->setEnabled(true);
    ui->comfirm->setEnabled(false);
    ui->cancle->setEnabled(false);
}

