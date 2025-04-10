#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QRegularExpression>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Mini DBMS");
    db_manager.load_databases_from_files();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_run_clicked()
{
    QString input = ui->putin->toPlainText();
    QStringList commands = sqlParser.parseMultiLineSQL(input);

    ui->show->clear();

    for (const QString& command : commands) {
        QString trimmedCmd = command.trimmed();
        if (!trimmedCmd.isEmpty()) {
            ui->show->appendPlainText("> " + trimmedCmd);
            execute_command(trimmedCmd);
            ui->show->appendPlainText("");
        }
    }
}

void MainWindow::execute_command( QString& command)
{
    if (command.isEmpty()) return;

    try {
        QString cmdUpper = command.toUpper();

        if (cmdUpper.startsWith("CREATE DATABASE")) {
            handleCreateDatabase(command);
        }
        else if (cmdUpper.startsWith("SHOW DATABASES")) {
            show_databases();
        }
        else if (cmdUpper.startsWith("USE")) {
            handleUseDatabase(command);
        }
        else if (cmdUpper.startsWith("CREATE TABLE")) {
            handleCreateTable(command);
        }
        else if (cmdUpper.startsWith("INSERT INTO")) {
            handleInsert(command);
        }
        else if (cmdUpper.startsWith("SHOW TABLES")) {
            show_tables(db_manager.get_current_database());
        }
        else if (cmdUpper.startsWith("DESCRIBE")) {
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
        else if (cmdUpper.startsWith("DELETE")) {
            handleDelete(command);
        }
        else if (cmdUpper.startsWith("SELECT")) {
            handleSelect(command);
        }
        else {
            ui->show->appendPlainText("无法识别的命令: " + command);
        }
    } catch (const std::exception& e) {
        ui->show->appendPlainText("错误: " + QString(e.what()));
    }
}

// 数据库操作
void MainWindow::handleCreateDatabase(const QString& command) {
    QRegularExpression re("CREATE DATABASE\\s+(\\w+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);

    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.createdatabase(db_name)) {
            ui->show->appendPlainText(QString("数据库 '%1' 创建成功").arg(db_name));
        } else {
            ui->show->appendPlainText(QString("错误: 数据库 '%1' 已存在").arg(db_name));
        }
    } else {
        ui->show->appendPlainText("语法错误: CREATE DATABASE <数据库名>");
    }
}

void MainWindow::handleUseDatabase(const QString& command) {
    QRegularExpression re("USE\\s+(\\w+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);

    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.use_database(db_name)) {
            ui->show->appendPlainText(QString("已切换到数据库 '%1'").arg(db_name));
        } else {
            ui->show->appendPlainText(QString("错误: 数据库 '%1' 不存在").arg(db_name));
        }
    } else {
        ui->show->appendPlainText("语法错误: USE <数据库名>");
    }
}

void MainWindow::handleDropDatabase(const QString& command) {
    QRegularExpression re("DROP DATABASE\\s+(\\w+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);

    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.dropdatabase(db_name)) {
            ui->show->appendPlainText(QString("数据库 '%1' 已删除").arg(db_name));
        } else {
            ui->show->appendPlainText(QString("错误: 数据库 '%1' 不存在").arg(db_name));
        }
    } else {
        ui->show->appendPlainText("语法错误: DROP DATABASE <数据库名>");
    }
}

// 表操作
void MainWindow::handleCreateTable( QString& command) {
    // 手动处理换行符，将换行符替换为空格
    QString processedCommand = command.replace(QRegularExpression("\\s+"), " ");

    QRegularExpression re("CREATE TABLE\\s+(\\w+)\\s*\\((.*)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(processedCommand);

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: CREATE TABLE <表名> (<列定义>)");
        return;
    }

    QString table_name = match.captured(1);
    QString fields_str = match.captured(2);
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    xhytable new_table(table_name);
    // 将字段字符串按逗号分隔，并处理换行符
    QStringList fields = fields_str.split(',', Qt::SkipEmptyParts);

    for (const auto& field_str : fields) {
        QRegularExpression field_re("(\\w+)\\s+(\\w+)(?:\\((\\d+)\\))?(?:\\s+(.+))?", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch field_match = field_re.match(field_str.trimmed());

        if (!field_match.hasMatch()) {
            ui->show->appendPlainText("错误: 列定义必须包含名称和类型");
            return;
        }

        QString field_name = field_match.captured(1);
        QString type_str = field_match.captured(2).toUpper();
        QString size = field_match.captured(3); // 捕获字段长度(如CHAR(1)中的1)
        QStringList constraints = field_match.captured(4).split(' ', Qt::SkipEmptyParts);

        xhyfield::datatype type;
        if (type_str == "INT") type = xhyfield::INT;
        else if (type_str == "VARCHAR") type = xhyfield::VARCHAR;
        else if (type_str == "FLOAT") type = xhyfield::FLOAT;
        else if (type_str == "DATE") type = xhyfield::DATE;
        else if (type_str == "BOOL") type = xhyfield::BOOL;
        else if (type_str == "CHAR") {
            type = xhyfield::CHAR;
            if (size.isEmpty()) {
                ui->show->appendPlainText("错误: CHAR类型必须指定长度，如CHAR(1)");
                return;
            }
            constraints.prepend("SIZE(" + size + ")"); // 将长度作为特殊约束存储
        }
        else {
            ui->show->appendPlainText(QString("错误: 不支持的类型 '%1'").arg(type_str));
            return;
        }

        new_table.addfield(xhyfield(field_name, type, constraints));
    }
    if (db_manager.createtable(current_db, new_table)) {
        ui->show->appendPlainText(QString("表 '%1' 创建成功").arg(table_name));
    } else {
        ui->show->appendPlainText(QString("错误: 表 '%1' 已存在").arg(table_name));
    }
}
void MainWindow::handleDropTable(const QString& command) {
    QRegularExpression re("DROP TABLE\\s+(\\w+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: DROP TABLE <表名>");
        return;
    }

    QString table_name = match.captured(1);
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    if (db_manager.droptable(current_db, table_name)) {
        ui->show->appendPlainText(QString("表 '%1' 已删除").arg(table_name));
    } else {
        ui->show->appendPlainText(QString("错误: 表 '%1' 不存在").arg(table_name));
    }
}

void MainWindow::handleDescribe(const QString& command) {
    QRegularExpression re("DESCRIBE\\s+(\\w+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: DESCRIBE <表名>");
        return;
    }

    QString table_name = match.captured(1);
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    show_schema(current_db, table_name);
}

// 数据操作
void MainWindow::handleInsert(const QString& command) {
    QRegularExpression re(
        "INSERT\\s+INTO\\s+(\\w+)\\s*(?:\\(([^\\)]+)\\))?\\s*VALUES\\s*\\(([^\\)]+)\\)\\s*(?:,\\s*\\(([^\\)]+)\\))*\\s*;?",
        QRegularExpression::CaseInsensitiveOption
        );

    QRegularExpressionMatch match = re.match(command);
    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: INSERT INTO 表名 [(列名,...)] VALUES (值,...)[,(值,...)...]");
        return;
    }

    QString table_name = match.captured(1);
    QString fields_part = match.captured(2);
    QString first_values_part = match.captured(3);
    QString additional_values = match.captured(4);
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    // 解析字段列表
    QStringList fields;
    if (!fields_part.isEmpty()) {
        fields = fields_part.split(',', Qt::SkipEmptyParts);
        for (auto& field : fields) {
            field = field.trimmed();
        }
    }

    // 收集所有值组
    QStringList all_values_parts;
    all_values_parts << first_values_part;
    if (!additional_values.isEmpty()) {
        // 处理多个VALUES子句
        QRegularExpression values_re("\\(([^\\)]+)\\)");
        QRegularExpressionMatchIterator it = values_re.globalMatch(command);
        it.next(); // 跳过第一个已捕获的值
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            all_values_parts << m.captured(1);
        }
    }

    int total_inserted = 0;

    // 处理每个VALUES子句
    for (const QString& values_part : all_values_parts) {
        // 解析值列表
        QStringList values = parseSqlValues(values_part);

        // 验证字段和值数量
        if (!fields.isEmpty() && fields.size() != values.size()) {
            ui->show->appendPlainText("错误: 列数与值数不匹配");
            continue;
        }

        // 构建字段-值映射
        QMap<QString, QString> fieldValues;
        if (fields.isEmpty()) {
            // 如果没有指定字段，使用表的所有字段
            xhydatabase* db = db_manager.find_database(current_db);
            if (!db) {
                ui->show->appendPlainText("错误: 数据库不存在");
                return;
            }

            xhytable* table = db->find_table(table_name);
            if (!table) {
                ui->show->appendPlainText(QString("错误: 表 '%1' 不存在").arg(table_name));
                return;
            }

            auto tableFields = table->fields();
            if (tableFields.size() != values.size()) {
                ui->show->appendPlainText("错误: 值数与表列数不匹配");
                continue;
            }

            for (int i = 0; i < tableFields.size(); ++i) {
                fieldValues[tableFields[i].name()] = values[i];
            }
        } else {
            for (int i = 0; i < fields.size(); ++i) {
                fieldValues[fields[i]] = values[i];
            }
        }

        // 执行插入
        if (db_manager.insertData(current_db, table_name, fieldValues)) {
            total_inserted++;
        } else {
            ui->show->appendPlainText("错误: 插入数据失败");
        }
    }

    if (total_inserted > 0) {
        ui->show->appendPlainText(QString("%1 行数据已插入").arg(total_inserted));
    }
}

void MainWindow::handleUpdate(const QString& command) {
    // 改进后的正则表达式，支持更复杂的SET和WHERE子句
    QRegularExpression re(
        "UPDATE\\s+(\\w+)\\s+SET\\s+([^\\s=]+\\s*=\\s*(?:'[^']*'|\"[^\"]*\"|\\S+)(?:\\s*,\\s*[^\\s=]+\\s*=\\s*(?:'[^']*'|\"[^\"]*\"|\\S+))*)\\s*(?:WHERE\\s+(.+))?\\s*;?",
        QRegularExpression::CaseInsensitiveOption
        );

    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: UPDATE 表名 SET 列1=值1[,列2=值2...] [WHERE 条件]");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString set_part = match.captured(2).trimmed();
    QString where_part = match.captured(3).trimmed();
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    // 解析SET部分
    QMap<QString, QString> updates;
    QStringList set_pairs = set_part.split(',', Qt::SkipEmptyParts);
    for (const QString& pair : set_pairs) {
        // 改进的键值对解析，支持各种空格情况和复杂表达式
        QRegularExpression key_value_re(R"(([\w\d_]+)\s*=\s*(('[^']*')|("[^"]*")|([^,]+)))");
        QRegularExpressionMatch key_value_match = key_value_re.match(pair.trimmed());
        if (!key_value_match.hasMatch()) {
            ui->show->appendPlainText(QString("语法错误: 无效的SET表达式 '%1'").arg(pair));
            return;
        }

        QString key = key_value_match.captured(1).trimmed();
        QString value = key_value_match.captured(2).trimmed();

        // 处理引号包围的值
        if ((value.startsWith('\'') && value.endsWith('\'')) ||
            (value.startsWith('"') && value.endsWith('"'))) {
            value = value.mid(1, value.length() - 2);
        }
        // 处理NULL值
        else if (value.compare("NULL", Qt::CaseInsensitive) == 0) {
            value = "NULL";
        }
        // 处理布尔值
        else if (value.compare("TRUE", Qt::CaseInsensitive) == 0) {
            value = "1";
        }
        else if (value.compare("FALSE", Qt::CaseInsensitive) == 0) {
            value = "0";
        }

        updates[key] = value;
    }

    // 解析WHERE条件
    QMap<QString, QString> conditions;
    if (!where_part.isEmpty()) {
        if (!parseWhereClause(where_part, conditions)) {
            ui->show->appendPlainText("错误: 无效的WHERE条件");
            return;
        }
    }

    // 执行更新
    try {
        int affected = db_manager.updateData(current_db, table_name, updates, conditions);
        ui->show->appendPlainText(QString("%1 行数据已更新").arg(affected));
    } catch (const std::exception& e) {
        ui->show->appendPlainText(QString("更新失败: %1").arg(e.what()));
    }
}


void MainWindow::handleDelete(const QString& command) {
    // 改进后的正则表达式，支持更复杂的WHERE子句
    QRegularExpression re(
        "DELETE\\s+FROM\\s+(\\w+)\\s*(?:WHERE\\s+(.+))?\\s*;?",
        QRegularExpression::CaseInsensitiveOption
        );

    QRegularExpressionMatch match = re.match(command);
    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: DELETE FROM 表名 [WHERE 条件]");
        return;
    }

    QString table_name = match.captured(1);
    QString where_part = match.captured(2);
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    // 检查表是否存在
    xhydatabase* db = db_manager.find_database(current_db);
    if (!db) {
        ui->show->appendPlainText("错误: 数据库不存在");
        return;
    }

    xhytable* table = db->find_table(table_name);
    if (!table) {
        ui->show->appendPlainText(QString("错误: 表 '%1' 不存在").arg(table_name));
        return;
    }

    // 如果没有WHERE条件，确认是否要删除所有数据
    if (where_part.isEmpty()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "确认",
                                      QString("确定要删除表 '%1' 中的所有数据吗?").arg(table_name),
                                      QMessageBox::Yes|QMessageBox::No);

        if (reply != QMessageBox::Yes) {
            ui->show->appendPlainText("删除操作已取消");
            return;
        }
    }

    // 解析WHERE条件
    QMap<QString, QString> conditions;
    if (!where_part.isEmpty() && !parseWhereClause(where_part, conditions)) {
        ui->show->appendPlainText("错误: 无效的WHERE条件");
        return;
    }

    // 执行删除
    try {
        int affected = db_manager.deleteData(current_db, table_name, conditions);
        ui->show->appendPlainText(QString("%1 行数据已删除").arg(affected));
    } catch (const std::exception& e) {
        ui->show->appendPlainText(QString("删除失败: %1").arg(e.what()));
    }
}


void MainWindow::handleSelect(const QString& command) {
    // 简单实现，只支持SELECT * FROM table [WHERE conditions]
    QRegularExpression re(
        "SELECT\\s+\\*\\s+FROM\\s+(\\w+)(?:\\s+WHERE\\s+(.+))?\\s*;?",
        QRegularExpression::CaseInsensitiveOption
        );

    QRegularExpressionMatch match = re.match(command);
    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: SELECT * FROM 表名 [WHERE 条件]");
        return;
    }

    QString table_name = match.captured(1);
    QString where_part = match.captured(2);
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    // 解析WHERE条件
    QMap<QString, QString> conditions;
    if (!where_part.isEmpty() && !parseWhereClause(where_part, conditions)) {
        ui->show->appendPlainText("错误: 无效的WHERE条件");
        return;
    }

    // 执行查询
    QVector<xhyrecord> results;
    if (!db_manager.selectData(current_db, table_name, conditions, results)) {
        ui->show->appendPlainText("错误: 查询失败");
        return;
    }

    // 显示结果
    if (results.isEmpty()) {
        ui->show->appendPlainText("查询结果为空");
        return;
    }

    // 获取表结构以显示列名
    xhydatabase* db = db_manager.find_database(current_db);
    if (!db) {
        ui->show->appendPlainText("错误: 数据库不存在");
        return;
    }

    xhytable* table = db->find_table(table_name);
    if (!table) {
        ui->show->appendPlainText(QString("错误: 表 '%1' 不存在").arg(table_name));
        return;
    }

    // 显示列名
    QString header;
    for (const auto& field : table->fields()) {
        header += field.name() + "\t";
    }
    ui->show->appendPlainText(header.trimmed());

    // 显示数据
    for (const auto& record : results) {
        QString row;
        for (const auto& field : table->fields()) {
            row += record.value(field.name()) + "\t";
        }
        ui->show->appendPlainText(row.trimmed());
    }
}

// 辅助函数
    QStringList MainWindow::parseSqlValues(const QString& input) {
        QStringList values;
        QString current;
        bool inQuotes = false;
        QChar quoteChar;

        for (int i = 0; i < input.length(); ++i) {
            QChar ch = input[i];

            if (inQuotes) {
                if (ch == quoteChar) {
                    // 检查转义引号
                    if (i > 0 && input[i-1] == '\\') {
                        current.chop(1);
                        current += ch;
                    } else {
                        inQuotes = false;
                    }
                } else {
                    current += ch;
                }
            } else {
                if (ch == '\'' || ch == '"') {
                    inQuotes = true;
                    quoteChar = ch;
                } else if (ch == ',' && current.isEmpty()) {
                    continue; // 跳过空值前的逗号
                } else if (ch == ',' && !inQuotes) {
                    values.append(current.trimmed());
                    current.clear();
                } else {
                    current += ch;
                }
            }
        }

        if (!current.isEmpty()) {
            values.append(current.trimmed());
        }

        return values;
    }

    bool MainWindow::parseWhereClause(const QString& whereStr, QMap<QString, QString>& conditions) {
        // 支持更复杂的条件表达式，包括子查询和函数调用
        QStringList tokens;
        QString currentToken;
        bool inQuotes = false;
        QChar quoteChar;
        int parenDepth = 0; // 支持嵌套括号

        // 使用状态机解析WHERE子句
        for (int i = 0; i < whereStr.length(); ++i) {
            QChar ch = whereStr[i];

            // 处理转义字符
            if (ch == '\\' && i+1 < whereStr.length()) {
                currentToken += whereStr[++i];
                continue;
            }

            // 处理引号内的内容
            if (inQuotes) {
                currentToken += ch;
                if (ch == quoteChar) {
                    inQuotes = false;
                }
                continue;
            }

            // 处理括号
            if (ch == '(') {
                parenDepth++;
                currentToken += ch;
                continue;
            } else if (ch == ')') {
                if (parenDepth > 0) {
                    parenDepth--;
                    currentToken += ch;
                    continue;
                }
                return false; // 不匹配的右括号
            }

            // 处理引号开始
            if (ch == '\'' || ch == '"') {
                inQuotes = true;
                quoteChar = ch;
                currentToken += ch;
                continue;
            }

            // 处理逻辑运算符（不破坏括号内的内容）
            if (parenDepth == 0) {
                // 检查AND/OR（不区分大小写）
                if (whereStr.mid(i, 3).compare("AND", Qt::CaseInsensitive) == 0) {
                    if (!currentToken.isEmpty()) {
                        tokens.append(currentToken.trimmed());
                        currentToken.clear();
                    }
                    i += 2; // 跳过AND
                    continue;
                } else if (whereStr.mid(i, 2).compare("OR", Qt::CaseInsensitive) == 0) {
                    if (!currentToken.isEmpty()) {
                        tokens.append(currentToken.trimmed());
                        currentToken.clear();
                    }
                    i += 1; // 跳过OR
                    continue;
                }
            }

            currentToken += ch;
        }

        // 添加最后一个token
        if (!currentToken.isEmpty()) {
            tokens.append(currentToken.trimmed());
        }

        // 检查括号是否匹配
        if (parenDepth != 0 || inQuotes) {
            return false;
        }

        // 解析每个条件表达式
        for (const QString& token : tokens) {
            // 支持的操作符：=, !=, <>, >, <, >=, <=, LIKE, NOT LIKE, IN, NOT IN, BETWEEN, IS NULL, IS NOT NULL
            QRegularExpression re(
                R"(([\w\d_]+|\(.*\))\s*)"
                R"((=|!=|<>|>|<|>=|<=|LIKE|NOT\s+LIKE|IN|NOT\s+IN|BETWEEN|IS\s+NULL|IS\s+NOT\s+NULL)\s*)"
                R"((?:('(?:[^']|'')*'|"(?:[^"]|"")*"|[\w\d_]+|\(.*\)|NULL)?))",
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
                );

            QRegularExpressionMatch match = re.match(token);
            if (!match.hasMatch()) {
                return false;
            }

            QString field = match.captured(1).trimmed();
            QString op = match.captured(2).trimmed().toUpper().replace(" ", "_"); // 将空格替换为下划线
            QString value = match.captured(3).trimmed();

            // 处理NULL相关操作
            if (op == "IS_NULL" || op == "IS_NOT_NULL") {
                conditions.insert(field, op);
                continue;
            }

            // 处理无值的操作符
            if (value.isEmpty()) {
                return false;
            }

            // 处理引号包围的值
            if ((value.startsWith('\'') && value.endsWith('\'')) ||
                (value.startsWith('"') && value.endsWith('"'))) {
                value = value.mid(1, value.length() - 2);
                // 处理转义引号
                value.replace("''", "'").replace("\"\"", "\"");
            }

            // 处理特殊操作符
            if (op == "LIKE" || op == "NOT_LIKE") {
                conditions.insert(field, op + " " + value);
            }
            else if (op == "IN" || op == "NOT_IN") {
                if (value.startsWith('(') && value.endsWith(')')) {
                    QStringList inValues = parseSqlValues(value.mid(1, value.length()-2));
                    conditions.insert(field, op + " (" + inValues.join(",") + ")");
                } else {
                    return false;
                }
            }
            else if (op == "BETWEEN") {
                QStringList parts = value.split(" AND ");
                if (parts.size() != 2) {
                    return false;
                }
                conditions.insert(field, "BETWEEN " + parts[0] + " AND " + parts[1]);
            }
            else {
                // 普通比较运算符
                conditions.insert(field, op + " " + value);
            }
        }

        return !conditions.isEmpty();
    }

void MainWindow::show_databases() {
    auto databases = db_manager.databases();
    if (databases.isEmpty()) {
        ui->show->appendPlainText("没有数据库");
        return;
    }

    QString output = "数据库列表:\n";
    for (const auto& db : databases) {
        output += " - " + db.name() + "\n";
    }
    ui->show->appendPlainText(output);
}

void MainWindow::show_tables(const QString& db_name) {
    if (db_name.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    auto databases = db_manager.databases();
    for (const auto& db : databases) {
        if (db.name() == db_name) {
            auto tables = db.tables();
            if (tables.isEmpty()) {
                ui->show->appendPlainText(QString("数据库 '%1' 中没有表").arg(db_name));
                return;
            }

            QString output = QString("数据库 '%1' 中的表:\n").arg(db_name);
            for (const auto& table : tables) {
                output += " - " + table.name() + "\n";
            }
            ui->show->appendPlainText(output);
            return;
        }
    }
    ui->show->appendPlainText(QString("数据库 '%1' 不存在").arg(db_name));
}

void MainWindow::show_schema(const QString& db_name, const QString& table_name) {
    auto databases = db_manager.databases();
    for (const auto& db : databases) {
        if (db.name() == db_name) {
            auto tables = db.tables();
            for (const auto& table : tables) {
                if (table.name() == table_name) {
                    QString output = QString("表 '%1' 的结构:\n").arg(table_name);
                    for (const auto& field : table.fields()) {
                        output += QString(" - %1 %2 %3\n")
                        .arg(field.name())
                            .arg(field.typestring())
                            .arg(field.constraints().join(" "));
                    }
                    ui->show->appendPlainText(output);
                    return;
                }
            }
            ui->show->appendPlainText(QString("表 '%1' 不存在").arg(table_name));
            return;
        }
    }
    ui->show->appendPlainText(QString("数据库 '%1' 不存在").arg(db_name));
}
