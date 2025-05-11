#include "xhydbmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QDir>

xhydbmanager::xhydbmanager() {
    QDir().mkdir(m_dataDir+"/data"); // 确保 data 目录存在
    load_databases_from_files();

    // 初始化系统数据库 ruanko.db
    QFile ruankoDB(m_dataDir+"ruanko.db");
    if (!ruankoDB.exists()) {
        if (ruankoDB.open(QIODevice::WriteOnly)) {
            ruankoDB.close();
            qDebug() << "系统数据库 ruanko.db 已创建";
        }
    }
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

bool xhydbmanager::dropdatabase(const QString& dbname) {
    for (auto it = m_databases.begin(); it != m_databases.end(); ++it) {
        if (it->name().compare(dbname, Qt::CaseInsensitive) == 0) { // 使用 compare 进行不区分大小写的比较
            // 删除数据库目录
            QString dbPath = QString("%1/data/%2").arg(m_dataDir, dbname); // m_dataDir 是您的根数据目录
            QDir db_dir(dbPath);
            if (db_dir.exists()) {
                if (!db_dir.removeRecursively()) { // 检查删除是否成功
                    qWarning() << "错误: 无法完全删除数据库目录: " << dbPath;
                    return false; // 如果目录删除失败，操作也失败
                }
            }

            // 从内存列表中移除
            it = m_databases.erase(it); // erase 返回下一个有效迭代器

            if (current_database.compare(dbname, Qt::CaseInsensitive) == 0) {
                current_database.clear();
            }
            qDebug() << "数据库已删除 (包括内存记录):" << dbname;

            // TODO: 更新 ruanko.db (如果它跟踪所有数据库的元数据)
            // 这部分逻辑比较复杂，需要读取 ruanko.db，移除对应的 DatabaseBlock，然后重写文件。
            // 或者标记为已删除。暂时跳过这一步，但这是持久化所必需的。

            return true;
        }
    }
    qWarning() << "错误: 尝试删除的数据库 '" << dbname << "' 在内存中未找到。";
    return false;
}
bool xhydbmanager::use_database(const QString& dbname) {
    for (const auto& db : m_databases) {
        if (db.name().toLower() == dbname.toLower()) {
            current_database = db.name(); // 保留原始大小写
            qDebug() << "Using database:" << current_database;
            return true;
        }
    }
    return false;
}

QString xhydbmanager::get_current_database() const {
    return current_database;
}

QList<xhydatabase> xhydbmanager::databases() const {
    return m_databases;
}

// In xhydbmanager.cpp
bool xhydbmanager::beginTransaction() {
    if (m_inTransaction) {
        qDebug() << "Manager is already in a transaction.";
        return false;
    }

    if (current_database.isEmpty()) {
        qWarning() << "Cannot begin transaction: No database selected.";
        return false;
    }
    xhydatabase* db = find_database(current_database);
    if (!db) {
        qWarning() << "Cannot begin transaction: Current database" << current_database << "not found.";
        return false;
    }

    db->beginTransaction(); // <--- 新增：让数据库对象也开始事务

    m_inTransaction = true;
    qDebug() << "Transaction started for database:" << current_database;
    return true;
}
// In xhydbmanager.cpp
bool xhydbmanager::commitTransaction() {
    if (!m_inTransaction) {
        qDebug() << "Manager is not in a transaction, cannot commit.";
        return false;
    }

    bool success = true;
    if (!current_database.isEmpty()) {
        xhydatabase* db = find_database(current_database);
        if (db) {
            db->commit(); // <--- 新增：让数据库对象也提交事务

            // 持久化已提交的数据
            for (const xhytable& table : db->tables()) { // db->tables() 现在返回的是已提交状态的表
                // save_table_to_file 应该只在最外层事务成功提交后执行
                // 如果 db->commit() 内部有复杂逻辑可能失败，需要更细致的错误处理
            }
            // 实际的文件保存应该在所有内存操作成功后进行
            if (success) { // 假设 db->commit() 总是成功，或者能指示成功
                for (const xhytable& table : db->tables()) {
                    save_table_to_file(current_database, table.name(), &table);
                }
            }

        } else {
            qWarning() << "Commit failed: Current database" << current_database << "not found.";
            success = false;
        }
    } else {
        qWarning() << "Commit failed: No database selected.";
        success = false;
    }

    if(success) {
        m_tempTables.clear(); // 清理管理器层面的临时表（用于DDL）
        m_inTransaction = false;
        qDebug() << "Transaction committed for database:" << current_database;
        return true;
    } else {
        // 如果提交过程中（例如db->commit()或文件保存前）发生错误，
        // 可能需要触发一次内部回滚db->rollback()以保持内存一致性，
        // 尽管外层事务标志m_inTransaction可能仍需根据策略设置为false。
        // 这是一个复杂点，取决于您希望如何处理提交阶段的失败。
        // 为简单起见，目前假设db->commit()成功。
        qDebug() << "Transaction commit failed. State might be inconsistent if db->commit() partially succeeded.";
            // m_inTransaction = false; // 即使提交失败，也可能需要结束事务状态
        return false;
    }
}
// In xhydbmanager.cpp
void xhydbmanager::rollbackTransaction() {
    if (m_inTransaction) {
        if (!current_database.isEmpty()) {
            xhydatabase* db = find_database(current_database);
            if (db) {
                db->rollback(); // <--- 修改：调用 xhydatabase 的 rollback 方法
            }
        }
        // m_tempTables 是为管理器层面的 DDL 事务准备的，例如 CREATE TABLE 后 ROLLBACK
        // 如果您的事务模型支持这种混合操作，清空它是合理的。
        m_tempTables.clear();
        m_inTransaction = false;
        qDebug() << "Transaction rolled back for database:" << current_database;
    }
}
bool xhydbmanager::add_column(const QString& database_name, const QString& table_name, const xhyfield& field) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    xhytable* table = db->find_table(table_name);
    if (!table) return false;

    if (table->has_field(field.name())) {
        return false; // 字段已存在
    }

    table->add_field(field);
    // 保存更改到文件或其他存储中
    return true;
}
bool xhydbmanager::drop_column(const QString& database_name, const QString& table_name, const QString& field_name) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    xhytable* table = db->find_table(table_name);
    if (!table) return false;

    if (!table->has_field(field_name)) {
        return false; // 字段不存在
    }

    table->remove_field(field_name);
    // 保存更改到文件或其他存储中
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
    if (!table) return false;

    // 先删除旧字段
    table->remove_field(old_field_name);
    // 添加新字段
    table->add_field(new_field);

    return true;
}
bool xhydbmanager::add_constraint(const QString& database_name, const QString& table_name, const QString& field_name, const QString& constraint) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    xhytable* table = db->find_table(table_name);
    if (!table) return false;

    // 获取字段
    const xhyfield* old_field = table->get_field(field_name);
    if (!old_field) return false;

    // 创建新字段并添加约束
    QStringList constraints = old_field->constraints();
    constraints.append(constraint);
    xhyfield new_field(old_field->name(), old_field->type(), constraints);

    // 替换原字段
    table->remove_field(field_name);
    table->addfield(new_field);

    return true;
}
bool xhydbmanager::drop_constraint(const QString& database_name, const QString& table_name, const QString& constraint_name) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    xhytable* table = db->find_table(table_name);
    if (!table) return false;

    // 假设我们能通过约束名称找到对应的字段
    for (auto& field : table->fields()) {
        auto constraints = field.constraints();
        if (constraints.contains(constraint_name)) {
            constraints.removeOne(constraint_name);
            xhyfield new_field(field.name(), field.type(), constraints);
            table->remove_field(field.name());
            table->addfield(new_field);
            return true; // 删除成功
        }
    }
    return false; // 未找到约束
}
bool xhydbmanager::rename_column(const QString& database_name, const QString& table_name, const QString& old_column_name, const QString& new_column_name) {
    xhydatabase* db = find_database(database_name);
    if (!db) return false;

    xhytable* table = db->find_table(table_name);
    if (!table) return false;

    const xhyfield* old_field = table->get_field(old_column_name);
    if (!old_field) return false;

    xhyfield new_field(new_column_name, old_field->type(), old_field->constraints());
    table->remove_field(old_column_name);
    table->addfield(new_field);

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
    xhydatabase* db = find_database(dbname);
    if (!db) {
        qWarning() << "错误: 数据库 '" << dbname << "' 未找到，无法创建表。";
         throw std::runtime_error(("数据库 '" + dbname + "' 未找到。").toStdString());
        return false;
    }

    // 调用 xhydatabase::createtable。这个方法内部会创建 xhytable 实例，
    // 并将 xhydatabase 自身的 'this' 指针作为父数据库传递给 xhytable 的构造函数。
    if (db->createtable(table)) {
        // 持久化新创建的表
        const xhytable* new_table_ptr = db->find_table(table.name());
        if (new_table_ptr) {
            save_table_to_file(dbname, new_table_ptr->name(), new_table_ptr);
            qDebug() << "表 '" << new_table_ptr->name() << "' 在数据库 '" << dbname << "' 中创建并已保存。";
            return true;
        } else {
            qWarning() << "严重错误：表在数据库中创建成功，但在回找时失败。文件未保存。";
            // 可能需要回滚内存中的表创建操作（如果xhydatabase支持）
            return false;
        }
    } else {
        // xhydatabase::createtable 内部可能因为表已存在等原因失败
        qWarning() << "在数据库 '" << dbname << "' 中创建表 '" << table.name() << "' 失败 (可能已存在或定义无效)。";
        return false;
    }
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
    QDir data_dir(m_dataDir + "/data"); // 确保 m_dataDir 已正确初始化
    if (!data_dir.exists()) {
        qWarning() << "[LOAD_DB] Data directory " << data_dir.absolutePath() << " does not exist. No databases to load.";
        if (!data_dir.mkpath(".")) {
            qWarning() << "[LOAD_DB] Failed to create data directory " << data_dir.absolutePath();
            return;
        }
        qDebug() << "[LOAD_DB] Created data directory " << data_dir.absolutePath();
    }
    QStringList db_dirs = data_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    m_databases.clear(); // 清空内存中的数据库列表

    for (const QString& dbname : db_dirs) {
        // 在将表添加到数据库之前，先创建数据库对象并将其添加到管理器
        xhydatabase databaseObj(dbname); // 创建数据库对象
        m_databases.append(databaseObj); // 添加到管理器列表
        xhydatabase* currentDbPtr = &m_databases.last(); // 获取指向刚添加的数据库对象的指针

        QDir db_dir_path(data_dir.filePath(dbname));
        QStringList tdf_files = db_dir_path.entryList(QStringList() << "*.tdf", QDir::Files);
        qDebug() << "[LOAD_DB] Loading database from directory:" << db_dir_path.path();

        for (const QString& tdf_filename : tdf_files) {
            QString current_table_name = tdf_filename.left(tdf_filename.length() - 4);
            qDebug() << "  [LOAD_DB] Attempting to load table:" << current_table_name;

            // 创建表对象时，传递父数据库指针
            xhytable table(current_table_name, currentDbPtr);
            bool tdf_load_successful = true;

            // --- Load Table Definition (.tdf) ---
            QFile tdfFile(db_dir_path.filePath(tdf_filename));
            if (tdfFile.open(QIODevice::ReadOnly)) {
                QDataStream tdf_in_stream(&tdfFile);
                tdf_in_stream.setVersion(QDataStream::Qt_5_15);

                // 可选：如果保存了字段数量，先读取并用于循环或验证
                // quint32 numFieldsExpected;
                // tdf_in_stream >> numFieldsExpected;
                // for (quint32 fieldIdx = 0; fieldIdx < numFieldsExpected; ++fieldIdx) { ... }

                while (!tdf_in_stream.atEnd()) { // 循环读取字段块，直到遇到外键数量标记或文件尾
                    // 尝试读取一个FieldBlock，如果失败（例如到达文件尾或读取不完整），则跳出
                    // 这个检查点是为了在字段块读完后，流指针指向外键数量
                    qint64 currentPos = tdfFile.pos();
                    if (currentPos + static_cast<qint64>(sizeof(FieldBlock)) > tdfFile.size() &&
                        currentPos + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                        // 剩余空间不足以容纳一个 FieldBlock，但可能足以容纳 quint32 (外键数量)
                        // 这表示字段块可能已读取完毕
                        break;
                    }
                    if (currentPos >= tdfFile.size()) break; // 已到文件末尾

                    FieldBlock fb;
                    int bytesRead = tdf_in_stream.readRawData(reinterpret_cast<char*>(&fb), sizeof(FieldBlock));

                    if (bytesRead == 0 && tdf_in_stream.atEnd()) break;
                    if (bytesRead < static_cast<int>(sizeof(FieldBlock))) {
                        if (tdf_in_stream.atEnd() && bytesRead > 0 && bytesRead < static_cast<int>(sizeof(quint32)) ) {
                            // 如果文件末尾的字节数小于一个 quint32，说明没有外键数量信息，可能是旧格式文件
                            qDebug() << "  [LOAD_DB_TDF] Reached end of file after reading partial data for table '" << current_table_name << "', assuming no FKs or old format.";
                            tdf_load_successful = true; // 允许部分成功，没有FKs
                            break;
                        }
                        qWarning() << "  [LOAD_DB_TDF] TDF read error for field block in table '" << current_table_name << "'. Expected " << sizeof(FieldBlock) << ", got " << bytesRead;
                        tdf_load_successful = false;
                        break;
                    }

                    QString field_name_str = QString::fromUtf8(fb.name, strnlen(fb.name, sizeof(fb.name)));
                    if (field_name_str.isEmpty()) { // 可能读到了文件末尾的填充或无效数据
                        qDebug() << "  [LOAD_DB_TDF] Encountered empty field name, assuming end of field blocks for " << current_table_name;
                        // 把流指针回退到 fb 开始的位置，因为这可能不是一个有效的 fb
                        tdfFile.seek(currentPos);
                        break;
                    }

                    xhyfield::datatype type = static_cast<xhyfield::datatype>(fb.type);
                    QStringList constraints;
                    if (fb.integrities & 0x01) constraints.append("PRIMARY_KEY");
                    if (fb.integrities & 0x02) constraints.append("NOT_NULL");
                    if (fb.integrities & 0x04) constraints.append("UNIQUE"); // 字段级 UNIQUE

                    // 根据 fb.param 和 fb.size 添加 SIZE, PRECISION, SCALE 约束
                    if ((type == xhyfield::CHAR || type == xhyfield::VARCHAR) && fb.param > 0) {
                        constraints.append(QString("SIZE(%1)").arg(fb.param));
                    } else if (type == xhyfield::DECIMAL && fb.param > 0) { // fb.param 是精度 P
                        constraints.append(QString("PRECISION(%1)").arg(fb.param));
                        constraints.append(QString("SCALE(%1)").arg(fb.size)); // fb.size 是标度 S
                    }
                    table.addfield(xhyfield(field_name_str, type, constraints));
                } // End while TDF fields

                // 读取外键信息
                if (tdf_load_successful && !tdfFile.atEnd()) { // 如果字段加载成功且文件未结束
                    quint32 numForeignKeys = 0; // 初始化
                    if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                        tdf_in_stream >> numForeignKeys;
                    } else if (!tdfFile.atEnd()){
                        qWarning() << "    [LOAD_DB_TDF] Not enough data left to read foreign key count for " << current_table_name;
                        // tdf_load_successful = false; // 可选：标记为失败
                    }


                    if (tdf_in_stream.status() == QDataStream::Ok && numForeignKeys > 0) {
                        qDebug() << "    [LOAD_DB_TDF] Expecting" << numForeignKeys << "foreign key definitions for table" << current_table_name;
                        for (quint32 i = 0; i < numForeignKeys; ++i) {
                            QString constraintName, fk_column, refTable, refColumn;
                            // QString onDelete, onUpdate; // 如果保存了规则
                            tdf_in_stream >> constraintName >> fk_column >> refTable >> refColumn;
                            // tdf_in_stream >> onDelete >> onUpdate;

                            if (tdf_in_stream.status() == QDataStream::Ok) {
                                table.add_foreign_key(fk_column, refTable, refColumn, constraintName);
                                // table.setForeignKeyActions(constraintName, onDelete, onUpdate); // 如果支持
                                qDebug() << "      [LOAD_DB_TDF_FK] Loaded FK:" << constraintName;
                            } else {
                                qWarning() << "    [LOAD_DB_TDF] Error reading foreign key definition " << (i + 1) << " for " << current_table_name;
                                tdf_load_successful = false; break;
                            }
                        }
                    } else if (tdf_in_stream.status() != QDataStream::Ok && numForeignKeys > 0) { // 流错误但期望有FK
                        qWarning() << "    [LOAD_DB_TDF] Stream error while trying to read foreign key count for " << current_table_name;
                        tdf_load_successful = false;
                    } else if (numForeignKeys == 0) {
                        qDebug() << "    [LOAD_DB_TDF] Table " << current_table_name << " has 0 foreign keys defined in TDF.";
                    }
                } else if (tdf_load_successful && tdfFile.atEnd()) {
                    qDebug() << "    [LOAD_DB_TDF] Reached end of TDF for " << current_table_name << " after field blocks, no FK count found (or 0 FKs).";
                }
                tdfFile.close();
            } else { // TDF 文件打开失败
                qWarning() << "  [LOAD_DB] Failed to open TDF file:" << tdfFile.fileName() << ". Skipping table.";
                tdf_load_successful = false;
            }

            if (!tdf_load_successful) {
                qWarning() << "  [LOAD_DB] Skipping TRD load for table '" << current_table_name << "' due to TDF load error.";
                continue; // 跳过此表，处理下一个TDF文件
            }
            if (table.fields().isEmpty()) { // 即使TDF加载“成功”，但没有字段定义，也是无效的表
                qWarning() << "  [LOAD_DB] Table '" << current_table_name << "' has no fields defined after TDF load. Skipping TRD load.";
                continue;
            }

            // --- Load Record Data (.trd) ---
            QString trdFilePath = db_dir_path.filePath(current_table_name + ".trd");
            QFile trdFile(trdFilePath);
            bool trd_processing_error_occurred = false; // Flag for critical errors within TRD processing

            if (trdFile.exists() && trdFile.open(QIODevice::ReadOnly)) {
                QDataStream record_file_stream(&trdFile);
                record_file_stream.setVersion(QDataStream::Qt_5_15);
                qDebug() << "    [LOAD_DB_TRD] Loading TRD for table:" << current_table_name << "from" << trdFilePath;

                while (!record_file_stream.atEnd()) {
                    if (trd_processing_error_occurred) break; // Stop if a critical error happened in a previous record

                    quint32 record_total_size_from_file;
                    record_file_stream >> record_total_size_from_file;
                    if (record_file_stream.status() != QDataStream::Ok) {
                        if (!record_file_stream.atEnd())
                            qWarning() << "    [LOAD_DB_TRD] Failed to read record size for '" << current_table_name << "'. Status: " << record_file_stream.status();
                        break;
                    }

                    QByteArray record_data_buffer(record_total_size_from_file, Qt::Uninitialized);
                    int bytes_actually_read = record_file_stream.readRawData(record_data_buffer.data(), record_total_size_from_file);

                    if (bytes_actually_read < static_cast<int>(record_total_size_from_file)) {
                        qWarning() << "    [LOAD_DB_TRD] Failed to read full record data for '" << current_table_name
                                   << "'. Expected: " << record_total_size_from_file << ", Got: " << bytes_actually_read
                                   << ". Stream status: " << record_file_stream.status();
                        break;
                    }

                    QDataStream field_parse_stream(record_data_buffer);
                    field_parse_stream.setVersion(QDataStream::Qt_5_15);
                    xhyrecord new_loaded_record;

                    for (const auto& field_def : table.fields()) {
                        QString value_to_insert_in_record;
                        quint8 is_null_marker;

                        field_parse_stream >> is_null_marker;
                        if (field_parse_stream.status() != QDataStream::Ok) {
                            qWarning() << "    [LOAD_DB_TRD] CRITICAL: Failed to read null marker for field '" << field_def.name() << "' in table '" << current_table_name << "'. Aborting TRD load for this table.";
                            trd_processing_error_occurred = true; // Set flag
                            break; // Exit field loop for this record
                        }

                        if (is_null_marker == 0) { // Value IS NOT NULL
                            QDataStream::Status field_read_status = QDataStream::Ok;
                            switch (field_def.type()) {
                            case xhyfield::TINYINT:  { qint8 val;     field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::SMALLINT: { qint16 val;    field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::INT:      { qint32 val;    field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::BIGINT:   { qlonglong val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::FLOAT:    { float val;     field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DOUBLE:   { double val;    field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DECIMAL:
                            case xhyfield::CHAR:
                            case xhyfield::VARCHAR:
                            case xhyfield::TEXT:
                            case xhyfield::ENUM:     { QByteArray strBytes; field_parse_stream >> strBytes; value_to_insert_in_record = QString::fromUtf8(strBytes); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DATE:     { QDate date; field_parse_stream >> date; if(date.isValid()) value_to_insert_in_record = date.toString("yyyy-MM-dd"); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DATETIME:
                            case xhyfield::TIMESTAMP:{ QDateTime datetime; field_parse_stream >> datetime; if(datetime.isValid()) value_to_insert_in_record = datetime.toString("yyyy-MM-dd HH:mm:ss"); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::BOOL:     { bool boolValue; field_parse_stream >> boolValue; value_to_insert_in_record = boolValue ? "1" : "0"; field_read_status = field_parse_stream.status(); break;}
                            default:
                                qWarning() << "    [LOAD_DB_TRD] Unhandled data type for field '" << field_def.name() << "' (Type ID: " << static_cast<int>(field_def.type()) << "). Reading as QByteArray.";
                                QByteArray unknownData;
                                if (!field_parse_stream.atEnd()) field_parse_stream >> unknownData;
                                value_to_insert_in_record = QString::fromUtf8(unknownData);
                                field_read_status = field_parse_stream.status();
                                break;
                            }
                            if (field_read_status != QDataStream::Ok) {
                                qWarning() << "    [LOAD_DB_TRD] Stream error after attempting to read non-NULL field '" << field_def.name()
                                << "' in table '" << current_table_name << "'. Status: " << field_read_status << ". Setting field to NULL.";
                                value_to_insert_in_record = QString();
                            }
                        }
                        new_loaded_record.insert(field_def.name(), value_to_insert_in_record);
                    } // end for fields in record

                    if (trd_processing_error_occurred) { // If error reading a field's null marker
                        qWarning() << "    [LOAD_DB_TRD] Aborted reading current record due to previous field read error in table " << current_table_name;
                        break; // Exit record loop (while !record_file_stream.atEnd())
                    }

                    if (field_parse_stream.status() != QDataStream::Ok && !field_parse_stream.atEnd()){
                        qWarning() << "    [LOAD_DB_TRD] Record parser: Stream has unread data or error for table '" << current_table_name
                                   << "' after processing all fields. Stream status: " << field_parse_stream.status();
                    }
                    table.addrecord(new_loaded_record);
                } // end while records in TRD
                trdFile.close();
            } else { // TRD file handling
                if(QFile::exists(trdFilePath)) {
                    qWarning() << "    [LOAD_DB] Failed to open TRD file for reading (but it exists):" << trdFilePath;
                } else {
                    qDebug() << "    [LOAD_DB] TRD file not found (this is normal for a new or empty table):" << trdFilePath;
                }
            }
             currentDbPtr->addTable(table); // Add the fully populated (or partially if TRD errors) table to the database object
        } // end for TDF files (tables)
     // Add the database to the manager's list
    } // end for DB directories
    qDebug() << "[LOAD_DB] Finished loading all databases and tables.";
}


xhydatabase* xhydbmanager::find_database(const QString& dbname) {
    for (auto& db : m_databases) {
        if (db.name().toLower() == dbname.toLower()) {
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
    if (!table) {
        qWarning() << "[SAVE_TDF] Error: Table pointer is null for path " << filePath;
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[SAVE_TDF] Error: Failed to open TDF file for writing: " << filePath;
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);
    qDebug() << "[SAVE_TDF] Saving TDF for table:" << table->name() << "to" << filePath;

    // 1. 保存字段数量 (可选，但有助于加载时验证)
    // quint32 numFields = static_cast<quint32>(table->fields().size());
    // out << numFields;

    for (const auto& field : table->fields()) {
        FieldBlock fb;
        memset(&fb, 0, sizeof(FieldBlock));
        // ... (原有的填充 FieldBlock fb 的逻辑，如 fb.name, fb.type, fb.param, fb.size, fb.integrities) ...
        qstrncpy(fb.name, field.name().toUtf8().constData(), sizeof(fb.name) - 1);
        fb.type = static_cast<int>(field.type());

        // 填充 fb.param 和 fb.size (基于 CHAR/VARCHAR 的 SIZE, DECIMAL 的 PRECISION/SCALE)
        // 这个逻辑需要从您之前的代码或 xhytable::addfield 的解析逻辑中获取
        const QStringList& constraints = field.constraints();
        bool sizeFound = false; // For CHAR/VARCHAR
        bool precisionFound = false; // For DECIMAL
        // bool scaleFound = false; // For DECIMAL (fb.size is scale)

        if (field.type() == xhyfield::CHAR || field.type() == xhyfield::VARCHAR) {
            for (const QString& c : constraints) {
                if (c.startsWith("SIZE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                    fb.param = c.mid(5, c.length() - 6).toInt(); sizeFound = true; break;
                }
            }
            if (!sizeFound && field.type() == xhyfield::CHAR) fb.param = 1; // Default CHAR(1)
            // VARCHAR 通常需要显式长度，若无则可能是255或错误
        } else if (field.type() == xhyfield::DECIMAL) {
            for (const QString& c : constraints) {
                if (c.startsWith("PRECISION(", Qt::CaseInsensitive) && c.endsWith(")")) {
                    fb.param = c.mid(10, c.length() - 11).toInt(); precisionFound = true;
                } else if (c.startsWith("SCALE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                    fb.size = c.mid(6, c.length() - 7).toInt(); // scaleFound = true;
                }
            }
            // DECIMAL 通常需要 PRECISION，SCALE 默认为 0
            if (!precisionFound) { /* qWarning or default precision */ }
        }

        fb.integrities = 0;
        if (table->primaryKeys().contains(field.name(), Qt::CaseInsensitive) || constraints.contains("PRIMARY_KEY", Qt::CaseInsensitive)) fb.integrities |= 0x01;
        if (table->notNullFields().contains(field.name()) || constraints.contains("NOT_NULL", Qt::CaseInsensitive)) fb.integrities |= 0x02;
        // 对于字段级 UNIQUE (从 xhyfield::constraints() 解析) 或表级 UNIQUE (从 xhytable::m_uniqueConstraints)
        // FieldBlock 的 integrities 可能不直接反映表级多字段 UNIQUE。
        // 这里简化为只检查字段本身的约束列表是否有 UNIQUE。
        if (constraints.contains("UNIQUE", Qt::CaseInsensitive)) fb.integrities |= 0x04;


        GetSystemTime(&fb.mtime); // Windows specific
        out.writeRawData(reinterpret_cast<const char*>(&fb), sizeof(FieldBlock));
    }

    // 2. 保存外键信息
    const QList<QMap<QString, QString>>& foreignKeys = table->foreignKeys();
    quint32 numForeignKeys = static_cast<quint32>(foreignKeys.size());
    out << numForeignKeys; // 写入外键定义的数量

    qDebug() << "  [SAVE_TDF] Saving" << numForeignKeys << "foreign key definitions for table" << table->name();
    for (const auto& fkMap : foreignKeys) {
        out << fkMap.value("constraintName");
        out << fkMap.value("column");
        out << fkMap.value("referenceTable");
        out << fkMap.value("referenceColumn");
        // 如果要保存 ON DELETE/UPDATE 规则，也在这里写入
        // out << fkMap.value("onDeleteAction", "RESTRICT"); // 假设有这些键
        // out << fkMap.value("onUpdateAction", "RESTRICT");
        qDebug() << "    [SAVE_TDF_FK] Saved FK:" << fkMap.value("constraintName")
                 << "Col:" << fkMap.value("column")
                 << "RefTable:" << fkMap.value("referenceTable")
                 << "RefCol:" << fkMap.value("referenceColumn");
    }

    file.close();
    qDebug() << "[SAVE_TDF] Finished saving TDF for table:" << table->name();
}




// 2. 保存记录文件
void xhydbmanager::save_table_records_file(const QString& filePath, const xhytable* table) {
    if (!table) {
        qWarning() << "[SAVE_TRD] Error: Table pointer is null for path " << filePath;
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { // Truncate to overwrite
        qWarning() << "[SAVE_TRD] Error: Failed to open TRD file for writing:" << filePath;
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);
    qDebug() << "[SAVE_TRD] Saving TRD for table:" << table->name() << "to" << filePath << "with" << table->records().count() << "records.";

    for (const auto& record : table->records()) { // records() should provide committed records
        QByteArray recordDataBuffer; // Buffer for a single record's fields
        QDataStream record_field_stream(&recordDataBuffer, QIODevice::WriteOnly);
        record_field_stream.setVersion(QDataStream::Qt_5_15);

        for (const auto& field : table->fields()) {
            QString str_value_from_record = record.value(field.name());

            if (str_value_from_record.isNull()) { // Check for genuinely null QString (SQL NULL)
                record_field_stream << static_cast<quint8>(1); // Marker: 1 for NULL
            } else {
                record_field_stream << static_cast<quint8>(0); // Marker: 0 for NOT NULL
                bool conversion_ok = true; // Assume conversion will be ok

                switch (field.type()) {
                case xhyfield::TINYINT:
                    record_field_stream << static_cast<qint8>(str_value_from_record.toShort(&conversion_ok));
                    if (!conversion_ok) qWarning() << "[SAVE_TRD] Conversion warning for TINYINT field '" << field.name() << "', value: '" << str_value_from_record << "'";
                    break;
                case xhyfield::SMALLINT:
                    record_field_stream << static_cast<qint16>(str_value_from_record.toShort(&conversion_ok));
                    if (!conversion_ok) qWarning() << "[SAVE_TRD] Conversion warning for SMALLINT field '" << field.name() << "', value: '" << str_value_from_record << "'";
                    break;
                case xhyfield::INT:
                    record_field_stream << str_value_from_record.toInt(&conversion_ok);
                    if (!conversion_ok) qWarning() << "[SAVE_TRD] Conversion warning for INT field '" << field.name() << "', value: '" << str_value_from_record << "'";
                    break;
                case xhyfield::BIGINT:
                    record_field_stream << str_value_from_record.toLongLong(&conversion_ok);
                    if (!conversion_ok) qWarning() << "[SAVE_TRD] Conversion warning for BIGINT field '" << field.name() << "', value: '" << str_value_from_record << "'";
                    break;
                case xhyfield::FLOAT:
                    record_field_stream << str_value_from_record.toFloat(&conversion_ok);
                    if (!conversion_ok) qWarning() << "[SAVE_TRD] Conversion warning for FLOAT field '" << field.name() << "', value: '" << str_value_from_record << "'";
                    break;
                case xhyfield::DOUBLE:
                    record_field_stream << str_value_from_record.toDouble(&conversion_ok);
                    if (!conversion_ok) qWarning() << "[SAVE_TRD] Conversion warning for DOUBLE field '" << field.name() << "', value: '" << str_value_from_record << "'";
                    break;
                case xhyfield::DECIMAL: // Store DECIMAL as string to preserve precision
                case xhyfield::CHAR:
                case xhyfield::VARCHAR:
                case xhyfield::TEXT:
                case xhyfield::ENUM: { // Store ENUM as its string value
                    QByteArray strBytes = str_value_from_record.toUtf8();
                    record_field_stream << strBytes; // QDataStream handles length-prefix for QByteArray
                    break;
                }
                case xhyfield::DATE: {
                    QDate date = QDate::fromString(str_value_from_record, "yyyy-MM-dd");
                    if (!date.isValid() && !str_value_from_record.isEmpty()) { // If original string was non-empty but invalid
                        qWarning() << "[SAVE_TRD] Invalid date string '" << str_value_from_record << "' for field " << field.name() << ". Saving as invalid QDate.";
                    }
                    record_field_stream << date;
                    break;
                }
                case xhyfield::DATETIME:
                case xhyfield::TIMESTAMP: { // Treat TIMESTAMP like DATETIME for serialization
                    QDateTime datetime = QDateTime::fromString(str_value_from_record, "yyyy-MM-dd HH:mm:ss");
                    if (!datetime.isValid() && !str_value_from_record.isEmpty()) {
                        qWarning() << "[SAVE_TRD] Invalid datetime string '" << str_value_from_record << "' for field " << field.name() << ". Saving as invalid QDateTime.";
                    }
                    record_field_stream << datetime;
                    break;
                }
                case xhyfield::BOOL:
                    record_field_stream << (str_value_from_record.compare("true", Qt::CaseInsensitive) == 0 || str_value_from_record == "1");
                    break;
                default:
                    qWarning() << "[SAVE_TRD] Unsupported data type for field '" << field.name() << "' (Type ID: " << static_cast<int>(field.type()) << "). Saving as raw UTF-8 string.";
                    QByteArray strBytes = str_value_from_record.toUtf8();
                    record_field_stream << strBytes; // Fallback
                    break;
                }
            }
        } // end for fields in record

        // Write the size of the record's data block, then the data block itself
        out << static_cast<quint32>(recordDataBuffer.size());
        out.writeRawData(recordDataBuffer.constData(), recordDataBuffer.size());
    } // end for records in table
    file.close();
    qDebug() << "[SAVE_TRD] Finished saving TRD for table:" << table->name();
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
