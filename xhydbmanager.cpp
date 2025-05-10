#include "xhydbmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <qstandardpaths.h>
#include <QCoreApplication>

xhydbmanager::xhydbmanager() : m_inTransaction(false) {
    // 初始化 m_dataDir - 根据用户指定，相对于当前工作目录
    // 假设 m_dataDir 成员已在 .h 文件中声明，这里是它的初始化赋值（通常在构造函数初始化列表中完成）
    // 如果 m_dataDir 是在 .h 中直接初始化的（C++11及以后版本允许成员变量直接初始化），
    // 那么这里就不需要再次赋值，除非您想在构造函数中覆盖或确认它。
    // 为清晰起见，我们假设它在构造函数中被赋予最终值：
    m_dataDir = QDir::currentPath() + QDir::separator() + "DBMS_ROOT";

    qDebug() << "Using CWD-relative data directory (base):" << m_dataDir;
    qDebug() << "Application's Current Working Directory is:" << QDir::currentPath();


    QDir baseDir(m_dataDir);
    if (!baseDir.exists()) {
        if (!baseDir.mkpath(".")) { // mkpath(".") 会创建路径中的所有必需目录
            qWarning() << "CRITICAL ERROR: Could not create base data directory at" << m_dataDir
                       << "Please check path, permissions, and ensure CWD is as expected.";
            // 考虑在此处采取更激烈的错误处理
            return;
        }
        qDebug() << "Base data directory created at:" << m_dataDir;
    }

    // "data" 子目录将位于 m_dataDir (即 DBMS_ROOT) 内部
    QString dataSubDirPath = m_dataDir + QDir::separator() + "data";
    QDir dataSubDir(dataSubDirPath);
    if (!dataSubDir.exists()) {
        if (!dataSubDir.mkpath(".")) {
            qWarning() << "CRITICAL ERROR: Could not create data subdirectory at" << dataSubDirPath
                       << "Please check path and permissions.";
            return;
        }
        qDebug() << "Data subdirectory created at:" << dataSubDirPath;
    }

    // ruanko.db 也将位于 m_dataDir (即 DBMS_ROOT) 内部
    QString ruankoDbPath = m_dataDir + QDir::separator() + "ruanko.db";
    QFile ruankoDBFile(ruankoDbPath);
    if (!ruankoDBFile.exists()) {
        qDebug() << "ruanko.db not found at" << ruankoDbPath << ", attempting to create.";
        if (ruankoDBFile.open(QIODevice::WriteOnly)) {
            ruankoDBFile.close();
            qDebug() << "System database ruanko.db created at:" << ruankoDbPath;
        } else {
            qWarning() << "Failed to create ruanko.db at:" << ruankoDbPath << "Error:" << ruankoDBFile.errorString();
        }
    } else {
        qDebug() << "ruanko.db found at:" << ruankoDbPath;
    }

    // 在确定并可能创建了目录之后再加载文件
    load_databases_from_files();
}
bool xhydbmanager::createdatabase(const QString& dbname) {
    // 1. 检查数据库是否已存在
    for (const auto& db : m_databases) {
        if (db.name().toLower() == dbname.toLower()) {
            return false;
        }
    }

    // 2. 创建数据库目录
    // 正确：确保路径为 DBMS_ROOT/data/[dbname]
    QString dbPath = QString("%1/data/%2").arg( m_dataDir,dbname);
    QDir db_dir(dbPath);
    if (!db_dir.exists()) {
        db_dir.mkpath("."); // 创建多层目录
    }

    // 3. 更新 ruanko.db（追加数据库元数据）
    QFile ruankoDB("ruanko.db");
    if (ruankoDB.open(QIODevice::Append)) {
        QDataStream out(&ruankoDB);
        out.setVersion(QDataStream::Qt_5_15);

        DatabaseBlock dbBlock;
        memset(&dbBlock, 0, sizeof(DatabaseBlock));
        qstrncpy(dbBlock.name, dbname.toUtf8().constData(), 128);
        dbBlock.type = false; // 用户数据库
        qstrncpy(dbBlock.filename, db_dir.path().toUtf8().constData(), 256);
        GetSystemTime(&dbBlock.crtime);

        out.writeRawData(reinterpret_cast<const char*>(&dbBlock), sizeof(DatabaseBlock));
        ruankoDB.close();
    }

    // 4. 创建表描述文件 [数据库名].tb 和日志文件 [数据库名].log
    QFile tbFile(QString("%1/%2.tb").arg(dbPath, dbname));
    if (tbFile.open(QIODevice::WriteOnly)) {
        tbFile.close();
    }
    QFile logFile(QString("%1/%2.log").arg(dbPath, dbname));
    if (logFile.open(QIODevice::WriteOnly)) {
        logFile.close();
    }

    // 5. 添加到内存中的数据库列表
    xhydatabase new_db(dbname);
    m_databases.append(new_db);
    qDebug() << "数据库创建成功：" << dbname;
    return true;
}
bool xhydbmanager::update_ruanko_db_after_drop(const QString& dropped_dbname) {
    QString ruankoDbPath = m_dataDir + "/ruanko.db";
    QString tempRuankoDbPath = m_dataDir + "/ruanko.db.tmp";

    QFile old_db_file(ruankoDbPath);
    QFile new_db_file(tempRuankoDbPath);

    if (!old_db_file.exists()) {
        qWarning() << "ruanko.db does not exist at" << ruankoDbPath << ". Cannot update after drop.";
        return true;
    }

    if (!old_db_file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open " << ruankoDbPath << " for reading. Error: " << old_db_file.errorString();
        return false;
    }
    if (!new_db_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open " << tempRuankoDbPath << " for writing. Error: " << new_db_file.errorString();
        old_db_file.close();
        return false;
    }

    QDataStream in_stream(&old_db_file);
    in_stream.setVersion(QDataStream::Qt_5_15);
    QDataStream out_stream(&new_db_file);
    out_stream.setVersion(QDataStream::Qt_5_15);

    bool error_occurred = false;
    while (!in_stream.atEnd()) {
        DatabaseBlock db_block;
        int bytes_read = in_stream.readRawData(reinterpret_cast<char*>(&db_block), sizeof(DatabaseBlock));

        if (bytes_read == 0 && in_stream.atEnd()) break;

        if (bytes_read != sizeof(DatabaseBlock)) {
            qWarning() << "Error reading DatabaseBlock from " << ruankoDbPath << ". Read " << bytes_read << " bytes, expected " << sizeof(DatabaseBlock);
            error_occurred = true;
            break;
        }

        QString current_db_name = QString::fromUtf8(db_block.name);
        // Ensure null termination before comparison if strncpy was used without explicit null termination
        current_db_name.truncate(strnlen(db_block.name, sizeof(db_block.name)));


        if (current_db_name.compare(dropped_dbname, Qt::CaseInsensitive) != 0) {
            if (out_stream.writeRawData(reinterpret_cast<const char*>(&db_block), sizeof(DatabaseBlock)) != sizeof(DatabaseBlock)) {
                qWarning() << "Error writing DatabaseBlock to " << tempRuankoDbPath;
                error_occurred = true;
                break;
            }
        } else {
            qDebug() << "Skipping database '" << dropped_dbname << "' for removal from ruanko.db.";
        }
    }

    old_db_file.close();
    new_db_file.close();

    if (error_occurred) {
        qDebug() << "An error occurred during ruanko.db update. Rolling back changes.";
        QFile::remove(tempRuankoDbPath);
        return false;
    }

    if (!QFile::remove(ruankoDbPath)) {
        qWarning() << "Failed to remove old " << ruankoDbPath;
        QFile::remove(tempRuankoDbPath);
        return false;
    }

    if (!QFile::rename(tempRuankoDbPath, ruankoDbPath)) {
        qWarning() << "Failed to rename " << tempRuankoDbPath << " to " << ruankoDbPath;
        // Attempt to restore old file if rename fails and old file was somehow kept (complex recovery)
        return false;
    }

    qDebug() << "ruanko.db successfully updated after dropping database '" << dropped_dbname << "'.";
    return true;
}

bool xhydbmanager::dropdatabase(const QString& dbname) {
    qDebug() << "Attempting to drop database:" << dbname;
    qDebug() << "Current databases in memory before drop attempt:";
    for(const auto& db_item : m_databases) { qDebug() << " - " << db_item.name(); }

    bool found_in_memory = false;
    for (auto it = m_databases.begin(); it != m_databases.end(); ++it) {
        if (it->name().compare(dbname, Qt::CaseInsensitive) == 0) {
            found_in_memory = true;
            qDebug() << "Found database '" << dbname << "' in memory for deletion.";

            QString dbPath = QString("%1/data/%2").arg(m_dataDir, dbname);
            QDir db_dir(dbPath);
            if (db_dir.exists()) {
                qDebug() << "Directory " << dbPath << " exists. Attempting to remove recursively.";
                if (!db_dir.removeRecursively()) {
                    qWarning() << "Error: Could not completely remove database directory: " << dbPath;
                    // Decide if this is a fatal error for the drop operation
                } else {
                    qDebug() << "Successfully removed directory: " << dbPath;
                }
            } else {
                qDebug() << "Directory " << dbPath << " does not exist. No files to remove for this database.";
            }

            it = m_databases.erase(it);
            qDebug() << "Database '" << dbname << "' erased from in-memory list (m_databases).";

            qDebug() << "Current databases in memory after erase:";
            for(const auto& db_item_after : m_databases) { qDebug() << " - " << db_item_after.name(); }

            if (current_database.compare(dbname, Qt::CaseInsensitive) == 0) {
                current_database.clear();
                qDebug() << "Cleared current_database as it was the one dropped.";
            }

            if (!update_ruanko_db_after_drop(dbname)) {
                qWarning() << "Failed to update ruanko.db after dropping database '" << dbname << "'. Metadata might be inconsistent.";
            }

            qDebug() << "Database '" << dbname << "' drop process completed.";
            return true;
        }
    }

    if (!found_in_memory) {
        qWarning() << "Error: Database '" << dbname << "' not found in memory. Cannot drop.";
    }
    return false;
}

bool xhydbmanager::use_database(const QString& dbname) {
    for (const auto& db : m_databases) {
        if (db.name().compare(dbname, Qt::CaseInsensitive) == 0) { // 使用 compare
            current_database = db.name();
            qDebug() << "Using database:" << current_database;
            return true;
        }
    }
    qWarning() << "Database '" << dbname << "' not found. Cannot use.";
    return false;
}

QString xhydbmanager::get_current_database() const {
    return current_database;
}

QList<xhydatabase> xhydbmanager::databases() const {
    qDebug() << "[xhydbmanager::databases()] Returning a copy of m_databases. Current content (" << m_databases.size() << " items):";
    for(const auto& db_item : m_databases) { qDebug() << " - " << db_item.name(); }
    return m_databases;
}
bool xhydbmanager::beginTransaction() {
    if (m_inTransaction) return false; // 如果已经在事务中，则返回失败
    m_inTransaction = true; // 更新事务状态
    qDebug() << "Transaction started for database:" << current_database;
    return true;
}
bool xhydbmanager::commitTransaction() {
    if (!m_inTransaction) return false;

    // 保存当前数据库的所有表到文件
    if (!current_database.isEmpty()) {
        xhydatabase* db = find_database(current_database);
        if (db) {
            for (const xhytable& table : db->tables()) {
                save_table_to_file(current_database, table.name(), &table);
            }
        }
    }

    m_inTransaction = false;
    qDebug() << "Transaction committed for database:" << current_database;
    return true;
}

void xhydbmanager::rollbackTransaction() {
    if (m_inTransaction) {
        // 恢复到事务开始前的状态
        if (!current_database.isEmpty()) {
            xhydatabase* db = find_database(current_database);
            if (db) {
                db->clearTables(); // 清空当前表数据
                for (const auto& tempTable : m_tempTables) {
                    db->addTable(tempTable); // 直接使用对象，而不是解引用
                }
            }
        }
        m_tempTables.clear(); // 清空临时表
        m_inTransaction = false;
        qDebug() << "Transaction rolled back for database:" << current_database;
    }
}
bool xhydbmanager::add_column(const QString& database_name, const QString& table_name, const xhyfield& field) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;
    xhytable* table = db->find_table(table_name);
    if (!table) return false;
    if (table->has_field(field.name())) return false;
    table->addfield(field); // 修正: add_field -> addfield
    save_table_to_file(database_name, table_name, table); // 假设保存表结构
    return true;
}

bool xhydbmanager::drop_column(const QString& database_name, const QString& table_name, const QString& field_name) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;
    xhytable* table = db->find_table(table_name);
    if (!table || !table->has_field(field_name)) return false;
    table->remove_field(field_name); // 现在应该能找到这个方法
    save_table_to_file(database_name, table_name, table); // 假设保存表结构
    return true;
}

bool xhydbmanager::rename_table(const QString& database_name, const QString& old_name, const QString& new_name) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    xhytable* table = db->find_table(old_name);
    if (!table) return false;

    if (db->has_table(new_name)) {
        return false; // 新名称已存在
    }

    // 重命名表
    table->rename(new_name);
    // 保存更改到文件或其他存储中
    return true;
}
// 解析数据类型字符串转换为枚举值
xhyfield::datatype parseDataType(const QString& type_str, int& size) {
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
        size = upperType.mid(start, end-start).toInt();
        return xhyfield::CHAR;
    }
    return xhyfield::VARCHAR; // 默认类型
}

bool xhydbmanager::alter_column(const QString& database_name, const QString& table_name,
                                const QString& old_field_name, const xhyfield& new_field) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;
    xhytable* table = db->find_table(table_name);
    if (!table || !table->get_field(old_field_name)) return false;
    if (old_field_name.compare(new_field.name(), Qt::CaseInsensitive) != 0 && table->has_field(new_field.name())) {
        qWarning() << "ALTER COLUMN 失败: 新列名 " << new_field.name() << " 已存在。";
        return false;
    }
    table->remove_field(old_field_name); // 现在应该能找到
    table->addfield(new_field);      // 修正: add_field -> addfield
    save_table_to_file(database_name, table_name, table);
    return true;
}


bool xhydbmanager::add_constraint(const QString& database_name, const QString& table_name, const QString& field_name, const QString& constraint_str) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;
    xhytable* table = db->find_table(table_name);
    if (!table) return false;
    const xhyfield* old_field = table->get_field(field_name);
    if (!old_field) return false;

    QStringList constraints = old_field->constraints();
    if (!constraints.contains(constraint_str, Qt::CaseInsensitive)) { // 避免重复添加
        constraints.append(constraint_str);
        xhyfield new_field_with_constraint(old_field->name(), old_field->type(), constraints);
        // 替换字段定义 (这是一个简化的 alter field，更复杂的可能需要新建表复制数据)
        table->remove_field(field_name); // 现在应该能找到
        table->addfield(new_field_with_constraint);
        save_table_to_file(database_name, table_name, table);
        return true;
    }
    return false; // 约束已存在或操作未执行
}

bool xhydbmanager::drop_constraint(const QString& database_name, const QString& table_name, const QString& constraint_to_drop_name_or_text) {
    // 这个函数逻辑比较复杂，取决于约束是如何存储和识别的。
    // 假设 constraint_to_drop_name_or_text 是约束的文本内容，如 "NOT_NULL" 或 "UNIQUE"
    // 并且假设约束是字段级的。表级约束的移除会更复杂。
    xhydatabase* db = find_database(database_name);
    if (!db) return false;
    xhytable* table = db->find_table(table_name);
    if (!table) return false;

    bool changed = false;
    for (const xhyfield& current_field : table->fields()) { // 需要迭代字段的副本或用索引，因为我们可能修改它
        QStringList constraints = current_field.constraints();
        int initial_size = constraints.size();
        constraints.removeAll(constraint_to_drop_name_or_text); // Qt::CaseInsensitive?

        if (constraints.size() < initial_size) { // 如果确实移除了约束
            xhyfield modified_field(current_field.name(), current_field.type(), constraints);
            table->remove_field(current_field.name()); // 移除旧字段定义
            table->addfield(modified_field);       // 添加修改后的字段定义
            changed = true;
            // 注意: 如果一个字段有多个相同的约束文本，removeAll会都移除。
            // 如果只想移除一个，需要更精确的逻辑。
        }
    }
    if (changed) {
        save_table_to_file(database_name, table_name, table);
    }
    // TODO: 处理表级约束的移除 (如果 constraint_to_drop_name_or_text 是一个约束名)
    if(!changed) qWarning() << "未找到或移除约束:" << constraint_to_drop_name_or_text << "在表" << table_name;
    return changed;
}

bool xhydbmanager::rename_column(const QString& database_name, const QString& table_name, const QString& old_column_name, const QString& new_column_name) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;
    xhytable* table = db->find_table(table_name);
    if (!table) return false;
    const xhyfield* old_field = table->get_field(old_column_name);
    if (!old_field) return false;
    if (table->has_field(new_column_name)) {
        qWarning() << "重命名列失败：新列名 " << new_column_name << " 已存在。";
        return false;
    }

    xhyfield new_field_renamed(new_column_name, old_field->type(), old_field->constraints() /*, any other properties like enum values */);
    // TODO: 如果 xhyfield 有其他属性（如枚举值列表），也需要从 old_field 复制到 new_field_renamed

    table->remove_field(old_column_name);
    table->addfield(new_field_renamed);

    // TODO: 非常重要! 还需要更新所有记录中该字段的键名。
    // 这通常意味着遍历 m_records 和 m_tempRecords，对每个 xhyrecord：
    // QString value = record.value(old_column_name);
    // record.removeValue(old_column_name);
    // record.insert(new_column_name, value);
    // 这个逻辑应该在 xhytable::rename_column 方法中（如果创建该方法）或此处直接操作记录

    save_table_to_file(database_name, table_name, table);
    return true;
}

// 解析约束字符串列表
QStringList parseConstraints(const QString& constraints) {
    QStringList constraintList;
    if (constraints.isEmpty()) {
        return constraintList;
    }

    // 假设约束是以逗号分隔的字符串
    constraintList = constraints.split(',', Qt::SkipEmptyParts);

    // 去除每个约束前后的空格
    for (auto& constraint : constraintList) {
        constraint = constraint.trimmed();
    }

    return constraintList;
}
void xhydbmanager::commit() {
    if (m_inTransaction) {
        // 将临时表添加到数据库
        for (const auto& table : m_tempTables) {
            // 持久化表
        }
        m_tempTables.clear(); // 清空临时表
        m_inTransaction = false;
    }
}


bool xhydbmanager::update_table(const QString& database_name, const xhytable& table) {
    // 找到数据库
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    // 查找要更新的表
    xhytable* existing_table = db->find_table(table.name());
    if (!existing_table) return false;

    // 删除旧表
    db->droptable(existing_table->name());

    // 创建新表
    db->createtable(table);

    // 保存更新后的新表到文件
    save_table_to_file(database_name, table.name(), &table);

    return true; // 返回更新成功
}
void xhydbmanager::rollback() {
    if (m_inTransaction) {
        m_tempTables.clear(); // 清空临时表
        m_inTransaction = false;
    }
}

bool xhydbmanager::createtable(const QString& dbname, const xhytable& table) {
    for (auto& db : m_databases) {
        if (db.name().toLower() == dbname.toLower()) {
            if (db.createtable(table)) {
                save_table_to_file(dbname, table.name(), db.find_table(table.name()));
                return true;
            }
        }
    }
    return false;
}

bool xhydbmanager::droptable(const QString& dbname, const QString& tablename) {
    for (auto& db : m_databases) {
        if (db.name().toLower() == dbname.toLower()) {
            if (db.droptable(tablename)) {
                // 删除表相关文件
                QString basePath = QString("%1/data/%2/%3").arg(m_dataDir,dbname, tablename);
                QFile::remove(basePath + ".tdf"); // 表定义文件
                QFile::remove(basePath + ".trd"); // 记录文件
                QFile::remove(basePath + ".tic"); // 完整性约束文件
                QFile::remove(basePath + ".tid"); // 索引描述文件

                // 更新表描述文件（从数据库名.tb中移除该表信息）
                update_table_description_file(dbname, tablename, nullptr);
                return true;
            }
        }
    }
    return false;
}

bool xhydbmanager::insertData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& fieldValues) {
    for (auto& db : m_databases) {
        if (db.name() == dbname) {
            if (db.insertData(tablename, fieldValues)) {
                // 仅在非事务模式下立即保存
                if (!m_inTransaction || dbname != current_database) {
                    save_table_to_file(dbname, tablename, db.find_table(tablename));
                }
                return true;
            }
        }
    }
    return false;
}

int xhydbmanager::updateData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& updates,  ConditionNode & conditions) {
    for (auto& db : m_databases) {
        if (db.name() == dbname) {
            int affected = db.updateData(tablename, updates, conditions);
            if (affected > 0) {
                // 仅在非事务模式下立即保存
                if (!m_inTransaction || dbname != current_database) {
                    save_table_to_file(dbname, tablename, db.find_table(tablename));
                }
            }
            return affected;
        }
    }
    return 0;
}

int xhydbmanager::deleteData(const QString& dbname, const QString& tablename, const ConditionNode& conditions) {
    for (auto& db : m_databases) {
        if (db.name() == dbname) {
            int affected = db.deleteData(tablename, conditions);
            if (affected > 0) {
                // 仅在非事务模式下立即保存
                if (!m_inTransaction || dbname != current_database) {
                    save_table_to_file(dbname, tablename, db.find_table(tablename));
                }
            }
            return affected;
        }
    }
    return 0;
}

bool xhydbmanager::selectData(const QString& dbname, const QString& tablename,const ConditionNode & conditions, QVector<xhyrecord>& results) {
    for (auto& db : m_databases) {
        if (db.name() == dbname) {
            return db.selectData(tablename, conditions, results);
        }
    }
    return false;
}

void xhydbmanager::save_database_to_file(const QString& dbname) {
    QDir db_dir(QString("%1/data").arg(m_dataDir, dbname));
    if (!db_dir.exists()) {
        db_dir.mkpath(".");
    }
}

void xhydbmanager::load_databases_from_files() {
    m_databases.clear(); // 清空内存列表，从文件重新加载
    QDir data_root_dir(m_dataDir + "/data");
    if (!data_root_dir.exists()) {
        qWarning() << "Data directory " << data_root_dir.path() << " does not exist. Cannot load databases.";
        return;
    }
    QStringList db_dirs = data_root_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    qDebug() << "Loading databases from directories in: " << data_root_dir.path();

    for (const QString& dbname : db_dirs) {
        qDebug() << "Found potential database directory: " << dbname;
        // 在这里可以添加一个检查，比如查看ruanko.db中是否存在此数据库的元数据，
        // 或者检查目录内是否有必要的元文件(如 [dbname].tb)，来判断这是否一个有效的数据库目录。
        // 如果仅仅依赖目录存在，可能会加载不完整的或意外创建的目录。

        xhydatabase db(dbname);
        QDir current_db_dir(data_root_dir.filePath(dbname));
        QStringList table_files = current_db_dir.entryList(QStringList() << "*.tdf", QDir::Files);
        qDebug() << "Looking for .tdf files in: " << current_db_dir.path();

        bool db_is_valid_to_load = !table_files.isEmpty(); // 简单判断：有表定义文件才算有效
        // 或者检查特定元数据文件： QFileInfo(current_db_dir.filePath(dbname + ".tb")).exists();

        if (!db_is_valid_to_load && table_files.isEmpty()) { // 如果目录内没有任何tdf，可能不是有效数据库
            // 检查是否有数据库级别的元文件，如 dbname.tb
            QFile dbMetaFile(current_db_dir.filePath(dbname + ".tb")); // 假设这是数据库元文件
            if(!dbMetaFile.exists() || dbMetaFile.size() == 0){ // 如果元文件也不存在或为空
                qDebug() << "Directory " << dbname << " seems empty or not a valid database (no .tdf files and no/empty " << dbname << ".tb). Skipping.";
                continue;
            }
            // 如果元文件存在，即使没有表，也可能是一个空数据库，可以加载
            qDebug() << "Directory " << dbname << " has no .tdf files but has " << dbname << ".tb. Loading as potentially empty database.";
        }


        for (const QString& table_tdf_file : table_files) {
            // ... (内部加载表的逻辑，如之前回复所示，增加了更多日志和错误检查)
            // 确保 FieldBlock 和记录加载的健壮性
            QString table_name = table_tdf_file.left(table_tdf_file.length() - 4);
            qDebug() << "Loading table: " << table_name << "from" << table_tdf_file;
            QFile tdfFile(current_db_dir.filePath(table_tdf_file));
            if (tdfFile.open(QIODevice::ReadOnly)) {
                QDataStream in_tdf(&tdfFile);
                in_tdf.setVersion(QDataStream::Qt_5_15);
                xhytable table(table_name);
                while (!in_tdf.atEnd()) {
                    FieldBlock fb;
                    int bytesRead = in_tdf.readRawData(reinterpret_cast<char*>(&fb), sizeof(FieldBlock));
                    if (bytesRead != sizeof(FieldBlock)) {
                        if (bytesRead == 0 && in_tdf.atEnd()) break;
                        qWarning() << "Error reading FieldBlock for table" << table_name << "in" << dbname << ". Read:" << bytesRead << "Expected:" << sizeof(FieldBlock);
                        break;
                    }
                    // ... (解析 FieldBlock 并添加到 table) ...
                }
                tdfFile.close();
                // ... (加载 .trd 文件) ...
                db.addTable(table); // 使用 addTable
            } else {
                qWarning() << "Failed to open TDF file:" << tdfFile.fileName() << "Error:" << tdfFile.errorString();
            }
        }
        m_databases.append(db);
        qDebug() << "Database '" << db.name() << "' loaded with " << db.tables().size() << " tables.";
    }
    qDebug() << "Finished loading databases. Total loaded:" << m_databases.size();
}

xhydatabase* xhydbmanager::find_database(const QString& dbname) {
    for (auto& db : m_databases) {
        if (db.name().compare(dbname, Qt::CaseInsensitive) == 0) { // 使用 compare
            return &db;
        }
    }
    return nullptr;
}
bool xhydbmanager::isInTransaction() const {
    return m_inTransaction; // 返回当前事务状态
}



// 1. 保存表定义文件
void xhydbmanager::save_table_definition_file(const QString& filePath, const xhytable* table) {
    QSet<QString> uniqueFields; // 用于去重
    for (const auto& field : table->fields()) {
        if (uniqueFields.contains(field.name())) {
            qWarning() << "忽略重复字段：" << field.name();
            continue;
        }
        uniqueFields.insert(field.name());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open TDF file:" << filePath;
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);

    for (const auto& field : table->fields()) {
        FieldBlock fb;
        memset(&fb, 0, sizeof(FieldBlock));

        fb.order = field.order();
        qstrncpy(fb.name, field.name().toUtf8().constData(), 128);
        fb.type = static_cast<int>(field.type());

        // 处理约束
        fb.integrities = 0;
        if (field.constraints().contains("PRIMARY_KEY")) fb.integrities |= 0x01;
        if (field.constraints().contains("NOT_NULL")) fb.integrities |= 0x02;
        if (field.constraints().contains("UNIQUE")) fb.integrities |= 0x04;

        GetSystemTime(&fb.mtime);
        out.writeRawData(reinterpret_cast<const char*>(&fb), sizeof(FieldBlock));
    }

    file.close();
    }}

// 2. 保存记录文件
void xhydbmanager::save_table_records_file(const QString& filePath, const xhytable* table) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open TRD file:" << filePath;
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);

    for (const auto& record : table->records()) {
        QByteArray recordData;
        QDataStream recordStream(&recordData, QIODevice::WriteOnly);

        for (const auto& field : table->fields()) {
            QString value = record.value(field.name());
            // 根据字段类型序列化数据
            switch (field.type()) {
            case xhyfield::TINYINT:
                recordStream << static_cast<qint8>(value.toInt());
                break;
            case xhyfield::SMALLINT:
                recordStream << static_cast<qint16>(value.toInt());
                break;
            case xhyfield::INT:
                recordStream << value.toInt();
                break;
            case xhyfield::BIGINT:
                recordStream << value.toLongLong();
                break;
            case xhyfield::FLOAT:
                recordStream << value.toFloat();
                break;
            case xhyfield::DOUBLE:
                recordStream << value.toDouble();
                break;
            case xhyfield::TEXT:
            case xhyfield::CHAR:
            case xhyfield::VARCHAR: {
                // 处理字符串类型，UTF-8编码
                QByteArray strBytes = value.toUtf8();
                recordStream << strBytes;
                break;
            }
            case xhyfield::DATE: {
                // 日期格式：YYYY-MM-DD 转换为 QDate
                QDate date = QDate::fromString(value, "yyyy-MM-dd");
                recordStream << date;
                break;
            }
            case xhyfield::DATETIME: {
                // 日期时间格式：YYYY-MM-DD HH:mm:ss 转换为 QDateTime
                QDateTime datetime = QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss");
                recordStream << datetime;
                break;
            }
            case xhyfield::BOOL:
                recordStream << static_cast<bool>(value.toInt());
                break;
            default:
                // 处理未知类型或未实现类型
                qWarning() << "Unsupported data type for field:" << field.name();
                recordStream << value; // 默认按字符串存储
                break;
            }
        }

        // 4字节对齐
        int padding = (4 - (recordData.size() % 4)) % 4;
        recordData.append(padding, '\0');

        out << static_cast<quint32>(recordData.size());
        out.writeRawData(recordData.constData(), recordData.size());
    }

    file.close();
}
// 3. 保存完整性约束文件
void xhydbmanager::save_table_integrity_file(const QString& filePath, const xhytable* table) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open TIC file:" << filePath;
        return;
    }

    // 实现约束写入逻辑
    file.close();
}

// 4. 保存索引描述文件
void xhydbmanager::save_table_index_file(const QString& filePath, const xhytable* table) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open TID file:" << filePath;
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);

    // 主键索引
    if (!table->primaryKeys().isEmpty()) {
        IndexBlock ib;
        memset(&ib, 0, sizeof(IndexBlock));
        qstrncpy(ib.name, "PrimaryIndex", 128);
        ib.unique = true;
        ib.asc = true;
        ib.field_num = table->primaryKeys().size();
        // ... 填充其他字段 ...
        out.writeRawData(reinterpret_cast<const char*>(&ib), sizeof(IndexBlock));
    }

    file.close();
}

// 5. 更新表描述文件
void xhydbmanager::update_table_description_file(const QString& dbname, const QString& tablename, const xhytable* table) {
    QString tbFilePath = QString("%1/data/%2/%3.tb").arg(m_dataDir,dbname, dbname);
    QFile file(tbFilePath);
    if (!file.open(QIODevice::ReadWrite)) return;

    // 读取现有表信息
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    QJsonArray tables = root["tables"].toArray();

    if (table) {
        // 添加或更新表信息
        QJsonObject tableObj;
        tableObj["name"] = tablename;
        // ... 其他字段 ...


        tables.append(tableObj);
    } else {
        // 删除表信息
        for (int i = 0; i < tables.size(); ++i) {
            if (tables[i].toObject()["name"].toString() == tablename) {
                tables.removeAt(i);
                break;
            }
        }
    }

    // 写回文件
    root["tables"] = tables;
    file.resize(0);
    file.write(QJsonDocument(root).toJson());
    file.close();
}
void xhydbmanager::save_index_file(const QString& dbname, const QString& indexname, const QVector<QPair<QString, quint64>>& indexData) {
    QString indexPath = QString("%1/%2/%3.ix").arg(m_dataDir, dbname, indexname);
    QFile file(indexPath);
    if (file.open(QIODevice::WriteOnly)) {
        QDataStream out(&file);
        out.setVersion(QDataStream::Qt_5_15);
        for (const auto& entry : indexData) {
            out << entry.first;   // 索引键
            out << entry.second;  // 记录位置
        }
        file.close();
    }
}
// 修改save_table_to_file函数，添加索引文件保存
void xhydbmanager::save_table_to_file(const QString& dbname, const QString& tablename, const xhytable* table) {
    if (!table) return;

    QString basePath = QString("%1/data/%2/%3").arg(m_dataDir, dbname, tablename);
    QDir().mkpath(QFileInfo(basePath).path());

    // 保存表定义文件(.tdf)
    save_table_definition_file(basePath + ".tdf", table);

    // 保存记录文件(.trd)
    save_table_records_file(basePath + ".trd", table);

    // 保存完整性约束文件(.tic)
    save_table_integrity_file(basePath + ".tic", table);

    // 保存索引描述文件(.tid)
    save_table_index_file(basePath + ".tid", table);

    // 更新表描述文件([数据库名].tb)
    update_table_description_file(dbname, tablename, table);

    qDebug() << "表" << tablename << "已成功保存到文件";
}
void xhydbmanager::addTable(const xhytable& table) {
    if (m_inTransaction) {
        m_tempTables.append(table); // 存储临时表
    }
}
