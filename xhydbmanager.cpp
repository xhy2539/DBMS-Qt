#include "xhydbmanager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>
#include <QDir>

xhydbmanager::xhydbmanager() {
    QDir().mkdir(m_dataDir); // 确保 data 目录存在
    load_databases_from_files();
}

bool xhydbmanager::createdatabase(const QString& dbname) {
    for (const auto& db : m_databases) {
        if (db.name().toLower() == dbname.toLower()) {
            return false; // 数据库已存在（不区分大小写）
        }
    }

    xhydatabase new_db(dbname);
    m_databases.append(new_db);

    // 创建数据库目录
    QDir db_dir(m_dataDir);
    if (!db_dir.exists(dbname)) {
        db_dir.mkdir(dbname);
    }

    save_database_to_file(dbname); // 使用 save_database_to_file
    qDebug() << "Database created:" << dbname;
    return true;
}

bool xhydbmanager::dropdatabase(const QString& dbname) {
    for (auto it = m_databases.begin(); it != m_databases.end(); ++it) {
        if (it->name().toLower() == dbname.toLower()) {
            // 删除数据库目录
            QDir db_dir(QString("%1/%2").arg(m_dataDir, dbname));
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

// 事务管理方法
void xhydbmanager::beginTransaction() {
    if (!m_inTransaction && !current_database.isEmpty()) {
        if (auto db = find_database(current_database)) {
            db->beginTransaction();
            m_inTransaction = true;
            qDebug() << "Transaction started for database:" << current_database;
        }
    }
}

void xhydbmanager::commit() {
    if (m_inTransaction && !current_database.isEmpty()) {
        if (auto db = find_database(current_database)) {
            db->commit();
            m_inTransaction = false;

            // 保存所有表
            for (const auto& table : db->tables()) {
                save_table_to_file(current_database, table.name(), &table);
            }

            qDebug() << "Transaction committed for database:" << current_database;
        }
    }
}

void xhydbmanager::rollback() {
    if (m_inTransaction && !current_database.isEmpty()) {
        if (auto db = find_database(current_database)) {
            db->rollback();
            m_inTransaction = false;
            qDebug() << "Transaction rolled back for database:" << current_database;
        }
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
                // 删除表文件
                QFile::remove(QString("%1/%2/%3.json").arg(m_dataDir, dbname, tablename));
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

int xhydbmanager::updateData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions) {
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

int xhydbmanager::deleteData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& conditions) {
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

bool xhydbmanager::selectData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& conditions, QVector<xhyrecord>& results) {
    for (auto& db : m_databases) {
        if (db.name() == dbname) {
            return db.selectData(tablename, conditions, results);
        }
    }
    return false;
}

void xhydbmanager::save_database_to_file(const QString& dbname) {
    QDir db_dir(QString("%1/%2").arg(m_dataDir, dbname));
    if (!db_dir.exists()) {
        db_dir.mkpath(".");
    }
}

void xhydbmanager::save_table_to_file(const QString& dbname, const QString& tablename, const xhytable* table) {
    if (!table) return;

    QString file_path = QString("%1/%2/%3.json").arg(m_dataDir, dbname, tablename);
    QJsonArray fields_array;

    for (const auto& field : table->fields()) {
        QJsonObject field_obj;
        field_obj["name"] = field.name();
        field_obj["type"] = field.typestring();

        QJsonArray constraints;
        for (const auto& constraint : field.constraints()) {
            constraints.append(constraint);
        }

        // 如果是CHAR类型，添加SIZE约束
        if (field.type() == xhyfield::CHAR) {
            for (const auto& constraint : field.constraints()) {
                if (constraint.startsWith("SIZE(")) {
                    field_obj["size"] = constraint.mid(5, constraint.indexOf(')')-5).toInt();
                    break;
                }
            }
        }

        field_obj["constraints"] = constraints;
        fields_array.append(field_obj);
    }

    // 保存表中的记录
    QJsonArray records_array;
    for (const auto& record : table->records()) {
        QJsonObject record_obj;
        for (const auto& field : table->fields()) {
            record_obj[field.name()] = record.value(field.name());
        }
        records_array.append(record_obj);
    }

    QJsonObject table_obj;
    table_obj["name"] = tablename;
    table_obj["fields"] = fields_array;
    table_obj["records"] = records_array;

    QFile file(file_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(table_obj).toJson());
        file.close();
        qDebug() << "Table saved to:" << file_path;
    } else {
        qDebug() << "Failed to save table:" << file_path;
    }
}

void xhydbmanager::load_databases_from_files() {
    QDir data_dir(m_dataDir);
    QStringList db_dirs = data_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& dbname : db_dirs) {
        xhydatabase db(dbname);
        QDir db_dir(QString("%1/%2").arg(m_dataDir, dbname));
        QStringList table_files = db_dir.entryList(QStringList() << "*.json", QDir::Files);

        for (const QString& table_file : table_files) {
            QString table_name = table_file.left(table_file.length() - 5); // 去掉 ".json"
            QFile file(db_dir.filePath(table_file));

            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                QJsonObject table_obj = doc.object();

                xhytable table(table_name);
                QJsonArray fields = table_obj["fields"].toArray();

                for (const auto& field_val : fields) {
                    QJsonObject field_obj = field_val.toObject();
                    QString name = field_obj["name"].toString();
                    QString type_str = field_obj["type"].toString();

                    xhyfield::datatype type;
                    QStringList constraints;

                    // 解析类型
                    if (type_str == "INT") {
                        type = xhyfield::INT;
                    }
                    else if (type_str == "VARCHAR") {
                        type = xhyfield::VARCHAR;
                    }
                    else if (type_str == "FLOAT") {
                        type = xhyfield::FLOAT;
                    }
                    else if (type_str == "DATE") {
                        type = xhyfield::DATE;
                    }
                    else if (type_str == "BOOL") {
                        type = xhyfield::BOOL;
                    }
                    else if (type_str.startsWith("CHAR(")) {
                        type = xhyfield::CHAR;
                        // 提取CHAR类型的长度
                        QString size_str = type_str.mid(5, type_str.indexOf(')')-5);
                        constraints.append("SIZE(" + size_str + ")");
                    }
                    else {
                        type = xhyfield::VARCHAR; // 默认类型
                    }

                    // 添加约束
                    for (const auto& c : field_obj["constraints"].toArray()) {
                        constraints.append(c.toString());
                    }

                    table.addfield(xhyfield(name, type, constraints));
                }

                // 加载记录
                QJsonArray records = table_obj["records"].toArray();
                for (const auto& record_val : records) {
                    QJsonObject record_obj = record_val.toObject();
                    xhyrecord record;

                    for (const auto& field : table.fields()) {
                        QString value = record_obj[field.name()].toString();
                        record.insert(field.name(), value);
                    }

                    table.addrecord(record);
                }

                db.createtable(table);
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
