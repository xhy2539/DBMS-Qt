#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QUiLoader>
#include <QFile>
#include <QWidget>
#include <QDebug>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->pushButton->setEnabled(false);
}

MainWindow::~MainWindow()
{
    delete ui;
}

int pageNm=1;
// connect()

void MainWindow::on_queryButton_released()
{
    QUiLoader loader;
    QFile file("C:/Users/T/Desktop/DBMS-Qt/querywidget.ui");
    QWidget *querywidget = loader.load(&file);
    ui->stackedWidget->addWidget(querywidget);
    ui->stackedWidget->setCurrentWidget(querywidget);

}

