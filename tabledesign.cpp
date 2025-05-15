#include "tabledesign.h"
#include "ui_tabledesign.h"
#include <QCombobox>
#include <QCheckBox>

tableDesign::tableDesign(QString tableName, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tableDesign)
    , tableName(tableName)
{
    ui->setupUi(this);
    ui->tabWidget->setStyleSheet("QTabWidget::pane { margin: 0px; border: 0px; }");

    addRow(0);
}

tableDesign::~tableDesign()
{
    delete ui;
}

void tableDesign::on_deleteField_released()
{
    int row;
    if(ui->tabWidget->currentIndex() == 0){
        if(ui->tableWidget->currentRow() == -1)
            row = ui->tableWidget->rowCount()-1;
        else
            row = ui->tableWidget->currentRow();
        ui->tableWidget->removeRow(row);
    }else {
        if(ui->tableWidget_2->currentRow() == -1)
            row = ui->tableWidget_2->rowCount()-1;
        else
            row = ui->tableWidget_2->currentRow();
        ui->tableWidget_2->removeRow(row);
    }
}

void tableDesign::setupCheckBoxInTableCell( int row, int column) {
    QCheckBox* checkBox = new QCheckBox();
    QWidget* container = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(container);
    layout->addWidget(checkBox);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    ui->tableWidget->setCellWidget(row, column, container);

}

bool tableDesign::getCheckBoxFromCell(int row, int column) {
    QWidget* cellWidget = ui->tableWidget->cellWidget(row, column);
    if (cellWidget) {
        return cellWidget->findChild<QCheckBox*>()->isChecked();
    }
    return false;
}

void tableDesign::on_addField_released()
{
    int row;
    if(ui->tabWidget->currentIndex() == 0){
        if(ui->tableWidget->currentRow() == -1)
            row = ui->tableWidget->rowCount();
        else
            row = ui->tableWidget->currentRow()+1;
        addRow(row);
    }else {
        if(ui->tableWidget_2->currentRow() == -1)
            row = ui->tableWidget_2->rowCount();
        else
            row = ui->tableWidget_2->currentRow()+1;
        ui->tableWidget_2->insertRow(row);
    }
}


void tableDesign::on_comfirm_released()
{
    QString sql = "CREATE TABLE "+tableName + " ( ";
    QStringList primary;
    for(int i= 0; i<ui->tableWidget->rowCount(); ++i){
        if(i != 0){
            sql += " , ";
        }
        if(ui->tableWidget->item(i,0))
            sql += ui->tableWidget->item(i,0)->text();

        QComboBox * combo = qobject_cast<QComboBox*>(ui->tableWidget->cellWidget(i, 1));
        QString type = combo->currentText();
        sql += (" "+type);

        if((type == "VARCHAR" || type == "CHAR")&& ui->tableWidget->item(i,2))
            sql += "("+ui->tableWidget->item(i,2)->text()+") ";
        if(type == "DECIMAL" && ui->tableWidget->item(i,2) && ui->tableWidget->item(i,3))
            sql += "("+ui->tableWidget->item(i,2)->text()+","+ui->tableWidget->item(i,3)->text()+") ";

        if(getCheckBoxFromCell(i,4))
            sql += " NOT NULL ";
        if(getCheckBoxFromCell(i,5))
            primary.append(ui->tableWidget->item(i,0)->text());
        if(getCheckBoxFromCell(i,6))
            sql += " UNIQUE ";
        if(ui->tableWidget->item(i,7)){
            sql += " DEFUALT ";
            sql += ui->tableWidget->item(i,7)->text();
        }
        if(ui->tableWidget->item(i,8)){
            sql += (" CHECK ( "+ui->tableWidget->item(i,8)->text()+" )");
        }
    }
    sql += " , primary key(";
    int i =1;
    for(QString prim :primary){

        if(i == 1){
            sql += prim;
        }else
            sql += (","+prim);
        ++i;
    }
    sql += ")";
    for(int row = 0; row<ui->tableWidget_2->rowCount(); ++row){
        QTableWidget* tablewidget = ui->tableWidget_2;
        if(tablewidget->item(row,0)&&tablewidget->item(row,1)&&tablewidget->item(row,2)){
            sql += " ,FOREIGN KEY(";
            sql += tablewidget->item(row,0)->text();
            sql += (") REFERENCES "+ tablewidget->item(row,1)->text()+"("+tablewidget->item(row,2)->text()+")");
        }
    }
    sql += ");";

    qDebug()<<sql;
    emit tableCreate(sql);
}

void tableDesign::addRow(int row){
    ui->tableWidget->insertRow(row);
    QComboBox *comboBox = new QComboBox();
    comboBox->addItems(typelist);
    ui->tableWidget->setCellWidget(row,1,comboBox);

    setupCheckBoxInTableCell(row,4);
    setupCheckBoxInTableCell(row,5);
    setupCheckBoxInTableCell(row,6);

}
