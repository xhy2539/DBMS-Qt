#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "xhyfield.h"
#include "xhyrecord.h"
#include "xhytable.h"
#include "xhydatabase.h"
#include "xhyindex.h"
#include "ConditionNode.h" // 确保这个 include 存在
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
// mainwindow.cpp
void MainWindow::handleCreateTable(QString& command) { // Consider const QString& if command is not modified
    QString processedCommand = command.replace(QRegularExpression(R"(\s+)"), " ").trimmed();
    QRegularExpression re(R"(CREATE\s+TABLE\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(processedCommand);

    if (!match.hasMatch()) {
        textBuffer.append("Syntax Error: CREATE TABLE <TableName> (<col1> <type1> [constraints], ...)");
        return;
    }
    QString table_name = match.captured(1).trimmed();
    QString fields_and_constraints_str = match.captured(2).trimmed();
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
            textBuffer.append("Error: Failed to start transaction for CREATE TABLE.");
            return;
        }
    }

    xhytable new_table(table_name);
    QStringList definitions;
    int parenLevel = 0;
    QString currentDef;
    bool inStringLiteral = false;
    QChar stringChar = ' ';

    for (QChar c : fields_and_constraints_str) {
        if (c == '\'' || c == '"') {
            if (inStringLiteral && c == stringChar) {
                if (!currentDef.isEmpty() && currentDef.endsWith('\\')) { currentDef.append(c); }
                else { inStringLiteral = false; currentDef.append(c); }
            } else if (!inStringLiteral) {
                inStringLiteral = true; stringChar = c; currentDef.append(c);
            } else { currentDef.append(c); }
        } else if (c == '(' && !inStringLiteral) {
            parenLevel++; currentDef.append(c);
        } else if (c == ')' && !inStringLiteral) {
            parenLevel--; currentDef.append(c);
        } else if (c == ',' && parenLevel == 0 && !inStringLiteral) {
            definitions.append(currentDef.trimmed()); currentDef.clear();
        } else { currentDef.append(c); }
    }
    if (!currentDef.isEmpty()) definitions.append(currentDef.trimmed());

    if (definitions.isEmpty()) {
        textBuffer.append("Error: No column definitions or constraints found in CREATE TABLE statement.");
        if (transactionStartedHere) db_manager.rollbackTransaction();
        return;
    }

    for (const QString& def_str_const : definitions) {
        QString def_str = def_str_const.trimmed();
        if (def_str.isEmpty()) continue;

        if (def_str.toUpper().startsWith("CONSTRAINT")) {
            handleTableConstraint(def_str, new_table);
        } else { // Column definition
            QString working_def_str = def_str.trimmed();
            QString field_name, type_str_full, col_constraints_str;

            int first_space_idx = working_def_str.indexOf(QRegularExpression(R"(\s)"));
            if (first_space_idx == -1) {
                textBuffer.append("Error: Column definition incomplete (missing type): '" + def_str + "'.");
                if (transactionStartedHere) db_manager.rollbackTransaction(); return;
            }
            field_name = working_def_str.left(first_space_idx);
            working_def_str = working_def_str.mid(first_space_idx).trimmed();

            int type_param_paren_open_idx = working_def_str.indexOf('(');
            int first_space_after_potential_type_name = working_def_str.indexOf(QRegularExpression(R"(\s)"));

            if (type_param_paren_open_idx != -1 &&
                (first_space_after_potential_type_name == -1 || type_param_paren_open_idx < first_space_after_potential_type_name)) {
                int balance = 0; int end_of_type_params_idx = -1; bool in_str_lit_param = false; QChar str_char_param = ' ';
                for (int i = type_param_paren_open_idx; i < working_def_str.length(); ++i) {
                    QChar c = working_def_str[i];
                    if (c == '\'' || c == '"') { if (in_str_lit_param && c == str_char_param) { if (i > 0 && working_def_str[i-1] == '\\') {} else in_str_lit_param = false; } else if (!in_str_lit_param) {in_str_lit_param = true; str_char_param = c;} }
                    else if (c == '(' && !in_str_lit_param) balance++;
                    else if (c == ')' && !in_str_lit_param) { balance--; if (balance == 0 && i >= type_param_paren_open_idx) { end_of_type_params_idx = i; break; } }
                }
                if (end_of_type_params_idx != -1) {
                    type_str_full = working_def_str.left(end_of_type_params_idx + 1);
                    col_constraints_str = working_def_str.mid(end_of_type_params_idx + 1).trimmed();
                } else {
                    textBuffer.append(QString("Error: Mismatched parentheses in type definition for field '%1': '%2'").arg(field_name, working_def_str));
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
                textBuffer.append(QString("Error for field '%1': Type '%2' parsing failed - %3").arg(field_name, type_str_full, type_parse_error));
                if (transactionStartedHere) db_manager.rollbackTransaction(); return;
            }

            QStringList user_defined_col_constraints = parseConstraints(col_constraints_str);
            QStringList final_constraints = auto_generated_type_constraints;
            final_constraints.append(user_defined_col_constraints);
            final_constraints.removeDuplicates();

            qDebug() << "[handleCreateTable] For field '" << field_name << "', type_str_full: '" << type_str_full << "'";
            qDebug() << "  Auto-generated constraints:" << auto_generated_type_constraints;
            qDebug() << "  User-defined constraints:" << user_defined_col_constraints;
            qDebug() << "  Final constraints for xhyfield:" << final_constraints;


            if (type == xhyfield::ENUM) {
                QRegularExpression enum_values_re(R"(ENUM\s*\((.+)\)\s*$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                QRegularExpressionMatch enum_match = enum_values_re.match(type_str_full);
                if (enum_match.hasMatch()) {
                    QString enum_content = enum_match.captured(1).trimmed();
                    QStringList enum_vals_list = parseSqlValues(enum_content);
                    if (enum_vals_list.isEmpty() && !enum_content.isEmpty() && enum_content != "''" && enum_content != "\"\"") {
                        textBuffer.append(QString("Error: ENUM field '%1' has empty or malformed value list: %2").arg(field_name, enum_content));
                        if(transactionStartedHere) db_manager.rollbackTransaction(); return;
                    }
                    xhyfield new_field_enum(field_name, type, final_constraints);
                    new_field_enum.set_enum_values(enum_vals_list);
                    new_table.addfield(new_field_enum);
                } else {
                    textBuffer.append(QString("Error: ENUM field '%1' definition invalid. Expected ENUM('val1',...). Got: '%2'").arg(field_name, type_str_full));
                    if (transactionStartedHere) db_manager.rollbackTransaction(); return;
                }
            } else {
                xhyfield new_field(field_name, type, final_constraints);
                new_table.addfield(new_field);
            }
        }
    }

    if (new_table.fields().isEmpty() && !fields_and_constraints_str.contains("CONSTRAINT", Qt::CaseInsensitive)) {
        textBuffer.append("Error: Table must have at least one column or a table-level constraint.");
        if (transactionStartedHere) db_manager.rollbackTransaction(); return;
    }

    if (db_manager.createtable(current_db_name, new_table)) {
        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                textBuffer.append(QString("Table '%1' created successfully in database '%2'. Transaction committed.").arg(table_name, current_db_name));
            } else {
                textBuffer.append(QString("Table '%1' created (in memory) but transaction commit FAILED for database '%2'.").arg(table_name, current_db_name));
            }
        } else {
            textBuffer.append(QString("Table '%1' created successfully in database '%2' (within existing transaction or no transaction).").arg(table_name, current_db_name));
        }
    } else {
        textBuffer.append(QString("Error: Failed to create table '%1' (it might already exist or definition is invalid).").arg(table_name));
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

void MainWindow::handleSelect(const QString& command) {
    QRegularExpression re(
        R"(SELECT\s+(.+?)\s+FROM\s+([\w\.]+)(?:\s+WHERE\s+(.+))?\s*;?)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) {
        textBuffer.append("语法错误: SELECT <列名1,...|*> FROM <表名> [WHERE <条件>]");
        return;
    }

    QString select_cols_str = match.captured(1).trimmed();
    QString table_name = match.captured(2).trimmed();
    QString where_part = match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) { textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }
    xhytable* table = db->find_table(table_name);
    if (!table) { textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, current_db_name)); return; }

    ConditionNode conditionRoot;
    if (!parseWhereClause(where_part, conditionRoot)) { return; } // **Aufruf**

    QVector<xhyrecord> results;
    if (!db_manager.selectData(current_db_name, table_name, conditionRoot, results)) {
        return;
    }

    if (results.isEmpty()) {
        textBuffer.append("查询结果为空。");
        return;
    }

    QStringList display_columns;
    if (select_cols_str == "*") {
        for (const auto& field : table->fields()) {
            display_columns.append(field.name());
        }
    } else {
        display_columns = select_cols_str.split(',', Qt::SkipEmptyParts);
        for (QString& col : display_columns) {
            col = col.trimmed();
            if (!table->has_field(col)) {
                textBuffer.append(QString("错误: 选择的列 '%1' 在表 '%2' 中不存在。").arg(col, table_name));
                return;
            }
        }
    }

    QString header_str;
    for (const QString& col_name : display_columns) { header_str += col_name + "\t"; }
    textBuffer.append(header_str.trimmed());
    textBuffer.append(QString(header_str.length()*2 < 80 ? header_str.length()*2 : 80, '-'));

    for (const auto& record : results) {
        QString row_str;
        for (const QString& col_name : display_columns) {
            row_str += record.value(col_name) + "\t";
        }
        textBuffer.append(row_str.trimmed());
    }
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
    QStringList constraints;
    if (constraints_str_input.isEmpty()) return constraints;

    int paren_level = 0;
    QString constraints_str = constraints_str_input.trimmed();
    QStringList tokens = constraints_str.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    for (int i = 0; i < tokens.size(); ++i) {
        QString token = tokens[i].toUpper();
        if (token == "PRIMARY" && i + 1 < tokens.size() && tokens[i+1].toUpper() == "KEY") {
            constraints.append("PRIMARY_KEY");
            i++;
        } else if (token == "FOREIGN" && i + 1 < tokens.size() && tokens[i+1].toUpper() == "KEY") {
            constraints.append("FOREIGN_KEY");
            i++;
        } else if (token == "NOT" && i + 1 < tokens.size() && tokens[i+1].toUpper() == "NULL") {
            constraints.append("NOT_NULL");
            i++;
        } else if (token == "CHECK") {
            QString check_expr = tokens[i];
            i++;
            if (i < tokens.size() && tokens[i].startsWith('(')) {
                check_expr += " " + tokens[i];
                paren_level = 0;
                for (int k = 0; k < tokens[i].length(); ++k) {
                    if(tokens[i][k] == '(') paren_level++;
                    else if(tokens[i][k] == ')') paren_level--;
                }
                while (paren_level > 0 && i + 1 < tokens.size()) {
                    i++;
                    check_expr += " " + tokens[i];
                    for (int k = 0; k < tokens[i].length(); ++k) {
                        if(tokens[i][k] == '(') paren_level++;
                        else if(tokens[i][k] == ')') paren_level--;
                    }
                }
                if (paren_level != 0) {qWarning() << "Fehlende Klammer im CHECK-Constraint: " << check_expr; }
            } else {
                qWarning() << "CHECK-Constraint ohne Bedingung in Klammern: " << tokens[i-1];
            }
            constraints.append(check_expr);
        }
        else {
            if (token.startsWith("SIZE(") && token.endsWith(")")) {
                constraints.append(token);
            } else {
                constraints.append(token);
            }
        }
    }
    return constraints;
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
    xhytable* table = db->find_table(table_name);
    if (!table) {
        textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, db_name));
        return;
    }
    QString output = QString("表 '%1.%2' 的结构:\n").arg(db_name, table_name);
    output += QString("%1\t%2\t%3\n").arg("列名", -20).arg("类型", -20).arg("约束");
    output += QString(60, '-') + "\n";
    for (const auto& field : table->fields()) {
        output += QString("%1\t%2\t%3\n")
        .arg(field.name(), -20)
            .arg(field.typestring(), -20)
            .arg(field.constraints().join(" "));
    }
    if (!table->primaryKeys().isEmpty()) {
        output += "\nPRIMARY KEY: (" + table->primaryKeys().join(", ") + ")\n";
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

