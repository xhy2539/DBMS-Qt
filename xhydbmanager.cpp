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
    QDir data_dir(m_dataDir + "/data");
    if (!data_dir.exists()) {
        qWarning() << "[LOAD_DB] 数据目录 " << data_dir.absolutePath() << " 不存在。";
        if (!data_dir.mkpath(".")) {
            qCritical() << "[LOAD_DB] 创建数据目录失败: " << data_dir.absolutePath();
            return;
        }
        qDebug() << "[LOAD_DB] 已创建数据目录 " << data_dir.absolutePath();
    }

    m_databases.clear(); // 清空内存中的数据库列表
    QStringList db_dirs = data_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    qInfo() << "[LOAD_DB] 开始从目录加载数据库和表定义: " << data_dir.path();

    for (const QString& dbname : db_dirs) {
        qInfo() << "[LOAD_DB] 正在处理数据库目录: " << dbname;
        xhydatabase databaseObj(dbname); // 创建数据库对象
        m_databases.append(databaseObj); // 添加到管理器列表
        xhydatabase* currentDbPtr = &m_databases.last(); // 获取指向刚添加的数据库对象的指针

        QDir db_dir_path(data_dir.filePath(dbname));
        QStringList tdf_files = db_dir_path.entryList(QStringList() << "*.tdf", QDir::Files);
        qDebug() << "  [LOAD_DB] 在数据库 '" << dbname << "' 中找到 " << tdf_files.count() << " 个TDF文件。";

        for (const QString& tdf_filename : tdf_files) {
            QString current_table_name = tdf_filename.left(tdf_filename.length() - 4); // 移除 ".tdf"
            qDebug() << "  [LOAD_DB] 开始加载表 '" << current_table_name << "' 从文件: " << tdf_filename;

            // 创建表对象时，传递父数据库指针
            xhytable table(current_table_name, currentDbPtr);
            bool tdf_load_successful = true;
            QFile tdfFile(db_dir_path.filePath(tdf_filename));

            if (!tdfFile.open(QIODevice::ReadOnly)) {
                qWarning() << "    [LOAD_DB_TDF_ERROR] 无法打开TDF文件: " << tdfFile.fileName() << " 错误: " << tdfFile.errorString();
                tdf_load_successful = false; // 标记TDF加载失败
                continue; // 跳过此表
            }

            QDataStream tdf_in_stream(&tdfFile);
            tdf_in_stream.setVersion(QDataStream::Qt_5_15); // 保持与保存时一致

            // --- 1. 加载字段定义 (FieldBlock) ---
            qDebug() << "    [LOAD_DB_TDF] 开始加载字段定义...";
            while (tdf_load_successful && !tdf_in_stream.atEnd()) {
                qint64 currentPosBeforeFieldBlockRead = tdfFile.pos();
                if (currentPosBeforeFieldBlockRead >= tdfFile.size()) break; // 已到文件末尾

                // 预读检查：确保至少有足够字节读取下一个可能的标记（外键数量等）或字段块
                // 这里假设最小的后续块是一个quint32（例如外键数量）
                if ( (tdfFile.size() - currentPosBeforeFieldBlockRead) < static_cast<qint64>(sizeof(quint32)) &&
                    (tdfFile.size() - currentPosBeforeFieldBlockRead) < static_cast<qint64>(sizeof(FieldBlock)) ) {
                    qDebug() << "    [LOAD_DB_TDF] 剩余字节不足以读取任何已知结构，可能已到达TDF文件有效数据末尾。";
                    break;
                }
                // 尝试读取字段块前，先检查是否可能是后续其他数据块的开始
                // （例如，如果剩余数据大小刚好是 quint32，可能是外键数量）
                // 这个逻辑比较复杂，依赖于文件格式的严格性。
                // 为简化，我们先尝试读取 FieldBlock，如果失败或读到空块，再判断。

                FieldBlock fb;
                int bytesRead = tdf_in_stream.readRawData(reinterpret_cast<char*>(&fb), sizeof(FieldBlock));

                if (bytesRead == 0 && tdf_in_stream.atEnd()) {
                    qDebug() << "    [LOAD_DB_TDF] 读取字段块时正常到达文件尾。";
                    break;
                }
                if (bytesRead < static_cast<int>(sizeof(FieldBlock))) {
                    // 如果不是在文件末尾且读取不足，说明文件可能损坏或格式不对
                    if (!tdf_in_stream.atEnd()) {
                        qWarning() << "    [LOAD_DB_TDF_ERROR] 读取字段块错误。期望 " << sizeof(FieldBlock)
                                << " 字节, 实际读取 " << bytesRead << " 字节。流状态: " << tdf_in_stream.status()
                                << " 文件位置: " << tdfFile.pos() << "/" << tdfFile.size();
                    } else {
                        qDebug() << "    [LOAD_DB_TDF] 读取字段块时到达文件尾，但读取字节不足（可能文件末尾不完整）。";
                    }
                    // 回退流指针，尝试读取后续部分（如外键数量）
                    tdfFile.seek(currentPosBeforeFieldBlockRead);
                    qDebug() << "      回退流指针到 " << currentPosBeforeFieldBlockRead << "，尝试解析后续数据块。";
                    break; // 退出字段读取循环，尝试读取外键等
                }

                QString field_name_str = QString::fromUtf8(fb.name, strnlen(fb.name, sizeof(fb.name)));
                if (field_name_str.isEmpty() && fb.type == 0 && fb.integrities == 0) {
                    qDebug() << "    [LOAD_DB_TDF] 检测到空字段块 (可能为文件结束标记或填充)，回退流指针。";
                    tdfFile.seek(currentPosBeforeFieldBlockRead); // 回退
                    break; // 停止读取字段块
                }
                if (field_name_str.isEmpty()){
                    qWarning() << "    [LOAD_DB_TDF_WARNING] 表 '" << current_table_name << "' 中读取到字段名为空的字段块。类型: " << fb.type << "。跳过此字段。";
                    continue;
                }

                xhyfield::datatype type = static_cast<xhyfield::datatype>(fb.type);
                QStringList constraints;
                if (fb.integrities & 0x01) constraints.append("PRIMARY_KEY"); // 来自保存时的FieldBlock
                if (fb.integrities & 0x02) constraints.append("NOT_NULL");
                if (fb.integrities & 0x04) constraints.append("UNIQUE");

                if ((type == xhyfield::CHAR || type == xhyfield::VARCHAR) && fb.param > 0) {
                    constraints.append(QString("SIZE(%1)").arg(fb.param));
                } else if (type == xhyfield::DECIMAL && fb.param > 0) {
                    constraints.append(QString("PRECISION(%1)").arg(fb.param));
                    constraints.append(QString("SCALE(%1)").arg(fb.size)); // fb.size 是 scale
                }

                xhyfield loaded_field(field_name_str, type, constraints);
                if (type == xhyfield::ENUM) {
                    if (fb.enum_values_str[0] != '\0') {
                        QString enums_combined_str = QString::fromUtf8(fb.enum_values_str, strnlen(fb.enum_values_str, sizeof(fb.enum_values_str)));
                        QStringList enum_vals_list = enums_combined_str.split(',', Qt::SkipEmptyParts);
                        QStringList cleaned_enum_vals_list;
                        for (const QString& val : enum_vals_list) {
                            QString trimmedVal = val.trimmed();
                            if (!trimmedVal.isEmpty()) cleaned_enum_vals_list.append(trimmedVal);
                        }
                        loaded_field.set_enum_values(cleaned_enum_vals_list);
                        qDebug() << "      [LOAD_DB_TDF_ENUM] 字段 '" << field_name_str << "' 加载 ENUM 值: " << cleaned_enum_vals_list;
                    }
                }
                table.addfield(loaded_field); // addfield 内部处理重复等
                qDebug() << "      [LOAD_DB_TDF] 已加载字段: " << field_name_str << " 类型: " << loaded_field.typestring() << " 约束: " << loaded_field.constraints();
            } // 字段加载循环结束
            qDebug() << "    [LOAD_DB_TDF] 字段定义加载完毕。当前文件位置: " << tdfFile.pos();


            // --- 2. 加载外键信息 (ForeignKeyDefinition) ---
            qDebug() << "    [LOAD_DB_TDF] 开始加载外键定义...";
            if (tdf_load_successful && !tdfFile.atEnd()) {
                quint32 numForeignKeys = 0;
                if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                    tdf_in_stream >> numForeignKeys;
                } else if (!tdfFile.atEnd()){
                    qWarning() << "    [LOAD_DB_TDF_WARNING] 剩余字节不足以读取外键数量。假定为0。";
                }

                if (tdf_in_stream.status() == QDataStream::Ok && numForeignKeys > 0) {
                    qDebug() << "      [LOAD_DB_TDF] 期望加载 " << numForeignKeys << " 个外键。";
                    for (quint32 i = 0; i < numForeignKeys && tdf_load_successful; ++i) {
                        QString fkConstraintName, fkRefTable;
                        quint32 numMappings;
                        qint8 onDeleteActionVal = static_cast<qint8>(ForeignKeyDefinition::NO_ACTION); // 默认值
                        qint8 onUpdateActionVal = static_cast<qint8>(ForeignKeyDefinition::NO_ACTION); // 默认值

                        tdf_in_stream >> fkConstraintName >> fkRefTable >> numMappings;
                        if (tdf_in_stream.status() != QDataStream::Ok) {
                            qWarning() << "      [LOAD_DB_TDF_ERROR] 读取外键头信息 (第 " << (i + 1) << " 个) 失败。";
                            tdf_load_successful = false; break;
                        }

                        QStringList childColumns, referencedColumns;
                        for (quint32 j = 0; j < numMappings; ++j) {
                            QString childCol, refCol;
                            tdf_in_stream >> childCol >> refCol;
                            if (tdf_in_stream.status() != QDataStream::Ok) {
                                qWarning() << "      [LOAD_DB_TDF_ERROR] 读取外键映射 (外键 #" << (i+1) << ", 映射 #" << (j+1) << ") 失败。";
                                tdf_load_successful = false; break;
                            }
                            childColumns.append(childCol);
                            referencedColumns.append(refCol);
                        }
                        if (!tdf_load_successful) break;

                        // 读取级联动作
                        if (tdfFile.pos() + static_cast<qint64>(sizeof(qint8) * 2) <= tdfFile.size()) {
                            tdf_in_stream >> onDeleteActionVal >> onUpdateActionVal;
                            if (tdf_in_stream.status() != QDataStream::Ok) {
                                qWarning() << "      [LOAD_DB_TDF_WARNING] 读取外键 '" << fkConstraintName << "' 的级联动作失败。使用默认值。";
                                onDeleteActionVal = static_cast<qint8>(ForeignKeyDefinition::NO_ACTION);
                                onUpdateActionVal = static_cast<qint8>(ForeignKeyDefinition::NO_ACTION);
                            }
                        } else {
                            qDebug() << "      [LOAD_DB_TDF] 外键 '" << fkConstraintName << "' 的TDF数据中缺少级联动作。默认为 NO_ACTION。";
                        }
                        ForeignKeyDefinition::ReferentialAction onDelete = static_cast<ForeignKeyDefinition::ReferentialAction>(onDeleteActionVal);
                        ForeignKeyDefinition::ReferentialAction onUpdate = static_cast<ForeignKeyDefinition::ReferentialAction>(onUpdateActionVal);

                        try {
                            table.add_foreign_key(childColumns, fkRefTable, referencedColumns, fkConstraintName, onDelete, onUpdate);
                            qDebug() << "        [LOAD_DB_TDF_FK] 已加载外键: " << fkConstraintName << " -> " << fkRefTable
                                     << " ON DELETE: " << onDeleteActionVal << " ON UPDATE: " << onUpdateActionVal;
                        } catch (const std::runtime_error& e) {
                            qWarning() << "      [LOAD_DB_TDF_ERROR] 添加已加载的外键 '" << fkConstraintName << "' 到表对象失败: " << e.what();
                        }
                    }
                } else if (tdf_in_stream.status() != QDataStream::Ok && numForeignKeys > 0) {
                    qWarning() << "    [LOAD_DB_TDF_ERROR] 读取外键数量后流状态错误。";
                    tdf_load_successful = false;
                } else if (numForeignKeys == 0) {
                    qDebug() << "      [LOAD_DB_TDF] 未找到或定义了0个外键。";
                }
            } else if (tdf_load_successful && tdfFile.atEnd()) {
                qDebug() << "    [LOAD_DB_TDF] 文件在读取外键信息前已结束（可能无外键）。";
            }
            qDebug() << "    [LOAD_DB_TDF] 外键定义加载完毕。当前文件位置: " << tdfFile.pos();

            // --- 3. 加载默认值信息 ---
            qDebug() << "    [LOAD_DB_TDF] 开始加载默认值定义...";
            if (tdf_load_successful && !tdfFile.atEnd()) {
                quint32 numDefaultValues = 0;
                if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                    tdf_in_stream >> numDefaultValues;
                } else if(!tdfFile.atEnd()) { /* warning */ }

                if (tdf_in_stream.status() == QDataStream::Ok && numDefaultValues > 0) {
                    qDebug() << "      [LOAD_DB_TDF] 期望加载 " << numDefaultValues << " 个默认值。";
                    for (quint32 i = 0; i < numDefaultValues; ++i) {
                        QString fieldName, defaultValueStored;
                        tdf_in_stream >> fieldName >> defaultValueStored;
                        if (tdf_in_stream.status() != QDataStream::Ok) {
                            qWarning() << "      [LOAD_DB_TDF_ERROR] 读取默认值 (第 " << (i+1) << " 个) 失败。";
                            tdf_load_successful = false; break;
                        }
                        // 确保字段存在于表中
                        if(table.has_field(fieldName)) {
                            table.m_defaultValues[fieldName] = defaultValueStored; // 直接访问或通过setter
                            qDebug() << "        [LOAD_DB_TDF_DEFAULT] 已加载字段 '" << fieldName << "' 的默认值: '" << defaultValueStored << "'";
                        } else {
                            qWarning() << "      [LOAD_DB_TDF_WARNING] 为不存在的字段 '" << fieldName << "' 加载默认值定义，已忽略。";
                        }
                    }
                } else if (tdf_in_stream.status() != QDataStream::Ok && numDefaultValues > 0) { /* warning */ }
                else if (numDefaultValues == 0) {qDebug() << "      [LOAD_DB_TDF] 未找到或定义了0个默认值。";}
            } else if (tdf_load_successful && tdfFile.atEnd()) { /* info: no default values section */ }
            qDebug() << "    [LOAD_DB_TDF] 默认值定义加载完毕。当前文件位置: " << tdfFile.pos();

            // --- 4. 加载 CHECK 约束信息 ---
            qDebug() << "    [LOAD_DB_TDF] 开始加载CHECK约束定义...";
            if (tdf_load_successful && !tdfFile.atEnd()) {
                quint32 numCheckConstraints = 0;
                if (tdfFile.pos() + static_cast<qint64>(sizeof(quint32)) <= tdfFile.size()) {
                    tdf_in_stream >> numCheckConstraints;
                } else if (!tdfFile.atEnd()) { /* warning */ }

                if (tdf_in_stream.status() == QDataStream::Ok && numCheckConstraints > 0) {
                    qDebug() << "      [LOAD_DB_TDF] 期望加载 " << numCheckConstraints << " 个CHECK约束。";
                    for (quint32 i = 0; i < numCheckConstraints; ++i) {
                        QString constraintName, checkExpression;
                        tdf_in_stream >> constraintName >> checkExpression;
                        if (tdf_in_stream.status() != QDataStream::Ok) {
                            qWarning() << "      [LOAD_DB_TDF_ERROR] 读取CHECK约束 (第 " << (i+1) << " 个) 失败。";
                            tdf_load_successful = false; break;
                        }
                        table.add_check_constraint(checkExpression, constraintName);
                        qDebug() << "        [LOAD_DB_TDF_CHECK] 已加载CHECK约束: Name='" << constraintName << "', Expr='" << checkExpression << "'";
                    }
                } else if (tdf_in_stream.status() != QDataStream::Ok && numCheckConstraints > 0) { /* warning */ }
                else if (numCheckConstraints == 0) { qDebug() << "      [LOAD_DB_TDF] 未找到或定义了0个CHECK约束。";}
            } else if (tdf_load_successful && tdfFile.atEnd()) { /* info: no check constraints section */ }
            qDebug() << "    [LOAD_DB_TDF] CHECK约束定义加载完毕。当前文件位置: " << tdfFile.pos();

            // --- TDF 文件加载结束 ---
            tdfFile.close();

            // 字段级CHECK约束文本回填 (可选，如果需要将表级CHECK关联回字段的原始定义文本)
            // (这部分逻辑比较复杂，且依赖于CHECK约束是如何命名的，暂时保持简化)
            // ...

            if (!tdf_load_successful) {
                qWarning() << "  [LOAD_DB_ERROR] 表 '" << current_table_name << "' 的TDF文件加载过程中发生错误，跳过TRD加载。";
                continue; // 处理下一个TDF文件
            }
            if (table.fields().isEmpty() && !current_table_name.contains("_temp_")) { // 允许内部临时表为空
                qWarning() << "  [LOAD_DB_WARNING] 表 '" << current_table_name << "' 在TDF加载后没有定义任何字段。";
            }

            // --- 加载记录数据 (.trd) ---
            // (这部分逻辑与您之前的版本基本一致，主要增加了流状态检查和错误处理的健壮性)
            QString trdFilePath = db_dir_path.filePath(current_table_name + ".trd");
            QFile trdFile(trdFilePath);
            if (trdFile.exists()) {
                if (!trdFile.open(QIODevice::ReadOnly)) {
                    qWarning() << "    [LOAD_DB_TRD_ERROR] 打开TRD文件进行读取失败: " << trdFilePath << " 错误: " << trdFile.errorString();
                } else {
                    qDebug() << "    [LOAD_DB_TRD] 开始为表 '" << current_table_name << "' 从 '" << trdFilePath << "' 加载记录。";
                    QDataStream record_file_stream(&trdFile);
                    record_file_stream.setVersion(QDataStream::Qt_5_15);
                    bool trd_processing_error_occurred = false;

                    while (!record_file_stream.atEnd() && !trd_processing_error_occurred) {
                        quint32 record_total_size_from_file;
                        if(record_file_stream.status() != QDataStream::Ok) { /* error */ trd_processing_error_occurred = true; break; }
                        record_file_stream >> record_total_size_from_file;
                        if (record_file_stream.status() != QDataStream::Ok) {
                            if (!record_file_stream.atEnd()) { /* warning */ } else { /* debug: end of file */ }
                            break;
                        }
                        if (record_total_size_from_file == 0) { /* debug: empty record marker */ continue; }

                        QByteArray record_data_buffer(record_total_size_from_file, Qt::Uninitialized);
                        int bytes_actually_read = record_file_stream.readRawData(record_data_buffer.data(), record_total_size_from_file);
                        if (bytes_actually_read < static_cast<int>(record_total_size_from_file)) {
                            /* warning */ trd_processing_error_occurred = true; break;
                        }

                        QDataStream field_parse_stream(record_data_buffer);
                        field_parse_stream.setVersion(QDataStream::Qt_5_15);
                        xhyrecord new_loaded_record;
                        bool current_record_field_read_error = false;

                        for (const auto& field_def : table.fields()) {
                            if (field_parse_stream.atEnd()) { /* warning */ current_record_field_read_error = true; break; }
                            QString value_to_insert_in_record;
                            quint8 is_null_marker;
                            if (field_parse_stream.status()!= QDataStream::Ok) { /* error */ current_record_field_read_error = true; break;}
                            field_parse_stream >> is_null_marker;
                            if (field_parse_stream.status()!= QDataStream::Ok) { /* error */ trd_processing_error_occurred = true; current_record_field_read_error=true; break;}

                            if (is_null_marker == 0) { // 非NULL
                                // (与您原版本相同的 switch-case 读取逻辑)
                                switch (field_def.type()) {
                                case xhyfield::TINYINT:  { qint8 val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); break; }
                                case xhyfield::SMALLINT: { qint16 val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); break; }
                                case xhyfield::INT:      { qint32 val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); break; }
                                case xhyfield::BIGINT:   { qlonglong val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); break; }
                                case xhyfield::FLOAT:    { float val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); break; }
                                case xhyfield::DOUBLE:   { double val; field_parse_stream >> val; value_to_insert_in_record = QString::number(val); break; }
                                case xhyfield::DECIMAL:
                                case xhyfield::CHAR:
                                case xhyfield::VARCHAR:
                                case xhyfield::TEXT:
                                case xhyfield::ENUM:     { QByteArray strBytes; field_parse_stream >> strBytes; value_to_insert_in_record = QString::fromUtf8(strBytes); break; }
                                case xhyfield::DATE:     { QDate date; field_parse_stream >> date; value_to_insert_in_record = date.isValid() ? date.toString(Qt::ISODate) : QString(); break; }
                                case xhyfield::DATETIME:
                                case xhyfield::TIMESTAMP:{ QDateTime dt; field_parse_stream >> dt; value_to_insert_in_record = dt.isValid() ? dt.toString(Qt::ISODate) : QString(); break; }
                                case xhyfield::BOOL:     { bool bVal; field_parse_stream >> bVal; value_to_insert_in_record = bVal ? "1" : "0"; break; }
                                default:                 { /* warning */ QByteArray unkData; if(!field_parse_stream.atEnd()) field_parse_stream >> unkData; value_to_insert_in_record = QString::fromUtf8(unkData); break;}
                                }
                                if (field_parse_stream.status() != QDataStream::Ok) {
                                    qWarning() << "      [LOAD_DB_TRD_WARNING] 表 '" << current_table_name << "' 字段 '" << field_def.name() << "' 读取非NULL值后流状态错误。值设为NULL。";
                                    value_to_insert_in_record = QString(); // SQL NULL
                                }
                            } else { // is_null_marker == 1 (SQL NULL)
                                value_to_insert_in_record = QString();
                            }
                            new_loaded_record.insert(field_def.name(), value_to_insert_in_record);
                        } // 字段解析循环结束

                        if (current_record_field_read_error || trd_processing_error_occurred) {
                            if(trd_processing_error_occurred) break; // 严重错误，停止处理此TRD文件
                            continue; // 跳过损坏的记录
                        }
                        if (!field_parse_stream.atEnd() && field_parse_stream.status() == QDataStream::Ok){/* warning: trailing data in record block */}
                        table.addrecord(new_loaded_record);
                    } // 记录加载循环结束
                    trdFile.close();
                    qDebug() << "    [LOAD_DB_TRD] 表 '" << current_table_name << "' 的记录加载完毕。";
                }
            } else { // TRD 文件不存在
                qDebug() << "    [LOAD_DB_TRD] TRD文件未找到 (对于新表或空表是正常的): " << trdFilePath;
            }

            // 将加载完成的表（如果有效）添加到数据库对象
            if (tdf_load_successful && !(table.fields().isEmpty() && !current_table_name.contains("_temp_"))) {
                currentDbPtr->addTable(table); // xhydatabase::addTable 将拷贝该表
                qInfo() << "  [LOAD_DB] 表 '" << current_table_name << "' 已成功加载并添加到数据库 '" << dbname << "'。";
            } else {
                qWarning() << "  [LOAD_DB_ERROR] 表 '" << current_table_name << "' 未能成功加载或无效，未添加到数据库。";
            }
        } // TDF 文件（表）循环结束
    } // 数据库目录循环结束
    qInfo() << "[LOAD_DB] 所有数据库和表的加载过程完成。";
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
        for (const auto& fkDef : foreignKeys) {
            // ... (写入 constraintName, referenceTable, numMappings, columnMappings) ...
            out << static_cast<qint8>(fkDef.onDeleteAction); // 保存 onDeleteAction
            out << static_cast<qint8>(fkDef.onUpdateAction); // 保存 onUpdateAction
            qDebug() << "    [SAVE_TDF_FK] Saved FK Actions: ON DELETE "
                     << static_cast<int>(fkDef.onDeleteAction) << ", ON UPDATE "
                     << static_cast<int>(fkDef.onUpdateAction);
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
