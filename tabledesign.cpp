#include "tabledesign.h"
#include "ui_tabledesign.h"

tableDesign::tableDesign(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::tableDesign)
{
    ui->setupUi(this);
    ui->tabWidget->setStyleSheet("QTabWidget::pane { margin: 0px; border: 0px; }");
}

tableDesign::~tableDesign()
{
    delete ui;
}
