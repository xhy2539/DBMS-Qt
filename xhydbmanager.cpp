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
        qWarning() << "[LOAD_DB] 数据目录 " << data_dir.absolutePath() << " 不存在，没有数据库可加载。";
        if (!data_dir.mkpath(".")) { //尝试创建目录
            qWarning() << "[LOAD_DB] 创建数据目录失败 " << data_dir.absolutePath();
            return;
        }
        qDebug() << "[LOAD_DB] 已创建数据目录 " << data_dir.absolutePath();
    }
    QStringList db_dirs = data_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    m_databases.clear(); // 清空内存中的数据库列表

    for (const QString& dbname : db_dirs) {
        // 先创建数据库对象并将其添加到管理器列表
        xhydatabase databaseObj(dbname);
        m_databases.append(databaseObj); // 添加到管理器列表
        xhydatabase* currentDbPtr = &m_databases.last(); // 获取指向刚添加的数据库对象的指针

        QDir db_dir_path(data_dir.filePath(dbname));
        QStringList tdf_files = db_dir_path.entryList(QStringList() << "*.tdf", QDir::Files);
        qDebug() << "[LOAD_DB] 正在从目录加载数据库:" << db_dir_path.path();

        for (const QString& tdf_filename : tdf_files) {
            QString current_table_name = tdf_filename.left(tdf_filename.length() - 4); // 移除 ".tdf"
            qDebug() << "  [LOAD_DB] 尝试加载表:" << current_table_name;

            // 创建表对象时，传递父数据库指针
            xhytable table(current_table_name, currentDbPtr);
            bool tdf_load_successful = true;

            // --- 加载表定义文件 (.tdf) ---
            QFile tdfFile(db_dir_path.filePath(tdf_filename));
            if (tdfFile.open(QIODevice::ReadOnly)) {
                QDataStream tdf_in_stream(&tdfFile);
                tdf_in_stream.setVersion(QDataStream::Qt_5_15);

                while (tdf_load_successful && !tdf_in_stream.atEnd()) {
                    qint64 currentPosBeforeFieldBlockRead = tdfFile.pos();

                    // 检查剩余字节是否足够容纳一个 FieldBlock
                    // 如果不足以容纳 FieldBlock 但可能足以容纳 quint32 (外键数量标记)，则跳出字段读取循环
                    if (currentPosBeforeFieldBlockRead + static_cast<qint64>(sizeof(FieldBlock)) > tdfFile.size() &&
                        currentPosBeforeFieldBlockRead + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                        qDebug() << "  [LOAD_DB_TDF] 表 '" << current_table_name << "'：剩余空间不足以容纳 FieldBlock，可能到达外键数量标记处。";
                        break;
                    }
                    if (currentPosBeforeFieldBlockRead >= tdfFile.size()) { // 已到文件末尾
                        qDebug() << "  [LOAD_DB_TDF] 表 '" << current_table_name << "'：已在读取下一个字段块前到达文件末尾。";
                        break;
                    }

                    FieldBlock fb; // 假定 FieldBlock 已按上述提示修改
                    // 从文件中读取原始字节到 fb 结构体
                    int bytesRead = tdf_in_stream.readRawData(reinterpret_cast<char*>(&fb), sizeof(FieldBlock));

                    if (bytesRead == 0 && tdf_in_stream.atEnd()) { // 正常读到文件尾（没有更多字段块）
                        qDebug() << "  [LOAD_DB_TDF] 表 '" << current_table_name << "'：读取字段块时正常到达文件尾。";
                        break;
                    }
                    if (bytesRead < static_cast<int>(sizeof(FieldBlock))) { // 读取的字节数不足一个完整的 FieldBlock
                        qWarning() << "  [LOAD_DB_TDF] 表 '" << current_table_name << "' 中读取字段块错误。期望 "
                                   << sizeof(FieldBlock) << " 字节, 实际读取 " << bytesRead
                                   << " 字节。流状态: " << tdf_in_stream.status();
                        tdf_load_successful = false; // 标记TDF加载失败
                        break; // 终止读取此TDF文件
                    }

                    QString field_name_str = QString::fromUtf8(fb.name, strnlen(fb.name, sizeof(fb.name)));

                    // 简单的有效性检查，防止因文件末尾的填充数据导致空字段名
                    if (field_name_str.isEmpty() && fb.type == 0 && fb.param == 0 && fb.size == 0) {
                        qDebug() << "  [LOAD_DB_TDF] 表 '" << current_table_name << "'：遇到可能是填充或结束的空字段块，回退流指针。";
                        tdfFile.seek(currentPosBeforeFieldBlockRead); // 回退，以便后续正确读取外键数量
                        break;
                    }
                    if (field_name_str.isEmpty()){
                        qWarning() << "  [LOAD_DB_TDF] 表 '" << current_table_name << "' 中读取到字段名为空的字段块。类型: " << fb.type << "。跳过此字段。";
                        continue; // 跳过这个可能损坏的字段块
                    }


                    xhyfield::datatype type = static_cast<xhyfield::datatype>(fb.type);
                    QStringList constraints;
                    // 根据 fb.integrities 解析约束
                    if (fb.integrities & 0x01) constraints.append("PRIMARY_KEY");
                    if (fb.integrities & 0x02) constraints.append("NOT_NULL");
                    if (fb.integrities & 0x04) constraints.append("UNIQUE"); // 字段级 UNIQUE

                    // 根据 fb.param 和 fb.size 添加 SIZE, PRECISION, SCALE 约束
                    if ((type == xhyfield::CHAR || type == xhyfield::VARCHAR) && fb.param > 0) {
                        constraints.append(QString("SIZE(%1)").arg(fb.param));
                    } else if (type == xhyfield::DECIMAL && fb.param > 0) {
                        constraints.append(QString("PRECISION(%1)").arg(fb.param));
                        constraints.append(QString("SCALE(%1)").arg(fb.size)); // fb.size 是标度 S
                    }

                    // *** 为 ENUM 类型加载允许值列表的修改 ***
                    xhyfield loaded_field(field_name_str, type, constraints);
                    if (type == xhyfield::ENUM) {
                        // 假设 fb.enum_values_str 是空终止的，或者在保存时对于非ENUM类型其首字节为'\0'
                        if (fb.enum_values_str[0] != '\0') {
                            QString enums_combined_str = QString::fromUtf8(fb.enum_values_str);
                            // 假设保存时用逗号分隔，且值不包含逗号或已妥善处理
                            QStringList enum_vals_list = enums_combined_str.split(',', Qt::SkipEmptyParts);
                            QStringList cleaned_enum_vals_list;
                            for (const QString& val : enum_vals_list) {
                                // ENUM 值在存储和比较时通常不带引号，所以这里只做 trim
                                QString trimmedVal = val.trimmed();
                                if (!trimmedVal.isEmpty()) {
                                    cleaned_enum_vals_list.append(trimmedVal);
                                }
                            }
                            loaded_field.set_enum_values(cleaned_enum_vals_list);
                            qDebug() << "    [LOAD_DB_TDF_ENUM] 表 '" << current_table_name << "'，字段 '" << field_name_str << "'：加载的ENUM值列表：" << cleaned_enum_vals_list;
                        } else {
                            qDebug() << "    [LOAD_DB_TDF_ENUM] 表 '" << current_table_name << "'，ENUM字段 '" << field_name_str << "' 在TDF中未指定枚举值列表 (或列表为空)。";
                            // loaded_field.m_enumValues 将保持为空，这与之前导致问题的状态一致
                        }
                    }
                    table.addfield(loaded_field); // 将字段（可能已设置ENUM值）添加到表中
                    // *** ENUM 修改结束 ***

                } // 结束 while TDF fields 循环

                // 加载外键信息 (保持您原有的逻辑，但增加流状态检查)
                if (tdf_load_successful && !tdfFile.atEnd()) { // 仅在TDF字段加载成功且文件未读完时尝试加载外键
                    quint32 numForeignKeys = 0;
                    // 在读取外键数量前检查流状态
                    if (tdf_in_stream.status() == QDataStream::Ok) {
                        if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                            tdf_in_stream >> numForeignKeys;
                        } else if (!tdfFile.atEnd()){
                            qWarning() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：剩余数据不足以读取外键数量。";
                        }
                    } else {
                        qWarning() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：在读取外键数量前流状态已出错。状态：" << tdf_in_stream.status();
                        tdf_load_successful = false;
                    }


                    if (tdf_in_stream.status() == QDataStream::Ok && numForeignKeys > 0) {
                        qDebug() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：期望加载 " << numForeignKeys << " 个外键定义。";
                        for (quint32 i = 0; i < numForeignKeys && tdf_load_successful; ++i) { // 增加 tdf_load_successful 条件
                            QString constraintName, refTable;
                            quint32 numMappings = 0;
                            tdf_in_stream >> constraintName >> refTable >> numMappings;

                            if (tdf_in_stream.status() != QDataStream::Ok) {
                                qWarning() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：读取外键头信息 (第 " << (i + 1) << " 个) 时出错。状态：" << tdf_in_stream.status();
                                tdf_load_successful = false; break;
                            }

                            QStringList childColumns;
                            QStringList referencedColumns;
                            for (quint32 j = 0; j < numMappings; ++j) {
                                QString childCol, refCol;
                                tdf_in_stream >> childCol >> refCol;
                                if (tdf_in_stream.status() != QDataStream::Ok) {
                                    qWarning() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：读取外键映射 (第 " << (j + 1) << " 个，外键 #" << (i+1) << ") 时出错。状态：" << tdf_in_stream.status();
                                    tdf_load_successful = false; break;
                                }
                                childColumns.append(childCol);
                                referencedColumns.append(refCol);
                            }
                            if (!tdf_load_successful) break; // 如果内层循环出错，跳出外层循环

                            try {
                                table.add_foreign_key(childColumns, refTable, referencedColumns, constraintName);
                                qDebug() << "      [LOAD_DB_TDF_FK] 表 '" << current_table_name << "'：已加载外键 '" << constraintName << "'";
                            } catch (const std::runtime_error& e) {
                                qWarning() << "      [LOAD_DB_TDF_FK] 表 '" << current_table_name << "'：添加外键 '" << constraintName << "' 失败：" << e.what();
                                // 可以选择标记 tdf_load_successful = false; 或者仅记录错误并继续
                            }
                        }
                    } else if (tdf_in_stream.status() != QDataStream::Ok && numForeignKeys > 0) {
                        qWarning() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：读取外键数量后流状态出错 (或数量 > 0 但流出错)。状态：" << tdf_in_stream.status();
                        tdf_load_successful = false;
                    } else if (numForeignKeys == 0 && tdf_in_stream.status() == QDataStream::Ok) {
                        qDebug() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "' 在TDF中定义了 0 个外键或未找到外键部分。";
                    }
                } else if (tdf_load_successful && tdfFile.atEnd()) {
                    qDebug() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：在字段块后已到达文件末尾，未找到外键数量（假定0个外键）。";
                } else if (!tdf_load_successful) {
                    qDebug() << "    [LOAD_DB_TDF] 表 '" << current_table_name << "'：因之前的TDF读取错误，跳过外键加载。";
                }
                // ***** 新增：读取默认值信息 *****
                if (tdf_load_successful && !tdfFile.atEnd()) { // 确保前面的加载步骤成功且文件未读完
                    quint32 numDefaultValues = 0;
                    // 安全读取数量
                    if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                        tdf_in_stream >> numDefaultValues;
                    } else if (!tdfFile.atEnd()){ // 文件未结束，但剩余字节不够一个quint32
                        qWarning() << "    [LOAD_DB_TDF] 表" << current_table_name << "的TDF文件：剩余字节不足以读取默认值数量。";
                    }

                    if (tdf_in_stream.status() == QDataStream::Ok && numDefaultValues > 0) {
                        qDebug() << "    [LOAD_DB_TDF] 正在为表" << current_table_name << "加载" << numDefaultValues << "个默认值定义";
                        for (quint32 i = 0; i < numDefaultValues; ++i) {
                            QString fieldName, defaultValue;
                            tdf_in_stream >> fieldName >> defaultValue; // 读取字段名和默认值
                            if (tdf_in_stream.status() != QDataStream::Ok) {
                                qWarning() << "    [LOAD_DB_TDF] 读取表" << current_table_name << "中字段" << fieldName << "的默认值时出错。";
                                tdf_load_successful = false; break; // 中断加载此表的默认值
                            }


                            table.m_defaultValues[fieldName] = defaultValue; // 直接访问或通过特定setter
                            qDebug() << "      [LOAD_DB_TDF_DEFAULT] 已加载字段:" << fieldName << "的默认值:" << defaultValue;
                        }
                    } else if (tdf_in_stream.status() != QDataStream::Ok && numDefaultValues > 0) { // 流状态错误
                        qWarning() << "    [LOAD_DB_TDF] 读取表" << current_table_name << "的默认值数量时流状态错误。";
                        tdf_load_successful = false;
                    }
                }

                // ***** 新增：读取 CHECK 约束信息 *****
                if (tdf_load_successful && !tdfFile.atEnd()) {
                    quint32 numCheckConstraints = 0;
                    // ... (安全读取 numCheckConstraints) ...
                    if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                        tdf_in_stream >> numCheckConstraints;
                    } else if (!tdfFile.atEnd()){ /* ... warning ... */ }

                    if (tdf_in_stream.status() == QDataStream::Ok && numCheckConstraints > 0) {
                        qDebug() << "    [LOAD_DB_TDF] 正在为表" << current_table_name << "加载" << numCheckConstraints << "个CHECK约束定义";
                        for (quint32 i = 0; i < numCheckConstraints; ++i) {
                            QString constraintName, checkExpression;
                            tdf_in_stream >> constraintName >> checkExpression;
                            if (tdf_in_stream.status() != QDataStream::Ok) {
                                qWarning() << "    [LOAD_DB_TDF] 读取表" << current_table_name << "的CHECK约束" << constraintName << "时出错。";
                                tdf_load_successful = false; break;
                            }
                            // 使用 xhytable::add_check_constraint 方法来填充 m_checkConstraints
                            table.add_check_constraint(checkExpression, constraintName);
                            qDebug() << "      [LOAD_DB_TDF_CHECK] 已加载CHECK约束:" << constraintName << "表达式:" << checkExpression;
                        }
                    } else if (tdf_in_stream.status() != QDataStream::Ok && numCheckConstraints > 0) { /* ... warning ... */ }
                }
                // ***** 新增逻辑：将识别出的字段级 CHECK 约束文本添加回对应 xhyfield 的 m_constraints *****
                if (tdf_load_successful && !table.m_fields.isEmpty() && !table.checkConstraints().isEmpty()) {
                    qDebug() << "  [LOAD_DB_TDF_POST_PROCESS] 开始为表" << table.name() << "的字段补加原始 CHECK 约束文本...";

                    // 为了能修改 xhyfield 对象，我们需要获取对 m_fields 中元素的非 const 访问权限
                    // 假设 xhydbmanager 是 xhytable 的友元类，可以直接访问 m_fields
                    // 或者 xhytable 提供一个返回 QList<xhyfield>& 的方法，如 table.getFieldsNonConst()
                    // 为简单起见，这里直接操作 table.m_fields，您可能需要调整

                    for (int field_idx = 0; field_idx < table.m_fields.size(); ++field_idx) {
                        // xhyfield& current_loaded_field = table.m_fields[field_idx]; // 获取字段的可修改引用

                        // 遍历所有已加载到 table.m_checkConstraints 的 CHECK 约束
                        for (auto it_check = table.checkConstraints().constBegin();
                             it_check != table.checkConstraints().constEnd(); ++it_check) {

                            const QString& constraintName = it_check.key();
                            const QString& condition = it_check.value();

                            // 尝试判断这个 CHECK 约束是否是针对当前字段的、匿名的（自动生成的约束名）
                            // 您的 xhytable::add_check_constraint(condition, "") 会生成类似 "CK_<table>_cond<N>" 的名字
                            // 如果 CHECK 表达式只涉及当前字段名，我们可以较有信心地认为它是该字段的约束

                            // 简化判断：如果约束名是自动生成的，并且条件表达式中包含当前字段名
                            // 注意：这个判断逻辑可能需要根据您的具体实现和 CHECK 表达式的复杂性来完善
                            // 例如，一个 CHECK 约束可能引用多个列，或者一个列有多个字段级 CHECK（虽然不常见）
                            if (constraintName.startsWith("CK_" + table.name() + "_cond") &&
                                condition.contains(table.m_fields[field_idx].name(), Qt::CaseInsensitive)) {

                                // 进一步检查，确保条件中不包含其他表内字段名，以增加准确性（可选）
                                bool onlyThisField = true;
                                for(const auto& otherField : table.fields()) {
                                    if (otherField.name() != table.m_fields[field_idx].name() && condition.contains(otherField.name(), Qt::CaseInsensitive)) {
                                        onlyThisField = false;
                                        break;
                                    }
                                }

                                if (onlyThisField) {
                                    QString check_string_to_add = QString("CHECK (%1)").arg(condition);
                                    // 使用 xhyfield 类中添加的 addConstraintString 方法
                                    table.m_fields[field_idx].addConstraintString(check_string_to_add);
                                    qDebug() << "    [LOAD_DB_TDF_POST_PROCESS] 为字段" << table.m_fields[field_idx].name()
                                             << "补加了原始 CHECK 约束文本:" << check_string_to_add;
                                    // 假设一个字段只有一个源于其定义的匿名 CHECK 约束，找到后可以跳出内部的 CHECK 循环
                                    // 如果您的设计允许一个字段有多个匿名 CHECK，则不应 break
                                    // break;
                                }
                            }
                        }
                    }
                }
                tdfFile.close();
            } else { // TDF 文件打开失败
                qWarning() << "  [LOAD_DB] 打开TDF文件失败:" << tdfFile.fileName() << "。跳过此表。错误: " << tdfFile.errorString();
                tdf_load_successful = false;
            }

            if (!tdf_load_successful) {
                qWarning() << "  [LOAD_DB] 表 '" << current_table_name << "' 的TDF文件加载失败，跳过TRD加载。";
                continue; // 跳过此表的剩余处理，处理下一个TDF文件
            }
            // 如果TDF加载成功，但没有定义任何字段 (且不是一个特殊的内部空表)
            if (table.fields().isEmpty() && !current_table_name.contains("_temp_")) { // 示例：允许名为 _temp_ 的表为空
                qWarning() << "  [LOAD_DB] 表 '" << current_table_name << "' 在TDF加载后没有定义任何字段。可能跳过TRD加载。";
                // 根据您的设计，一个没有字段的表是否有效。如果无效，可以 continue;
            }

            // --- 加载记录数据 (.trd) --- (保持您原有的逻辑，但增加流状态检查和对空记录的处理)
            QString trdFilePath = db_dir_path.filePath(current_table_name + ".trd");
            QFile trdFile(trdFilePath);
            bool trd_processing_error_occurred = false;

            if (trdFile.exists() && trdFile.open(QIODevice::ReadOnly)) {
                QDataStream record_file_stream(&trdFile);
                record_file_stream.setVersion(QDataStream::Qt_5_15);
                qDebug() << "    [LOAD_DB_TRD] 正在为表 '" << current_table_name << "' 从 '" << trdFilePath << "' 加载TRD。";

                while (!record_file_stream.atEnd()) {
                    if (trd_processing_error_occurred) { // 如果之前的记录处理发生严重错误，停止处理此文件
                        qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：因之前的记录错误，中止TRD加载。";
                        break;
                    }

                    quint32 record_total_size_from_file;
                    // 读取记录大小前检查流状态
                    if(record_file_stream.status() != QDataStream::Ok) {
                        qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：读取记录大小前流状态已出错。";
                        trd_processing_error_occurred = true; break;
                    }
                    record_file_stream >> record_total_size_from_file;
                    if (record_file_stream.status() != QDataStream::Ok) {
                        if (!record_file_stream.atEnd()) { // 避免在正常文件末尾时也报警告
                            qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：读取记录大小失败。状态：" << record_file_stream.status();
                        } else {
                            qDebug() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：读取记录大小到达文件尾。";
                        }
                        break; // 无法读取大小，无法继续
                    }

                    // 处理0字节记录（可能用于空表或特殊标记）
                    if (record_total_size_from_file == 0) {
                        if (table.fields().isEmpty()) {
                            qDebug() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'（无字段）：读取到0字节记录，添加空记录。";
                            table.addrecord(xhyrecord()); // 如果表允许无字段记录
                        } else {
                            qDebug() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'（有字段）：读取到0字节记录，跳过此空记录。";
                        }
                        continue; // 处理下一条记录
                    }


                    QByteArray record_data_buffer(record_total_size_from_file, Qt::Uninitialized);
                    int bytes_actually_read = record_file_stream.readRawData(record_data_buffer.data(), record_total_size_from_file);

                    if (bytes_actually_read < static_cast<int>(record_total_size_from_file)) {
                        qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：读取完整记录数据失败。期望："
                                   << record_total_size_from_file << "字节, 实际读取：" << bytes_actually_read
                                   << "字节。流状态：" << record_file_stream.status();
                        trd_processing_error_occurred = true; // 严重错误，可能影响后续记录
                        break;
                    }

                    QDataStream field_parse_stream(record_data_buffer); // 用记录数据块创建新的流
                    field_parse_stream.setVersion(QDataStream::Qt_5_15);
                    xhyrecord new_loaded_record;
                    bool current_record_field_read_error = false; // 标记当前记录内部的字段读取是否有错

                    for (const auto& field_def : table.fields()) {
                        if (field_parse_stream.atEnd()) { // 数据块提前结束
                            qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'，字段 '" << field_def.name() << "'：记录数据块提前结束，字段数据不完整。";
                            current_record_field_read_error = true; break;
                        }

                        QString value_to_insert_in_record;
                        quint8 is_null_marker;

                        if (field_parse_stream.status() != QDataStream::Ok) { /* 字段解析流状态错误 */ current_record_field_read_error = true; break; }
                        field_parse_stream >> is_null_marker;
                        if (field_parse_stream.status() != QDataStream::Ok) {
                            qWarning() << "    [LOAD_DB_TRD] 严重错误：表 '" << current_table_name << "'，字段 '" << field_def.name() << "'：读取NULL标记失败。中止此表TRD加载。";
                            trd_processing_error_occurred = true;
                            current_record_field_read_error = true;
                            break;
                        }

                        if (is_null_marker == 0) { // 值非NULL
                            QDataStream::Status field_read_status = QDataStream::Ok;
                            // (保持您原有的 switch-case 来读取各种类型的值)
                            switch (field_def.type()) {
                            case xhyfield::TINYINT:  { qint8 val;     field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::SMALLINT: { qint16 val;    field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::INT:      { qint32 val;    field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::BIGINT:   { qlonglong val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::FLOAT:    { float val;     field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DOUBLE:   { double val;    field_parse_stream >> val; value_to_insert_in_record = QString::number(val); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DECIMAL: // DECIMAL, CHAR等作为字符串存储和读取
                            case xhyfield::CHAR:
                            case xhyfield::VARCHAR:
                            case xhyfield::TEXT:
                            case xhyfield::ENUM:     { QByteArray strBytes; field_parse_stream >> strBytes; value_to_insert_in_record = QString::fromUtf8(strBytes); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DATE:     { QDate date; field_parse_stream >> date; if(date.isValid()) value_to_insert_in_record = date.toString(Qt::ISODate); else value_to_insert_in_record = QString(); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::DATETIME:
                            case xhyfield::TIMESTAMP:{ QDateTime datetime; field_parse_stream >> datetime; if(datetime.isValid()) value_to_insert_in_record = datetime.toString(Qt::ISODate); else value_to_insert_in_record = QString(); field_read_status = field_parse_stream.status(); break;}
                            case xhyfield::BOOL:     { bool boolValue; field_parse_stream >> boolValue; value_to_insert_in_record = boolValue ? "1" : "0"; field_read_status = field_parse_stream.status(); break;}
                            default:
                                qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'，字段 '" << field_def.name() << "'：未处理的数据类型 (ID: " << static_cast<int>(field_def.type()) << ")。尝试作为QByteArray读取。";
                                QByteArray unknownData;
                                if (!field_parse_stream.atEnd()) field_parse_stream >> unknownData;
                                value_to_insert_in_record = QString::fromUtf8(unknownData); // 可能不正确
                                field_read_status = field_parse_stream.status();
                                break;
                            }

                            if (field_read_status != QDataStream::Ok) {
                                qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'，字段 '" << field_def.name() << "'：读取非NULL值后流状态错误。状态：" << field_read_status << "。此字段值将设为NULL。";
                                value_to_insert_in_record = QString(); // 设为SQL NULL
                            }
                        } else { // 值是NULL (is_null_marker == 1)
                            value_to_insert_in_record = QString(); // QString() 代表 SQL NULL
                        }
                        new_loaded_record.insert(field_def.name(), value_to_insert_in_record);
                    } // 结束 for fields in record 循环

                    if (current_record_field_read_error || trd_processing_error_occurred) {
                        qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：因字段读取错误，中止处理当前记录。";
                        if(trd_processing_error_occurred) break; // 如果是严重错误，中止整个TRD文件处理
                        continue; // 否则，跳过此损坏的记录，尝试下一条
                    }

                    // 检查记录数据块是否有多余未读数据
                    if (!field_parse_stream.atEnd() && field_parse_stream.status() == QDataStream::Ok){
                        qWarning() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "'：记录解析器在处理完所有预期字段后，流中仍有未读数据或状态错误。字节剩余："
                                   << field_parse_stream.device()->bytesAvailable()
                                   << "。流状态：" << field_parse_stream.status() << "。此记录可能已损坏。";
                    }
                    table.addrecord(new_loaded_record);
                } // 结束 while records in TRD 循环
                trdFile.close();
            } else { // TRD 文件处理 (打开失败或不存在)
                if(QFile::exists(trdFilePath)) { // 文件存在但打开失败
                    qWarning() << "    [LOAD_DB] 打开TRD文件进行读取失败 (但文件存在): " << trdFilePath << " 错误: " << trdFile.errorString();
                } else { // 文件不存在
                    qDebug() << "    [LOAD_DB] TRD文件未找到 (对于新表或空表是正常的): " << trdFilePath;
                }
            }
            // currentDbPtr->addTable(table); // 将加载完成的表添加到当前数据库对象中
            // 注意：在您的原始代码中，addTable是在外层循环后执行的。
            // 我的理解是，每个 TDF 文件对应一个表，加载完 TDF 和 TRD 后，这个 table 对象就应该添加到 currentDbPtr。
            // 如果 currentDbPtr->addTable(table) 是在 tdf_files 循环之后，那么只有最后一个 table 会被添加。
            // 假设您的 xhydatabase::addTable 是拷贝语义，并且应该在每个表加载完成后调用。
            // 但由于 'table' 是在 tdf_files 循环内声明的，它在每次迭代时都会被重新创建。
            // 所以，要么 addTable 在这里，要么您需要一个 QList<xhytable> tables_for_this_db，
            // 在循环内 table.add(...)，循环后再 currentDbPtr->setTables(tables_for_this_db)。
            // 鉴于您之前的代码结构，`currentDbPtr->addTable(table);` 应该在 `tdf_files` 循环的末尾，
            // 但是这意味着您`m_databases`中的`databaseObj`需要一个`addTable`方法，
            // 而 `currentDbPtr`是指向`m_databases.last()`的。所以`currentDbPtr->addTable(table)`是对的。
            // **已确认：** 您的 `xhydatabase.cpp` 中有 `void xhydatabase::addTable(const xhytable& table)`，它执行 `m_tables.append(table)`。
            // 所以，在 `tdf_files` 循环的末尾（但在 continue 之前）调用 `currentDbPtr->addTable(table)` 是正确的。
            // 但前提是，如果 `tdf_load_successful` 为 false，或者 `table.fields().isEmpty()`，我们 `continue` 了，
            // 就不应该执行 `addTable`。
            if (tdf_load_successful && !(table.fields().isEmpty() && !current_table_name.contains("_temp_"))) {
                // 只有当 TDF 成功加载且表有效（有字段或为特殊表）时才添加
                // currentDbPtr->addTable(table); // 这个调用已在 xhyttable 构造时通过父指针关联，
                // 且 xhydatabase::m_tables 是 QList<xhytable>，
                // xhytable 对象直接在 m_tables 中构造或复制。
                // 在这种模型下，不需要显式调用 addTable，
                // xhytable 对象 'table' 在循环结束时会析构，
                // 我们需要确保其内容已正确复制到 currentDbPtr->m_tables 中。

                // 查看 xhydatabase.h/cpp:
                // xhydatabase::m_tables 是 QList<xhytable>
                // xhytable table(current_table_name, currentDbPtr); 创建一个栈对象
                // currentDbPtr->addTable(table); (如您原代码) 会将这个栈对象的拷贝添加到 currentDbPtr->m_tables
                // 这是可以的。
                // 之前的代码中，这一行是 databaseObj.addTable(table); 然后 m_databases.append(databaseObj);
                // 我改成了 m_databases.append(databaseObj); currentDbPtr = &m_databases.last();
                // ... currentDbPtr->addTable(table);  <-- 这才是正确的，将表添加到已在m_databases中的对象。
                currentDbPtr->addTable(table);
            }

        } // 结束 for TDF files (tables) 循环
    } // 结束 for DB directories 循环
    qDebug() << "[LOAD_DB] 完成所有数据库和表的加载。";
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
        }if (field.type() == xhyfield::ENUM) {
            QString enums_combined = field.enum_values().join(','); // 使用逗号分隔
            qstrncpy(fb.enum_values_str, enums_combined.toUtf8().constData(), sizeof(fb.enum_values_str) - 1);
        } else {
            fb.enum_values_str[0] = '\0'; // 对于非ENUM类型，确保为空字符串
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
    const QList<ForeignKeyDefinition>& foreignKeys = table->foreignKeys(); // <-- 现在返回的是 ForeignKeyDefinition
    quint32 numForeignKeys = static_cast<quint32>(foreignKeys.size());
    out << numForeignKeys; // 写入外键定义的数量

    qDebug() << "  [SAVE_TDF] Saving" << numForeignKeys << "foreign key definitions for table" << table->name();
    for (const auto& fkDef : foreignKeys) { // 遍历 ForeignKeyDefinition 对象
        out << fkDef.constraintName;
        out << fkDef.referenceTable;
        // 保存列映射的数量，然后逐个保存键值对
        quint32 numMappings = static_cast<quint32>(fkDef.columnMappings.size());
        out << numMappings;
        for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
            out << it.key();   // 子表列名
            out << it.value(); // 父表列名
        }
        qDebug() << "    [SAVE_TDF_FK] Saved FK:" << fkDef.constraintName
                 << " RefTable:" << fkDef.referenceTable
                 << " Mappings:" << fkDef.columnMappings;
    }
    // ***** 新增：保存默认值信息 *****
    const QMap<QString, QString>& defaultValues = table->defaultValues(); // 获取默认值映射
    quint32 numDefaultValues = static_cast<quint32>(defaultValues.size());
    out << numDefaultValues; // 写入默认值定义的数量
    qDebug() << "  [SAVE_TDF] 正在为表" << table->name() << "保存" << numDefaultValues << "个默认值定义";
    for (auto it = defaultValues.constBegin(); it != defaultValues.constEnd(); ++it) {
        out << it.key();   // 字段名 (QString)
        out << it.value(); // 默认值表达式 (QString)
        qDebug() << "    [SAVE_TDF_DEFAULT] 已保存字段:" << it.key() << "的默认值:" << it.value();
    }

    // ***** 新增：保存 CHECK 约束信息 *****
    const QMap<QString, QString>& checkConstraints = table->checkConstraints();
    quint32 numCheckConstraints = static_cast<quint32>(checkConstraints.size());
    out << numCheckConstraints; // 写入CHECK约束定义的数量
    qDebug() << "  [SAVE_TDF] 正在为表" << table->name() << "保存" << numCheckConstraints << "个CHECK约束定义";
    if (numCheckConstraints > 0) { // 添加一个判断，确保有东西可保存时才打印每个条目
        for (auto it = checkConstraints.constBegin(); it != checkConstraints.constEnd(); ++it) {
            out << it.key();   // 约束名 (QString)
            out << it.value(); // CHECK约束表达式 (QString)
            qDebug() << "    [SAVE_TDF_CHECK] 已保存CHECK约束:" << it.key() << "表达式:" << it.value();
        }
    }
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
