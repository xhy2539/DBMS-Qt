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


void MainWindow::on_run_clicked()
{
    QString input = ui->putin->toPlainText();
    QStringList commands = SQLParser::parseMultiLineSQL(input); // SQLParser::静态调用

    ui->show->clear();

    for (const QString& command_const : commands) {
        QString trimmedCmd = command_const.trimmed();
        if (!trimmedCmd.isEmpty() && trimmedCmd.endsWith(';')) {
            ui->show->appendPlainText("> " + trimmedCmd);
            execute_command(trimmedCmd);
            ui->show->appendPlainText("");
        } else if (!trimmedCmd.isEmpty()){
            ui->show->appendPlainText("! 忽略未以分号结尾的语句: " + trimmedCmd);
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
                ui->show->appendPlainText("错误: 检测到未完成的SQL语句（末尾缺少分号）: " + remaining);
            }
        }
    }
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
                ui->show->appendPlainText("事务开始。");
            } else {
                ui->show->appendPlainText("错误: 无法开始事务 (可能已在事务中或当前数据库不支持)。");
            }
        }
        else if (cmdUpper.startsWith("COMMIT")) {
            if (db_manager.commitTransaction()) {
                ui->show->appendPlainText("事务提交成功。");
            } else {
                ui->show->appendPlainText("错误: 事务提交失败 (可能不在事务中或没有更改)。");
            }
        }
        else if (cmdUpper.startsWith("ROLLBACK")) {
            db_manager.rollbackTransaction();
            ui->show->appendPlainText("事务已回滚 (如果存在活动事务)。");
        }
        else {
            ui->show->appendPlainText("无法识别的命令: " + command);
        }
    } catch (const std::runtime_error& e) {
        QString errMsg = "运行时错误: " + QString::fromStdString(e.what());
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
            errMsg += " (事务已回滚)";
        }
        ui->show->appendPlainText(errMsg);
    } catch (...) {
        QString errMsg = "发生未知类型的严重错误。";
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
            errMsg += " (事务已回滚)";
        }
        ui->show->appendPlainText(errMsg);
    }
}

void MainWindow::handleCreateDatabase(const QString& command) {
    QRegularExpression re(R"(CREATE\s+DATABASE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.createdatabase(db_name)) {
            ui->show->appendPlainText(QString("数据库 '%1' 创建成功。").arg(db_name));
        } else {
            ui->show->appendPlainText(QString("错误: 创建数据库 '%1' 失败 (可能已存在或名称无效)。").arg(db_name));
        }
    } else {
        ui->show->appendPlainText("语法错误: CREATE DATABASE <数据库名>");
    }
}

void MainWindow::handleUseDatabase(const QString& command) {
    QRegularExpression re(R"(USE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        if (db_manager.use_database(db_name)) {
            current_db = db_name;
            ui->show->appendPlainText(QString("已切换到数据库 '%1'。").arg(db_name));
        } else {
            ui->show->appendPlainText(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        }
    } else {
        ui->show->appendPlainText("语法错误: USE <数据库名>");
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
                ui->show->appendPlainText(QString("数据库 '%1' 已删除。").arg(db_name));
                if (current_db.compare(db_name, Qt::CaseInsensitive) == 0) {
                    current_db.clear();
                    ui->show->appendPlainText("注意：当前使用的数据库已被删除。");
                }
            } else {
                ui->show->appendPlainText(QString("错误: 删除数据库 '%1' 失败 (可能不存在或文件删除失败)。").arg(db_name));
            }
        } else {
            ui->show->appendPlainText("删除数据库操作已取消。");
        }
    } else {
        ui->show->appendPlainText("语法错误: DROP DATABASE <数据库名>");
    }
}

void MainWindow::handleCreateTable(QString& command) {
    QString processedCommand = command.replace(QRegularExpression(R"(\s+)"), " ").trimmed();
    QRegularExpression re(R"(CREATE\s+TABLE\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(processedCommand);

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: CREATE TABLE <表名> (<列定义1> <类型1> [约束1], ...)");
        return;
    }
    QString table_name = match.captured(1).trimmed();
    QString fields_and_constraints_str = match.captured(2).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库。请先使用 USE <数据库名> 命令。");
        return;
    }

    xhytable new_table(table_name);
    QStringList definitions;
    int parenLevel = 0;
    QString currentDef;
    bool inStringDef = false;
    QChar stringCharDef = ' ';

    for(QChar c : fields_and_constraints_str) {
        if (c == '\'' || c == '"') {
            if (inStringDef && c == stringCharDef) {
                if (!currentDef.isEmpty() && currentDef.endsWith('\\')) { // 简单的转义处理，例如 \'
                    currentDef.append(c);
                } else {
                    inStringDef = false;
                    currentDef.append(c);
                }
            } else if (!inStringDef) {
                inStringDef = true;
                stringCharDef = c;
                currentDef.append(c);
            } else { // inStringDef 为 true, aber c != stringCharDef (z.B. 'abc"def') - dies sollte nicht vorkommen bei korrekten SQL-Literalen
                currentDef.append(c);
            }
        } else if (c == '(' && !inStringDef) {
            parenLevel++;
            currentDef.append(c);
        } else if (c == ')' && !inStringDef) {
            parenLevel--;
            currentDef.append(c);
        } else if (c == ',' && parenLevel == 0 && !inStringDef) {
            definitions.append(currentDef.trimmed());
            currentDef.clear();
        } else {
            currentDef.append(c);
        }
    }
    if(!currentDef.isEmpty()) definitions.append(currentDef.trimmed());


    for (const QString& def_str_const : definitions) {
        QString def_str = def_str_const.trimmed();
        if (def_str.toUpper().startsWith("CONSTRAINT")) {
            handleTableConstraint(def_str, new_table);
        } else {
            QRegularExpression field_re(R"(([\w_]+)\s+([\w\s\(\),'"\-\\]+?)(?:\s+(.*))?$)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch field_match = field_re.match(def_str);
            if (!field_match.hasMatch()) {
                ui->show->appendPlainText("错误: 无效的列定义格式: " + def_str);
                return;
            }
            QString field_name = field_match.captured(1).trimmed();
            QString type_str_full = field_match.captured(2).trimmed();
            QString constraints_str = field_match.captured(3).trimmed();

            int size_val = 0;
            xhyfield::datatype type = parseDataType(type_str_full, &size_val);
            QStringList constraints = parseConstraints(constraints_str);

            if ((type == xhyfield::CHAR || type == xhyfield::VARCHAR) && size_val > 0) {
                bool sizeConstraintExists = false;
                for(const QString& c : constraints) if(c.toUpper().startsWith("SIZE(")) sizeConstraintExists = true;
                if(!sizeConstraintExists) constraints.prepend("SIZE(" + QString::number(size_val) + ")");
            } else if (type == xhyfield::CHAR && size_val <= 0) {
                if (type_str_full.toUpper().startsWith("CHAR") && !type_str_full.toUpper().contains("(")) {
                    ui->show->appendPlainText(QString("错误: CHAR类型字段 '%1' 必须指定长度, 如 CHAR(10).").arg(field_name));
                    return;
                }
            }
            if (type == xhyfield::ENUM) {
                QRegularExpression enum_values_re(R"(ENUM\s*\((.+)\))", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                QRegularExpressionMatch enum_match = enum_values_re.match(type_str_full);
                if (enum_match.hasMatch()) {
                    QStringList enum_vals_str = parseSqlValues(enum_match.captured(1));
                    xhyfield new_field_enum(field_name, type, constraints);
                    new_field_enum.set_enum_values(enum_vals_str); // 此函数需要存在于 xhyfield 类中
                    new_table.addfield(new_field_enum);
                } else {
                    ui->show->appendPlainText(QString("错误: ENUM 类型字段 '%1' 定义无效，应为 ENUM('val1','val2',...).").arg(field_name));
                    return;
                }
            } else {
                xhyfield new_field(field_name, type, constraints);
                new_table.addfield(new_field);
            }
            if (constraints.contains("PRIMARY_KEY", Qt::CaseInsensitive)) {
                new_table.add_primary_key({field_name});
            }
        }
    }

    if (new_table.fields().isEmpty() && !fields_and_constraints_str.contains("CONSTRAINT", Qt::CaseInsensitive) ){
        ui->show->appendPlainText("错误: 表至少需要定义一列或一个约束。");
        return;
    }

    if (db_manager.createtable(current_db_name, new_table)) {
        ui->show->appendPlainText(QString("表 '%1' 在数据库 '%2' 中创建成功。").arg(table_name, current_db_name));
    } else {
        ui->show->appendPlainText(QString("错误: 创建表 '%1' 失败 (可能已存在或定义无效)。").arg(table_name));
    }
}


void MainWindow::handleDropTable(const QString& command) {
    QRegularExpression re(R"(DROP\s+TABLE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString table_name = match.captured(1);
        QString current_db_name = db_manager.get_current_database();
        if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "确认删除表",
                                                                 QString("确定要永久删除表 '%1' 吗? 此操作不可恢复!").arg(table_name),
                                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (db_manager.droptable(current_db_name, table_name)) {
                ui->show->appendPlainText(QString("表 '%1' 已从数据库 '%2' 删除。").arg(table_name, current_db_name));
            } else {
                ui->show->appendPlainText(QString("错误: 删除表 '%1' 失败 (可能不存在)。").arg(table_name));
            }
        } else {
            ui->show->appendPlainText("删除表操作已取消。");
        }
    } else {
        ui->show->appendPlainText("语法错误: DROP TABLE <表名>");
    }
}

void MainWindow::handleDescribe(const QString& command) {
    QRegularExpression re(R"((?:DESCRIBE|DESC)\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString table_name = match.captured(1);
        QString current_db_name = db_manager.get_current_database();
        if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }
        show_schema(current_db_name, table_name);
    } else {
        ui->show->appendPlainText("语法错误: DESCRIBE <表名> 或 DESC <表名>");
    }
}

void MainWindow::handleInsert(const QString& command) {
    QRegularExpression re(
        R"(INSERT\s+INTO\s+([\w_]+)\s*(?:\(([^)]+)\))?\s*VALUES\s*((?:\([^)]*\)\s*,?\s*)+)\s*;?)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch main_match = re.match(command.trimmed());

    if (!main_match.hasMatch()) {
        ui->show->appendPlainText("语法错误: INSERT INTO 表名 [(列名1,...)] VALUES (值1,...)[, (值A,...)];");
        return;
    }

    QString table_name = main_match.captured(1).trimmed();
    QString fields_part = main_match.captured(2).trimmed();
    QString all_value_groups_string = main_match.captured(3).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if(!db_manager.beginTransaction()) {
            ui->show->appendPlainText("警告: 无法开始新事务，操作将在无事务保护下进行。");
        } else transactionStartedHere = true;
    }

    try {
        QStringList specified_fields;
        if (!fields_part.isEmpty()) {
            specified_fields = fields_part.split(',', Qt::SkipEmptyParts);
            for (QString& field : specified_fields) field = field.trimmed();
        }

        QList<QStringList> rows_of_values;
        QRegularExpression value_group_re(R"(\(([^)]*)\))");
        QRegularExpressionMatchIterator it = value_group_re.globalMatch(all_value_groups_string);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            rows_of_values.append(parseSqlValues(m.captured(1)));
        }

        if (rows_of_values.isEmpty()) {
            throw std::runtime_error("VALUES 子句中没有有效的值组。");
        }

        int total_inserted = 0;
        for (const QStringList& single_row_values : rows_of_values) {
            QMap<QString, QString> field_value_map;
            if (specified_fields.isEmpty()) {
                xhydatabase* db = db_manager.find_database(current_db_name);
                xhytable* table = db ? db->find_table(table_name) : nullptr;
                if (!table) throw std::runtime_error(("表 '" + table_name + "' 不存在。").toStdString());
                const auto& table_fields = table->fields();
                if (single_row_values.size() != table_fields.size()) {
                    throw std::runtime_error(QString("值数量 (%1) 与表 '%2' 的列数量 (%3) 不匹配。")
                                                 .arg(single_row_values.size()).arg(table_name).arg(table_fields.size()).toStdString());
                }
                for (int i = 0; i < table_fields.size(); ++i) {
                    field_value_map[table_fields[i].name()] = single_row_values[i];
                }
            } else {
                if (single_row_values.size() != specified_fields.size()) {
                    throw std::runtime_error(QString("值数量 (%1) 与指定列数量 (%2) 不匹配。")
                                                 .arg(single_row_values.size()).arg(specified_fields.size()).toStdString());
                }
                for (int i = 0; i < specified_fields.size(); ++i) {
                    field_value_map[specified_fields[i]] = single_row_values[i];
                }
            }

            if (db_manager.insertData(current_db_name, table_name, field_value_map)) {
                total_inserted++;
            } else {
                // 错误已在 xhydbmanager 或 xhytable 中处理并打印
            }
        }

        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                ui->show->appendPlainText(QString("%1 行数据已成功插入。").arg(total_inserted));
            } else {
                ui->show->appendPlainText("错误: 事务提交失败，更改已回滚。");
            }
        } else {
            ui->show->appendPlainText(QString("%1 行数据已插入 (在现有事务中或无事务)。").arg(total_inserted));
        }

    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("插入数据错误: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("插入数据时发生未知错误。");
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
    qDebug() << "[parseSubExpression] Parsing sub-expression: '" << expression << "'";
    ConditionNode node;

    // 1. 移除最外层括号 (如果它们正确地包裹了整个表达式)
    while (expression.startsWith('(') && expression.endsWith(')')) {
        int parenLevel = 0;
        bool allEnclosedProperly = true;
        if (expression.length() <= 2) { // 例如 "()" 或 " ( ) "
            qDebug() << "  Expression too short for meaningful parenthesized content, breaking paren stripping.";
            break;
        }
        // 检查括号是否真正包裹整个表达式
        for (int i = 0; i < expression.length() - 1; ++i) { // 扫描到倒数第二个字符
            if (expression[i] == '(') parenLevel++;
            else if (expression[i] == ')') parenLevel--;
            if (parenLevel == 0) { // 如果在末尾前括号层级回到0，说明不是完全包裹
                allEnclosedProperly = false;
                qDebug() << "  Parentheses do not enclose the entire sub-expression (level became 0 at index " << i << ").";
                break;
            }
        }
        // 确保在扫描完整个表达式（除了最后一个字符）后，parenLevel 仍然大于0
        // 并且最后一个字符是 ')'，并且 parenLevel 最终为 1 (表示第一层括号)
        if (allEnclosedProperly && parenLevel == 1 && expression.at(expression.length()-1) == ')') {
            QString newExpression = expression.mid(1, expression.length() - 2).trimmed();
            qDebug() << "  Stripped outer parentheses. Old: '" << expression << "', New: '" << newExpression << "'";
            expression = newExpression;
        } else {
            qDebug() << "  Outer parentheses not stripped. AllEnclosed:" << allEnclosedProperly
                     << "FinalParenLevel (before last char):" << parenLevel
                     << "LastCharIsClosingParen:" << (expression.length() > 0 && expression.at(expression.length()-1) == ')');
            break; // 不是完全由一对最外层括号包裹
        }
    }
    if (expression.isEmpty()) { // 如果剥离括号后为空
        qDebug() << "  Expression became empty after stripping parentheses, treating as error or empty node.";
        // 根据您的逻辑，这里可能应该抛出错误或返回一个 EMPTY 节点
        // throw std::runtime_error("括号内的表达式为空。");
        node.type = ConditionNode::EMPTY; // 或者视为一个空的有效节点
        return node;
    }


    // 2. 处理 NOT 操作符 (创建 NEGATION_OP 节点)
    //    (假设 ConditionNode::isNegated 已被移除)
    if (expression.toUpper().startsWith("NOT ") &&
        !expression.toUpper().startsWith("NOT LIKE") && // 避免 "NOT LIKE" 等被错误解析为 "NOT (LIKE...)"
        !expression.toUpper().startsWith("NOT IN") &&
        !expression.toUpper().startsWith("NOT BETWEEN")) {
        QString restOfExpression = QStringView(expression).mid(4).toString().trimmed();
        qDebug() << "  Found NOT prefix. Remainder for recursive call: '" << restOfExpression << "'";
        if (restOfExpression.isEmpty()) {
            throw std::runtime_error("NOT 操作符后缺少条件表达式。");
        }
        ConditionNode childNode = parseSubExpression(restOfExpression);
        node.type = ConditionNode::NEGATION_OP;
        node.children.append(childNode);
        qDebug() << "  Created NEGATION_OP node with child type:" << childNode.type;
        return node;
    }

    // 3. 处理逻辑运算符 OR, AND
    //    findLowestPrecedenceOperator 应优先找到 OR (如果它在列表中靠前)
    QPair<int, QString> opDetails = findLowestPrecedenceOperator(expression, {"OR", "AND"});
    if (opDetails.first != -1) {
        qDebug() << "  Found logical operator: '" << opDetails.second << "' at pos " << opDetails.first;
        node.type = ConditionNode::LOGIC_OP;
        node.logicOp = opDetails.second.toUpper();

        QString leftPart = QStringView(expression).left(opDetails.first).toString().trimmed();
        QString rightPart = QStringView(expression).mid(opDetails.first + opDetails.second.length()).toString().trimmed();

        if (leftPart.isEmpty() || rightPart.isEmpty()) {
            throw std::runtime_error("逻辑运算符 '" + opDetails.second.toStdString() + "' 缺少操作数。表达式: " + expression.toStdString());
        }
        qDebug() << "    Left part: '" << leftPart << "', Right part: '" << rightPart << "'";
        node.children.append(parseSubExpression(leftPart));
        node.children.append(parseSubExpression(rightPart));
        return node;
    }

    // 4. 处理比较运算符
    const QList<QString> comparisonOps = {
        "IS NOT NULL", "IS NULL",       // 优先匹配这些，因为它们是单操作数或特殊格式
        "NOT BETWEEN", "BETWEEN",       // 多词操作符
        "NOT LIKE", "LIKE",
        "NOT IN", "IN",
        ">=", "<=", "<>", "!=", "=", ">", "<" // 单/双字符操作符
    };

    for (const QString& op : comparisonOps) {
        QRegularExpression compRe;
        QString patternStr;
        // 字段名模式: 简单标识符，或反引号/方括号包裹的标识符
        QString fieldNamePattern = R"(([\w_][\w\d_]*|`[^`]+`|\[[^\]]+\]))"; // 改进的字段名模式
        bool opTakesNoRightValue = (op.compare("IS NULL", Qt::CaseInsensitive) == 0 || op.compare("IS NOT NULL", Qt::CaseInsensitive) == 0);

        if (opTakesNoRightValue) {
            patternStr = QString(R"(^\s*%1\s+(%2)\s*$)").arg(fieldNamePattern).arg(QRegularExpression::escape(op));
        } else if (op.compare("BETWEEN", Qt::CaseInsensitive) == 0 || op.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
            // 操作符两侧的空格为 \s* (可选), AND 两侧为 \s+ (必需)
            patternStr = QString(R"(^\s*%1\s*(%2)\s*(.+?)\s+AND\s+(.+?)\s*$)").arg(fieldNamePattern).arg(QRegularExpression::escape(op));
        } else {
            // 操作符两侧的空格为 \s* (可选)
            patternStr = QString(R"(^\s*%1\s*(%2)\s*(.+)\s*$)").arg(fieldNamePattern).arg(QRegularExpression::escape(op));
        }

        compRe.setPattern(patternStr);
        compRe.setPatternOptions(QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch match = compRe.match(expression);

        if (match.hasMatch()) {
            qDebug() << "  Matched comparison operator: '" << op << "' for expression: '" << expression << "'";
            node.type = ConditionNode::COMPARISON_OP;
            node.comparison.fieldName = match.captured(1).trimmed();
            node.comparison.operation = op.toUpper(); // 使用列表中的 op (已规范化)
            qDebug() << "    Field: '" << node.comparison.fieldName << "', Op: '" << node.comparison.operation << "'";

            if (op.compare("BETWEEN", Qt::CaseInsensitive) == 0 || op.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
                QString val1Str = match.captured(3).trimmed(); // captured(2) 是操作符本身
                QString val2Str = match.captured(4).trimmed();
                node.comparison.value = parseLiteralValue(val1Str);
                node.comparison.value2 = parseLiteralValue(val2Str);
                qDebug() << "    Value1: " << node.comparison.value << " (Raw: '" << val1Str << "')"
                         << ", Value2: " << node.comparison.value2 << " (Raw: '" << val2Str << "')";
            } else if (!opTakesNoRightValue) { // 对于其他有右值的操作符
                QString valuePart = match.captured(match.lastCapturedIndex()).trimmed(); // captured(2)是操作符, captured(3)是值
                qDebug() << "    ValuePart (raw for literal parsing): '" << valuePart << "'";
                if (op.compare("IN", Qt::CaseInsensitive) == 0 || op.compare("NOT IN", Qt::CaseInsensitive) == 0) {
                    if (!valuePart.startsWith('(') || !valuePart.endsWith(')')) {
                        throw std::runtime_error("IN 子句的值必须用括号括起来。 Got: " + valuePart.toStdString());
                    }
                    QString innerValues = valuePart.mid(1, valuePart.length() - 2).trimmed();
                    if (innerValues.isEmpty()) {
                        throw std::runtime_error("IN 子句的值列表不能为空 (例如 IN () )");
                    }
                    QStringList valuesStr = parseSqlValues(innerValues); // parseSqlValues 应能处理逗号分隔的值

                    for(const QString& valStr : valuesStr) {
                        if (!valStr.trimmed().isEmpty()) { // 避免添加由连续逗号产生的空值
                            node.comparison.valueList.append(parseLiteralValue(valStr.trimmed()));
                        }
                    }
                    // 再次检查，以防 parseSqlValues 返回包含单个空字符串的列表
                    if (node.comparison.valueList.isEmpty() && !valuesStr.isEmpty() && valuesStr.first().isEmpty()) {
                        // This case might happen if input was IN ('')
                        // If IN ('') is not allowed, throw error. If it is, parseLiteralValue should handle it.
                    }
                    if (node.comparison.valueList.isEmpty()) { // 如果在去除所有空值后列表为空
                        throw std::runtime_error("IN 子句的值列表解析后为空或格式错误。");
                    }
                    qDebug() << "    IN values parsed:" << node.comparison.valueList;
                } else { // LIKE, =, >, < etc.
                    node.comparison.value = parseLiteralValue(valuePart);
                    qDebug() << "    Value: " << node.comparison.value;
                }
            }
            // 对于 IS NULL / IS NOT NULL，没有值需要解析
            return node; // 成功解析比较操作
        }
    }
    // 如果以上所有规则都不匹配，则表达式无效
    qDebug() << "  No operator pattern matched after trying all known operators. Throwing error for expression: '" << expression << "'";
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
        ui->show->appendPlainText(errorMsg); // 显示错误到UI
        return false;
    }
}


void MainWindow::handleUpdate(const QString& command) {
    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) {
        ui->show->appendPlainText("错误: 未选择数据库");
        return;
    }

    QRegularExpression re(
        R"(UPDATE\s+([\w\.]+)\s+SET\s+(.+?)(?:\s+WHERE\s+(.+))?\s*;?$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: UPDATE 表名 SET 列1=值1,... [WHERE 条件]");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString set_part = match.captured(2).trimmed();
    QString where_part = match.captured(3).trimmed();


    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (!db_manager.beginTransaction()) {
            ui->show->appendPlainText("警告: 无法开始新事务，操作将在无事务保护下进行。");
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
                    ui->show->appendPlainText(QString("%1 行已更新。").arg(affected_rows));
                } else {
                    ui->show->appendPlainText("错误：事务提交失败。更改已回滚。");
                }
            } else {
                ui->show->appendPlainText(QString("%1 行已更新 (在现有事务中)。").arg(affected_rows));
            }
        } else {
            ui->show->appendPlainText("错误：更新数据失败。");
            if(transactionStartedHere) db_manager.rollbackTransaction();
        }
    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("更新操作错误: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("更新操作发生未知错误。");
    }
}

void MainWindow::handleDelete(const QString& command) {
    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }

    QRegularExpression re(R"(DELETE\s+FROM\s+([\w\.]+)(?:\s+WHERE\s+(.+))?\s*;?$)",
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) { ui->show->appendPlainText("语法错误: DELETE FROM <表名> [WHERE <条件>]"); return; }
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
                ui->show->appendPlainText("删除操作已取消。");
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
                    ui->show->appendPlainText(QString("%1 行已删除。").arg(affected_rows));
                } else { ui->show->appendPlainText("错误：事务提交失败。更改已回滚。");}
            } else {
                ui->show->appendPlainText(QString("%1 行已删除 (在现有事务中)。").arg(affected_rows));
            }
        } else { ui->show->appendPlainText("错误：删除数据失败。"); if(transactionStartedHere) db_manager.rollbackTransaction(); }
    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("删除操作错误: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("删除操作发生未知错误。");
    }
}

void MainWindow::handleSelect(const QString& command) {
    QRegularExpression re(
        R"(SELECT\s+(.+?)\s+FROM\s+([\w\.]+)(?:\s+WHERE\s+(.+))?\s*;?)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: SELECT <列名1,...|*> FROM <表名> [WHERE <条件>]");
        return;
    }

    QString select_cols_str = match.captured(1).trimmed();
    QString table_name = match.captured(2).trimmed();
    QString where_part = match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }

    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) { ui->show->appendPlainText("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }
    xhytable* table = db->find_table(table_name);
    if (!table) { ui->show->appendPlainText(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, current_db_name)); return; }

    ConditionNode conditionRoot;
    if (!parseWhereClause(where_part, conditionRoot)) { return; } // **Aufruf**

    QVector<xhyrecord> results;
    if (!db_manager.selectData(current_db_name, table_name, conditionRoot, results)) {
        return;
    }

    if (results.isEmpty()) {
        ui->show->appendPlainText("查询结果为空。");
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
                ui->show->appendPlainText(QString("错误: 选择的列 '%1' 在表 '%2' 中不存在。").arg(col, table_name));
                return;
            }
        }
    }

    QString header_str;
    for (const QString& col_name : display_columns) { header_str += col_name + "\t"; }
    ui->show->appendPlainText(header_str.trimmed());
    ui->show->appendPlainText(QString(header_str.length()*2 < 80 ? header_str.length()*2 : 80, '-'));

    for (const auto& record : results) {
        QString row_str;
        for (const QString& col_name : display_columns) {
            row_str += record.value(col_name) + "\t";
        }
        ui->show->appendPlainText(row_str.trimmed());
    }
}


void MainWindow::handleAlterTable(const QString& command) {
    QRegularExpression re(
        R"(ALTER\s+TABLE\s+([\w_]+)\s+(ADD|DROP|ALTER|MODIFY|RENAME)\s+(?:COLUMN\s+)?(.+?)\s*;?$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: ALTER TABLE <表名> ADD|DROP|ALTER|MODIFY|RENAME [COLUMN] <参数>");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString action = match.captured(2).trimmed().toUpper();
    QString parameters = match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }

    bool success = false;
    QString result_msg;
    bool transactionStartedHere = false;
    if(!db_manager.isInTransaction()){
        if(!db_manager.beginTransaction()) { /* ... */ } else transactionStartedHere = true;
    }

    try {
        if (action == "ADD") {
            if (parameters.toUpper().startsWith("CONSTRAINT")) {
                xhydatabase* db = db_manager.find_database(current_db_name);
                xhytable* table = db ? db->find_table(table_name) : nullptr;
                if(!table) throw std::runtime_error(("表 " + table_name + " 未找到").toStdString());
                handleTableConstraint(parameters, *table);
                success = db_manager.update_table(current_db_name, *table);
                result_msg = success ? "表级约束添加成功。" : "表级约束添加失败。";
            } else {
                QRegularExpression addColRe(R"(([\w_]+)\s+([\w\(\)]+)(?:\s+(.*))?)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch addMatch = addColRe.match(parameters);
                if (!addMatch.hasMatch()) throw std::runtime_error("语法错误: ADD COLUMN <列名> <类型>[(长度)] [列约束]");

                QString field_name = addMatch.captured(1).trimmed();
                QString type_str_full = addMatch.captured(2).trimmed();
                QString constraints_str = addMatch.captured(3).trimmed();
                int size_val = 0;
                xhyfield::datatype type = parseDataType(type_str_full, &size_val);
                QStringList constraints = parseConstraints(constraints_str);
                if ((type == xhyfield::CHAR || type == xhyfield::VARCHAR) && size_val > 0) {
                    bool sizeConstraintExists = false;
                    for(const QString& c : constraints) if(c.toUpper().startsWith("SIZE(")) sizeConstraintExists = true;
                    if(!sizeConstraintExists) constraints.prepend("SIZE(" + QString::number(size_val) + ")");
                }
                xhyfield new_field(field_name, type, constraints);
                success = db_manager.add_column(current_db_name, table_name, new_field);
                result_msg = success ? QString("列 '%1' 已添加到表 '%2'。").arg(field_name, table_name)
                                     : QString("添加列 '%1' 失败 (可能已存在或定义无效)。").arg(field_name);
            }
        } else if (action == "DROP") {
            if (parameters.toUpper().startsWith("CONSTRAINT")) {
                QRegularExpression dropConstrRe(R"(CONSTRAINT\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch dropMatch = dropConstrRe.match(parameters);
                if(!dropMatch.hasMatch()) throw std::runtime_error("语法错误: DROP CONSTRAINT <约束名>");
                QString constr_name = dropMatch.captured(1).trimmed();
                success = db_manager.drop_constraint(current_db_name, table_name, constr_name);
                result_msg = success ? QString("约束 '%1' 已删除。").arg(constr_name) : QString("删除约束 '%1' 失败。").arg(constr_name);
            } else {
                QString field_name = parameters.trimmed();
                if (field_name.contains(QRegularExpression("\\s"))) throw std::runtime_error("语法错误: DROP COLUMN <列名> (列名中不能有空格)");
                success = db_manager.drop_column(current_db_name, table_name, field_name);
                result_msg = success ? QString("列 '%1' 已从表 '%2' 删除。").arg(field_name, table_name)
                                     : QString("删除列 '%1' 失败 (可能不存在)。").arg(field_name);
            }
        } else if (action == "ALTER" || action == "MODIFY") {
            QRegularExpression alterColRe(R"(([\w_]+)\s+TYPE\s+([\w\(\)]+))", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch alterMatch = alterColRe.match(parameters);
            if(!alterMatch.hasMatch()) throw std::runtime_error("语法错误: ALTER/MODIFY COLUMN <列名> TYPE <新类型>[(长度)]");
            QString field_name = alterMatch.captured(1).trimmed();
            QString new_type_str_full = alterMatch.captured(2).trimmed();
            int size_val = 0;
            xhyfield::datatype new_type = parseDataType(new_type_str_full, &size_val);
            xhydatabase* db = db_manager.find_database(current_db_name);
            xhytable* table = db ? db->find_table(table_name) : nullptr;
            if (!table) throw std::runtime_error(("表 " + table_name + " 未找到").toStdString());
            const xhyfield* old_field = table->get_field(field_name);
            if(!old_field) throw std::runtime_error(("列 " + field_name + " 未找到").toStdString());
            QStringList new_constraints = old_field->constraints();
            new_constraints.removeIf([](const QString& c){ return c.toUpper().startsWith("SIZE("); });
            if ((new_type == xhyfield::CHAR || new_type == xhyfield::VARCHAR) && size_val > 0) {
                new_constraints.prepend("SIZE(" + QString::number(size_val) + ")");
            }
            xhyfield modified_field(field_name, new_type, new_constraints);
            success = db_manager.alter_column(current_db_name, table_name, field_name, modified_field);
            result_msg = success ? QString("列 '%1' 类型已修改。").arg(field_name) : QString("修改列 '%1' 类型失败。").arg(field_name);
        } else if (action == "RENAME") {
            if (parameters.toUpper().startsWith("COLUMN")) {
                QRegularExpression renameColRe(R"(COLUMN\s+([\w_]+)\s+TO\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch renameMatch = renameColRe.match(parameters);
                if(!renameMatch.hasMatch()) throw std::runtime_error("语法错误: RENAME COLUMN <旧列名> TO <新列名>");
                QString old_col_name = renameMatch.captured(1).trimmed();
                QString new_col_name = renameMatch.captured(2).trimmed();
                success = db_manager.rename_column(current_db_name, table_name, old_col_name, new_col_name);
                result_msg = success ? QString("列 '%1' 已重命名为 '%2'。").arg(old_col_name, new_col_name)
                                     : QString("重命名列 '%1' 失败。").arg(old_col_name);
            } else if (parameters.toUpper().startsWith("TO")) {
                QRegularExpression renameTableRe(R"(TO\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch renameMatch = renameTableRe.match(parameters);
                if(!renameMatch.hasMatch()) throw std::runtime_error("语法错误: RENAME TO <新表名>");
                QString new_table_name = renameMatch.captured(1).trimmed();
                success = db_manager.rename_table(current_db_name, table_name, new_table_name);
                result_msg = success ? QString("表 '%1' 已重命名为 '%2'。").arg(table_name, new_table_name)
                                     : QString("重命名表 '%1' 失败。").arg(table_name);
            } else {
                throw std::runtime_error("语法错误: RENAME COLUMN <旧列名> TO <新列名> 或 RENAME TO <新表名>");
            }
        } else {
            throw std::runtime_error(("不支持的 ALTER TABLE 操作: " + action).toStdString());
        }

        if (success) {
            if (transactionStartedHere) {
                if(db_manager.commitTransaction()) ui->show->appendPlainText(result_msg);
                else ui->show->appendPlainText("错误：事务提交失败。" + result_msg);
            } else {
                ui->show->appendPlainText(result_msg + " (在现有事务中)");
            }
        } else {
            if (transactionStartedHere) db_manager.rollbackTransaction();
            ui->show->appendPlainText("错误: " + result_msg);
        }

    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("ALTER TABLE 操作错误: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        ui->show->appendPlainText("ALTER TABLE 操作发生未知错误。");
    }
}


void MainWindow::handleExplainSelect(const QString& command) {
    QRegularExpression re(R"(EXPLAIN\s+SELECT\s+\*\s+FROM\s+([\w_]+)(?:\s+WHERE\s+(.+))?\s*;?)",
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: EXPLAIN SELECT * FROM <表名> [WHERE <条件>]");
        return;
    }

    QString tableName = match.captured(1).trimmed();
    QString wherePart = match.captured(2).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }

    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) { ui->show->appendPlainText("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }
    xhytable* table = db->find_table(tableName);
    if (!table) { ui->show->appendPlainText(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(tableName, current_db_name)); return; }

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
            ui->show->appendPlainText(QString("查询计划: 可能使用字段 '%1' 上的索引 '%2'。").arg(firstFieldNameInCondition, index->name()));
        } else {
            ui->show->appendPlainText(QString("查询计划: 字段 '%1' 上没有直接可用的索引，或索引不属于表 '%2'。可能进行全表扫描。").arg(firstFieldNameInCondition, tableName));
        }
    } else if (!wherePart.isEmpty()){
        ui->show->appendPlainText("查询计划: 无法从WHERE条件中简单提取用于索引检查的字段，可能进行全表扫描。");
    } else {
        ui->show->appendPlainText("查询计划: 无WHERE条件，将进行全表扫描。");
    }

    ui->show->appendPlainText("模拟执行查询以验证...");
    QVector<xhyrecord> results;
    if (db_manager.selectData(current_db_name, tableName, conditionRootForExplain, results)) {
        QString header_str;
        for (const auto& field : table->fields()) { header_str += field.name() + "\t"; }
        ui->show->appendPlainText(header_str.trimmed());
        ui->show->appendPlainText(QString(header_str.length()*2 < 80 ? header_str.length()*2 : 80, '-'));
        if (results.isEmpty()) {
            ui->show->appendPlainText("(无符合条件的数据)");
        } else {
            for (const auto& record : results) {
                QString row_str;
                for (const auto& field : table->fields()) { row_str += record.value(field.name()) + "\t"; }
                ui->show->appendPlainText(row_str.trimmed());
            }
            ui->show->appendPlainText(QString("共 %1 行结果。").arg(results.size()));
        }
    } else {
        ui->show->appendPlainText("执行 EXPLAIN 的模拟查询时出错。");
    }
}


void MainWindow::handleCreateIndex(const QString& command) {
    QRegularExpression re(R"(CREATE\s+(UNIQUE\s+)?INDEX\s+([\w_]+)\s+ON\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) {
        ui->show->appendPlainText("语法错误: CREATE [UNIQUE] INDEX <索引名> ON <表名> (<列1>[, <列2>...])");
        return;
    }
    bool unique = !match.captured(1).trimmed().isEmpty();
    QString idxname = match.captured(2).trimmed();
    QString tablename = match.captured(3).trimmed();
    QString colsPart = match.captured(4).trimmed();
    QStringList cols = colsPart.split(',', Qt::SkipEmptyParts);

    for(auto &c : cols) { c = c.trimmed().split(" ").first(); if (c.isEmpty()) {ui->show->appendPlainText("错误：索引列名不能为空。"); return;} }
    if (cols.isEmpty()){ ui->show->appendPlainText("错误：索引必须至少包含一列。"); return; }

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }

    auto* db = db_manager.find_database(current_db_name);
    if (!db) { ui->show->appendPlainText("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }

    xhytable* table = db->find_table(tablename);
    if (!table) { ui->show->appendPlainText(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(tablename, current_db_name)); return; }

    for(const auto &col : cols) {
        if (!table->has_field(col)) {
            ui->show->appendPlainText(QString("错误: 字段 '%1' 在表 '%2' 中不存在。").arg(col, tablename)); return;
        }
    }

    if(db->createIndex(xhyindex(idxname, tablename, cols, unique))) {
        ui->show->appendPlainText(QString("索引 '%1' 在表 '%2' 上创建成功。").arg(idxname, tablename));
    } else {
        ui->show->appendPlainText(QString("错误：创建索引 '%1' 失败 (可能已存在或名称/列定义无效)。").arg(idxname));
    }
}

void MainWindow::handleDropIndex(const QString& command) {
    QRegularExpression re(R"(DROP\s+INDEX\s+([\w_]+)(?:\s+ON\s+([\w_]+))?\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if(!match.hasMatch()){
        ui->show->appendPlainText("语法错误: DROP INDEX <索引名> [ON <表名>]");
        return;
    }
    QString idxname = match.captured(1).trimmed();
    QString onTableName = match.captured(2).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }
    auto* db = db_manager.find_database(current_db_name);
    if(!db){ ui->show->appendPlainText("错误: 数据库 '" + current_db_name + "' 未找到。"); return;}

    if(!onTableName.isEmpty()){
        const xhyindex* idxToDrop = db->findIndexByName(idxname);
        if(idxToDrop && idxToDrop->tableName().compare(onTableName, Qt::CaseInsensitive) != 0){
            ui->show->appendPlainText(QString("错误: 索引 '%1' 不属于表 '%2'。").arg(idxname, onTableName));
            return;
        }
    }

    if(db->dropIndex(idxname)) {
        ui->show->appendPlainText(QString("索引 '%1' 已删除。").arg(idxname));
    } else {
        ui->show->appendPlainText(QString("错误：删除索引 '%1' 失败 (可能不存在)。").arg(idxname));
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
    if (current_db_name.isEmpty()) { ui->show->appendPlainText("错误: 未选择数据库。"); return; }
    auto* db = db_manager.find_database(current_db_name);
    if(!db){ ui->show->appendPlainText("错误: 数据库 '" + current_db_name + "' 未找到。"); return;}

    QList<xhyindex> indexes = db->allIndexes();
    if(indexes.isEmpty()){
        ui->show->appendPlainText("数据库 '" + current_db_name + "' 中没有索引。");
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
            ui->show->appendPlainText(QString("表 '%1' 上没有找到索引。").arg(filterTableName));
        } else {
            ui->show->appendPlainText("当前数据库没有符合条件的索引。");
        }
    } else {
        ui->show->appendPlainText(output);
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
            ui->show->appendPlainText("错误: 无效的表级约束定义: " + constraint_str);
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
        if (columns.isEmpty()) { ui->show->appendPlainText("错误: PRIMARY KEY 约束必须指定列。"); return; }
        table.add_primary_key(columns);
        ui->show->appendPlainText(QString("表约束 '%1' (PRIMARY KEY on %2) 已添加。").arg(constraintName.isEmpty() ? "auto_pk" : constraintName, columns.join(",")));
    } else if (constraintTypeStr == "UNIQUE") {
        if (columns.isEmpty()) { ui->show->appendPlainText("错误: UNIQUE 约束必须指定列。"); return; }
        table.add_unique_constraint(columns, constraintName);
        ui->show->appendPlainText(QString("表约束 '%1' (UNIQUE on %2) 已添加。").arg(constraintName, columns.join(",")));
    } else if (constraintTypeStr == "CHECK") {
        if (columnsPart.isEmpty()) { ui->show->appendPlainText("错误: CHECK 约束必须指定条件表达式 (in Klammern)."); return; }
        table.add_check_constraint(columnsPart, constraintName);
        ui->show->appendPlainText(QString("表约束 '%1' (CHECK %2) 已添加。").arg(constraintName, columnsPart));
    } else if (constraintTypeStr == "FOREIGN_KEY") {
        if (columns.isEmpty()) { ui->show->appendPlainText("错误: FOREIGN KEY 约束必须指定列。"); return; }

        QRegularExpression fkRefRe(R"(REFERENCES\s+([\w_]+)\s*\(([\w_,\s]+)\))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch fkMatch = fkRefRe.match(remainingPart);
        if (!fkMatch.hasMatch()) {
            ui->show->appendPlainText("错误: FOREIGN KEY 约束缺少有效的 REFERENCES 子句。"); return;
        }
        QString referencedTable = fkMatch.captured(1).trimmed();
        QStringList referencedColumnsList;
        QString referencedColumnsPart = fkMatch.captured(2).trimmed();
        referencedColumnsList = referencedColumnsPart.split(',', Qt::SkipEmptyParts);
        for(QString& rcol : referencedColumnsList) rcol = rcol.trimmed();

        if (columns.size() != referencedColumnsList.size()) {
            ui->show->appendPlainText("错误: FOREIGN KEY 列数量与引用的列数量不匹配。"); return;
        }
        if(columns.size() == 1 && referencedColumnsList.size() == 1) {
            table.add_foreign_key(columns.first(), referencedTable, referencedColumnsList.first(), constraintName);
            ui->show->appendPlainText(QString("表约束 '%1' (FOREIGN KEY %2 REFERENCES %3(%4)) 已添加。")
                                          .arg(constraintName, columns.first(), referencedTable, referencedColumnsList.first()));
        } else {
            ui->show->appendPlainText(QString("表约束 '%1' (FOREIGN KEY (%2) REFERENCES %3(%4)) 已添加。 (提示: 组合外键逻辑需要在xhytable中特别处理)")
                                          .arg(constraintName, columns.join(", "), referencedTable, referencedColumnsList.join(", ")));
            ui->show->appendPlainText("警告: 此UI的组合外键逻辑仅为简化表示。");
        }
    } else {
        ui->show->appendPlainText("错误: 不支持的表约束类型: " + constraintTypeStr);
    }
}

void MainWindow::show_databases() {
    auto databases = db_manager.databases();
    if (databases.isEmpty()) {
        ui->show->appendPlainText("没有数据库。");
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
    ui->show->appendPlainText(output.trimmed());
}

void MainWindow::show_tables(const QString& db_name_input) {
    QString db_name = db_name_input.trimmed();
    if (db_name.isEmpty()) {
        ui->show->appendPlainText("错误: 未指定数据库名，或当前未选择数据库。");
        return;
    }
    xhydatabase* db = db_manager.find_database(db_name);
    if (!db) {
        ui->show->appendPlainText(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        return;
    }
    auto tables = db->tables();
    if (tables.isEmpty()) {
        ui->show->appendPlainText(QString("数据库 '%1' 中没有表。").arg(db_name));
        return;
    }
    QString output = QString("数据库 '%1' 中的表:\n").arg(db_name);
    for (const auto& table : tables) {
        output += "  " + table.name() + "\n";
    }
    ui->show->appendPlainText(output.trimmed());
}

void MainWindow::show_schema(const QString& db_name_input, const QString& table_name_input) {
    QString db_name = db_name_input.trimmed();
    QString table_name = table_name_input.trimmed();

    if (db_name.isEmpty()) { ui->show->appendPlainText("错误: 未指定数据库名。"); return; }
    if (table_name.isEmpty()) { ui->show->appendPlainText("错误: 未指定表名。"); return; }

    xhydatabase* db = db_manager.find_database(db_name);
    if (!db) {
        ui->show->appendPlainText(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        return;
    }
    xhytable* table = db->find_table(table_name);
    if (!table) {
        ui->show->appendPlainText(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, db_name));
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
    ui->show->appendPlainText(output.trimmed());
}
// ENDE von mainwindow.cpp
