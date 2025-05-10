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
