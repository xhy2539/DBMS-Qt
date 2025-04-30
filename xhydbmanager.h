#ifndef XHYDBMANAGER_H
#define XHYDBMANAGER_H

#include <QString>
#include <QList>
#include <QMap>
#include "xhydatabase.h"
#include "xhytable.h"
#include "xhyfield.h"
#include "xhyrecord.h"
#include<windows.h>
#include <QDir>
class xhydbmanager {
public:
    xhydbmanager();
//描述性文件模块
// 定义结构体
#pragma pack(push, 1)
    struct DatabaseBlock {
        char name[128];
        bool type;
        char filename[256];
        SYSTEMTIME crtime;
    };

    struct TableBlock {
        char name[128];
        int record_num;
        int field_num;
        char tdf[256];
        char tic[256];
        char trd[256];
        char tid[256];
        SYSTEMTIME crtime;
        int mtime;
    };

    struct FieldBlock {
        int order;
        char name[128];
        int type;
        int param;
        SYSTEMTIME mtime;
        int integrities;
    };

    struct IndexBlock {
        char name[128];
        bool unique;
        bool asc;
        int field_num;
        char fields[2][128];
        char record_file[256];
        char index_file[256];
    };
#pragma pack(pop)

    // 数据库操作
    bool createdatabase(const QString& dbname);
    bool dropdatabase(const QString& dbname);
    bool use_database(const QString& dbname);
    QString get_current_database() const;
    QList<xhydatabase> databases() const;

    // 表操作
    bool createtable(const QString& dbname, const xhytable& table);
    bool droptable(const QString& dbname, const QString& tablename);
    bool add_column(const QString& database_name, const QString& table_name, const xhyfield& field);
    bool drop_column(const QString& database_name, const QString& table_name, const QString& field_name);
    bool rename_table(const QString& database_name, const QString& old_name, const QString& new_name);

    // 事务管理
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    bool isInTransaction() const;

    // 数据操作
    bool insertData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& fieldValues);
    int updateData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions);
    int deleteData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& conditions);
    bool selectData(const QString& dbname, const QString& tablename, const QMap<QString, QString>& conditions, QVector<xhyrecord>& results);

    // 辅助函数
    void save_table_to_file(const QString& dbname, const QString& tablename, const xhytable* table);
    void save_database_to_file(const QString& dbname);
    void load_databases_from_files();
    xhydatabase* find_database(const QString& dbname);

      bool update_table(const QString& database_name, const xhytable& table);
    bool add_constraint(const QString& database_name, const QString& table_name, const QString& field_name, const QString& constraint);
    bool drop_constraint(const QString& database_name, const QString& table_name, const QString& constraint_name);
    bool rename_column(const QString& database_name, const QString& table_name, const QString& old_column_name, const QString& new_column_name);
    bool alter_column(const QString& database_name, const QString& table_name, const QString& old_field_name, const xhyfield& new_field);

    void commit();
    void rollback();
    void save_index_file(const QString &dbname, const QString &indexname, const QVector<QPair<QString, quint64> > &indexData);
private:
    void save_table_definition_file(const QString& filePath, const xhytable* table);
    void save_table_records_file(const QString& filePath, const xhytable* table);
    void save_table_integrity_file(const QString& filePath, const xhytable* table);
    void save_table_index_file(const QString& filePath, const xhytable* table);
    void update_table_description_file(const QString& dbname, const QString& tablename, const xhytable* table);
    QString m_dataDir = QDir::currentPath()
                        + QDir::separator() + "DBMS_ROOT";
    QList<xhydatabase> m_databases;
    QString current_database;
    bool m_inTransaction = false;

};

#endif // XHYDBMANAGER_H
