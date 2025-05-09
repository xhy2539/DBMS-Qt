#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "sqlparser.h"
#include "xhydbmanager.h"
#include "ConditionNode.h"
#include "userfilemanager.h"
#include <QVariant>
#include <QStringView>

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
    bool isColumnDefinition(const QString& def);//是否是字段
    void parseAndAddField(const QString& fieldStr, xhytable& table);//添加字段
    QStringList smartSplit(const QString& input);

    QStringList parseSqlValues(const QString &input);
    xhyfield::datatype parseDataType(const QString& type_str, int* size = nullptr);

    QStringList parseConstraints(const QString& constraints_str_input); // 参数名修改
    QPair<int, QString> findLowestPrecedenceOperator(const QString &expr, const QStringList &operatorsInPrecedenceOrder);
    void handleTableConstraint(const QString& constraint_str, xhytable& table);
    void flattenConditionTree(const ConditionNode &node, ConditionNode &output);

private slots:
    void on_run_clicked();

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
};
#endif // MAINWINDOW_H
