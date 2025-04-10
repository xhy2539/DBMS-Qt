#ifndef XHYDBMANAGER_H
#define XHYDBMANAGER_H

#include <QString>
#include <QList>
#include <QMap>
#include "xhydatabase.h"
#include "xhytable.h"
#include "xhyfield.h"
#include "xhyrecord.h"

class xhydbmanager {
public:
    xhydbmanager();

    // 数据库操作
    bool createdatabase(const QString& dbname);
    bool dropdatabase(const QString& dbname);
    bool use_database(const QString& dbname);
    QString get_current_database() const;
    QList<xhydatabase> databases() const;

    // 表操作
    bool createtable(const QString& dbname, const xhytable& table);
    bool droptable(const QString& dbname, const QString& tablename);

    // 事务管理
    void beginTransaction();
    void commit();
    void rollback();

    // 数据操作
    bool insertData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& fieldValues);
    int updateData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions);
    int deleteData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& conditions);
    bool selectData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& conditions, QVector<xhyrecord>& results);

    // 辅助函数
    void save_table_to_file(const QString& dbname, const QString& tablename, const xhytable* table); // 修改为 const xhytable*
    void save_database_to_file(const QString& dbname); // 声明 save_database_to_file
    void load_databases_from_files();
    xhydatabase* find_database(const QString& dbname);
private:
    QString m_dataDir = "G:/C++/DBMS/database";
    QList<xhydatabase> m_databases;
    QString current_database;
    bool m_inTransaction = false;
};

#endif // XHYDBMANAGER_H
