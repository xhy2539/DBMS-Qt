#include "querywidget.h"
#include "ui_querywidget.h"

queryWidget::queryWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::queryWidget)
{
    ui->setupUi(this);
}

queryWidget::~queryWidget()
{
    delete ui;
}
