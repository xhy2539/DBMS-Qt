#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "sqlparser.h"
#include "xhydbmanager.h"
#include "ConditionNode.h"
#include "userfilemanager.h"
#include <QVariant>
#include <QStringView>
#include <QTreeWidgetItem>
#include <QFile>
#include <QDir>
#include "querywidget.h"
#include "popupwidget.h"
#include "tablelist.h"
#include "viewlist.h"
#include "functionlist.h"
#include "querylist.h"
#include "tableshow.h"
#include "userfilemanager.h"

struct Database{
    QString database;
    QStringList tables;
    QStringList views;
    QStringList functions;
    QStringList queries;
};

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &name, QWidget *parent = nullptr);
    ~MainWindow();

    bool parseWhereClause(const QString &whereStr, ConditionNode &rootNode);

    void handleCreateDatabase(const QString &command);
    void handleUseDatabase(const QString &command);
    void handleDropDatabase(const QString &command);
    void handleCreateTable( QString &command); // 注意：如果这里确实需要修改command，则不能是const
    void handleDropTable(const QString &command);
    void handleDescribe(const QString &command);
    void handleInsert(const QString &command);
    void handleUpdate(const QString &command);
    void handleDelete(const QString &command);
    void handleSelect(const QString &command);
    void handleAlterTable(const QString &command);
    void handleExplainSelect(const QString& command);
    void handleCreateIndex(const QString& command);
    void handleDropIndex(const QString& command);
    void handleShowIndexes(const QString& command);

    QStringList parseSqlValues(const QString &input);
    xhyfield::datatype parseDataType(const QString& type_str, int* size = nullptr);
    QStringList parseConstraints(const QString& constraints_str_input); // 参数名修改
    void handleTableConstraint(const QString &constraint_str, xhytable &table);

    QPair<int, QString> findLowestPrecedenceOperator(const QString &expr, const QStringList &operatorsInPrecedenceOrder);
private slots:
    // void on_run_clicked();
    void on_addQuery_released();

    void on_tableButton_released();

    void on_viewButton_released();

    void on_functionButton_released();

    void on_queryButton_released();

    void on_tabWidget_tabCloseRequested(int index);

    void openRegisterUserDialog(); // 添加

private:
    void execute_command(const QString& command);

    void show_databases();
    void show_tables(const QString& db_name);
    void show_schema(const QString& db_name, const QString& table_name);

    //获取数据库权限
    int getDatabaseRole(QString dbname);

    Ui::MainWindow *ui;
    xhydbmanager db_manager;
    SQLParser sqlParser;
    QString current_db;
    QString findDataFile();
    QString username;
    UserFileManager Account;

    QVariant parseLiteralValue(const QString& valueStr);
    int findBalancedOperatorPos(const QString& text, const QStringList& operatorsToFind, int startPos = 0);
    ConditionNode parseSubExpression(QStringView expressionView);
    ComparisonDetails parseComparisonDetails(const QString& field, const QString& op, const QString& valuePart);
    //check 条件括号匹配
    bool validateCheckExpression(const QString& expression);

    //GUI
    popupWidget *popup;
    QList<Database> GUI_dbms;
    QString current_GUI_Db = nullptr;

    QStringList textBuffer;

    QTabBar *tabBar;
    queryWidget *current_query;
    tableList *tablelist;
    viewList *viewlist;
    functionList *functionlist;
    queryList *querylist;

    void dataSearch();
    void buildTree();
    void updateList(QString currentDb);
    void handleItemClicked(QTreeWidgetItem *item, int column);
    void handleItemDoubleClicked(QTreeWidgetItem *item, int column);
    void openTable(QString tableName);
    void handleString(const QString& text, queryWidget* querywidget);
    xhyfield::datatype parseDataTypeAndParams(
        const QString& type_str_input,
        QStringList& auto_generated_constraints,
        QString& out_error_message
        );
    bool extractParenthesizedParams(
        const QString& params_str_with_parens,
        int& p_val,
        int& s_val,
        bool& s_specified
        );

    // 由zyh新增，用于聚组函数
    QVariant calculateAggregate(const QString& function, const QString& column, const QVector<xhyrecord>& records);
    bool isNumeric(const QString& str) const;

    // 由zyh新增，用于解决函数里无法使用表别名的情况
    QString removeTableAlias(const QString& col, const QString& table_alias, const QString& table_name);

    //用户数据库对应权限
    QVector<UserDatabaseInfo> userDatabaseInfo;
};
#endif // MAINWINDOW_H
