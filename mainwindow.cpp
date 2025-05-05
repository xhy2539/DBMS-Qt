#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "querywidget.h"

#include <QWidget>
#include <QDebug>



MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->linkButton->setEnabled(false);
    ui->tableButton->setEnabled(false);

    ui->tabWidget->setStyleSheet("QTabWidget::pane { margin: 0px; border: 0px; }");
    tablelist = new tableList();
    viewlist = new viewList();
    functionlist = new functionList();
    querylist = new queryList();
    ui->tabWidget->addTab(tablelist,"对象");

    tabBar = ui->tabWidget->tabBar();

    // 隐藏第一个标签页的关闭按钮（索引为 0）
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }

    dataSearch();
    buildTree();
    // popup = new popupWidget(ui->tabWidget);

    // QHBoxLayout *layout = new QHBoxLayout(ui->widget);
    // QListWidget *list = new QListWidget(ui->widget);
    // layout->addWidget(list);
    // ui->widget->setFixedHeight(ui->tabWidget->height()*2);
    // ui->widget->raise();
    // popup->showPopup();
    connect(ui->treeWidget, &QTreeWidget::itemClicked, this, &MainWindow::handleItemClicked);

    // QObject::connect(ui->treeWidget, &QTreeWidget::itemClicked, [=](QTreeWidgetItem *item, int column) {
    //     QString text = item->text(column); // 获取被点击项的文本
    //     qDebug() << "Clicked item:" << text << "at column" << column;
    // });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::dataSearch(){
    dbms = {
        {
            "数据库1",
            {"student", "teacher"},  // 表
            {"view1", "view2"},      // 视图
            {"func1"},              // 函数
            {"query1", "query2"}    // 查询
        },
        {
            "数据库2",
            {"order", "product"},    // 表
            {"sales_view"},         // 视图
            {"calculate_total"},    // 函数
            {}      // 查询
        }
    };
}

void MainWindow::buildTree(){
    ui->treeWidget->clear();

    for (const Database &db : dbms) {
        // 创建数据库节点（作为根的子节点）
        QTreeWidgetItem *dbItem = new QTreeWidgetItem(ui->treeWidget);
        dbItem->setText(0, db.database);

        // 创建固定子节点：表、视图、函数、查询
        QTreeWidgetItem *tablesItem = new QTreeWidgetItem(dbItem);
        tablesItem->setText(0, "表");

        QTreeWidgetItem *viewsItem = new QTreeWidgetItem(dbItem);
        viewsItem->setText(0, "视图");

        QTreeWidgetItem *functionsItem = new QTreeWidgetItem(dbItem);
        functionsItem->setText(0, "函数");

        QTreeWidgetItem *queriesItem = new QTreeWidgetItem(dbItem);
        queriesItem->setText(0, "查询");
        // 填充表
            for (const QString &table : db.tables) {
            QTreeWidgetItem *tableItem = new QTreeWidgetItem(tablesItem);
            tableItem->setText(0, table);
        }

        // 填充视图
        for (const QString &view : db.views) {
            QTreeWidgetItem *viewItem = new QTreeWidgetItem(viewsItem);
            viewItem->setText(0, view);
        }

        // 填充函数
        for (const QString &func : db.functions) {
            QTreeWidgetItem *funcItem = new QTreeWidgetItem(functionsItem);
            funcItem->setText(0, func);
        }

        // 填充查询
        for (const QString &query : db.queries) {
            QTreeWidgetItem *queryItem = new QTreeWidgetItem(queriesItem);
            queryItem->setText(0, query);
        }
    }

}

void MainWindow::updateList(QString currentDb){

    tablelist->clear();
    viewlist->clear();
    functionlist->clear();
    querylist->clear();

    for (const Database &db : dbms) {

        if(db.database == currentDb){
            // 填充表
            tablelist->addItems(db.tables);

            // 填充视图
            viewlist->addItems(db.views);

            // 填充函数
            functionlist->addItems(db.functions);

            // 填充查询
            querylist->addItems(db.queries);
        }
    }
}

void MainWindow::handleItemClicked(QTreeWidgetItem *item, int column)
{
    if (item) {
        QString text = item->text(column);
        qDebug() << "Clicked item: " << text;
    }
    if(item->parent() != nullptr){
        if(item->text(0) == "表" || item->parent()->text(0) == "表"){
            if(ui->tableButton->isEnabled()){
                MainWindow::on_tableButton_released();
            }

        }else if(item->text(0) == "视图" || item->parent()->text(0) == "视图"){
            if(ui->viewButton->isEnabled()){
                MainWindow::on_viewButton_released();
            }

        }else if(item->text(0) == "函数" || item->parent()->text(0) == "函数"){
            if(ui->functionButton->isEnabled()){
                MainWindow::on_functionButton_released();
            }

        }else if(item->text(0) == "查询" || item->parent()->text(0) == "查询"){
            if(ui->queryButton->isEnabled()){
                MainWindow::on_queryButton_released();
            }
        }
    }
    while(item->parent() != nullptr){
        item=item->parent();
    }
    if(currentDb == nullptr || item->text(0) != currentDb){
        currentDb = item->text(0);
        qDebug() << "数据库改变";

        updateList(currentDb);
    }
}

void MainWindow::on_addQuery_released()
{
    ui->tabWidget->addTab(new queryWidget,"新建查询");
}

void MainWindow::on_tableButton_released()
{
    ui->tableButton->setEnabled(false);
    ui->viewButton->setEnabled(true);
    ui->functionButton->setEnabled(true);
    ui->queryButton->setEnabled(true);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,tablelist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}

void MainWindow::on_viewButton_released()
{
    ui->tableButton->setEnabled(true);
    ui->viewButton->setEnabled(false);
    ui->functionButton->setEnabled(true);
    ui->queryButton->setEnabled(true);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,viewlist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}

void MainWindow::on_functionButton_released()
{
    ui->tableButton->setEnabled(true);
    ui->viewButton->setEnabled(true);
    ui->functionButton->setEnabled(false);
    ui->queryButton->setEnabled(true);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,functionlist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}

void MainWindow::on_queryButton_released()
{
    ui->tableButton->setEnabled(true);
    ui->viewButton->setEnabled(true);
    ui->functionButton->setEnabled(true);
    ui->queryButton->setEnabled(false);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,querylist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}


void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    ui->tabWidget->removeTab(index);
}

