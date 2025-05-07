#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QRegularExpression>
#include <QDebug>
#include <QStack>
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
        // 再次确认以分号结尾（双重检查）
        if (!trimmedCmd.isEmpty() && trimmedCmd.endsWith(';')) {
            ui->show->appendPlainText("> " + trimmedCmd);
            execute_command(trimmedCmd);
            ui->show->appendPlainText("");
        } else {
            ui->show->appendPlainText("! 忽略未以分号结尾的语句: " + trimmedCmd);
        }
    }

    // 如果有未处理的剩余内容（不以分号结尾）
    QString remaining = input.section(';', -1).trimmed();
    if(!remaining.isEmpty()) {
        ui->show->appendPlainText("错误: 检测到未完成的SQL语句（缺少分号）: " + remaining);
    }
}

void MainWindow::execute_command(QString& command)
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
        else if (cmdUpper.startsWith("ALTER TABLE")) {
            handleAlterTable(command);
        }
        else if (cmdUpper.startsWith("BEGIN")) {
            db_manager.beginTransaction();
            ui->show->appendPlainText("事务开始");
        }
        else if (cmdUpper.startsWith("COMMIT")) {
            if (db_manager.commitTransaction()) {
                ui->show->appendPlainText("事务提交成功");
            } else {
                ui->show->appendPlainText("错误: 事务提交失败");
            }
        }
        else if (cmdUpper.startsWith("ROLLBACK")) {
            db_manager.rollbackTransaction();
            ui->show->appendPlainText("事务已回滚");
        }
        else {
            ui->show->appendPlainText("无法识别的命令: " + command);
        }
    } catch (const std::exception& e) {
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
        }
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
void MainWindow::handleCreateTable(QString& command) {
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
    QStringList fields = fields_str.split(',', Qt::SkipEmptyParts);

    for (const auto& field_str : fields) {
        QRegularExpression field_re(
            R"((\w+)\s+(\w+)(?:\((\d+)\))?\s*(.*))", // 匹配字段名、类型、长度和约束
            QRegularExpression::CaseInsensitiveOption
            );
        QRegularExpressionMatch field_match = field_re.match(field_str.trimmed());

        if (!field_match.hasMatch()) {
            ui->show->appendPlainText("错误: 列定义格式应为 <列名> <类型>[(长度)] [约束]");
            return;
        }

        QString field_name = field_match.captured(1).trimmed();
        QString type_str = field_match.captured(2).trimmed().toUpper();
        QString size_str = field_match.captured(3).trimmed();
        QString constraints_str = field_match.captured(4).trimmed();

        // 处理 PRIMARY KEY 合并
        QStringList constraints = constraints_str.split(' ', Qt::SkipEmptyParts);
        for (int i = 0; i < constraints.size(); ++i) {
            if (constraints[i].toUpper() == "PRIMARY" &&
                i + 1 < constraints.size() &&
                constraints[i+1].toUpper() == "KEY")
            {
                constraints[i] = "PRIMARY_KEY";
                constraints.removeAt(i+1);
            }
        }

        // 处理其他约束（如 NOT NULL）
        for (int i = 0; i < constraints.size(); ++i) {
            if (constraints[i].toUpper() == "NOT" &&
                i + 1 < constraints.size() &&
                constraints[i+1].toUpper() == "NULL")
            {
                constraints[i] = "NOT_NULL";
                constraints.removeAt(i+1);
            }
        }

        // 强制 VARCHAR 必须指定长度
        if (type_str == "VARCHAR") {
            if (size_str.isEmpty()) {
                ui->show->appendPlainText("错误: VARCHAR 必须指定长度，例如 VARCHAR(255)");
                return;
            }
            bool ok;
            int varchar_length = size_str.toInt(&ok);
            if (!ok || varchar_length <= 0) {
                ui->show->appendPlainText("错误: 无效的 VARCHAR 长度 '" + size_str + "'");
                return;
            }
            constraints.prepend("SIZE(" + size_str + ")");
        }

        // 解析字段类型
        int size_val = 0;
        xhyfield::datatype type = parseDataType(type_str, &size_val);

        // 处理 CHAR 类型长度
        if (type == xhyfield::CHAR && size_str.isEmpty()) {
            ui->show->appendPlainText("错误: CHAR 类型必须指定长度，例如 CHAR(20)");
            return;
        }

        // 创建字段对象
        xhyfield new_field(field_name, type, constraints);

        // 添加主键约束
        if (constraints.contains("PRIMARY_KEY")) {
            new_table.add_primary_key({field_name});
        }

        new_table.addfield(new_field);
    }

    if (db_manager.createtable(current_db, new_table)) {
        ui->show->appendPlainText(QString("表 '%1' 创建成功").arg(table_name));
    } else {
        ui->show->appendPlainText(QString("错误: 表 '%1' 已存在").arg(table_name));
    }
}

void MainWindow::handleTableConstraint(const QString& constraint_str, xhytable& table) {
    QRegularExpression constraint_re(
        "CONSTRAINT\\s+(\\w+)\\s+(PRIMARY\\s+KEY|FOREIGN\\s+KEY|UNIQUE|CHECK)\\s*\\(([^)]+)\\)",
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = constraint_re.match(constraint_str);
    if (!match.hasMatch()) return;

    QString constr_name = match.captured(1);
    QString constr_type = match.captured(2).toUpper();
    QString constr_details = match.captured(3);

    if (constr_type == "PRIMARY KEY") {
        table.add_primary_key(constr_details.split(',', Qt::SkipEmptyParts));
    }
    else if (constr_type == "FOREIGN KEY") {
        QRegularExpression fk_re("(\\w+)\\s+REFERENCES\\s+(\\w+)\\s*\\((\\w+)\\)");
        QRegularExpressionMatch fk_match = fk_re.match(constr_details);
        if (fk_match.hasMatch()) {
            table.add_foreign_key(
                fk_match.captured(1),  // 列名
                fk_match.captured(2),  // 引用表
                fk_match.captured(3),  // 引用列
                constr_name);         // 约束名
        }
    }
    else if (constr_type == "UNIQUE") {
        table.add_unique_constraint(constr_details.split(',', Qt::SkipEmptyParts), constr_name);
    }
    else if (constr_type == "CHECK") {
        table.add_check_constraint(constr_details, constr_name);
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

    // 开启事务
    bool transactionStarted = db_manager.beginTransaction();
    if (!transactionStarted) {
        ui->show->appendPlainText("警告: 无法开始事务，继续非事务操作");
    }

    try {
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

        // 提交事务
        if (transactionStarted) {
            if (db_manager.commitTransaction()) {
                ui->show->appendPlainText(QString("%1 行数据已插入").arg(total_inserted));
            } else {
                ui->show->appendPlainText("错误: 事务提交失败");
                db_manager.rollbackTransaction();
            }
        } else {
            ui->show->appendPlainText(QString("%1 行数据已插入").arg(total_inserted));
        }
    } catch (...) {
        if (transactionStarted) {
            db_manager.rollbackTransaction();
        }
        ui->show->appendPlainText("错误: 插入过程中发生异常");
        throw;
    }
}

void MainWindow::handleUpdate(const QString& command) {
    ConditionTree conditions;
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

    // 开启事务
    bool transactionStarted = db_manager.beginTransaction();
    if (!transactionStarted) {
        ui->show->appendPlainText("警告: 无法开始事务，继续非事务操作");
    }

    try {
        // 解析SET部分
        QMap<QString, QString> updates;
        QStringList set_pairs = set_part.split(',', Qt::SkipEmptyParts);
        for (const QString& pair : set_pairs) {
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
        ConditionNode  conditions;
        if (!where_part.isEmpty()) {
            if (!parseWhereClause(where_part, conditions)) {
                ui->show->appendPlainText("错误: 无效的WHERE条件");
                return;
            }
        }

        // 执行更新
        int affected = db_manager.updateData(current_db, table_name, updates, conditions);

        // 提交事务
        if (transactionStarted) {
            if (db_manager.commitTransaction()) {
                ui->show->appendPlainText(QString("%1 行数据已更新").arg(affected));
            } else {
                ui->show->appendPlainText("错误: 事务提交失败");
                db_manager.rollbackTransaction();
            }
        } else {
            ui->show->appendPlainText(QString("%1 行数据已更新").arg(affected));
        }
    } catch (...) {
        if (transactionStarted) {
            db_manager.rollbackTransaction();
        }
        ui->show->appendPlainText("错误: 更新过程中发生异常");
        throw;
    }
}

void MainWindow::handleDelete(const QString& command) {
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

    // 开启事务
    bool transactionStarted = db_manager.beginTransaction();
    if (!transactionStarted) {
        ui->show->appendPlainText("警告: 无法开始事务，继续非事务操作");
    }

    try {
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
        ConditionNode  conditions;
        if (!where_part.isEmpty() && !parseWhereClause(where_part, conditions)) {
            ui->show->appendPlainText("错误: 无效的WHERE条件");
            return;
        }

        // 执行删除
        int affected = db_manager.deleteData(current_db, table_name, conditions);

        // 提交事务
        if (transactionStarted) {
            if (db_manager.commitTransaction()) {
                ui->show->appendPlainText(QString("%1 行数据已删除").arg(affected));
            } else {
                ui->show->appendPlainText("错误: 事务提交失败");
                db_manager.rollbackTransaction();
            }
        } else {
            ui->show->appendPlainText(QString("%1 行数据已删除").arg(affected));
        }
    } catch (...) {
        if (transactionStarted) {
            db_manager.rollbackTransaction();
        }
        ui->show->appendPlainText("错误: 删除过程中发生异常");
        throw;
    }
}

void MainWindow::handleSelect(const QString& command) {
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
    QString where_part = match.captured(2).trimmed();
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    // 解析WHERE条件
    ConditionNode conditionRoot;


        if (!parseWhereClause(where_part, conditionRoot)) {
            ui->show->appendPlainText("错误: WHERE 条件语法无效");
            return;
        }




    // 执行查询
    QVector<xhyrecord> results;
    if (!db_manager.selectData(current_db, table_name, conditionRoot, results)) {
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
// 辅助函数：将条件树扁平化为 QMap
// 修改 flattenConditionTree 函数
void MainWindow::flattenConditionTree(const ConditionNode& node, ConditionNode& output) {
    if (node.type == ConditionNode::COMPARISON) {
        // 合并 comparison 的键值对
        for (auto it = node.comparison.constBegin(); it != node.comparison.constEnd(); ++it) {
            output.comparison.insert(it.key(), it.value()); // 使用 insert 插入键值对
        }
    } else {
        // 递归处理子节点
        for (const auto& child : node.children) {
            flattenConditionTree(child, output);
        }
    }
}
void MainWindow::handleAlterTable(const QString& command) {
    QRegularExpression re(
        "ALTER\\s+TABLE\\s+(\\w+)\\s+(ADD|DROP|ALTER|RENAME|MODIFY)\\s+(COLUMN\\s+)?(.+)",
        QRegularExpression::CaseInsensitiveOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: ALTER TABLE <表名> <操作> <参数>");
        return;
    }

    QString table_name = match.captured(1);
    QString action = match.captured(2).toUpper();
    QString parameters = match.captured(4).trimmed();
    QString current_db = db_manager.get_current_database();

    if (current_db.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    bool success = false;
    QString result;

    if (action == "ADD") {
        // 处理ADD COLUMN或ADD CONSTRAINT
        if (parameters.contains("CONSTRAINT")) {
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

            handleTableConstraint(parameters, *table);
            success = db_manager.update_table(current_db, *table);
            result = success ? "约束添加成功" : "约束添加失败";
        }
        else {
            QRegularExpression addRe(
                "(\\w+)\\s+(\\w+)(?:\\((\\d+)\\))?\\s*(.*)",
                QRegularExpression::CaseInsensitiveOption
                );
            QRegularExpressionMatch addMatch = addRe.match(parameters);

            if (!addMatch.hasMatch()) {
                ui->show->appendPlainText("语法错误: ADD [COLUMN] <列名> <类型>[(<长度>)] [<约束>]");
                return;
            }

            QString field_name = addMatch.captured(1);
            QString type_str = addMatch.captured(2);
            QString size_str = addMatch.captured(3);
            QString constraints_str = addMatch.captured(4);

            int size = 0;
            xhyfield::datatype type = parseDataType(type_str, &size);
            QStringList constraints = parseConstraints(constraints_str);

            if (type == xhyfield::CHAR && size > 0) {
                constraints.append("SIZE(" + QString::number(size) + ")");
            }

            xhyfield new_field(field_name, type, constraints);
            success = db_manager.add_column(current_db, table_name, new_field);
            result = success ? QString("成功添加列 '%1' 到表 '%2'").arg(field_name, table_name)
                             : QString("添加列失败: 列 '%1' 可能已存在").arg(field_name);
        }
    }
    else if (action == "DROP") {
        // 处理DROP COLUMN或DROP CONSTRAINT
        if (parameters.contains("CONSTRAINT")) {
            QRegularExpression dropConstrRe(
                "CONSTRAINT\\s+(\\w+)",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch dropConstrMatch = dropConstrRe.match(parameters);

            if (dropConstrMatch.hasMatch()) {
                QString constr_name = dropConstrMatch.captured(1);
                success = db_manager.drop_constraint(current_db, table_name, constr_name);
                result = success ? QString("成功删除约束 '%1'").arg(constr_name)
                                 : QString("删除约束失败");
            }
        }
        else {
            QRegularExpression dropRe("(\\w+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch dropMatch = dropRe.match(parameters);

            if (!dropMatch.hasMatch()) {
                ui->show->appendPlainText("语法错误: DROP [COLUMN] <列名>");
                return;
            }

            QString field_name = dropMatch.captured(1);
            success = db_manager.drop_column(current_db, table_name, field_name);
            result = success ? QString("成功从表 '%1' 中删除列 '%2'").arg(table_name, field_name)
                             : QString("删除列失败: 列 '%1' 不存在").arg(field_name);
        }
    }
    else if (action == "ALTER" || action == "MODIFY") {
        QRegularExpression alterRe(
            "COLUMN\\s+(\\w+)\\s+(TYPE\\s+(\\w+)(?:\\((\\d+)\\))?|SET\\s+(\\w+)\\s+(\\w+))",
            QRegularExpression::CaseInsensitiveOption
            );
        QRegularExpressionMatch alterMatch = alterRe.match(parameters);

        if (!alterMatch.hasMatch()) {
            ui->show->appendPlainText("语法错误: ALTER COLUMN <列名> TYPE <新类型>[(<长度>)] 或 ALTER COLUMN <列名> SET <属性> <值>");
            return;
        }

        QString field_name = alterMatch.captured(1);

        if (!alterMatch.captured(3).isEmpty()) {
            QString new_type_str = alterMatch.captured(3);
            QString size_str = alterMatch.captured(4);

            int size = 0;
            xhyfield::datatype new_type = parseDataType(new_type_str, &size);

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

            const xhyfield* old_field = table->get_field(field_name);
            if (!old_field) {
                ui->show->appendPlainText(QString("错误: 列 '%1' 不存在").arg(field_name));
                return;
            }

            QStringList constraints = old_field->constraints();
            constraints.removeIf([](const QString& c) {
                return c.contains(QRegularExpression("^SIZE\\(\\d+\\)$"));
            });

            if (new_type == xhyfield::CHAR && size > 0) {
                constraints.append("SIZE(" + QString::number(size) + ")");
            }

            xhyfield new_field(field_name, new_type, constraints);
            success = db_manager.alter_column(current_db, table_name, field_name, new_field);
            result = success ? QString("成功修改列 '%1' 的类型为 '%2'").arg(field_name, new_type_str)
                             : QString("修改列类型失败");
        } else {
            QString attribute = alterMatch.captured(5).toUpper();
            QString value = alterMatch.captured(6).toUpper();

            if (attribute == "NOT" && value == "NULL") {
                success = db_manager.add_constraint(current_db, table_name, field_name, "NOT_NULL");
                result = success ? QString("成功为列 '%1' 添加 NOT NULL 约束").arg(field_name)
                                 : QString("添加约束失败");
            } else if (attribute == "DEFAULT") {
                success = db_manager.add_constraint(current_db, table_name, field_name, "DEFAULT " + value);
                result = success ? QString("成功为列 '%1' 设置默认值 '%2'").arg(field_name, value)
                                 : QString("设置默认值失败");
            } else {
                ui->show->appendPlainText("错误: 不支持的列属性修改");
                return;
            }
        }
    }
    else if (action == "RENAME") {
        QRegularExpression renameColRe(
            "COLUMN\\s+(\\w+)\\s+TO\\s+(\\w+)",
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch renameColMatch = renameColRe.match(parameters);

        if (renameColMatch.hasMatch()) {
            QString oldName = renameColMatch.captured(1);
            QString newName = renameColMatch.captured(2);
            success = db_manager.rename_column(current_db, table_name, oldName, newName);
            result = success ? QString("成功将列 '%1' 重命名为 '%2'").arg(oldName, newName)
                             : QString("重命名列失败");
        }
        else {
            QRegularExpression renameRe("TO\\s+(\\w+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch renameMatch = renameRe.match(parameters);
            if (renameMatch.hasMatch()) {
                QString new_name = renameMatch.captured(1);
                success = db_manager.rename_table(current_db, table_name, new_name);
                result = success ? QString("成功将表 '%1' 重命名为 '%2'").arg(table_name, new_name)
                                 : QString("重命名表失败: 新表名可能已存在");
            }
        }
    }

    if (success) {
        ui->show->appendPlainText(result);
    } else {
        ui->show->appendPlainText("错误: " + result);
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
                continue;
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
bool MainWindow::parseWhereClause(const QString& whereStr, ConditionNode& root) {
    if (whereStr.trimmed().isEmpty()) {
        root.type = ConditionNode::COMPARISON;
        root.comparison["1"] = "= 1"; // 构造恒真条件 1=1
        return true;
    }
    QStack<ConditionNode*> stack;
    stack.push(&root);
    QString currentToken;
    bool inQuotes = false;
    QChar quoteChar;
    int parenDepth = 0;

    for (int i = 0; i < whereStr.length(); ++i) {
        QChar ch = whereStr[i];

        // 处理引号
        if (ch == '\'' || ch == '"') {
            if (!inQuotes) {
                inQuotes = true;
                quoteChar = ch;
            } else if (ch == quoteChar) {
                inQuotes = false;
            }
            currentToken += ch;
            continue;
        }

        // 处理括号
        if (ch == '(' && !inQuotes) {
            parenDepth++;
            ConditionNode newChild;
            newChild.type = ConditionNode::LOGIC_OP;
            stack.top()->children.append(newChild);
            stack.push(&stack.top()->children.last());
            currentToken.clear();
            continue;
        } else if (ch == ')' && !inQuotes) {
            if (parenDepth == 0) {
                ui->show->appendPlainText("错误: 括号不匹配");
                return false;
            }
            parenDepth--;
            stack.pop();
            currentToken.clear();
            continue;
        }

        // 处理逻辑运算符
        if (ch == ' ' && !inQuotes) {
            QString upperToken = currentToken.trimmed().toUpper();
            if (upperToken == "AND" || upperToken == "OR") {
                if (stack.top()->type != ConditionNode::LOGIC_OP) {
                    ConditionNode newLogicNode;
                    newLogicNode.type = ConditionNode::LOGIC_OP;
                    newLogicNode.logicOp = upperToken;
                    stack.top()->children.append(newLogicNode);
                    stack.push(&stack.top()->children.last());
                } else {
                    stack.top()->logicOp = upperToken;
                }
                currentToken.clear();
                continue;
            }
        }

        currentToken += ch;

        // 解析原子条件（如 id=1）
        if ((ch == ' ' || i == whereStr.length() - 1) && !inQuotes) {
            currentToken = currentToken.trimmed();
            if (!currentToken.isEmpty()) {
                QRegularExpression re(R"((\w+)\s*(=|!=|>|<|>=|<=|LIKE|IN)\s*(.*))");
                QRegularExpressionMatch match = re.match(currentToken);
                if (match.hasMatch()) {
                    ConditionNode compNode;
                    compNode.type = ConditionNode::COMPARISON;
                    QString value = match.captured(3).trimmed();
                    if (value.startsWith('\'') || value.startsWith('"')) {
                        value = value.mid(1, value.length() - 2); // 去除引号
                    }
                    compNode.comparison[match.captured(1)] = match.captured(2) + " " + value;
                    stack.top()->children.append(compNode);
                } else {
                    ui->show->appendPlainText("错误: 无效条件表达式 '" + currentToken + "'");
                    return false;
                }
                currentToken.clear();
            }
        }
    }

    // 检查括号是否闭合
    if (parenDepth != 0) {
        ui->show->appendPlainText("错误: 括号未闭合");
        return false;
    }
    return true;
}

xhyfield::datatype MainWindow::parseDataType(const QString& type_str, int* size) {
        QString upperType = type_str.toUpper();
        if (upperType == "INT") {
            return xhyfield::INT;
        } else if (upperType == "VARCHAR") {
            return xhyfield::VARCHAR;
        } else if (upperType == "FLOAT") {
            return xhyfield::FLOAT;
        } else if (upperType == "DATE") {
            return xhyfield::DATE;
        } else if (upperType == "BOOL") {
            return xhyfield::BOOL;
        } else if (upperType.startsWith("CHAR(")) {
            // 提取CHAR类型的长度
            int start = upperType.indexOf('(') + 1;
            int end = upperType.indexOf(')');
            if (size != nullptr) {
                *size = upperType.mid(start, end-start).toInt();
            }
            return xhyfield::CHAR;
        }
        return xhyfield::VARCHAR; // 默认类型
    }

    QStringList MainWindow::parseConstraints(const QString& constraints) {
        QStringList constraintList;
        if (constraints.isEmpty()) {
            return constraintList;
        }

        // 假设约束是以空格分隔的字符串
        constraintList = constraints.split(' ', Qt::SkipEmptyParts);

        // 去除每个约束前后的空格
        for (auto& constraint : constraintList) {
            constraint = constraint.trimmed();
        }

        return constraintList;
    }
    void MainWindow::show_databases() {
        auto databases = db_manager.databases();
        if (databases.isEmpty()) {
            ui->show->appendPlainText("没有数据库");
            return;
        }

        // 使用 QSet 去重
        QSet<QString> uniqueNames;
        for (const auto& db : databases) {
            uniqueNames.insert(db.name());
        }

        QString output = "数据库列表:\n";
        for (const QString& name : uniqueNames) {
            output += " - " + name + "\n";
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
