#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidgetItem>
#include <QFile>
#include <QDir>
#include "popupwidget.h"
#include "tablelist.h"
#include "viewlist.h"
#include "functionlist.h"
#include "querylist.h"

struct Database{
    QString database;
    QStringList tables;
    QStringList views;
    QStringList functions;
    QStringList queries;
};

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:

    void on_addQuery_released();

    void on_tableButton_released();

    void on_viewButton_released();

    void on_functionButton_released();

    void on_queryButton_released();

    void on_tabWidget_tabCloseRequested(int index);

private:
    Ui::MainWindow *ui;
    popupWidget *popup;
    QList<Database> dbms;
    QString currentDb = nullptr;

    QTabBar *tabBar;
    tableList *tablelist;
    viewList *viewlist;
    functionList *functionlist;
    queryList *querylist;

    void dataSearch();
    void buildTree();
    void updateList(QString currentDb);
    void handleItemClicked(QTreeWidgetItem *item, int column);
    QString m_dataDir = QDir::currentPath() + QDir::separator() + "DBMS_ROOT";
};
#endif // MAINWINDOW_H
