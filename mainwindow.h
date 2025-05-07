#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "sqlparser.h"
#include "xhydbmanager.h"
#include "ConditionNode.h"
#include "userfilemanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
using ConditionTree = QMap<QString, QString>;
QT_END_NAMESPACE

/**
 * @brief 主界面控制类
 *
 * 处理用户交互和命令执行流程
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    //修改构造函数传入用户名（用于查看权限）
    explicit MainWindow(const QString &name, QWidget *parent = nullptr);
    ~MainWindow();

    xhydatabase *findDatabase(const QString &name);
    bool parseWhereClause(const QString &whereStr, QMap<QString, QString> &conditions);
    bool parseWhereClause(const QString &whereStr, ConditionNode &root);


    void handleCreateDatabase(const QString &command);
    void handleUseDatabase(const QString &command);
    void handleDropDatabase(const QString &command);
    void handleCreateTable( QString &command);
    void handleDropTable(const QString &command);
    void handleDescribe(const QString &command);
    void handleInsert(const QString &command);
    void handleUpdate(const QString &command);
    void handleDelete(const QString &command);
    void handleSelect(const QString &command);

    QStringList parseSqlValues(const QString &input);
    void handleAlterTable(const QString &command);
    xhyfield::datatype parseDataType(const QString& type_str, int* size = nullptr);
    QStringList parseConstraints(const QString& constraints);
    void handleTableConstraint(const QString &constraint_str, xhytable &table);
    void flattenConditionTree(const ConditionNode &node, ConditionNode &output);
private slots:
    /// 处理Run按钮点击事件
    void on_run_clicked();

private:
    /// 执行用户输入的命令
    void execute_command( QString& command);

    /// 显示所有数据库列表
    void show_databases();

    /// 显示指定数据库的表列表
    void show_tables(const QString& db_name);

    /// 显示表结构
    void show_schema(const QString& db_name, const QString& table_name);


    Ui::MainWindow *ui;            ///< UI界面指针
    xhydbmanager db_manager;       ///< 数据库管理器实例
    SQLParser sqlParser;
    QString current_db;            ///< 当前选中的数据库名称
    QString findDataFile(); //查找账号信息地址
    QString username; //用户名用于检查权限
    UserFileManager Account; //账号管理
};
#endif // MAINWINDOW_H
