#include "querywidget.h"
#include "ui_querywidget.h"

queryWidget::queryWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::queryWidget)
{
    ui->setupUi(this);
    connect(ui->run,&QPushButton::released,[=]{
        emit sendString(ui->putin->toPlainText());
    });
}

queryWidget::~queryWidget()
{
    delete ui;
}

void queryWidget::clear(){
    ui->show->clear();
}

void queryWidget::setPlainText(const QString& text){
    ui->show->setPlainText(text);

}

void queryWidget::appendPlainText(const QString& text){
    ui->show->appendPlainText(text);
}

void queryWidget::putin_focus(){
    ui->putin->setFocus();
}
