#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "xhyfield.h"
#include "xhyrecord.h"
#include "xhytable.h"
#include "xhydatabase.h"
#include "xhyindex.h"
#include "ConditionNode.h" // 确保这个 include 存在
#include "createuserdialog.h" // <-- 如果要打开注册用户对话框，需要包含此头文件
#include <QMessageBox>
#include <QRegularExpression>
#include <QDebug>
#include <QStack>
#include <QStringView>
#include <QVariant>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <stdexcept> // 包含 stdexcept
#include <limits>    // 用于 std::numeric_limits
#include <QRegularExpressionMatchIterator>
#include <QMenu>          // <-- 添加这一行
#include <QMenuBar>       // <-- 添加这一行
#include <QAction>        // <-- 添加这一行



MainWindow::MainWindow(const QString &name,QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , Account(findDataFile())
    , username(name)
{
    ui->setupUi(this);
    setWindowTitle("Mini DBMS");
    if (!Account.loadUsers()) {
        qWarning() << "警告：用户数据加载/初始化失败。";
    }
    db_manager.load_databases_from_files();

    //菜单栏（注册账号）

    // 获取或创建主窗口的菜单栏
    QMenuBar *menuBar = this->menuBar(); // QMainWindow::menuBar() 会返回菜单栏指针，如果没有则会自动创建

    // 1. 添加“帮助”菜单及其子项
    QMenu *helpMenu = menuBar->addMenu("帮助(&H)"); // "&H" 是快捷键提示，按Alt+H可以快速打开
    QAction *aboutAction = new QAction("相关(&A)...", this); // <-- 修正后的代码
    helpMenu->addAction(aboutAction);

    // 连接“相关”菜单项的触发信号到显示“关于”对话框的槽函数
    connect(aboutAction, &QAction::triggered, this, [this](){
        QMessageBox::about(this, "关于 Mini DBMS", "Mini DBMS 是一个迷你数据库管理系统。\n\n版本: 1.0\。");
    });

    // 2. 添加“账户”菜单及其子项
    QMenu *accountMenu = menuBar->addMenu("账户(&C)"); // "&C" 是快捷键提示
    QAction *registerAccountAction = new QAction("注册账户(&R)...", this); // <-- 修正后的代码
    accountMenu->addAction(registerAccountAction);

    if (Account.getUserRole(username) < 2) { // 只有权限等于2（管理员）的用户才能看到“注册账户”
        registerAccountAction->setVisible(false); // 隐藏该菜单项
    }

    // 连接“注册账户”菜单项的触发信号到打开注册用户对话框的槽函数
    connect(registerAccountAction, &QAction::triggered, this, &MainWindow::openRegisterUserDialog);

    // ====================== 结束添加菜单栏代码 ======================


    //GUI
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
    connect(ui->treeWidget, &QTreeWidget::itemDoubleClicked, this, &MainWindow::handleItemDoubleClicked);
    connect(tablelist,&tableList::tableOpen,this ,&MainWindow::openTable);
}

MainWindow::~MainWindow()
{
    delete ui;
}

QString MainWindow::findDataFile() {
    QStringList possiblePaths = {
        "data/default_userdata.dat",
        "../data/default_userdata.dat",
        "../../data/default_userdata.dat",
        "../../../data/default_userdata.dat",
        QCoreApplication::applicationDirPath() + "/data/default_userdata.dat"
    };

    for (const QString &path : possiblePaths) {
        QFileInfo fileInfo(path);
        if (fileInfo.exists() && fileInfo.isFile()) {
            qDebug() << "Found user data file at:" << fileInfo.absoluteFilePath();
            return fileInfo.absoluteFilePath();
        }
    }
    qWarning() << "User data file not found in candidate paths. Will use/create at default AppDataLocation.";
    QDir appDataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!appDataDir.exists("DBMSData")) {
        appDataDir.mkpath("DBMSData");
    }
    QString defaultPath = appDataDir.filePath("DBMSData/default_userdata.dat");
    qDebug() << "Using default user data file path:" << defaultPath;
    return defaultPath;
}

void MainWindow::execute_command(const QString& command)
{
    if (command.isEmpty()) return;

    try {
        QString cmdUpper = command.toUpper();

        if (cmdUpper.startsWith("EXPLAIN SELECT")) {
            handleExplainSelect(command);
        } else if (cmdUpper.startsWith("CREATE UNIQUE INDEX")) {
            handleCreateIndex(command);
        } else if (cmdUpper.startsWith("CREATE INDEX")) {
            handleCreateIndex(command);
        } else if (cmdUpper.startsWith("DROP INDEX")) {
            handleDropIndex(command);
        } else if (cmdUpper.startsWith("SHOW INDEXES")) {
            handleShowIndexes(command);
        }else if (cmdUpper.startsWith("CREATE DATABASE")) {
            handleCreateDatabase(command);
        }
        else if (cmdUpper.startsWith("SHOW DATABASES")) {
            show_databases();
        }
        else if (cmdUpper.startsWith("USE")) {
            handleUseDatabase(command);
        }
        else if (cmdUpper.startsWith("CREATE TABLE")) {
            QString mutableCommand = command;
            handleCreateTable(mutableCommand);
        }
        else if (cmdUpper.startsWith("INSERT INTO")) {
            handleInsert(command);
        }
        else if (cmdUpper.startsWith("SHOW TABLES")) {
            show_tables(db_manager.get_current_database());
        }
        else if (cmdUpper.startsWith("DESCRIBE ") || cmdUpper.startsWith("DESC ")) {
            handleDescribe(command);
        }
        else if (cmdUpper.startsWith("DROP DATABASE")) {
            handleDropDatabase(command);
        }
        else if (cmdUpper.startsWith("DROP TABLE")) {
            handleDropTable(command);
        }
        else if (cmdUpper.startsWith("UPDATE")) {
            handleUpdate(command);
        }
        else if (cmdUpper.startsWith("DELETE FROM")) {
            handleDelete(command);
        }
        else if (cmdUpper.startsWith("SELECT")) {
            handleSelect(command);
        }
        else if (cmdUpper.startsWith("ALTER TABLE")) {
            handleAlterTable(command);
        }
        else if (cmdUpper.startsWith("BEGIN TRANSACTION") || cmdUpper.startsWith("BEGIN")) {
            if (db_manager.beginTransaction()) {
                textBuffer.append("事务开始。");
            } else {
                textBuffer.append("错误: 无法开始事务 (可能已在事务中或当前数据库不支持)。");
            }
        }
        else if (cmdUpper.startsWith("COMMIT")) {
            if (db_manager.commitTransaction()) {
                textBuffer.append("事务提交成功。");
            } else {
                textBuffer.append("错误: 事务提交失败 (可能不在事务中或没有更改)。");
            }
        }
        else if (cmdUpper.startsWith("ROLLBACK")) {
            db_manager.rollbackTransaction();
            textBuffer.append("事务已回滚 (如果存在活动事务)。");
        }
        else {
            textBuffer.append("无法识别的命令: " + command);
        }
    } catch (const std::runtime_error& e) {
        QString errMsg = "运行时错误: " + QString::fromStdString(e.what());
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
            errMsg += " (事务已回滚)";
        }
        textBuffer.append(errMsg);
    } catch (...) {
        QString errMsg = "发生未知类型的严重错误。";
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
            errMsg += " (事务已回滚)";
        }
        textBuffer.append(errMsg);
    }
}

void MainWindow::handleCreateDatabase(const QString& command) {
    QRegularExpression re(R"(CREATE\s+DATABASE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.createdatabase(db_name)) {
            textBuffer.append(QString("数据库 '%1' 创建成功。").arg(db_name));
        } else {
            textBuffer.append(QString("错误: 创建数据库 '%1' 失败 (可能已存在或名称无效)。").arg(db_name));
        }
    } else {
        textBuffer.append("语法错误: CREATE DATABASE <数据库名>");
    }
}

void MainWindow::handleUseDatabase(const QString& command) {
    QRegularExpression re(R"(USE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.use_database(db_name)) {
            current_db = db_name;
            textBuffer.append(QString("已切换到数据库 '%1'。").arg(db_name));
        } else {
            textBuffer.append(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        }
    } else {
        textBuffer.append("语法错误: USE <数据库名>");
    }
}

void MainWindow::handleDropDatabase(const QString& command) {
    QRegularExpression re(R"(DROP\s+DATABASE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "确认删除",
                                                                 QString("确定要永久删除数据库 '%1' 及其所有数据吗? 此操作不可恢复!").arg(db_name),
                                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (db_manager.dropdatabase(db_name)) {
                textBuffer.append(QString("数据库 '%1' 已删除。").arg(db_name));
                if (current_db.compare(db_name, Qt::CaseInsensitive) == 0) {
                    current_db.clear();
                    textBuffer.append("注意：当前使用的数据库已被删除。");
                }
            } else {
                textBuffer.append(QString("错误: 删除数据库 '%1' 失败 (可能不存在或文件删除失败)。").arg(db_name));
            }
        } else {
            textBuffer.append("删除数据库操作已取消。");
        }
    } else {
        textBuffer.append("语法错误: DROP DATABASE <数据库名>");
    }
}


// Helper function definition: Extracts parameters like (L), (P), or (P,S)
bool MainWindow::extractParenthesizedParams(const QString& params_str_with_parens, int& p_val, int& s_val, bool& s_specified) {
    p_val = 0;
    s_val = 0;
    s_specified = false;

    QString trimmed_params = params_str_with_parens.trimmed();
    if (!trimmed_params.startsWith('(') || !trimmed_params.endsWith(')')) {
        return false; // 不是以括号包围
    }

    QString inner_params = trimmed_params.mid(1, trimmed_params.length() - 2).trimmed();
    if (inner_params.isEmpty()) {
        return false; // 例如 "()" 括号内为空
    }

    QStringList param_parts = inner_params.split(',', Qt::SkipEmptyParts);
    bool conv_ok_p = false;
    bool conv_ok_s = false;

    if (param_parts.size() >= 1) {
        QString p_str = param_parts[0].trimmed();
        if (!p_str.isEmpty()) {
            p_val = p_str.toInt(&conv_ok_p);
        } else {
            conv_ok_p = false; // 空的部分不是有效数字
        }
    }

    if (param_parts.size() >= 2) {
        QString s_str = param_parts[1].trimmed();
        if (!s_str.isEmpty()) {
            s_val = s_str.toInt(&conv_ok_s);
            if (conv_ok_s) {
                s_specified = true;
            }
        } else {
            conv_ok_s = false; // 空的部分不是有效数字 (对于S)
        }
    }
    return conv_ok_p; // 返回 P 是否成功解析为一个整数
}
// mainwindow.cpp

xhyfield::datatype MainWindow::parseDataTypeAndParams(
    const QString& type_str_input,
    QStringList& auto_generated_constraints,
    QString& out_error_message)
{
    auto_generated_constraints.clear();
    out_error_message.clear();

    QString type_def_str = type_str_input.trimmed();
    QString type_name_upper;
    QString params_in_paren_str; // 存储括号及其内部内容，如 "(10,2)" 或 "('A','B')"

    int paren_start = type_def_str.indexOf('(');
    if (paren_start != -1) {
        type_name_upper = type_def_str.left(paren_start).trimmed().toUpper();
        // 简单检查末尾是否有括号。对于 ENUM，其参数可能复杂，所以允许更宽松的括号内容。
        if (type_def_str.endsWith(')')) {
            params_in_paren_str = type_def_str.mid(paren_start); // 包括括号，例如 "(10,2)"
        } else {
            // 如果类型是 ENUM，允许这种情况，因为 ENUM 的值列表可能包含未正确匹配的括号（由后续 ENUM 值解析处理）
            // 对于其他需要参数的类型，如果括号不匹配，则 params_in_paren_str 将为空，后续逻辑会报错。
            if (type_name_upper != "ENUM") {
                out_error_message = QString("类型 '%1' 的定义中括号不匹配: '%2'").arg(type_name_upper, type_str_input);
                // 让后续逻辑基于空的 params_in_paren_str 来判断是否缺少必要参数
            }
            // params_in_paren_str 保持为空，或者可以尝试更复杂的括号匹配
        }
    } else {
        type_name_upper = type_def_str.toUpper();
    }

    if (type_name_upper.isEmpty()) {
        out_error_message = QString("类型定义为空: '%1'").arg(type_str_input);
        return xhyfield::VARCHAR; // 默认回退
    }

    int p_val = 0;
    int s_val = 0;
    bool s_is_specified = false; // s_val 是否在括号中被明确指定

    if (type_name_upper != "ENUM" && !params_in_paren_str.isEmpty()) {
        if (!extractParenthesizedParams(params_in_paren_str, p_val, s_val, s_is_specified)) {
            // extractParenthesizedParams 返回false意味着括号内不是有效的数字参数P
            // (或者是空的括号内容，例如 "TYPE()")
            if (type_name_upper == "VARCHAR" || type_name_upper == "CHAR" ||
                type_name_upper == "DECIMAL" || type_name_upper == "NUMERIC") {
                // 这些类型如果带有括号，则括号内应有有效参数
                out_error_message = QString("类型 '%1' 的参数 '%2' 格式无效或缺失.").arg(type_name_upper).arg(params_in_paren_str);
                // p_val 会是0 (或extractParenthesizedParams的初始值), 后续类型判断逻辑会处理
            }
        }
    }

    // 类型判断和约束生成
    if (type_name_upper == "INT" || type_name_upper == "INTEGER") return xhyfield::INT;
    if (type_name_upper == "TINYINT") return xhyfield::TINYINT;
    if (type_name_upper == "SMALLINT") return xhyfield::SMALLINT;
    if (type_name_upper == "BIGINT") return xhyfield::BIGINT;
    if (type_name_upper == "FLOAT") return xhyfield::FLOAT;
    if (type_name_upper == "DOUBLE" || type_name_upper == "REAL") return xhyfield::DOUBLE;

    if (type_name_upper == "DECIMAL" || type_name_upper == "NUMERIC") {
        // DECIMAL 必须有参数 P，S 可选（默认为0）
        if (params_in_paren_str.isEmpty() || p_val <= 0 || (s_is_specified && (s_val < 0 || s_val > p_val))) {
            out_error_message = QString("DECIMAL/NUMERIC 定义 '%1' 无效. 期望格式如 DECIMAL(P,S)，其中 P > 0 且 0 <= S <= P.")
                                    .arg(type_str_input);
            return xhyfield::VARCHAR; // 回退
        }
        auto_generated_constraints.append(QString("PRECISION(%1)").arg(p_val));
        auto_generated_constraints.append(QString("SCALE(%1)").arg(s_val)); // s_val 由 extract... 初始化为0（若未指定）
        return xhyfield::DECIMAL;
    }

    if (type_name_upper == "CHAR") {
        if (params_in_paren_str.isEmpty()) { // SQL 标准：CHAR 等同于 CHAR(1)
            p_val = 1;
        } else if (p_val <= 0) { // CHAR() 或 CHAR(0) 或 CHAR(负数) 是无效的
            out_error_message = QString("CHAR 类型在 '%1' 中的长度无效. 长度必须 > 0.").arg(type_str_input);
            return xhyfield::VARCHAR; // 回退
        }
        auto_generated_constraints.append(QString("SIZE(%1)").arg(p_val));
        return xhyfield::CHAR;
    }

    if (type_name_upper == "VARCHAR") {
        if (params_in_paren_str.isEmpty() || p_val <= 0) { // VARCHAR 必须指定长度 > 0
            out_error_message = QString("VARCHAR 类型在 '%1' 中的长度无效或未指定. 长度必须 > 0.").arg(type_str_input);
            return xhyfield::TEXT; // 或者您可以选择返回一个具有非常大默认长度的VARCHAR，或直接报错
        }
        auto_generated_constraints.append(QString("SIZE(%1)").arg(p_val));
        return xhyfield::VARCHAR;
    }

    if (type_name_upper == "TEXT") return xhyfield::TEXT;
    if (type_name_upper == "DATE") return xhyfield::DATE;
    if (type_name_upper == "DATETIME") return xhyfield::DATETIME;
    if (type_name_upper == "TIMESTAMP") return xhyfield::TIMESTAMP;
    if (type_name_upper == "BOOL" || type_name_upper == "BOOLEAN") return xhyfield::BOOL;

    if (type_name_upper == "ENUM") {
        if (params_in_paren_str.isEmpty() || !params_in_paren_str.startsWith('(') || !params_in_paren_str.endsWith(')')) {
            out_error_message = QString("ENUM 类型定义 '%1' 需要括号括起来的值列表，例如 ENUM('a','b').").arg(type_str_input);
            return xhyfield::VARCHAR; // 回退
        }
        // ENUM 值的实际解析（括号内的内容）由调用者 (handleCreateTable/handleAlterTable) 完成
        return xhyfield::ENUM;
    }

    out_error_message = QString("未知数据类型: '%1'.").arg(type_str_input);
    return xhyfield::VARCHAR; // 对未知类型的回退
}
void MainWindow::handleCreateTable(QString& command) { // 保持 QString& 以匹配您原有的函数签名
    QString processedCommand = command.replace(QRegularExpression(R"(\s+)"), " ").trimmed();
    QRegularExpression re(R"(CREATE\s+TABLE\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(processedCommand);

    if (!match.hasMatch()) {
        textBuffer.append("语法错误: CREATE TABLE <表名> (<列定义1> <类型1> [约束], ...)");
        return;
    }
    QString table_name = match.captured(1).trimmed();
    QString fields_and_constraints_str = match.captured(2).trimmed(); // 括号内的所有内容
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("错误: 未选择数据库。请先使用 'USE <数据库名>;' 命令。");
        return;
    }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (db_manager.beginTransaction()) {
            transactionStartedHere = true;
        } else {
            textBuffer.append("错误: 为 CREATE TABLE 操作启动事务失败。");
            return;
        }
    }

    xhytable new_table(table_name); // 为新表创建 xhytable 对象

    // 解析括号内的定义列表 (处理嵌套括号和字符串内的逗号)
    QStringList definitions;
    int parenLevel = 0;
    QString currentDef;
    bool inStringLiteral = false;
    QChar stringChar = ' ';

    // 字段和约束定义分割逻辑 (保持不变)
    for (QChar c : fields_and_constraints_str) {
        if (c == '\'' || c == '"') {
            if (inStringLiteral && c == stringChar) {
                // 检查是否是转义的引号
                if (!currentDef.isEmpty() && currentDef.endsWith('\\')) {
                    currentDef.append(c); // 保留转义的引号和前面的反斜杠
                } else {
                    inStringLiteral = false; // 字符串字面量结束
                    currentDef.append(c); // 包含结束引号
                }
            } else if (!inStringLiteral) {
                inStringLiteral = true; // 字符串字面量开始
                stringChar = c;
                currentDef.append(c); // 包含开始引号
            } else { // 在字符串字面量内部，但不是结束引号或转义引号
                currentDef.append(c);
            }
        } else if (c == '(' && !inStringLiteral) {
            parenLevel++;
            currentDef.append(c);
        } else if (c == ')' && !inStringLiteral) {
            parenLevel--;
            currentDef.append(c);
        } else if (c == ',' && parenLevel == 0 && !inStringLiteral) {
            // 顶层的逗号，表示一个定义项的结束
            definitions.append(currentDef.trimmed());
            currentDef.clear();
        } else {
            currentDef.append(c);
        }
    }
    if (!currentDef.isEmpty()) { // 添加最后一个定义项
        definitions.append(currentDef.trimmed());
    }

    if (definitions.isEmpty()) {
        textBuffer.append("错误: CREATE TABLE 语句中未找到列定义或约束。");
        if (transactionStartedHere) db_manager.rollbackTransaction();
        return;
    }


    for (const QString& def_str_const : definitions) {
        QString def_str = def_str_const.trimmed();
        if (def_str.isEmpty()) continue;


        QString def_str_upper = def_str.toUpper();
        bool isLikelyTableConstraint = false;

        // **【核心修改】** 判断此定义项是否为表级约束
        if (def_str_upper.startsWith("CONSTRAINT")) {
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("PRIMARY KEY")) {
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("UNIQUE") && def_str.contains('(')) {
            // 表级 UNIQUE 约束通常是 UNIQUE (col1, col2) 形式
            // 使用 contains('(') 初步判断，handleTableConstraint 会用正则精确匹配
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("FOREIGN KEY") && def_str.contains('(')) {
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("CHECK") && def_str.contains('(')) {
            // 表级 CHECK 约束通常是 CHECK (expression) 形式
            isLikelyTableConstraint = true;
        }

        if (isLikelyTableConstraint) {
            qDebug() << "[handleCreateTable] 识别为潜在的表级约束，交由 handleTableConstraint 处理:" << def_str;
            handleTableConstraint(def_str, new_table); // handleTableConstraint 内部有更详细的正则匹配
        } else {
            // 如果不是明确的表级约束开头，则作为列定义处理
            qDebug() << "[handleCreateTable] 识别为列定义:" << def_str;

            // === 以下是您原有的、用于解析单个列定义的完整代码块 ===

            QString working_def_str = def_str.trimmed();
            QString field_name, type_str_full, col_constraints_str;

            int first_space_idx = working_def_str.indexOf(QRegularExpression(R"(\s)"));
            if (first_space_idx == -1) {
                textBuffer.append("错误: 列定义不完整 (缺少类型): '" + def_str + "'.");
                if (transactionStartedHere) db_manager.rollbackTransaction(); return;
            }
            field_name = working_def_str.left(first_space_idx);
            working_def_str = working_def_str.mid(first_space_idx).trimmed();

            // (以下是您原代码中解析 type_str_full 和 col_constraints_str 的复杂逻辑)
            int type_param_paren_open_idx = working_def_str.indexOf('(');
            int first_space_after_potential_type_name = working_def_str.indexOf(QRegularExpression(R"(\s)"));

            if (type_param_paren_open_idx != -1 &&
                (first_space_after_potential_type_name == -1 || type_param_paren_open_idx < first_space_after_potential_type_name)) {
                int balance = 0; int end_of_type_params_idx = -1; bool in_str_lit_param = false; QChar str_char_param = ' ';
                for (int i = type_param_paren_open_idx; i < working_def_str.length(); ++i) {
                    QChar c_loop = working_def_str[i]; // 避免与外层循环的 c 变量冲突
                    if (c_loop == '\'' || c_loop == '"') {
                        if (in_str_lit_param && c_loop == str_char_param) {
                            if (i > 0 && working_def_str[i-1] == '\\') { /* escaped quote */ }
                            else { in_str_lit_param = false; }
                        } else if (!in_str_lit_param) {
                            in_str_lit_param = true; str_char_param = c_loop;
                        }
                    }
                    else if (c_loop == '(' && !in_str_lit_param) balance++;
                    else if (c_loop == ')' && !in_str_lit_param) {
                        balance--;
                        if (balance == 0 && i >= type_param_paren_open_idx) {
                            end_of_type_params_idx = i;
                            break;
                        }
                    }
                }
                if (end_of_type_params_idx != -1) {
                    type_str_full = working_def_str.left(end_of_type_params_idx + 1);
                    col_constraints_str = working_def_str.mid(end_of_type_params_idx + 1).trimmed();
                } else {
                    textBuffer.append(QString("错误: 字段 '%1' 的类型定义中括号不匹配: '%2'").arg(field_name, working_def_str));
                    if (transactionStartedHere) db_manager.rollbackTransaction(); return;
                }
            } else {
                if (first_space_after_potential_type_name != -1) {
                    type_str_full = working_def_str.left(first_space_after_potential_type_name).trimmed();
                    col_constraints_str = working_def_str.mid(first_space_after_potential_type_name).trimmed();
                } else {
                    type_str_full = working_def_str.trimmed(); col_constraints_str = "";
                }
            }

            QStringList auto_generated_type_constraints; QString type_parse_error;
            xhyfield::datatype type = parseDataTypeAndParams(type_str_full, auto_generated_type_constraints, type_parse_error);

            if (!type_parse_error.isEmpty()) {
                textBuffer.append(QString("错误：字段 '%1'：类型 '%2' 解析失败 - %3").arg(field_name, type_str_full, type_parse_error));
                if (transactionStartedHere) db_manager.rollbackTransaction(); return;
            }

            QStringList user_defined_col_constraints = parseConstraints(col_constraints_str); // 解析列级约束字符串
            QStringList final_constraints = auto_generated_type_constraints;
            final_constraints.append(user_defined_col_constraints);
            final_constraints.removeDuplicates();

            qDebug() << "[handleCreateTable] 对于字段 '" << field_name << "', 解析的类型字符串: '" << type_str_full << "'";
            qDebug() << "  自动生成的类型约束:" << auto_generated_type_constraints;
            qDebug() << "  用户定义的列约束:" << user_defined_col_constraints;
            qDebug() << "  最终用于 xhyfield 的约束:" << final_constraints;

            if (type == xhyfield::ENUM) {
                QRegularExpression enum_values_re(R"(ENUM\s*\((.+)\)\s*$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                QRegularExpressionMatch enum_match = enum_values_re.match(type_str_full);
                if (enum_match.hasMatch()) {
                    QString enum_content = enum_match.captured(1).trimmed();
                    QStringList enum_vals_list = parseSqlValues(enum_content); // 解析 ENUM 值
                    if (enum_vals_list.isEmpty() && !enum_content.isEmpty() && enum_content != "''" && enum_content != "\"\"") {
                        textBuffer.append(QString("错误: ENUM 字段 '%1' 的值列表为空或格式错误: %2").arg(field_name, enum_content));
                        if(transactionStartedHere) db_manager.rollbackTransaction(); return;
                    }
                    xhyfield new_field_enum(field_name, type, final_constraints);
                    new_field_enum.set_enum_values(enum_vals_list);
                    new_table.addfield(new_field_enum);
                } else {
                    textBuffer.append(QString("错误: ENUM 字段 '%1' 定义无效。期望格式 ENUM('val1',...). 得到: '%2'").arg(field_name, type_str_full));
                    if (transactionStartedHere) db_manager.rollbackTransaction(); return;
                }
            } else {
                xhyfield new_field(field_name, type, final_constraints);
                new_table.addfield(new_field);
            }
            // === 列定义解析逻辑结束 ===
        }
    } // 结束 for 循环 (遍历 definitions)

    // 检查表是否至少有一个字段（除非它仅包含表级约束，但这种情况比较少见且可能需要进一步逻辑判断）
    if (new_table.fields().isEmpty()) {
        // 进一步检查是否真的没有任何有效的定义（例如，只有错误的表级约束定义）
        // 一个更严格的检查是：如果 definitions 列表不为空，但 new_table.fields() 为空，
        // 并且没有成功添加任何表级约束（这需要 handleTableConstraint 返回状态或 new_table 有方法查询），
        // 则可能意味着所有定义都有问题。
        // 为简单起见，如果字段列表为空，就报错（除非您的设计允许无字段的表）。
        bool hasOnlyTableConstraints = true;
        for(const QString& def : definitions) {
            if (!def.toUpper().startsWith("CONSTRAINT") && !def.toUpper().startsWith("PRIMARY KEY") &&
                !def.toUpper().startsWith("UNIQUE") && !def.toUpper().startsWith("FOREIGN KEY") &&
                !def.toUpper().startsWith("CHECK")) {
                hasOnlyTableConstraints = false;
                break;
            }
        }
        if (!hasOnlyTableConstraints) { // 如果定义中包含非表约束的内容但字段仍为空，说明列定义失败
            textBuffer.append("错误: 表必须至少包含一个有效的列定义。");
            if (transactionStartedHere) db_manager.rollbackTransaction(); return;
        } else if (definitions.isEmpty()){ // definitions 本身就是空的
            textBuffer.append("错误: 未提供任何列定义或表约束。");
            if (transactionStartedHere) db_manager.rollbackTransaction(); return;
        }
        // 如果 hasOnlyTableConstraints 为 true，意味着可能只定义了表级约束而没有列，
        // 这在标准SQL中通常是不允许的，但您的系统是否允许需要看设计。
        // 此处简化，若无字段则报错。
    }

    if (new_table.fields().isEmpty() && !fields_and_constraints_str.contains("CONSTRAINT", Qt::CaseInsensitive)) {
        // 这个检查可能需要调整，因为即使没有显式的列定义，也可能只有表级约束（例如，一个全是约束的表定义，虽然不常见）
        // 但如果`handleTableConstraint`成功添加了主键等，`new_table.primaryKeys()`等就不会为空。
        // 更安全的检查是：if (new_table.fields().isEmpty() && new_table.primaryKeys().isEmpty() && new_table.uniqueConstraints().isEmpty() && ...)
        // 暂时保留原逻辑，因为通常表至少会有一个字段。
        textBuffer.append("Error: Table must have at least one column or a table-level constraint.");
        if (transactionStartedHere) db_manager.rollbackTransaction(); return;
    }


    // 尝试创建表并提交/回滚事务
    if (db_manager.createtable(current_db_name, new_table)) {
        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                textBuffer.append(QString("表 '%1' 在数据库 '%2' 中创建成功。事务已提交。").arg(table_name, current_db_name));
            } else {
                // 理论上 createtable 成功后，commit 应该也成功，除非有外部因素或db_manager内部问题
                textBuffer.append(QString("表 '%1' 创建成功（内存中），但事务提交失败！数据库 '%2' 可能处于不一致状态。").arg(table_name, current_db_name));
                // 可能需要更复杂的错误恢复逻辑
            }
        } else { // 在一个已存在的事务中操作，或者系统不支持事务
            textBuffer.append(QString("表 '%1' 在数据库 '%2' 中创建成功（在现有事务中或无事务模式下）。").arg(table_name, current_db_name));
        }
    } else {
        textBuffer.append(QString("错误: 创建表 '%1' 失败 (可能已存在，或定义无效，或表级约束解析失败)。").arg(table_name));
        if (transactionStartedHere) db_manager.rollbackTransaction();
    }
}

void MainWindow::handleDropTable(const QString& command) {
    QRegularExpression re(R"(DROP\s+TABLE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString table_name = match.captured(1);
        QString current_db_name = db_manager.get_current_database();
        if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "确认删除表",
                                                                 QString("确定要永久删除表 '%1' 吗? 此操作不可恢复!").arg(table_name),
                                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (db_manager.droptable(current_db_name, table_name)) {
                textBuffer.append(QString("表 '%1' 已从数据库 '%2' 删除。").arg(table_name, current_db_name));
            } else {
                textBuffer.append(QString("错误: 删除表 '%1' 失败 (可能不存在)。").arg(table_name));
            }
        } else {
            textBuffer.append("删除表操作已取消。");
        }
    } else {
        textBuffer.append("语法错误: DROP TABLE <表名>");
    }
}

void MainWindow::handleDescribe(const QString& command) {
    QRegularExpression re(R"((?:DESCRIBE|DESC)\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString table_name = match.captured(1);
        QString current_db_name = db_manager.get_current_database();
        if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
        show_schema(current_db_name, table_name);
    } else {
        textBuffer.append("语法错误: DESCRIBE <表名> 或 DESC <表名>");
    }
}

// mainwindow.cpp

void MainWindow::handleInsert(const QString& command) {
    QRegularExpression re(
        R"(INSERT\s+INTO\s+([\w_]+)\s*(?:\(([^)]+)\))?\s*VALUES\s*((?:\([^)]*\)\s*,?\s*)+)\s*;?)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch main_match = re.match(command.trimmed());

    if (!main_match.hasMatch()) {
        textBuffer.append("Syntax Error: INSERT INTO TableName [(col1,...)] VALUES (val1,...)[, (valA,...)];");
        return;
    }

    QString table_name = main_match.captured(1).trimmed();
    QString fields_part = main_match.captured(2).trimmed();
    QString all_value_groups_string = main_match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("Error: No database selected. Use 'USE <database_name>;' first.");
        return;
    }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (!db_manager.beginTransaction()) {
            textBuffer.append("Warning: Could not start a new transaction. Operation will proceed without explicit transaction control from here.");
            // transactionStartedHere remains false
        } else {
            transactionStartedHere = true;
        }
    }

    try {
        QStringList specified_fields;
        if (!fields_part.isEmpty()) {
            specified_fields = fields_part.split(',', Qt::SkipEmptyParts);
            for (QString& field : specified_fields) field = field.trimmed();
        }

        QList<QStringList> rows_of_values;
        QRegularExpression value_group_re(R"(\(([^)]*)\))");
        QRegularExpressionMatchIterator it_val_groups = value_group_re.globalMatch(all_value_groups_string);
        while (it_val_groups.hasNext()) {
            QRegularExpressionMatch m = it_val_groups.next();
            rows_of_values.append(parseSqlValues(m.captured(1))); // parseSqlValues handles 'val1','val2',...
        }

        if (rows_of_values.isEmpty()) {
            throw std::runtime_error("No valid value groups found in VALUES clause.");
        }

        int total_inserted_successfully = 0;
        bool all_rows_processed_successfully = true;

        for (const QStringList& single_row_values : rows_of_values) {
            QMap<QString, QString> field_value_map;
            if (specified_fields.isEmpty()) { // INSERT INTO Table VALUES (val1, val2)
                xhydatabase* db = db_manager.find_database(current_db_name);
                if (!db) throw std::runtime_error(("Database '" + current_db_name + "' not found.").toStdString());
                const xhytable* table_ptr = db->find_table(table_name); // Use const version for introspection
                if (!table_ptr) throw std::runtime_error(("Table '" + table_name + "' not found in database '" + current_db_name + "'.").toStdString());

                const auto& table_fields_ordered = table_ptr->fields(); // Assuming fields() returns in defined order
                if (single_row_values.size() != table_fields_ordered.size()) {
                    throw std::runtime_error(QString("Value count (%1) does not match column count (%2) for table '%3'.")
                                                 .arg(single_row_values.size()).arg(table_name).arg(table_fields_ordered.size()).toStdString());
                }
                for (int i = 0; i < table_fields_ordered.size(); ++i) {
                    field_value_map[table_fields_ordered[i].name()] = single_row_values[i];
                }
            } else { // INSERT INTO Table (col1, col2) VALUES (val1, val2)
                if (single_row_values.size() != specified_fields.size()) {
                    throw std::runtime_error(QString("Value count (%1) does not match specified column count (%2).")
                                                 .arg(single_row_values.size()).arg(specified_fields.size()).toStdString());
                }
                for (int i = 0; i < specified_fields.size(); ++i) {
                    field_value_map[specified_fields[i]] = single_row_values[i];
                }
            }

            if (db_manager.insertData(current_db_name, table_name, field_value_map)) {
                total_inserted_successfully++;
            } else {
                all_rows_processed_successfully = false;
                // Error message already printed by db_manager.insertData or underlying calls
                if (rows_of_values.size() > 1) {
                    textBuffer.append(QString("Warning: One of the rows for table '%1' failed to insert. Continuing with other rows if any...").arg(table_name));
                    // To stop on first error: break; or throw std::runtime_error("Multi-row insert failed.");
                } else {
                    // Single row insert failed, error already printed.
                }
            }
        } // End for each row

        if (transactionStartedHere) {
            if (all_rows_processed_successfully) {
                if (db_manager.commitTransaction()) {
                    textBuffer.append(QString("%1 row(s) successfully inserted into '%2' and transaction committed.")
                                                       .arg(total_inserted_successfully).arg(table_name));
                } else {
                    // This case implies data was inserted into memory structures but commit to persistent storage failed.
                    textBuffer.append(QString("CRITICAL Error: %1 row(s) inserted into '%2' (in memory), but transaction COMMIT FAILED. Database may be inconsistent.")
                                                       .arg(total_inserted_successfully).arg(table_name));
                }
            } else { // Some rows failed
                db_manager.rollbackTransaction();
                textBuffer.append(QString("Insert operation for '%1' had errors (attempted %2 row(s), successfully inserted %3). Transaction rolled back.")
                                                   .arg(table_name).arg(rows_of_values.size()).arg(total_inserted_successfully));
            }
        } else { // Not in a transaction started by this function
            if (all_rows_processed_successfully) {
                textBuffer.append(QString("%1 row(s) successfully processed for '%2' (within existing transaction or no transaction).")
                                                   .arg(total_inserted_successfully).arg(table_name));
            } else {
                textBuffer.append(QString("Insert operation for '%1' had errors (attempted %2 row(s), successfully inserted %3) (within existing transaction or no transaction).")
                                                   .arg(table_name).arg(rows_of_values.size()).arg(total_inserted_successfully));
            }
        }

    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("Insert Data Runtime Error: " + QString::fromStdString(e.what()) + (transactionStartedHere ? " (Transaction rolled back)" : ""));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
       textBuffer.append(QString("Unknown critical error during Insert Data.") + (transactionStartedHere ? " (Transaction rolled back)" : ""));
    }
}



// ============================================================================
// START: Implementation of parseLiteralValue, parseWhereClause and helpers
// ============================================================================

QVariant MainWindow::parseLiteralValue(const QString& valueStr) {
    QString trimmedVal = valueStr.trimmed();

    if (trimmedVal.compare("NULL", Qt::CaseInsensitive) == 0) {
        return QVariant(); // Represents SQL NULL
    }

    // String literal (handles single and double quotes)
    if ((trimmedVal.startsWith('\'') && trimmedVal.endsWith('\'')) ||
        (trimmedVal.startsWith('"') && trimmedVal.endsWith('"'))) {
        if (trimmedVal.length() >= 2) {
            QString inner = trimmedVal.mid(1, trimmedVal.length() - 2);
            if (trimmedVal.startsWith('\'')) {
                inner.replace("''", "'"); // Unescape '' to '
            } else { // Double quotes
                inner.replace("\"\"", "\""); // Unescape "" to "
            }
            return inner;
        }
        return QString(""); // Empty string if only quotes
    }

    // Boolean literals
    if (trimmedVal.compare("TRUE", Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmedVal.compare("FALSE", Qt::CaseInsensitive) == 0) {
        return false;
    }

    // Numeric literals
    bool ok;
    // Try parsing as integer (long long for wider range)
    qlonglong longVal = trimmedVal.toLongLong(&ok);
    if (ok) {
        // If it doesn't contain a decimal point or exponent, it's likely an integer type
        if (!trimmedVal.contains('.') && !trimmedVal.contains('e', Qt::CaseInsensitive)) {
            // Check if it fits in a standard int
            if (longVal >= std::numeric_limits<int>::min() && longVal <= std::numeric_limits<int>::max()) {
                return static_cast<int>(longVal);
            }
            return longVal; // Otherwise, return as qlonglong
        }
    }

    // If integer parsing failed or it contains a decimal/exponent, try as double
    double doubleVal = trimmedVal.toDouble(&ok);
    if (ok) {
        return doubleVal;
    }

    // If not quoted, and not NULL, TRUE, FALSE, or a valid number,
    // it might be an unquoted string (e.g., enum values in some SQL dialects) or a column name.
    // At this stage of SQL parsing, a literal parser usually only handles true literals.
    // Column names should be handled by other parts of the parser.
    // For simplicity, if it's none of the above, we treat it as a (potentially erroneous) unquoted string.
    // Alternatively, you might throw an error here.
    // qWarning() << "Could not uniquely identify literal as number, string, or boolean:" << trimmedVal;
    return trimmedVal; // Fallback: treat as string
}

QPair<int, QString> MainWindow::findLowestPrecedenceOperator(const QString& expr, const QStringList& operatorsInPrecedenceOrder) {
    int currentParenLevel = 0;
    int bestOpPos = -1;
    QString bestOpFound;
    // Lower index in operatorsInPrecedenceOrder means lower precedence (parsed first when recursing)
    int lowestPrecedenceIndex = operatorsInPrecedenceOrder.size();

    for (int i = expr.length() - 1; i >= 0; --i) {
        QChar c = expr[i];
        if (c == ')') {
            currentParenLevel++;
        } else if (c == '(') {
            currentParenLevel--;
        } else if (currentParenLevel == 0) {
            for (int opIdx = 0; opIdx < operatorsInPrecedenceOrder.size(); ++opIdx) {
                const QString& opCandidate = operatorsInPrecedenceOrder[opIdx];
                if (i >= opCandidate.length() - 1) {
                    QString sub = expr.mid(i - opCandidate.length() + 1, opCandidate.length());
                    if (sub.compare(opCandidate, Qt::CaseInsensitive) == 0) {
                        bool leftOk = (i - opCandidate.length() < 0) || expr[i - opCandidate.length()].isSpace() || expr[i - opCandidate.length()] == ')' || expr[i - opCandidate.length()] == '(';
                        bool rightOk = (i + 1 >= expr.length()) || expr[i + 1].isSpace() || expr[i + 1] == '(' || expr[i + 1] == ')';

                        if (leftOk && rightOk) {
                            // Find the operator with the lowest precedence (smallest index in operatorsInPrecedenceOrder)
                            // If precedence is the same, the rightmost one (due to right-to-left scan) is chosen.
                            if (opIdx < lowestPrecedenceIndex) {
                                lowestPrecedenceIndex = opIdx;
                                bestOpPos = i - opCandidate.length() + 1;
                                bestOpFound = opCandidate.toUpper();
                            }
                        }
                    }
                }
            }
        }
    }
    return {bestOpPos, bestOpFound};
}

// MainWindow::parseSubExpression 的实现
ConditionNode MainWindow::parseSubExpression(QStringView expressionView) {
    QString expression = expressionView.toString().trimmed();
    // Keep the initial qDebug for the overall expression
    // qDebug() << "[parseSubExpression] Overall Parsing sub-expression: '" << expression << "'";
    ConditionNode node;

    // 1. 移除最外层括号 ... (这部分代码与您之前提供的版本相同，保持不变)
    while (expression.startsWith('(') && expression.endsWith(')')) {
        int parenLevel = 0;
        bool allEnclosedProperly = true;
        if (expression.length() <= 2) {
            // qDebug() << "  Expression too short for meaningful parenthesized content, breaking paren stripping.";
            break;
        }
        for (int i = 0; i < expression.length() - 1; ++i) {
            if (expression[i] == '(') parenLevel++;
            else if (expression[i] == ')') parenLevel--;
            if (parenLevel == 0) {
                allEnclosedProperly = false;
                // qDebug() << "  Parentheses do not enclose the entire sub-expression (level became 0 at index " << i << ").";
                break;
            }
        }
        if (allEnclosedProperly && parenLevel == 1 && expression.at(expression.length()-1) == ')') {
            QString newExpression = expression.mid(1, expression.length() - 2).trimmed();
            // qDebug() << "  Stripped outer parentheses. Old: '" << expression << "', New: '" << newExpression << "'";
            expression = newExpression;
        } else {
            // qDebug() << "  Outer parentheses not stripped. AllEnclosed:" << allEnclosedProperly
            //          << "FinalParenLevel (before last char):" << parenLevel
            //          << "LastCharIsClosingParen:" << (expression.length() > 0 && expression.at(expression.length()-1) == ')');
            break;
        }
    }
    if (expression.isEmpty()) {
        // qDebug() << "  Expression became empty after stripping parentheses, treating as empty node.";
        node.type = ConditionNode::EMPTY;
        return node;
    }

    // 2. 处理 NOT 操作符 ... (这部分代码与您之前提供的版本相同，保持不变)
    if (expression.toUpper().startsWith("NOT ") &&
        !expression.toUpper().startsWith("NOT LIKE") &&
        !expression.toUpper().startsWith("NOT IN") &&
        !expression.toUpper().startsWith("NOT BETWEEN")) {
        QString restOfExpression = QStringView(expression).mid(4).toString().trimmed();
        // qDebug() << "  Found NOT prefix. Remainder for recursive call: '" << restOfExpression << "'";
        if (restOfExpression.isEmpty()) {
            throw std::runtime_error("NOT 操作符后缺少条件表达式。");
        }
        ConditionNode childNode = parseSubExpression(restOfExpression);
        node.type = ConditionNode::NEGATION_OP;
        node.children.append(childNode);
        // qDebug() << "  Created NEGATION_OP node with child type:" << childNode.type;
        return node;
    }

    // zyh在这里加了这样一段，然后就可以处理BETWEEN ... AND ...语句了
    if (expression.contains("BETWEEN", Qt::CaseInsensitive)) {
        // 解析 BETWEEN ... AND ...
        QRegularExpression betweenRegex(R"((.+?)\s+BETWEEN\s+(.+?)\s+AND\s+(.+))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = betweenRegex.match(expression);

        if (match.hasMatch()) {
            QString field = match.captured(1).trimmed();
            QString value1 = match.captured(2).trimmed();
            QString value2 = match.captured(3).trimmed();

            node.type = ConditionNode::COMPARISON_OP;
            node.comparison.fieldName = field;
            node.comparison.operation = "BETWEEN";
            node.comparison.value = parseLiteralValue(value1);
            node.comparison.value2 = parseLiteralValue(value2);

            qDebug() << "Parsed BETWEEN: Field=" << field << ", Value1=" << value1 << ", Value2=" << value2;
            return node;
        }
    }

    // 3. 处理逻辑运算符 OR, AND ... (这部分代码与您之前提供的版本相同，保持不变)
    QPair<int, QString> opDetails = findLowestPrecedenceOperator(expression, {"OR", "AND"});
    if (opDetails.first != -1) {
        // qDebug() << "  Found logical operator: '" << opDetails.second << "' at pos " << opDetails.first;
        node.type = ConditionNode::LOGIC_OP;
        node.logicOp = opDetails.second.toUpper();
        QString leftPart = QStringView(expression).left(opDetails.first).toString().trimmed();
        QString rightPart = QStringView(expression).mid(opDetails.first + opDetails.second.length()).toString().trimmed();
        if (leftPart.isEmpty() || rightPart.isEmpty()) {
            throw std::runtime_error("逻辑运算符 '" + opDetails.second.toStdString() + "' 缺少操作数。表达式: " + expression.toStdString());
        }
        // qDebug() << "    Left part: '" << leftPart << "', Right part: '" << rightPart << "'";
        node.children.append(parseSubExpression(leftPart));
        node.children.append(parseSubExpression(rightPart));
        return node;
    }

    // 4. 处理比较运算符
    const QList<QString> comparisonOps = {
        "IS NOT NULL", "IS NULL",
        "NOT BETWEEN", "BETWEEN",
        "NOT LIKE", "LIKE",
        "NOT IN", "IN",
        ">=", "<=", "<>", "!=", "=", ">", "<"
    };

    for (const QString& op_from_list : comparisonOps) {
        QRegularExpression compRe;
        QString patternStr;
        QString fieldNamePattern = R"(([\w_][\w\d_]*|`[^`]+`|\[[^\]]+\]))";
        QString escaped_op = QRegularExpression::escape(op_from_list);
        QString operator_pattern_segment;

        // 为特定关键字操作符添加单词边界 \b
        // IS NULL, IS NOT NULL, BETWEEN, NOT BETWEEN 是多词操作符，它们的模式已经通过空格自然形成了边界
        if (op_from_list == "IN" || op_from_list == "NOT IN" ||
            op_from_list == "LIKE" || op_from_list == "NOT LIKE") {
            operator_pattern_segment = QString(R"(\b(%1)\b)").arg(escaped_op);
        } else {
            // 对于 "IS NULL", "IS NOT NULL", "BETWEEN", "NOT BETWEEN" 以及符号操作符
            // 不需要额外的 \b, 因为它们要么是多词本身就由空格分隔，要么是符号。
            operator_pattern_segment = QString("(%1)").arg(escaped_op);
        }

        // 构建完整的正则表达式模式
        if (op_from_list.compare("IS NULL", Qt::CaseInsensitive) == 0 || op_from_list.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) {
            // 对于 IS NULL/IS NOT NULL, operator_pattern_segment 已经是 "IS NULL" 或 "IS NOT NULL"
            patternStr = QString(R"(^\s*%1\s+%2\s*$)").arg(fieldNamePattern).arg(operator_pattern_segment);
        } else if (op_from_list.compare("BETWEEN", Qt::CaseInsensitive) == 0 || op_from_list.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
            // operator_pattern_segment 是 "BETWEEN" 或 "NOT BETWEEN"
            patternStr = QString(R"(^\s*%1\s*%2\s*(.+?)\s+AND\s+(.+?)\s*$)").arg(fieldNamePattern).arg(operator_pattern_segment);
        } else { // 适用于 IN, LIKE, =, >, < 等其他需要右操作数的二元操作符
            patternStr = QString(R"(^\s*%1\s*%2\s*(.+)\s*$)").arg(fieldNamePattern).arg(operator_pattern_segment);
        }

        compRe.setPattern(patternStr);
        compRe.setPatternOptions(QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch match = compRe.match(expression);

        if (match.hasMatch()) {
            // 还原详细的 qDebug 输出，以帮助调试（如果问题仍然存在）
            qDebug() << "[parseSubExpression][COMPARISON] Matched op_from_list: '" << op_from_list
                     << "' with pattern: '" << patternStr
                     << "' on expression: '" << expression << "'";

            node.type = ConditionNode::COMPARISON_OP;
            node.comparison.fieldName = match.captured(1).trimmed();
            node.comparison.operation = op_from_list.toUpper();

            qDebug() << "  [COMPARISON] Field from regex: '" << node.comparison.fieldName
                     << "', Operation set to: '" << node.comparison.operation << "'";
            // for(int capIdx = 0; capIdx <= match.lastCapturedIndex(); ++capIdx) {
            //     qDebug() << "    [COMPARISON] Regex Capture Group " << capIdx << ": '" << match.captured(capIdx) << "'";
            // }


            if (node.comparison.operation == "IS NULL" || node.comparison.operation == "IS NOT NULL") {
                qDebug() << "    [COMPARISON] Branch: IS NULL / IS NOT NULL";
            } else if (node.comparison.operation == "BETWEEN" || node.comparison.operation == "NOT BETWEEN") {
                qDebug() << "    [COMPARISON] Branch: BETWEEN / NOT BETWEEN";
                QString val1Str = match.captured(3).trimmed();
                QString val2Str = match.captured(4).trimmed();
                node.comparison.value = parseLiteralValue(val1Str);
                node.comparison.value2 = parseLiteralValue(val2Str);
                qDebug() << "      Values: Value1=" << node.comparison.value
                         << " (Raw: '" << val1Str << "'), Value2=" << node.comparison.value2
                         << " (Raw: '" << val2Str << "')";
            } else if (node.comparison.operation == "IN" || node.comparison.operation == "NOT IN") {
                qDebug() << "    [COMPARISON] Branch: IN / NOT IN";
                QString valueListPart = match.captured(3).trimmed();
                qDebug() << "      valueListPart (raw from capture group 3): '" << valueListPart << "'";
                if (!valueListPart.startsWith('(') || !valueListPart.endsWith(')')) {
                    throw std::runtime_error("IN 子句的值必须用括号括起来。 Got: " + valueListPart.toStdString());
                }
                QString innerValues = valueListPart.mid(1, valueListPart.length() - 2).trimmed();
                if (innerValues.isEmpty()) {
                    throw std::runtime_error("IN 子句的值列表不能为空 (例如 IN () )");
                }
                QStringList valuesStr = parseSqlValues(innerValues);
                for(const QString& valStr : valuesStr) {
                    if (!valStr.trimmed().isEmpty()) {
                        node.comparison.valueList.append(parseLiteralValue(valStr.trimmed()));
                    }
                }
                if (node.comparison.valueList.isEmpty()) {
                    throw std::runtime_error("IN 子句的值列表解析后为空或格式错误。 (Inner: " + innerValues.toStdString() + ")");
                }
                qDebug() << "      IN/NOT IN values parsed:" << node.comparison.valueList;
            } else {
                qDebug() << "    [COMPARISON] Branch: Other binary operators (e.g., LIKE, =, >, <)";
                QString valuePart = match.captured(3).trimmed();
                qDebug() << "      valuePart (raw from capture group 3): '" << valuePart << "'";
                node.comparison.value = parseLiteralValue(valuePart);
                qDebug() << "      Parsed value: " << node.comparison.value;
            }
            return node;
        }
    }

    qDebug() << "[parseSubExpression] No operator pattern matched for expression: '" << expression << "'";
    throw std::runtime_error("无效的表达式或 WHERE 子句中不支持的语法: '" + expression.toStdString() + "'");
}

// MainWindow::parseWhereClause 的主实现
bool MainWindow::parseWhereClause(const QString& whereStr, ConditionNode& rootNode) {
    QString currentWhere = whereStr.trimmed();
    if (currentWhere.endsWith(';')) {
        currentWhere.chop(1); // 移除末尾的分号
        currentWhere = currentWhere.trimmed(); // 再次 trim
    }

    if (currentWhere.isEmpty()) {
        rootNode.type = ConditionNode::EMPTY;
        qDebug() << "[parseWhereClause] WHERE clause is empty.";
        return true;
    }

    qDebug() << "[parseWhereClause] Parsing cleaned WHERE clause: '" << currentWhere << "'";
    try {
        rootNode = parseSubExpression(QStringView(currentWhere));
        qDebug() << "[parseWhereClause] Parsed successfully. Root node type:" << rootNode.type;
        return true;
    } catch (const std::runtime_error& e) {
        QString errorMsg = "解析WHERE子句错误: " + QString::fromStdString(e.what());
        qDebug() << "[parseWhereClause] Error:" << errorMsg;
        textBuffer.append(errorMsg); // 显示错误到UI
        return false;
    }
}


void MainWindow::handleUpdate(const QString& command) {
    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) {
        textBuffer.append("错误: 未选择数据库");
        return;
    }

    QRegularExpression re(
        R"(UPDATE\s+([\w\.]+)\s+SET\s+(.+?)(?:\s+WHERE\s+(.+))?\s*;?$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        textBuffer.append("语法错误: UPDATE 表名 SET 列1=值1,... [WHERE 条件]");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString set_part = match.captured(2).trimmed();
    QString where_part = match.captured(3).trimmed();


    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (!db_manager.beginTransaction()) {
            textBuffer.append("警告: 无法开始新事务，操作将在无事务保护下进行。");
        } else {
            transactionStartedHere = true;
        }
    }

    try {
        QMap<QString, QString> updates;
        QStringList set_clauses = set_part.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& clause : set_clauses) {
            QStringList parts = clause.split(QLatin1Char('='));
            if (parts.size() == 2) {
                QString fieldName = parts[0].trimmed();
                QString valueStr = parts[1].trimmed();
                QVariant parsedVal = parseLiteralValue(valueStr); // **Aufruf**
                updates[fieldName] = parsedVal.isNull() ? "NULL" : parsedVal.toString();
            } else {
                throw std::runtime_error(("无效的SET子句: " + clause).toStdString());
            }
        }

        ConditionNode conditionRoot;
        if (!parseWhereClause(where_part, conditionRoot)) { // **Aufruf**
            if(transactionStartedHere) db_manager.rollbackTransaction();
            return;
        }

        int affected_rows = db_manager.updateData(current_db_name, table_name, updates, conditionRoot);
        if (affected_rows >= 0) {
            if (transactionStartedHere) {
                if (db_manager.commitTransaction()) {
                    textBuffer.append(QString("%1 行已更新。").arg(affected_rows));
                } else {
                    textBuffer.append("错误：事务提交失败。更改已回滚。");
                }
            } else {
                textBuffer.append(QString("%1 行已更新 (在现有事务中)。").arg(affected_rows));
            }
        } else {
            textBuffer.append("错误：更新数据失败。");
            if(transactionStartedHere) db_manager.rollbackTransaction();
        }
    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("更新操作错误: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("更新操作发生未知错误。");
    }
}

void MainWindow::handleDelete(const QString& command) {
    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    QRegularExpression re(R"(DELETE\s+FROM\s+([\w\.]+)(?:\s+WHERE\s+(.+))?\s*;?$)",
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) { textBuffer.append("语法错误: DELETE FROM <表名> [WHERE <条件>]"); return; }
    QString table_name = match.captured(1).trimmed();
    QString where_part = match.captured(2).trimmed();

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if(!db_manager.beginTransaction()) { /* ... */ } else transactionStartedHere = true;
    }

    try {
        if (where_part.isEmpty()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "确认删除",
                                                                      QString("确定要删除表 '%1' 中的所有数据吗? 此操作不可恢复!").arg(table_name),
                                                                      QMessageBox::Yes|QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::No) {
                if (transactionStartedHere) db_manager.rollbackTransaction();
                textBuffer.append("删除操作已取消。");
                return;
            }
        }
        ConditionNode conditionRoot;
        if (!parseWhereClause(where_part, conditionRoot)) { // **Aufruf**
            if(transactionStartedHere) db_manager.rollbackTransaction();
            return;
        }
        int affected_rows = db_manager.deleteData(current_db_name, table_name, conditionRoot);
        if (affected_rows >= 0) {
            if (transactionStartedHere) {
                if (db_manager.commitTransaction()) {
                    textBuffer.append(QString("%1 行已删除。").arg(affected_rows));
                } else { textBuffer.append("错误：事务提交失败。更改已回滚。");}
            } else {
                textBuffer.append(QString("%1 行已删除 (在现有事务中)。").arg(affected_rows));
            }
        } else { textBuffer.append("错误：删除数据失败。"); if(transactionStartedHere) db_manager.rollbackTransaction(); }
    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("删除操作错误: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("删除操作发生未知错误。");
    }
}

// by zyh
void MainWindow::handleSelect(const QString& command) {
    // 解析 SELECT 语句，支持表别名
    QRegularExpression re(
        R"(SELECT\s+(.+?)\s+FROM\s+(\S+?)(?:\s+(?:AS\s+)?(?!ORDER\s+BY|WHERE|GROUP\s+BY|HAVING|LIMIT)(\w+))?\s*(?:WHERE\s+(.+?))?\s*(?:GROUP\s+BY\s+(.+?))?\s*(?:HAVING\s+(.+?))?\s*(?:ORDER\s+BY\s+([\w\s,]+))?\s*(?:LIMIT\s+(\d+))?\s*;?\s*$)",
        QRegularExpression::CaseInsensitiveOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    /* 后面记得用这个，下面只是用于调试
    if (!match.hasMatch()) {
        textBuffer.append("语法错误: SELECT <列名1,...|*> FROM <表名> [AS <别名>] [WHERE <条件>] [GROUP BY <列1,...>] [HAVING <条件>] [ORDER BY <列1> [ASC|DESC],...] [LIMIT <数量>]");
        return;
    }
*/

    // 添加调试信息在这里
    qDebug() << "解析SQL: " << command;
    qDebug() << "匹配成功: " << match.hasMatch();
    qDebug() << "从SQL: " << command << "中提取表名";
    qDebug() << "匹配的表名: " << match.captured(2);
    if (match.hasMatch()) {
        qDebug() << "捕获组1 (SELECT列): " << match.captured(1);
        qDebug() << "捕获组2 (FROM表): " << match.captured(2);
        qDebug() << "捕获组3 (表别名): " << match.captured(3);
        qDebug() << "捕获组4 (WHERE): " << match.captured(4);
        qDebug() << "捕获组5 (GROUP BY): " << match.captured(5);
        qDebug() << "捕获组6 (HAVING): " << match.captured(6);
        qDebug() << "捕获组7 (ORDER BY): " << match.captured(7);
        qDebug() << "捕获组8 (LIMIT): " << match.captured(8);
    } else {
        qDebug() << "SQL语法匹配失败!";
        textBuffer.append("语法错误: SELECT <列名1,...|*> FROM <表名> [AS <别名>] [WHERE <条件>] [GROUP BY <列1,...>] [HAVING <条件>] [ORDER BY <列1> [ASC|DESC],...] [LIMIT <数量>]");
        return;
    }

    QString select_cols_str = match.captured(1).trimmed();
    QString table_name = match.captured(2).trimmed();
    QString table_alias = match.captured(3).trimmed();
    QString where_part = match.captured(4).trimmed();
    QString group_by_part = match.captured(5).trimmed();
    QString having_part = match.captured(6).trimmed();
    QString order_by_part = match.captured(7).trimmed();
    QString limit_part = match.captured(8).trimmed();

    // 解析LIMIT子句
    int limit = -1; // 默认不限制
    if (!limit_part.isEmpty()) {
        bool ok;
        limit = limit_part.toInt(&ok);
        if (!ok || limit < 0) {
            textBuffer.append("错误: LIMIT子句必须是一个非负整数。");
            return;
        }
    }

    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) { textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }
    xhytable* table = db->find_table(table_name);
    if (!table) { textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, current_db_name)); return; }

    // 工具函数：去表别名
    // 修改后 - 在MainWindow::handleSelect函数中
    auto removeAlias = [&](const QString& col) -> QString {
        return this->removeTableAlias(col, table_alias, table_name);
    };

    // 处理WHERE条件
    ConditionNode conditionRoot;
    if (!where_part.isEmpty() && !parseWhereClause(where_part, conditionRoot)) {
        return;
    }

    QVector<xhyrecord> results;
    if (!db_manager.selectData(current_db_name, table_name, conditionRoot, results)) {
        return;
    }

    // 解析SELECT列
    bool hasAggregateFunction = false;
    QStringList display_columns; // 显示名称（可能有别名）
    QMap<QString, QString> column_aliases; // 列名到显示名称
    QMap<QString, QPair<QString, QString>> aggregateFuncs; // 显示名 -> <函数名, 列名>
    QMap<QString, QString> columnRealNames; // 显示名 -> 原始列名

    if (select_cols_str == "*") {
        const QList<xhyfield>& fields = table->fields();
        for (int i = 0; i < fields.size(); ++i) {
            const QString& fieldName = fields[i].name();
            display_columns.append(fieldName);
            columnRealNames[fieldName] = fieldName;
        }
    } else {
        QStringList cols = select_cols_str.split(',', Qt::SkipEmptyParts);
        for (int i = 0; i < cols.size(); ++i) {
            QString col = cols[i].trimmed();
            // 检查是否为聚合函数
            QRegularExpression funcRe(R"(^(COUNT|SUM|AVG|MIN|MAX)\(([^)]*)\)(?:\s+AS\s+(.+))?$)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch funcMatch = funcRe.match(col);
            if (funcMatch.hasMatch()) {
                hasAggregateFunction = true;
                QString functionType = funcMatch.captured(1).toUpper();
                QString columnName = removeAlias(funcMatch.captured(2).trimmed());
                QString aliasName = funcMatch.captured(3).trimmed();
                if (aliasName.isEmpty()) aliasName = functionType + "(" + columnName + ")";
                if (columnName != "*" && !columnName.isEmpty() && !table->has_field(columnName)) {
                    textBuffer.append(QString("错误: 聚合函数中的列 '%1' 在表 '%2' 中不存在。").arg(columnName, table_name));
                    return;
                }
                aggregateFuncs[aliasName] = qMakePair(functionType, columnName);
                display_columns.append(aliasName);
            } else {
                QRegularExpression aliasRe(R"(^([\w\.]+)(?:\s+AS\s+(.+))?$)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch aliasMatch = aliasRe.match(col);
                if (aliasMatch.hasMatch()) {
                    QString realColumnName = removeAlias(aliasMatch.captured(1).trimmed());
                    QString alias = aliasMatch.captured(2).trimmed();
                    if (!table->has_field(realColumnName)) {
                        textBuffer.append(QString("错误: 选择的列 '%1' 在表 '%2' 中不存在。").arg(realColumnName, table_name));
                        return;
                    }
                    QString displayName = alias.isEmpty() ? realColumnName : alias;
                    display_columns.append(displayName);
                    columnRealNames[displayName] = realColumnName;
                    if (!alias.isEmpty()) column_aliases[realColumnName] = displayName;
                } else {
                    QString realColumnName = removeAlias(col);
                    if (!table->has_field(realColumnName)) {
                        textBuffer.append(QString("错误: 选择的列 '%1' 在表 '%2' 中不存在。").arg(realColumnName, table_name));
                        return;
                    }
                    display_columns.append(col);
                    columnRealNames[col] = realColumnName;
                }
            }
        }
    }

    // 处理GROUP BY
    QStringList groupByColumns;
    if (!group_by_part.isEmpty()) {
        groupByColumns = group_by_part.split(',', Qt::SkipEmptyParts);
        for (int i = 0; i < groupByColumns.size(); ++i) {
            QString originalCol = groupByColumns[i];
            groupByColumns[i] = removeAlias(groupByColumns[i]);
            if (!table->has_field(groupByColumns[i])) {
                // 尝试查找是否是表别名的问题
                if (originalCol.contains('.') && !table_alias.isEmpty()) {
                    QString prefix = originalCol.split('.').first();
                    if (prefix == table_alias) {
                        // 获取除别名外的列名部分
                        QString colOnly = originalCol.mid(prefix.length() + 1);
                        if (table->has_field(colOnly)) {
                            groupByColumns[i] = colOnly;
                            continue;
                        }
                    }
                }
                textBuffer.append(QString("错误: GROUP BY中的列 '%1' 未在选择列表中或表中不存在。").arg(originalCol));
                return;
            }
        }
    }

    // 分组：key为所有分组列值组成的QList
    using GroupKey = QList<QString>;
    // 修改后 - 在mainwindow.cpp中的handleSelect函数内处理分组的代码
    QMap<GroupKey, QVector<xhyrecord>> groupedResultsMap;
    if (!groupByColumns.isEmpty()) {
        for (const xhyrecord& record : results) {
            GroupKey groupKey;
            for (const QString& groupCol : groupByColumns) {
                // 确保使用正确的列名而不是可能带表别名的原始名
                QString realColName = groupCol; // 这里应已经是去掉别名的实际列名
                groupKey << record.value(realColName);
            }
            groupedResultsMap[groupKey].append(record);
        }
    } else if (hasAggregateFunction) {
        // 如果有聚合函数但没有GROUP BY，将所有记录放在一个组中
        groupedResultsMap[GroupKey{"all"}] = results;
    }

    // 确保groupedResults有数据
    QList<QPair<GroupKey, QVector<xhyrecord>>> groupedResults;
    for (auto it = groupedResultsMap.begin(); it != groupedResultsMap.end(); ++it) {
        groupedResults.append(qMakePair(it.key(), it.value()));
    }

    // HAVING过滤
    // 修改后 - 在MainWindow::handleSelect函数中处理HAVING的部分
    if (!having_part.isEmpty() && !groupByColumns.isEmpty()) {
        // 支持简单的 HAVING <聚合函数>(列) <op> <值> 或 HAVING COUNT(*) > 数值 等形式
        QRegularExpression havingRe(R"(([\w\(\)\*\.]+)\s*(=|!=|<>|<|>|<=|>=)\s*(.+))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch havingMatch = havingRe.match(having_part);
        if (havingMatch.hasMatch()) {
            QString havingCol = havingMatch.captured(1).trimmed();
            QString havingOp = havingMatch.captured(2).trimmed();
            QString havingValStr = havingMatch.captured(3).trimmed();

            // 处理引号包裹的字符串值
            if ((havingValStr.startsWith('\'') && havingValStr.endsWith('\'')) ||
                (havingValStr.startsWith('"') && havingValStr.endsWith('"'))) {
                havingValStr = havingValStr.mid(1, havingValStr.length() - 2);
            }

            QString havingColNoAlias = removeAlias(havingCol);
            QList<QPair<GroupKey, QVector<xhyrecord>>> filteredGroups;
            QRegularExpression funcRe(R"(^(COUNT|SUM|AVG|MIN|MAX)\(([^)]*)\)$)", QRegularExpression::CaseInsensitiveOption);

            for (const auto& pair : groupedResults) {
                const GroupKey& groupKey = pair.first;
                const QVector<xhyrecord>& group = pair.second;
                QVariant groupAggValue;
                bool isAggFunc = false;

                QRegularExpressionMatch funcMatch = funcRe.match(havingColNoAlias);
                if (funcMatch.hasMatch()) {
                    QString funcType = funcMatch.captured(1).toUpper();
                    QString colName = removeAlias(funcMatch.captured(2).trimmed());
                    groupAggValue = calculateAggregate(funcType, colName, group);
                    isAggFunc = true;
                } else {
                    // 尝试检查是否是分组列
                    int idx = groupByColumns.indexOf(havingColNoAlias);
                    if (idx != -1) {
                        groupAggValue = groupKey[idx];
                    } else {
                        // 看看是不是 COUNT(*) 的特殊情况
                        if (havingColNoAlias.toUpper() == "COUNT(*)") {
                            groupAggValue = group.size();
                            isAggFunc = true;
                        }
                    }
                }

                bool pass = false;
                if (groupAggValue.isValid()) {
                    // 获取比较值
                    QVariant havingVal;
                    bool isHavingValNumeric = false;
                    double havingNumVal = havingValStr.toDouble(&isHavingValNumeric);

                    if (isHavingValNumeric) {
                        havingVal = havingNumVal;
                    } else {
                        havingVal = havingValStr;
                    }

                    // 判断聚合结果是否为数字
                    bool isAggValueNumeric = false;
                    double aggNumVal = 0.0;

                    if (isAggFunc) {
                        // 聚合函数结果通常是数值
                        aggNumVal = groupAggValue.toDouble(&isAggValueNumeric);
                    } else {
                        // 尝试转换为数值
                        aggNumVal = groupAggValue.toString().toDouble(&isAggValueNumeric);
                    }

                    // 执行比较
                    if (isAggValueNumeric && isHavingValNumeric) {
                        // 数值比较
                        if (havingOp == "=" || havingOp == "==") pass = qFuzzyCompare(aggNumVal, havingNumVal);
                        else if (havingOp == "!=" || havingOp == "<>") pass = !qFuzzyCompare(aggNumVal, havingNumVal);
                        else if (havingOp == ">") pass = aggNumVal > havingNumVal;
                        else if (havingOp == "<") pass = aggNumVal < havingNumVal;
                        else if (havingOp == ">=") pass = aggNumVal >= havingNumVal;
                        else if (havingOp == "<=") pass = aggNumVal <= havingNumVal;
                    } else {
                        // 字符串比较
                        QString aggStrVal = groupAggValue.toString();
                        if (havingOp == "=" || havingOp == "==") pass = (aggStrVal == havingValStr);
                        else if (havingOp == "!=" || havingOp == "<>") pass = (aggStrVal != havingValStr);
                        // 其他比较运算符不适用于非数值类型
                    }
                }

                if (pass) {
                    filteredGroups.append(pair);
                }
            }

            groupedResults = filteredGroups;
        } else {
            textBuffer.append("警告: HAVING子句语法无效，已忽略: " + having_part);
        }
    }

    // ORDER BY
    QList<QPair<QString, bool>> orderColumns; // <列名, 是否降序>
    if (!order_by_part.isEmpty()) {
        QStringList orderParts = order_by_part.split(',', Qt::SkipEmptyParts);
        for (const QString& part : orderParts) {
            QString trimmedPart = part.trimmed();
            QRegularExpression orderRe(R"(^([\w\.]+)(?:\s+(ASC|DESC))?$)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch orderMatch = orderRe.match(trimmedPart);
            if (orderMatch.hasMatch()) {
                QString colName = removeAlias(orderMatch.captured(1).trimmed());
                bool isDesc = orderMatch.captured(2).trimmed().toUpper() == "DESC";
                orderColumns.append(qMakePair(colName, isDesc));
            } else {
                textBuffer.append(QString("警告: ORDER BY子句语法无效: '%1'，已忽略。").arg(trimmedPart));
            }
        }
    }
    // 排序实现
    if (!orderColumns.isEmpty()) {
        if (!groupByColumns.isEmpty() || hasAggregateFunction) {
            // 分组后排序
            std::sort(groupedResults.begin(), groupedResults.end(), [&](const QPair<GroupKey, QVector<xhyrecord>>& a, const QPair<GroupKey, QVector<xhyrecord>>& b) {
                for (const auto& orderPair : orderColumns) {
                    const QString& colName = orderPair.first;
                    bool isDesc = orderPair.second;
                    // 先在分组列里找
                    int idx = groupByColumns.indexOf(colName);
                    if (idx != -1) {
                        QString valA = a.first[idx], valB = b.first[idx];
                        bool okA, okB;
                        double numA = valA.toDouble(&okA), numB = valB.toDouble(&okB);
                        int comp = 0;
                        if (okA && okB) comp = numA < numB ? -1 : (numA > numB ? 1 : 0);
                        else comp = valA.compare(valB, Qt::CaseInsensitive);
                        if (comp != 0) return isDesc ? comp > 0 : comp < 0;
                    } else if (aggregateFuncs.contains(colName)) {
                        // 支持按聚合排序，比如 ORDER BY COUNT(*) DESC
                        QVariant aggA = calculateAggregate(aggregateFuncs[colName].first, aggregateFuncs[colName].second, a.second);
                        QVariant aggB = calculateAggregate(aggregateFuncs[colName].first, aggregateFuncs[colName].second, b.second);
                        bool okA, okB;
                        double numA = aggA.toDouble(&okA), numB = aggB.toDouble(&okB);
                        int comp = 0;
                        if (okA && okB) comp = numA < numB ? -1 : (numA > numB ? 1 : 0);
                        else comp = aggA.toString().compare(aggB.toString(), Qt::CaseInsensitive);
                        if (comp != 0) return isDesc ? comp > 0 : comp < 0;
                    }
                }
                return false;
            });
        } else {
            // 非分组，直接对results排序 - 修复排序逻辑
            std::sort(results.begin(), results.end(), [&](const xhyrecord& a, const xhyrecord& b) {
                for (const auto& orderPair : orderColumns) {
                    const QString& colName = orderPair.first;
                    bool isDesc = orderPair.second;
                    QString valA = a.value(colName), valB = b.value(colName);
                    bool okA, okB;
                    double numA = valA.toDouble(&okA), numB = valB.toDouble(&okB);
                    int comp = 0;

                    if (okA && okB) {
                        // 数值比较
                        comp = (numA < numB) ? -1 : ((numA > numB) ? 1 : 0);
                    } else {
                        // 字符串比较
                        comp = valA.compare(valB, Qt::CaseInsensitive);
                    }

                    if (comp != 0) {
                        // 根据排序方向返回比较结果 - 修复排序逻辑
                        return isDesc ? (comp > 0) : (comp < 0);
                    }
                }
                return false;
            });
        }
    }

    // 准备输出数据
    QList<QStringList> outputRows;

    // 准备数据
    if (!groupByColumns.isEmpty() || hasAggregateFunction) {
        for (const auto& pair : groupedResults) {
            const GroupKey& groupKey = pair.first;
            const QVector<xhyrecord>& group = pair.second;
            if (group.isEmpty()) continue;

            QStringList row;
            for (const QString& display_name : display_columns) {
                bool isHandled = false;

                // 处理分组列
                for (int groupByIdx = 0; groupByIdx < groupByColumns.size(); ++groupByIdx) {
                    if (display_name == groupByColumns[groupByIdx] ||
                        columnRealNames.value(display_name, "") == groupByColumns[groupByIdx]) {
                        row.append(groupKey[groupByIdx]);
                        isHandled = true;
                        break;
                    }
                }

                // 如果不是分组列，检查是否是聚合函数
                if (!isHandled && aggregateFuncs.contains(display_name)) {
                    const QPair<QString, QString>& funcInfo = aggregateFuncs[display_name];
                    QVariant result = calculateAggregate(funcInfo.first, funcInfo.second, group);
                    row.append(result.isValid() ? result.toString() : "NULL");
                    isHandled = true;
                }

                // 如果既不是分组列也不是聚合函数，则取该组的第一条记录对应列的值
                if (!isHandled) {
                    QString realColName = columnRealNames.value(display_name, display_name);
                    row.append(group.first().value(realColName));
                }
            }
            outputRows.append(row);
        }
    } else {
        for (const auto& record : results) {
            QStringList row;
            for (const QString& display_name : display_columns) {
                QString realColName = columnRealNames.value(display_name, display_name);
                row.append(record.value(realColName));
            }
            outputRows.append(row);
        }
    }

    // 应用LIMIT
    if (limit > 0 && limit < outputRows.size()) {
        outputRows = outputRows.mid(0, limit);
    }

    // 输出表头
    QString header_str;
    for (const QString& col_name : display_columns) header_str += col_name + "\t";
    textBuffer.append(header_str.trimmed());
    textBuffer.append(QString(header_str.length()*2 < 80 ? header_str.length()*2 : 80, '-'));

    // 输出数据
    for (const QStringList& row : outputRows) {
        textBuffer.append(row.join("\t"));
    }

    // 显示返回的记录数
    textBuffer.append(QString("\n%1 行记录已返回").arg(outputRows.size()));
}


// mainwindow.cpp

void MainWindow::handleAlterTable(const QString& command) {
    QRegularExpression re(
        R"(ALTER\s+TABLE\s+([\w_]+)\s+(ADD|DROP|ALTER|MODIFY|RENAME)\s+(?:COLUMN\s+)?(.+?)\s*;?$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        textBuffer.append("Syntax Error: ALTER TABLE <TableName> ADD|DROP|ALTER|MODIFY|RENAME [COLUMN] <parameters>");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString action = match.captured(2).trimmed().toUpper();
    QString parameters = match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("Error: No database selected. Use 'USE <database_name>;' first.");
        return;
    }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (db_manager.beginTransaction()) {
            transactionStartedHere = true;
        } else {
            textBuffer.append("Error: Failed to start transaction for ALTER TABLE.");
            return;
        }
    }

    bool success = false;
    QString result_msg;

    try {
        if (action == "ADD") {
            if (parameters.toUpper().startsWith("CONSTRAINT")) {
                xhydatabase* db = db_manager.find_database(current_db_name);
                if (!db) throw std::runtime_error("Current database not found during ALTER TABLE ADD CONSTRAINT.");
                xhytable* table_ptr = db->find_table(table_name);
                if (!table_ptr) throw std::runtime_error(("Table '" + table_name + "' not found for ADD CONSTRAINT.").toStdString());
                handleTableConstraint(parameters, *table_ptr);
                success = db_manager.update_table(current_db_name, *table_ptr);
                result_msg = success ? "Table-level constraint added." : "Failed to add or persist table-level constraint.";
                if (!success) throw std::runtime_error(result_msg.toStdString());
            } else { // ADD COLUMN
                QString working_param_str = parameters.trimmed();
                QString field_name, type_str_full, col_constraints_str;

                int first_space_idx_ac = working_param_str.indexOf(QRegularExpression(R"(\s)"));
                if (first_space_idx_ac == -1) {
                    throw std::runtime_error("ADD COLUMN syntax error: Missing type for column. Parameters: " + parameters.toStdString());
                }
                field_name = working_param_str.left(first_space_idx_ac);
                working_param_str = working_param_str.mid(first_space_idx_ac).trimmed();

                int type_param_paren_open_idx_ac = working_param_str.indexOf('(');
                int first_space_after_base_type_idx_ac = working_param_str.indexOf(QRegularExpression(R"(\s)"));
                if (type_param_paren_open_idx_ac != -1 &&
                    (first_space_after_base_type_idx_ac == -1 || type_param_paren_open_idx_ac < first_space_after_base_type_idx_ac)) {
                    int balance_ac = 0; int end_of_type_params_idx_ac = -1; bool in_str_lit_ac = false; QChar str_char_ac = ' ';
                    for (int i = type_param_paren_open_idx_ac; i < working_param_str.length(); ++i) {
                        QChar c = working_param_str[i];
                        if (c == '\'' || c == '"') { if (in_str_lit_ac && c == str_char_ac) {if (i>0 && working_param_str[i-1]=='\\'){} else in_str_lit_ac = false;} else if (!in_str_lit_ac) {in_str_lit_ac=true; str_char_ac=c;} }
                        else if (c == '(' && !in_str_lit_ac) balance_ac++;
                        else if (c == ')' && !in_str_lit_ac) { balance_ac--; if (balance_ac == 0 && i >= type_param_paren_open_idx_ac) { end_of_type_params_idx_ac = i; break; } }
                    }
                    if (end_of_type_params_idx_ac != -1) {
                        type_str_full = working_param_str.left(end_of_type_params_idx_ac + 1);
                        col_constraints_str = working_param_str.mid(end_of_type_params_idx_ac + 1).trimmed();
                    } else {
                        throw std::runtime_error(QString("Mismatched parentheses in ADD COLUMN type definition for '%1': '%2'").arg(field_name, working_param_str).toStdString());
                    }
                } else {
                    if (first_space_after_base_type_idx_ac != -1) {
                        type_str_full = working_param_str.left(first_space_after_base_type_idx_ac).trimmed();
                        col_constraints_str = working_param_str.mid(first_space_after_base_type_idx_ac).trimmed();
                    } else {
                        type_str_full = working_param_str.trimmed(); col_constraints_str = "";
                    }
                }

                QStringList auto_gen_constraints_alt; QString type_parse_error_alt;
                xhyfield::datatype type_alt = parseDataTypeAndParams(type_str_full, auto_gen_constraints_alt, type_parse_error_alt);
                if (!type_parse_error_alt.isEmpty()) {
                    throw std::runtime_error(QString("Error parsing type for new column '%1' ('%2'): %3").arg(field_name, type_str_full, type_parse_error_alt).toStdString());
                }
                QStringList user_defined_col_constraints_alt = parseConstraints(col_constraints_str);
                QStringList final_constraints_alt = auto_gen_constraints_alt + user_defined_col_constraints_alt;
                final_constraints_alt.removeDuplicates();

                qDebug() << "[handleAlterTable ADD] For field '" << field_name << "', type_str_full: '" << type_str_full << "'";
                qDebug() << "  Auto-generated constraints:" << auto_gen_constraints_alt;
                qDebug() << "  User-defined constraints:" << user_defined_col_constraints_alt;
                qDebug() << "  Final constraints for xhyfield:" << final_constraints_alt;

                xhyfield new_field_obj;
                if (type_alt == xhyfield::ENUM) {
                    QRegularExpression enum_values_re_alt(R"(ENUM\s*\((.+)\)\s*$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                    QRegularExpressionMatch enum_match_alt = enum_values_re_alt.match(type_str_full);
                    if (enum_match_alt.hasMatch()) {
                        QString enum_content_alt = enum_match_alt.captured(1).trimmed();
                        QStringList enum_vals_list_alt = parseSqlValues(enum_content_alt);
                        if (enum_vals_list_alt.isEmpty() && !enum_content_alt.isEmpty() && enum_content_alt != "''" && enum_content_alt != "\"\"") {
                            throw std::runtime_error(QString("Error: ENUM field '%1' has empty or malformed value list: %2").arg(field_name, enum_content_alt).toStdString());
                        }
                        xhyfield temp_enum_field(field_name, type_alt, final_constraints_alt);
                        temp_enum_field.set_enum_values(enum_vals_list_alt);
                        new_field_obj = temp_enum_field;
                    } else {
                        throw std::runtime_error(QString("Error: ENUM field '%1' definition invalid. Expected ENUM('val1',...). Got: '%2'").arg(field_name, type_str_full).toStdString());
                    }
                } else {
                    new_field_obj = xhyfield(field_name, type_alt, final_constraints_alt);
                }
                success = db_manager.add_column(current_db_name, table_name, new_field_obj);
                result_msg = success ? QString("Column '%1' added to table '%2'.").arg(field_name, table_name)
                                     : QString("Failed to add column '%1'. It might already exist or definition is invalid.").arg(field_name);
                if (!success) throw std::runtime_error(result_msg.toStdString());
            }
        } else if (action == "DROP") {
            // (您的 DROP COLUMN / DROP CONSTRAINT 逻辑，确保也使用 try-catch 和事务处理)
            if (parameters.toUpper().startsWith("CONSTRAINT")) {
                QRegularExpression dropConstrRe(R"(CONSTRAINT\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch dropMatch = dropConstrRe.match(parameters);
                if(!dropMatch.hasMatch()) throw std::runtime_error("Syntax Error: DROP CONSTRAINT <constraint_name>");
                QString constr_name = dropMatch.captured(1).trimmed();
                success = db_manager.drop_constraint(current_db_name, table_name, constr_name);
                result_msg = success ? QString("Constraint '%1' dropped.").arg(constr_name) : QString("Failed to drop constraint '%1'.").arg(constr_name);
            } else {
                QString field_name_to_drop = parameters.trimmed();
                if (field_name_to_drop.contains(QRegularExpression("\\s"))) throw std::runtime_error("Syntax Error: DROP COLUMN <column_name> (column name cannot contain spaces)");
                success = db_manager.drop_column(current_db_name, table_name, field_name_to_drop);
                result_msg = success ? QString("Column '%1' dropped from table '%2'.").arg(field_name_to_drop, table_name)
                                     : QString("Failed to drop column '%1'.").arg(field_name_to_drop);
            }
            if (!success) throw std::runtime_error(result_msg.toStdString());
        } else if (action == "ALTER" || action == "MODIFY") {
            // (您的 MODIFY COLUMN 逻辑，确保也使用新的类型解析和事务处理)
            // 这是一个复杂的操作，涉及到旧列查找、新类型解析、数据转换（如果类型改变）、约束更新等。
            // 下面是一个简化的骨架，您需要根据您的 db_manager.alter_column 实现来填充。
            QRegularExpression alterColRe(R"(([\w_]+)\s+(?:TYPE\s+)?([\w\s\(\),'"\-\\]+))", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch alterMatch = alterColRe.match(parameters);
            if(!alterMatch.hasMatch()) throw std::runtime_error("Syntax Error: ALTER/MODIFY COLUMN <col_name> <new_type_definition>");

            QString old_field_name = alterMatch.captured(1).trimmed();
            QString new_type_str_full = alterMatch.captured(2).trimmed();
            QStringList auto_gen_constraints_modify; QString type_parse_error_modify;
            xhyfield::datatype new_type = parseDataTypeAndParams(new_type_str_full, auto_gen_constraints_modify, type_parse_error_modify);
            if (!type_parse_error_modify.isEmpty()) throw std::runtime_error(QString("Error parsing new type for column '%1' ('%2'): %3").arg(old_field_name, new_type_str_full, type_parse_error_modify).toStdString());

            // 获取旧字段的非类型约束，与 auto_gen_constraints_modify 合并 -> final_constraints_modify
            // 这部分逻辑比较复杂，需要您根据实际情况实现
            // xhyfield modified_field_obj(old_field_name, new_type, final_constraints_modify);
            // ... (ENUM value setting if new_type is ENUM) ...
            // success = db_manager.alter_column(current_db_name, table_name, old_field_name, modified_field_obj);
            // result_msg = ...
            textBuffer.append("注意: ALTER/MODIFY COLUMN 的完整实现较为复杂，此处为简化结构。");
            success = false; // 暂时标记为不成功，直到您完成实现
            result_msg = "ALTER/MODIFY COLUMN not fully implemented yet.";
            if (!success) throw std::runtime_error(result_msg.toStdString());

        } else if (action == "RENAME") {
            // (您的 RENAME COLUMN / RENAME TABLE TO 逻辑)
            if (parameters.toUpper().startsWith("COLUMN")) {
                QRegularExpression renameColRe(R"(COLUMN\s+([\w_]+)\s+TO\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch renameMatch = renameColRe.match(parameters);
                if(!renameMatch.hasMatch()) throw std::runtime_error("Syntax Error: RENAME COLUMN <old_name> TO <new_name>");
                QString old_col_name = renameMatch.captured(1).trimmed();
                QString new_col_name = renameMatch.captured(2).trimmed();
                success = db_manager.rename_column(current_db_name, table_name, old_col_name, new_col_name);
                result_msg = success ? QString("Column '%1' renamed to '%2'.").arg(old_col_name, new_col_name)
                                     : QString("Failed to rename column '%1'.").arg(old_col_name);
            } else if (parameters.toUpper().startsWith("TO")) {
                QRegularExpression renameTableRe(R"(TO\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch renameMatch = renameTableRe.match(parameters);
                if(!renameMatch.hasMatch()) throw std::runtime_error("Syntax Error: RENAME TO <new_table_name>");
                QString new_table_name = renameMatch.captured(1).trimmed();
                success = db_manager.rename_table(current_db_name, table_name, new_table_name);
                result_msg = success ? QString("Table '%1' renamed to '%2'.").arg(table_name, new_table_name)
                                     : QString("Failed to rename table '%1'.").arg(table_name);
            } else {
                throw std::runtime_error("Unsupported RENAME syntax.");
            }
            if (!success) throw std::runtime_error(result_msg.toStdString());
        } else {
            throw std::runtime_error(("Unsupported ALTER TABLE action: " + action).toStdString());
        }

        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                textBuffer.append(result_msg + " Transaction committed.");
            } else {
                textBuffer.append(result_msg + " BUT Transaction commit FAILED. Database might be inconsistent.");
            }
        } else {
            textBuffer.append(result_msg + " (executed within existing transaction or no transaction).");
        }

    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("ALTER TABLE Error: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("An unknown error occurred during ALTER TABLE.");
    }
}


void MainWindow::handleExplainSelect(const QString& command) {
    QRegularExpression re(R"(EXPLAIN\s+SELECT\s+\*\s+FROM\s+([\w_]+)(?:\s+WHERE\s+(.+))?\s*;?)",
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        textBuffer.append("语法错误: EXPLAIN SELECT * FROM <表名> [WHERE <条件>]");
        return;
    }

    QString tableName = match.captured(1).trimmed();
    QString wherePart = match.captured(2).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) { textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }
    xhytable* table = db->find_table(tableName);
    if (!table) { textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(tableName, current_db_name)); return; }

    ConditionNode conditionRootForExplain;
    if (!parseWhereClause(wherePart, conditionRootForExplain)) { // **Aufruf**
        return;
    }

    QString firstFieldNameInCondition;
    if (conditionRootForExplain.type == ConditionNode::COMPARISON_OP) {
        firstFieldNameInCondition = conditionRootForExplain.comparison.fieldName;
    } else if (conditionRootForExplain.type == ConditionNode::LOGIC_OP && !conditionRootForExplain.children.isEmpty()) {
        const ConditionNode* firstComparableChild = nullptr;
        QStack<const ConditionNode*> stack;
        stack.push(&conditionRootForExplain);
        while(!stack.isEmpty()){
            const ConditionNode* current = stack.pop();
            if(current->type == ConditionNode::COMPARISON_OP){
                firstComparableChild = current;
                break;
            }
            for(const auto& child : current->children) stack.push(&child);
        }
        if(firstComparableChild) firstFieldNameInCondition = firstComparableChild->comparison.fieldName;
    }

    if (!firstFieldNameInCondition.isEmpty()) {
        const xhyindex* index = db->findIndex(firstFieldNameInCondition);
        if (index && index->tableName().compare(tableName, Qt::CaseInsensitive) == 0) {
            textBuffer.append(QString("查询计划: 可能使用字段 '%1' 上的索引 '%2'。").arg(firstFieldNameInCondition, index->name()));
        } else {
            textBuffer.append(QString("查询计划: 字段 '%1' 上没有直接可用的索引，或索引不属于表 '%2'。可能进行全表扫描。").arg(firstFieldNameInCondition, tableName));
        }
    } else if (!wherePart.isEmpty()){
        textBuffer.append("查询计划: 无法从WHERE条件中简单提取用于索引检查的字段，可能进行全表扫描。");
    } else {
        textBuffer.append("查询计划: 无WHERE条件，将进行全表扫描。");
    }

    textBuffer.append("模拟执行查询以验证...");
    QVector<xhyrecord> results;
    if (db_manager.selectData(current_db_name, tableName, conditionRootForExplain, results)) {
        QString header_str;
        for (const auto& field : table->fields()) { header_str += field.name() + "\t"; }
        textBuffer.append(header_str.trimmed());
        textBuffer.append(QString(header_str.length()*2 < 80 ? header_str.length()*2 : 80, '-'));
        if (results.isEmpty()) {
            textBuffer.append("(无符合条件的数据)");
        } else {
            for (const auto& record : results) {
                QString row_str;
                for (const auto& field : table->fields()) { row_str += record.value(field.name()) + "\t"; }
                textBuffer.append(row_str.trimmed());
            }
            textBuffer.append(QString("共 %1 行结果。").arg(results.size()));
        }
    } else {
        textBuffer.append("执行 EXPLAIN 的模拟查询时出错。");
    }
}


void MainWindow::handleCreateIndex(const QString& command) {
    QRegularExpression re(R"(CREATE\s+(UNIQUE\s+)?INDEX\s+([\w_]+)\s+ON\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) {
        textBuffer.append("语法错误: CREATE [UNIQUE] INDEX <索引名> ON <表名> (<列1>[, <列2>...])");
        return;
    }
    bool unique = !match.captured(1).trimmed().isEmpty();
    QString idxname = match.captured(2).trimmed();
    QString tablename = match.captured(3).trimmed();
    QString colsPart = match.captured(4).trimmed();
    QStringList cols = colsPart.split(',', Qt::SkipEmptyParts);

    for(auto &c : cols) { c = c.trimmed().split(" ").first(); if (c.isEmpty()) {textBuffer.append("错误：索引列名不能为空。"); return;} }
    if (cols.isEmpty()){ textBuffer.append("错误：索引必须至少包含一列。"); return; }

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    auto* db = db_manager.find_database(current_db_name);
    if (!db) { textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }

    xhytable* table = db->find_table(tablename);
    if (!table) { textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(tablename, current_db_name)); return; }

    for(const auto &col : cols) {
        if (!table->has_field(col)) {
            textBuffer.append(QString("错误: 字段 '%1' 在表 '%2' 中不存在。").arg(col, tablename)); return;
        }
    }

    if(db->createIndex(xhyindex(idxname, tablename, cols, unique))) {
        textBuffer.append(QString("索引 '%1' 在表 '%2' 上创建成功。").arg(idxname, tablename));
    } else {
        textBuffer.append(QString("错误：创建索引 '%1' 失败 (可能已存在或名称/列定义无效)。").arg(idxname));
    }
}

void MainWindow::handleDropIndex(const QString& command) {
    QRegularExpression re(R"(DROP\s+INDEX\s+([\w_]+)(?:\s+ON\s+([\w_]+))?\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if(!match.hasMatch()){
        textBuffer.append("语法错误: DROP INDEX <索引名> [ON <表名>]");
        return;
    }
    QString idxname = match.captured(1).trimmed();
    QString onTableName = match.captured(2).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
    auto* db = db_manager.find_database(current_db_name);
    if(!db){ textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return;}

    if(!onTableName.isEmpty()){
        const xhyindex* idxToDrop = db->findIndexByName(idxname);
        if(idxToDrop && idxToDrop->tableName().compare(onTableName, Qt::CaseInsensitive) != 0){
            textBuffer.append(QString("错误: 索引 '%1' 不属于表 '%2'。").arg(idxname, onTableName));
            return;
        }
    }

    if(db->dropIndex(idxname)) {
        textBuffer.append(QString("索引 '%1' 已删除。").arg(idxname));
    } else {
        textBuffer.append(QString("错误：删除索引 '%1' 失败 (可能不存在)。").arg(idxname));
    }
}

void MainWindow::handleShowIndexes(const QString& command) {
    QString filterTableName;
    QRegularExpression re(R"(SHOW\s+INDEXES\s+(?:FROM|ON)\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if(match.hasMatch()){
        filterTableName = match.captured(1).trimmed();
    }

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
    auto* db = db_manager.find_database(current_db_name);
    if(!db){ textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return;}

    QList<xhyindex> indexes = db->allIndexes();
    if(indexes.isEmpty()){
        textBuffer.append("数据库 '" + current_db_name + "' 中没有索引。");
        return;
    }

    QString output = "索引列表 (数据库: " + current_db_name;
    if(!filterTableName.isEmpty()) output += ", 表: " + filterTableName;
    output += "):\n";
    output += QString("%1\t%2\t%3\t%4\n").arg("索引名", -20).arg("表名", -20).arg("列名", -30).arg("是否唯一");
    output += QString(80, '-') + "\n";

    bool foundAny = false;
    for(const auto& idx : indexes){
        if (filterTableName.isEmpty() || idx.tableName().compare(filterTableName, Qt::CaseInsensitive) == 0) {
            output += QString("%1\t%2\t%3\t%4\n")
            .arg(idx.name(), -20)
                .arg(idx.tableName(), -20)
                .arg(idx.columns().join(", "), -30)
                .arg(idx.isUnique() ? "是" : "否");
            foundAny = true;
        }
    }

    if (!foundAny) {
        if (!filterTableName.isEmpty()) {
            textBuffer.append(QString("表 '%1' 上没有找到索引。").arg(filterTableName));
        } else {
            textBuffer.append("当前数据库没有符合条件的索引。");
        }
    } else {
        textBuffer.append(output);
    }
}

QStringList MainWindow::parseSqlValues(const QString &input_raw) {
    QString input = input_raw.trimmed();
    QStringList values;
    QString current_value;
    bool in_string_literal = false;
    QChar quote_char = QChar::Null;

    for (int i = 0; i < input.length(); ++i) {
        QChar ch = input[i];

        if (in_string_literal) {
            if (ch == quote_char) {
                if (i + 1 < input.length() && input[i + 1] == quote_char) {
                    current_value += quote_char;
                    i++;
                } else {
                    in_string_literal = false;
                }
            } else {
                current_value += ch;
            }
        } else {
            if (ch == '\'' || ch == '"') {
                in_string_literal = true;
                quote_char = ch;
            } else if (ch == ',') {
                values.append(current_value.trimmed());
                current_value.clear();
            } else {
                current_value += ch;
            }
        }
    }
    values.append(current_value.trimmed());
    return values;
}

xhyfield::datatype MainWindow::parseDataType(const QString& type_str_input, int* size_ptr) {
    QString type_str = type_str_input.trimmed();
    QString upperType = type_str.toUpper();
    if (size_ptr) *size_ptr = 0;

    QRegularExpression typeWithSizeRe(R"((VARCHAR|CHAR|DECIMAL|NUMERIC)\s*\(\s*(\d+)(?:\s*,\s*(\d+))?\s*\))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression typeOnlyRe(R"([\w_]+)");

    QRegularExpressionMatch match = typeWithSizeRe.match(type_str);
    QString actualType;
    int size1 = 0;

    if (match.hasMatch()) {
        actualType = match.captured(1).toUpper();
        size1 = match.captured(2).toInt();
        if (size_ptr) *size_ptr = size1;
    } else {
        match = typeOnlyRe.match(upperType);
        if(match.hasMatch()){
            actualType = match.captured(0);
        } else {
            qWarning() << "无法解析数据类型：" << type_str;
            return xhyfield::VARCHAR;
        }
    }

    if (actualType == "INT" || actualType == "INTEGER") return xhyfield::INT;
    if (actualType == "TINYINT") return xhyfield::TINYINT;
    if (actualType == "SMALLINT") return xhyfield::SMALLINT;
    if (actualType == "BIGINT") return xhyfield::BIGINT;
    if (actualType == "FLOAT") return xhyfield::FLOAT;
    if (actualType == "DOUBLE" || actualType == "REAL") return xhyfield::DOUBLE;
    if (actualType == "DECIMAL" || actualType == "NUMERIC") {
        return xhyfield::DECIMAL;
    }
    if (actualType == "CHAR") { if(size_ptr && size1 == 0) *size_ptr = 1; return xhyfield::CHAR;}
    if (actualType == "VARCHAR") { if(size_ptr && size1 == 0) *size_ptr = 255; return xhyfield::VARCHAR;}
    if (actualType == "TEXT") return xhyfield::TEXT;
    if (actualType == "DATE") return xhyfield::DATE;
    if (actualType == "DATETIME") return xhyfield::DATETIME;
    if (actualType == "TIMESTAMP") return xhyfield::TIMESTAMP;
    if (actualType == "BOOL" || actualType == "BOOLEAN") return xhyfield::BOOL;
    if (actualType == "ENUM") return xhyfield::ENUM;

    qWarning() << "未知的数据类型：" << actualType << " (来自: " << type_str_input << ")";
    return xhyfield::VARCHAR;
}

QStringList MainWindow::parseConstraints(const QString& constraints_str_input) {
    QStringList parsed_constraints_for_xhyfield;
    QString current_str = constraints_str_input.trimmed();
    int current_pos = 0;

    while (current_pos < current_str.length()) {
        current_str = current_str.mid(current_pos).trimmed();
        current_pos = 0;
        if (current_str.isEmpty()) break;

        QString upper_current_str = current_str.toUpper();
        bool constraint_found_in_iteration = false;

        if (upper_current_str.startsWith("NOT NULL")) {
            parsed_constraints_for_xhyfield.append("NOT_NULL");
            current_pos = QString("NOT NULL").length();
            constraint_found_in_iteration = true;
        }
        else if (upper_current_str.startsWith("PRIMARY KEY")) {
            parsed_constraints_for_xhyfield.append("PRIMARY_KEY");
            current_pos = QString("PRIMARY KEY").length();
            constraint_found_in_iteration = true;
        }
        else if (upper_current_str.startsWith("UNIQUE")) {
            parsed_constraints_for_xhyfield.append("UNIQUE");
            current_pos = QString("UNIQUE").length();
            constraint_found_in_iteration = true;
        }
        else if (upper_current_str.startsWith("DEFAULT ")) {
            parsed_constraints_for_xhyfield.append("DEFAULT");
            QString default_keyword_part = current_str.left(QString("DEFAULT ").length());
            QString value_part_candidate = current_str.mid(default_keyword_part.length()).trimmed();

            QRegularExpression re_next_constraint_or_end(
                // 使用标准字符串，转义 \ 和 "
                "^\\s*(?:('([^']*(?:''[^']*)*)')|(\"([^\"]*(?:\"\"[^\"]*)*)\")|([^\\s,()]+))(\\s+|$)",
                QRegularExpression::CaseInsensitiveOption
                );
            QRegularExpressionMatch val_match = re_next_constraint_or_end.match(value_part_candidate);

            if (val_match.hasMatch() && val_match.capturedStart() == 0) {
                QString actual_default_value_str = val_match.captured(1).trimmed();
                QVariant parsed_literal = parseLiteralValue(actual_default_value_str);

                QString stored_default_val = parsed_literal.isNull() ? "NULL" : parsed_literal.toString();
                parsed_constraints_for_xhyfield.append(stored_default_val);
                current_pos = default_keyword_part.length() + val_match.capturedLength(0);
            } else {
                qWarning() << "警告: DEFAULT 关键字后未能正确解析默认值：" << value_part_candidate;
                current_pos = default_keyword_part.length();
            }
            constraint_found_in_iteration = true;
        }
        // ** 【核心修改点：处理 CHECK 约束】 **
        else if (upper_current_str.startsWith("CHECK ")) { // 匹配 "CHECK " (带空格)
            QString check_keyword_part_original_case = current_str.left(QString("CHECK ").length()); // 保留原始 "CHECK " 的长度
            QString expr_part_candidate = current_str.mid(check_keyword_part_original_case.length()).trimmed(); // 表达式部分，去除前后空格

            if (expr_part_candidate.startsWith('(')) {
                int paren_balance = 0;
                int expr_end_idx = -1; // 括号内表达式在 expr_part_candidate 中的结束索引
                bool in_check_str_literal = false;
                QChar check_str_char = ' ';

                for (int i = 0; i < expr_part_candidate.length(); ++i) {
                    QChar c = expr_part_candidate.at(i);
                    if (c == '\'' || c == '"') {
                        if (in_check_str_literal && c == check_str_char) {
                            if (i > 0 && expr_part_candidate.at(i-1) == '\\') {}
                            else in_check_str_literal = false;
                        } else if (!in_check_str_literal) {
                            in_check_str_literal = true; check_str_char = c;
                        }
                    } else if (c == '(' && !in_check_str_literal) {
                        paren_balance++;
                    } else if (c == ')' && !in_check_str_literal) {
                        paren_balance--;
                        if (paren_balance == 0 && i > 0) { // 确保不是空的 ()
                            expr_end_idx = i; // ')' 的索引
                            break;
                        }
                    }
                }

                if (expr_end_idx != -1 && paren_balance == 0) {
                    // 提取括号内的原始表达式，并去除其内部的前后空格
                    QString raw_expression_in_parens = expr_part_candidate.mid(1, expr_end_idx - 1).trimmed();

                    // 构建规范化的 CHECK 约束字符串: "CHECK(EXPRESSION)"
                    QString normalized_check_constraint = QString("CHECK(%1)").arg(raw_expression_in_parens);

                    parsed_constraints_for_xhyfield.append(normalized_check_constraint.toUpper()); // 存储为大写的规范格式

                    // 更新 current_pos 以跳过已处理的 "CHECK (expression)" 部分
                    current_pos = check_keyword_part_original_case.length() + expr_end_idx + 1;
                        // (expr_end_idx 是在 expr_part_candidate 中的索引，所以 +1 包含 ')' )
                } else {
                    qWarning() << "警告: CHECK 约束括号不匹配或表达式不完整：" << expr_part_candidate;
                    current_pos = check_keyword_part_original_case.length(); // 至少消耗 "CHECK "
                }
            } else {
                qWarning() << "警告: CHECK 关键字后缺少括号表达式：" << expr_part_candidate;
                current_pos = check_keyword_part_original_case.length(); // 至少消耗 "CHECK "
            }
            constraint_found_in_iteration = true;
        }
        // ** 【CHECK 约束处理结束】 **

        if (!constraint_found_in_iteration && !current_str.isEmpty()) {
            int next_space = current_str.indexOf(' ');
            QString unknown_token = (next_space == -1) ? current_str : current_str.left(next_space);
            qWarning() << "警告: 在列约束字符串中遇到未知或格式错误的标记：" << unknown_token;
            current_pos = unknown_token.length();
            if (current_pos == 0) current_pos = current_str.length();
        }
        if (current_pos == 0 && !current_str.isEmpty()) {
            qWarning() << "错误：parseConstraints 可能进入死循环，当前字符串：" << current_str;
            break;
        }
    }
    return parsed_constraints_for_xhyfield;
}


bool MainWindow::validateCheckExpression(const QString& expression) {
    // 简确保括号匹配
    int balance = 0;
    for (QChar c : expression) {
        if (c == '(') balance++;
        else if (c == ')') balance--;
        if (balance < 0) return false; // 括号不匹配
    }
    return balance == 0;
}

void MainWindow::handleTableConstraint(const QString &constraint_str_input, xhytable &table) {
    QString constraint_str = constraint_str_input.trimmed();
    QRegularExpression re_named(R"(CONSTRAINT\s+([\w_]+)\s+(PRIMARY\s+KEY|UNIQUE|CHECK|FOREIGN\s+KEY)\s*(?:\((.+?)\))?(.*))", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression re_unnamed(R"((PRIMARY\s+KEY|UNIQUE|CHECK|FOREIGN\s+KEY)\s*\((.+?)\)(.*))", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatch match = re_named.match(constraint_str);
    QString constraintName;
    QString constraintTypeStr;
    QString columnsPart;
    QString remainingPart;

    if (match.hasMatch()) {
        constraintName = match.captured(1).trimmed();
        constraintTypeStr = match.captured(2).trimmed().toUpper().replace(" ", "_");
        columnsPart = match.captured(3).trimmed();
        remainingPart = match.captured(4).trimmed();
    } else {
        match = re_unnamed.match(constraint_str);
        if (match.hasMatch()) {
            constraintTypeStr = match.captured(1).trimmed().toUpper().replace(" ", "_");
            columnsPart = match.captured(2).trimmed();
            remainingPart = match.captured(3).trimmed();
        } else {
            textBuffer.append("错误: 无效的表级约束定义: " + constraint_str);
            return;
        }
    }

    QStringList columns;
    if(!columnsPart.isEmpty() && (constraintTypeStr == "PRIMARY_KEY" || constraintTypeStr == "UNIQUE" || constraintTypeStr == "FOREIGN_KEY")){
        columns = columnsPart.split(',', Qt::SkipEmptyParts);
        for(QString& col : columns) col = col.trimmed();
        if (constraintName.isEmpty() && !columns.isEmpty()) {
            constraintName = constraintTypeStr + "_" + table.name();
            for(const QString& col : columns) constraintName += "_" + col;
        }
    }
    if (constraintName.isEmpty() && constraintTypeStr == "CHECK") {
        constraintName = constraintTypeStr + "_" + table.name() + "_auto_chk" + QString::number(table.fields().size() +1);
    }


    if (constraintTypeStr == "PRIMARY_KEY") {
        if (columns.isEmpty()) { textBuffer.append("错误: PRIMARY KEY 约束必须指定列。"); return; }
        table.add_primary_key(columns);
        textBuffer.append(QString("表约束 '%1' (PRIMARY KEY on %2) 已添加。").arg(constraintName.isEmpty() ? "auto_pk" : constraintName, columns.join(",")));
    } else if (constraintTypeStr == "UNIQUE") {
        if (columns.isEmpty()) { textBuffer.append("错误: UNIQUE 约束必须指定列。"); return; }
        table.add_unique_constraint(columns, constraintName);
        textBuffer.append(QString("表约束 '%1' (UNIQUE on %2) 已添加。").arg(constraintName, columns.join(",")));
    } else if (constraintTypeStr == "CHECK") {
        if (columnsPart.isEmpty()) { textBuffer.append("错误: CHECK 约束必须指定条件表达式 (in Klammern)."); return; }
        table.add_check_constraint(columnsPart, constraintName);
        textBuffer.append(QString("表约束 '%1' (CHECK %2) 已添加。").arg(constraintName, columnsPart));
    } else if (constraintTypeStr == "FOREIGN_KEY") {
        if (columns.isEmpty()) { textBuffer.append("错误: FOREIGN KEY 约束必须指定列。"); return; }

        QRegularExpression fkRefRe(R"(REFERENCES\s+([\w_]+)\s*\(([\w_,\s]+)\))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch fkMatch = fkRefRe.match(remainingPart);
        if (!fkMatch.hasMatch()) {
            textBuffer.append("错误: FOREIGN KEY 约束缺少有效的 REFERENCES 子句。"); return;
        }
        QString referencedTable = fkMatch.captured(1).trimmed();
        QStringList referencedColumnsList;
        QString referencedColumnsPart = fkMatch.captured(2).trimmed();
        referencedColumnsList = referencedColumnsPart.split(',', Qt::SkipEmptyParts);
        for(QString& rcol : referencedColumnsList) rcol = rcol.trimmed();

        if (columns.size() != referencedColumnsList.size()) {
            textBuffer.append("错误: FOREIGN KEY 列数量与引用的列数量不匹配。"); return;
        }

        // 调用修改后的 add_foreign_key 方法，传入列列表
        table.add_foreign_key(columns, referencedTable, referencedColumnsList, constraintName); // <-- 修改此处！
        textBuffer.append(QString("表约束 '%1' (FOREIGN KEY (%2) REFERENCES %3(%4)) 已添加。")
                              .arg(constraintName, columns.join(", "), referencedTable, referencedColumnsList.join(", ")));

    } else {
        textBuffer.append("错误: 不支持的表约束类型: " + constraintTypeStr);
    }
}


void MainWindow::show_databases() {
    auto databases = db_manager.databases();
    if (databases.isEmpty()) {
        textBuffer.append("没有数据库。");
        return;
    }
    QSet<QString> uniqueNames;
    for (const auto& db : databases) {
        uniqueNames.insert(db.name());
    }
    QString output = "数据库列表:\n";
    for (const QString& name : uniqueNames) {
        output += "  " + name + "\n";
    }
    textBuffer.append(output.trimmed());
}

void MainWindow::show_tables(const QString& db_name_input) {
    QString db_name = db_name_input.trimmed();
    if (db_name.isEmpty()) {
        textBuffer.append("错误: 未指定数据库名，或当前未选择数据库。");
        return;
    }
    xhydatabase* db = db_manager.find_database(db_name);
    if (!db) {
        textBuffer.append(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        return;
    }
    auto tables = db->tables();
    if (tables.isEmpty()) {
        textBuffer.append(QString("数据库 '%1' 中没有表。").arg(db_name));
        return;
    }
    QString output = QString("数据库 '%1' 中的表:\n").arg(db_name);
    for (const auto& table : tables) {
        output += "  " + table.name() + "\n";
    }
    textBuffer.append(output.trimmed());
}



namespace { // 使用匿名命名空间，使这两个函数仅在本文件可见


int visualLength(const QString& str) {
    int length = 0;
    for (QChar qc : str) {
        char16_t c = qc.unicode();
        // 这个范围是一个简化的判断，主要覆盖中日韩统一表意文字等常见的东亚宽字符。
        // 更精确的判断可能需要更复杂的Unicode属性库。
        if ((c >= 0x2E80 && c <= 0x9FFF) || // CJK Radicals Supplement, CJK Unified Ideographs, etc.
            (c >= 0xAC00 && c <= 0xD7A3) || // Hangul Syllables
            (c >= 0xF900 && c <= 0xFAFF) || // CJK Compatibility Ideographs
            (c >= 0xFF00 && c <= 0xFFEF)    // Fullwidth Forms
            ) {
            length += 2;
        } else {
            length += 1;
        }
    }
    return length;
}

// 将字符串左对齐填充到指定的视觉宽度
QString padToVisualWidth(const QString& str, int targetVisualWidth) {
    QString paddedStr = str;
    int currentVisualLen = visualLength(str);
    if (currentVisualLen < targetVisualWidth) {
        paddedStr += QString(targetVisualWidth - currentVisualLen, ' ');
    }
    // 如果需要，可以添加截断逻辑：
    else if (currentVisualLen > targetVisualWidth) {
        // 简单的截断 (可能不理想，因为它不考虑字符边界)
         paddedStr = str.left(targetVisualWidth - 3) + "...";
    }
    return paddedStr;
}

}
void MainWindow::show_schema(const QString& db_name_input, const QString& table_name_input) {
    QString db_name = db_name_input.trimmed();
    QString table_name = table_name_input.trimmed();

    if (db_name.isEmpty()) { textBuffer.append("错误: 未指定数据库名。"); return; }
    if (table_name.isEmpty()) { textBuffer.append("错误: 未指定表名。"); return; }

    xhydatabase* db = db_manager.find_database(db_name);
    if (!db) {
        textBuffer.append(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        return;
    }
    xhytable* table = db->find_table(table_name); // 修改为非 const 指针以调用非 const 方法 defaultValues() 和 foreignKeys()


    if (!table) {
        textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, db_name));
        return;
    }
    QString output = QString("表 '%1.%2' 的结构:\n").arg(db_name, table_name);
    output += QString("%1\t%2\t%3\n").arg("列名", -20).arg("类型", -20).arg("约束");
    output += QString(80, '-') + "\n"; // 增加分隔符长度以适应更长的约束字符串

    const QMap<QString, QString>& default_values_map = table->defaultValues();
    const QStringList& primary_keys_list = table->primaryKeys();
    const QSet<QString>& not_null_fields_set = table->notNullFields();

    for (const auto& field : table->fields()) {
        QStringList display_constraints_for_field;

        // 1. 从字段对象获取原始约束 (例如 CHECK, UNIQUE(列级), SIZE, PRECISION, SCALE)
        display_constraints_for_field.append(field.constraints());

        // 2. 处理主键约束标记
        if (primary_keys_list.contains(field.name(), Qt::CaseInsensitive)) {
            // 如果是主键成员，确保 PRIMARY_KEY 和 NOT_NULL 标记存在
            if (!display_constraints_for_field.contains("PRIMARY_KEY", Qt::CaseInsensitive)) {
                display_constraints_for_field.prepend("PRIMARY_KEY"); // 放在前面，更显眼
            }
            // 主键列必然非空。如果m_notNullFields不包含它（例如表级PK未更新m_notNullFields），也应显示NOT_NULL
            if (!not_null_fields_set.contains(field.name()) && !display_constraints_for_field.contains("NOT_NULL", Qt::CaseInsensitive)) {
                display_constraints_for_field.append("NOT_NULL");
            }
        }

        // 3. 处理明确的 NOT NULL 约束 (如果不是主键但有NOT NULL)
        if (not_null_fields_set.contains(field.name())) {
            if (!display_constraints_for_field.contains("NOT_NULL", Qt::CaseInsensitive) &&
                !primary_keys_list.contains(field.name(), Qt::CaseInsensitive)) { // 避免重复添加NOT_NULL
                display_constraints_for_field.append("NOT_NULL");
            }
        }

        // 4. 处理默认值
        if (default_values_map.contains(field.name())) {
            // 避免重复添加 DEFAULT (如果 field.constraints() 中已包含)
            // 简单的检查方法是查看是否已经有以 "DEFAULT " 开头的约束
            bool defaultExists = false;
            for(const QString& constr : display_constraints_for_field) {
                if (constr.startsWith("DEFAULT ", Qt::CaseInsensitive)) {
                    defaultExists = true;
                    break;
                }
            }
            if (!defaultExists) {
                display_constraints_for_field.append("DEFAULT " + default_values_map.value(field.name()));
            }
        }

        // 移除可能因上述逻辑引入的重复项 (例如 "NOT_NULL" 可能来自 field.constraints() 和 m_notNullFields)
        display_constraints_for_field.removeDuplicates();


        output += QString("%1\t%2\t%3\n")
                      .arg(field.name(), -20)
                      .arg(field.typestring(), -20) // typestring() 已包含如 VARCHAR(100), DECIMAL(10,2)
                      .arg(display_constraints_for_field.join(" "));
    }

    // 显示表级主键信息 (已有的逻辑)
    if (!primary_keys_list.isEmpty()) {
        output += "\nPRIMARY KEY: (" + primary_keys_list.join(", ") + ")\n";
    }

    // ***** 新增：显示外键信息 *****
    const QList<ForeignKeyDefinition>& foreign_keys_list = table->foreignKeys();
    if (!foreign_keys_list.isEmpty()) {
        output += "\nFOREIGN KEYS:\n";
        for (const auto& fkDef : foreign_keys_list) {
            QStringList childCols, parentCols;

            for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
                childCols.append(it.key());
                parentCols.append(it.value());
            }
            output += QString("  CONSTRAINT %1 FOREIGN KEY (%2) REFERENCES %3 (%4)\n")
                          .arg(fkDef.constraintName)
                          .arg(childCols.join(", "))
                          .arg(fkDef.referenceTable)
                          .arg(parentCols.join(", "));
        }
    }
    // ***** 结束新增外键信息 *****
    const QMap<QString, QString>& check_constraints_map = table->checkConstraints();
    if (!check_constraints_map.isEmpty()) {
        output += "\nCHECK CONSTRAINTS:\n"; // 添加一个清晰的标题
        for (auto it = check_constraints_map.constBegin(); it != check_constraints_map.constEnd(); ++it) {
            // it.key() 是约束名, it.value() 是约束表达式
            output += QString("  CONSTRAINT %1 CHECK (%2)\n")
                          .arg(it.key())
                          .arg(it.value());
        }
    }
    textBuffer.append(output.trimmed());
}


// ENDE von mainwindow.cpp
///////////////////////////////////////////////////
//GUI

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
    QWidget* widget = ui->tabWidget->widget(index);
    ui->tabWidget->removeTab(index);
    widget->deleteLater();
}

void MainWindow::openRegisterUserDialog()
{
    CreateUserDialog createUserDialog(&Account,&db_manager,this); // 创建注册用户对话框实例
    createUserDialog.exec();
}


void MainWindow::dataSearch(){
    GUI_dbms.clear();
    for(xhydatabase database: db_manager.databases()){
        Database gui_database = {database.name()};

        QStringList gui_tables;
        QStringList gui_views;
        QStringList gui_functions;
        QStringList gui_queries;

        for(xhytable table : database.tables()){
            gui_tables.append(table.name());
        }
        gui_database.tables=gui_tables;


        GUI_dbms.append(gui_database);
    }

    // GUI_dbms = {
    //     {
    //         "数据库1",
    //         {"student", "teacher"},  // 表
    //         {"view1", "view2"},      // 视图
    //         {"func1"},              // 函数
    //         {"query1", "query2"}    // 查询
    //     },
    //     {
    //         "数据库2",
    //         {"order", "product"},    // 表
    //         {"sales_view"},         // 视图
    //         {"calculate_total"},    // 函数
    //         {}      // 查询
    //     }
    // };
}

void MainWindow::buildTree(){
    ui->treeWidget->clear();

    for (const Database &db : GUI_dbms) {
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

    for (const Database &db : GUI_dbms) {

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
    if(current_GUI_Db == nullptr || item->text(0) != current_GUI_Db){
        current_GUI_Db = item->text(0);
        qDebug() << "数据库改变";

        updateList(current_GUI_Db);
    }
}

void MainWindow::handleItemDoubleClicked(QTreeWidgetItem *item, int column){
    if(item->parent() == nullptr){

        return;
    }
    QString parentText = item->parent()->text(0);
    if(parentText == "表"){

        QString tablename = item->text(0);
        openTable(tablename);
        //tableshow->

    }else if(parentText == "视图"){

    }else if(parentText == "函数"){

    }else if(parentText == "查询"){

    }
}

void MainWindow::openTable(QString tableName){
    for(int i=0; i < ui->tabWidget->count(); ++i){
        if(tableName+" @"+current_GUI_Db == ui->tabWidget->tabText(i)){
            return;
        }
    }
    tableShow *tableshow = new tableShow;
    ui->tabWidget->addTab(tableshow,tableName+" @"+current_GUI_Db);
    ui->tabWidget->setCurrentWidget(tableshow);

    for(xhydatabase database : db_manager.databases()){
        if(database.name() == current_GUI_Db){
            for(xhytable table : database.tables()){
                if(table.name() == tableName){

                    tableshow->setTable(table);
                    return ;
                }
            }
        }
    }
}

void MainWindow::handleString(const QString& text, queryWidget* query){
    current_query = query;
    QString input = text;
    QStringList commands = SQLParser::parseMultiLineSQL(input); // SQLParser::静态调用

    current_query->clear();

    for (const QString& command_const : commands) {
        QString trimmedCmd = command_const.trimmed();
        if (!trimmedCmd.isEmpty() && trimmedCmd.endsWith(';')) {
            textBuffer.append("> " + trimmedCmd);
            execute_command(trimmedCmd);
            textBuffer.append("");
        } else if (!trimmedCmd.isEmpty()){
            textBuffer.append("! 忽略未以分号结尾的语句: " + trimmedCmd);
        }
    }
    QString remaining = input.trimmed().section(';', -1).trimmed();
    if(!remaining.isEmpty()) {
        bool alreadyProcessedOrEmpty = commands.isEmpty();
        if (!commands.isEmpty()) {
            QString lastProcessedCmdFull = commands.last().trimmed();
            QString lastProcessedCmdNoSemi = lastProcessedCmdFull;
            if (lastProcessedCmdNoSemi.endsWith(';')) {
                lastProcessedCmdNoSemi.chop(1);
            }
            if (lastProcessedCmdNoSemi == remaining || lastProcessedCmdFull == remaining) {
                alreadyProcessedOrEmpty = true;
            }
        }
        if (!alreadyProcessedOrEmpty) {
            bool isPartOfProcessed = false;
            for(const QString& cmd : commands) {
                if (cmd.trimmed() == remaining + ";" || cmd.trimmed() == remaining) {
                    isPartOfProcessed = true;
                    break;
                }
            }
            if (!isPartOfProcessed) {
                textBuffer.append("错误: 检测到未完成的SQL语句（末尾缺少分号）: " + remaining);
            }
        }
    }
    for(const QString& msg : textBuffer ){
        query->appendPlainText(msg);
    }
    textBuffer.clear();
    dataSearch();
    buildTree();
    updateList(current_GUI_Db);
}

void MainWindow::on_addQuery_released()
{
    queryWidget *query = new queryWidget;
    ui->tabWidget->addTab(query,"新建查询");
    connect(query,&queryWidget::sendString,[=](const QString& text){
        handleString(text,query);
    });
    ui->tabWidget->setCurrentWidget(query);
    query->putin_focus();
}

// 由zyh新增，用于handleSelect里的聚组函数
// 修改后 - MainWindow::calculateAggregate函数
QVariant MainWindow::calculateAggregate(const QString& function, const QString& column, const QVector<xhyrecord>& records) {
    // 特殊处理COUNT(*)
    if (function == "COUNT" && (column == "*" || column.isEmpty())) {
        return records.size();
    }

    // 列名为空但不是COUNT(*)的情况
    if (column.isEmpty() && function != "COUNT") {
        return QVariant();
    }

    if (function == "COUNT") {
        int count = 0;
        for (const auto& record : records) {
            if (!record.value(column).isEmpty()) {
                count++;
            }
        }
        return count;
    } else if (function == "SUM") {
        double sum = 0.0;
        bool valid = false;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                sum += val;
                valid = true;
            }
        }
        return valid ? QVariant(sum) : QVariant();
    } else if (function == "AVG") {
        double sum = 0.0;
        int count = 0;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                sum += val;
                count++;
            }
        }
        if (count > 0) {
            double avg = sum / count;
            // 使用std::round四舍五入到两位小数
            avg = std::round(avg * 100) / 100;
            return QVariant(avg);
        }
        return QVariant();
    } else if (function == "MIN") {
        double min = std::numeric_limits<double>::max();
        bool found = false;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                min = qMin(min, val);
                found = true;
            }
        }
        return found ? QVariant(min) : QVariant();
    } else if (function == "MAX") {
        double max = std::numeric_limits<double>::lowest();
        bool found = false;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                max = qMax(max, val);
                found = true;
            }
        }
        return found ? QVariant(max) : QVariant();
    }

    return QVariant(); // NULL for unsupported functions
}

// 同上
bool MainWindow::isNumeric(const QString& str) const {
    bool ok;
    str.toDouble(&ok);
    return ok;
}

// 同上
// 实现从.h移动到.cpp的removeTableAlias函数
QString MainWindow::removeTableAlias(const QString& col, const QString& table_alias, const QString& table_name) {
    QString result = col.trimmed();
    if (!table_alias.isEmpty() && result.startsWith(table_alias + ".")) {
        result = result.mid(table_alias.length() + 1);
    } else {
        int dotPos = result.indexOf('.');
        if (dotPos != -1) {
            QString prefix = result.left(dotPos);
            QString colNameOnly = result.mid(dotPos + 1);
            if (prefix.compare(table_name, Qt::CaseInsensitive) == 0) {
                result = colNameOnly;
            }
        }
    }
    return result;
}
