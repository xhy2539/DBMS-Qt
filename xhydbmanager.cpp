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
        if (it->name().toLower() == dbname.toLower()) {
            // 删除数据库目录
            QString dbPath = QString("%1/data/%2").arg(m_dataDir, dbname);
              QDir db_dir(dbPath);
            if (db_dir.exists()) {
                db_dir.removeRecursively();
            }

            m_databases.erase(it);
            if (current_database.toLower() == dbname.toLower()) {
                current_database.clear();
            }
            qDebug() << "Database dropped:" << dbname;
            return true;
        }
    }
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
    QDir data_dir(QString("%1/data").arg(m_dataDir));
    QStringList db_dirs = data_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& dbname : db_dirs) {
        xhydatabase db(dbname);
        QDir db_dir(data_dir.filePath(dbname));
        QStringList table_files = db_dir.entryList(QStringList() << "*.tdf", QDir::Files);

        for (const QString& table_file : table_files) {
            QString table_name = table_file.left(table_file.length() - 4); // 去掉 .tdf 后缀
            QFile tdfFile(db_dir.filePath(table_file));

            if (tdfFile.open(QIODevice::ReadOnly)) {
                QDataStream in(&tdfFile);
                in.setVersion(QDataStream::Qt_5_15);

                xhytable table(table_name);
                while (!in.atEnd()) {
                    FieldBlock fb;
                    in.readRawData(reinterpret_cast<char*>(&fb), sizeof(FieldBlock));

                    QString name(fb.name);
                    xhyfield::datatype type = static_cast<xhyfield::datatype>(fb.type);
                    QStringList constraints;

                    if (fb.integrities & 0x01) constraints.append("PRIMARY_KEY");
                    if (fb.integrities & 0x02) constraints.append("NOT_NULL");
                    if (fb.integrities & 0x04) constraints.append("UNIQUE");

                    table.addfield(xhyfield(name, type, constraints));
                }

                // 加载记录文件 (.trd)
                QString trdFilePath = QString("%1/%2.trd").arg(db_dir.path(), table_name);
                QFile trdFile(trdFilePath);
                if (trdFile.open(QIODevice::ReadOnly)) {
                    QDataStream recordStream(&trdFile);
                    recordStream.setVersion(QDataStream::Qt_5_15);

                    while (!recordStream.atEnd()) {
                        quint32 recordSize;
                        recordStream >> recordSize;

                        QByteArray recordData(recordSize, 0);
                        recordStream.readRawData(recordData.data(), recordSize);

                        QDataStream recordInputStream(recordData);
                        recordInputStream.setVersion(QDataStream::Qt_5_15);

                        xhyrecord record;
                        for (const auto& field : table.fields()) {
                            QString value;
                            switch (field.type()) {
                            case xhyfield::INT:
                                qint32 intValue;
                                recordInputStream >> intValue;
                                value = QString::number(intValue);
                                break;
                            case xhyfield::FLOAT:
                                float floatValue;
                                recordInputStream >> floatValue;
                                value = QString::number(floatValue);
                                break;
                            case xhyfield::VARCHAR:
                            case xhyfield::CHAR: {
                                QByteArray strBytes;
                                recordInputStream >> strBytes;
                                value = QString::fromUtf8(strBytes);
                                break;
                            }
                            case xhyfield::DATE: {
                                QDate date;
                                recordInputStream >> date;
                                value = date.toString("yyyy-MM-dd");
                                break;
                            }
                            case xhyfield::DATETIME: {
                                QDateTime datetime;
                                recordInputStream >> datetime;
                                value = datetime.toString("yyyy-MM-dd HH:mm:ss");
                                break;
                            }
                            case xhyfield::BOOL: {
                                bool boolValue;
                                recordInputStream >> boolValue;
                                value = boolValue ? "1" : "0";
                                break;
                            }
                            default:
                                qWarning() << "Unsupported data type for field:" << field.name();
                                break;
                            }
                            record.insert(field.name(), value);
                        }
                        table.addrecord(record);
                    }
                } else {
                    qWarning() << "Failed to open TRD file:" << trdFilePath;
                }

                db.createtable(table);
            } else {
                qWarning() << "Failed to open TDF file:" << tdfFile.fileName();
            }
        }

        m_databases.append(db);
    }
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
