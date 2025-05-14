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
    // 新增的辅助函数，用于从合并记录中获取字段的原始类型
    xhyfield::datatype getFieldTypeFromJoinedRecord(
        const QString& columnNameOrAlias, // WHERE 或 ORDER BY 中使用的列名/别名
        const xhyrecord& joinedRecord,    // 合并后的记录
        xhytable* table1,                 // 指向表1的指针
        const QString& table1DisplayName, // 表1的显示名称 (可能是别名)
        xhytable* table2,                 // 指向表2的指针
        const QString& table2DisplayName  // 表2的显示名称 (可能是别名)
        );

    // 新增的条件匹配函数，专门用于 JOIN 后的记录
    bool matchJoinedRecordConditions(
        const xhyrecord& joinedRecord,    // 合并后的记录
        const ConditionNode& condition,   // 条件节点
        xhytable* table1,                 // 指向表1的指针 (用于类型转换和比较方法)
        const QString& table1DisplayName, // 表1的显示名称
        xhytable* table2,                 // 指向表2的指针
        const QString& table2DisplayName  // 表2的显示名称
        );
    QPair<int, QString> findLowestPrecedenceOperator(const QString &expr, const QStringList &operatorsInPrecedenceOrder);

    QPair<QString, QString> parseQualifiedColumn(const QString &qualifiedName);
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
    QString cleanIdentifier(QString id) const; // 如果已添加，确保为 const

    QPair<QString, QString> parseQualifiedColumn(const QString& qualifiedName) const; // 添加 const


    QString sqlLikeToRegex(const QString& likePattern, QChar customEscapeChar = QChar::Null) const; // 保持 const

    // 新增 visualLength 的声明
    int visualLength(const QString& str) const;

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
};
#endif // MAINWINDOW_H
