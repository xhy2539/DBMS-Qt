#include "tableshow.h"
#include "ui_tableshow.h"
#include "CustomDelegate.h"

tableShow::tableShow(xhydbmanager* dbms,QString dbName ,QString tableName , QWidget *parent )
    : QWidget(parent)
    , ui(new Ui::tableShow)
    , m_tableName(tableName)
    , m_dbName(dbName)
    , m_dbms(dbms)
{
    xhydatabase* db = m_dbms->find_database(m_dbName);
    m_table = db->find_table(m_tableName);
    ui->setupUi(this);
    ui->comfirm->setEnabled(false);
    ui->cancle->setEnabled(false);
    CustomDelegate *delegate = new CustomDelegate(ui->tableWidget);
    ui->tableWidget->setItemDelegate(delegate); // 设置委托

    // 连接自定义信号到槽函数
    connect(delegate, &CustomDelegate::dataChanged,[=](int row, int col, const QString &oldVal, const QString &newVal) {
        if(row < m_table->records().count()){
            // qDebug() << "行:" << row << "列:" << col << "旧值:" << oldVal << "新值:" << newVal <<m_table.records().count();
            // qDebug()<<" "<<ui->tableWidget->item(row,col)->text();
            QString updateString,columnName;
            columnName = m_table->fields().at(col).name();

            updateString = "UPDATE "+m_table->name()+" SET "+ columnName +" = "+newVal+" WHERE ";
            for(QString primary : m_table->primaryKeys()){
                int i=1;
                if(i == 1){
                    updateString += (primary + " = " +m_table->records().at(row).value(primary));
                }else
                    updateString += (" AND "+primary + " = " +m_table->records().at(row).value(primary));
                i++;
            }
            updateString += ";";
            // qDebug()<<updateString;
            emit dataChanged(updateString);
        }
    });

}

tableShow::~tableShow()
{
    delete ui;
}

// void tableShow::setTable(const xhytable& table){
//     m_table = table;

//     // for(QString primaryKey : table.primaryKeys()){
//     //     qDebug()<<primaryKey;
//     // }
// }

void tableShow::resetShow(){
    xhydatabase* db = m_dbms->find_database(m_dbName);
    m_table = db->find_table(m_tableName);

    ui->tableWidget->clear();
    ui->tableWidget->setColumnCount(m_table->fields().count());
    ui->tableWidget->setRowCount(m_table->records().count());

    int row = 0;
    QStringList headers;
    ui->tableWidget->horizontalHeader()->setStyleSheet(
        "QHeaderView::section {"
        "    padding: 4px;"
        "    qproperty-wordWrap: true;"  // 允许换行
        "    text-align: top-left;"       // 对齐方式
        "}"
    );
    for(xhyfield field : m_table->fields()){
        headers << (field.name()+"\n("+field.typestring()+")");

    }
    ui->tableWidget->setHorizontalHeaderLabels(headers);
    ui->tableWidget->horizontalHeader()->setMinimumSectionSize(40);
    for(xhyrecord record : m_table->records()){
        int column = 0;

        for(int column = 0; column < m_table->fields().count(); ++column){
            ui->tableWidget->setItem(row,column,new QTableWidgetItem(record.value(m_table->fields().at(column).name())));

        }
        row++;
    }
}

void tableShow::on_addRecord_released()
{
    ui->tableWidget->insertRow(ui->tableWidget->rowCount());
    resetButton(false);
}


void tableShow::on_deleteRecord_released()
{
    if(ui->tableWidget->currentRow() == -1) return;
    int row = ui->tableWidget->currentRow();
    QString deleteString = "DELETE FROM "+m_table->name() +" WHERE ";

    for(QString primary : m_table->primaryKeys()){
        int i=1;
        if(i == 1){
        deleteString += (primary + " = " +m_table->records().at(row).value(primary));
        }else
            deleteString += (" AND "+primary + " = " +m_table->records().at(row).value(primary));
        i++;
    }
    deleteString += ";";
    ui->tableWidget->removeRow(row);
    // qDebug()<<deleteString;
    emit dataChanged(deleteString);
}


void tableShow::on_comfirm_released()
{

    QString insertString = "INSERT INTO "+m_table->name()+"(";
    int i=1;
    for(xhyfield field : m_table->fields()){
        if(i == 1)
            insertString += field.name();
        else
            insertString += ","+field.name();
        i++;
    }
    insertString += ") VALUES (";
    i=1;
    for(int col=0; col<ui->tableWidget->columnCount(); ++col){
        int row=ui->tableWidget->rowCount()-1;

        QString val;
        if(!ui->tableWidget->item(row,col)){
            val = "NULL";
        }
        else val = ui->tableWidget->item(row,col)->text();
        if(i == 1){
            insertString += val;
        }
        else{
            insertString += (","+ val);
        }
        i++;
    }
    insertString += ");";
    // qDebug()<<insertString;
    emit dataChanged(insertString);
}


void tableShow::on_cancle_released()
{
    ui->tableWidget->removeRow(ui->tableWidget->rowCount()-1);
    resetButton(true);
}

void tableShow::resetButton(bool yes){
    ui->addRecord->setEnabled(yes);
    ui->deleteRecord->setEnabled(yes);
    ui->comfirm->setEnabled(!yes);
    ui->cancle->setEnabled(!yes);
}

void tableShow::on_refresh_released()
{
    resetShow();
    if(ui->comfirm->isEnabled()) {
        resetButton(true);
    }
}

