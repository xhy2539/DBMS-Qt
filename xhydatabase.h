#ifndef XHYDATABASE_H
#define XHYDATABASE_H

#include <QString>
#include <QList>
#include "xhytable.h"

class xhydatabase {
public:
    explicit xhydatabase(const QString& name);

    // 数据库元数据
    QString name() const;
    QList<xhytable> tables() const;
    xhytable* find_table(const QString& tablename);

    // 表操作
    bool createtable(const xhytable& table);
    bool droptable(const QString& tablename);

    // 事务管理
    void beginTransaction();
    void commit();
    void rollback();

    // 数据操作（增加异常抛出）
    bool insertData(const QString& tablename, const QMap<QString, QString>& fieldValues);
    int updateData(const QString& tablename,
                   const QMap<QString, QString>& updates,
                   const QMap<QString, QString>& conditions);
    int deleteData(const QString& tablename,
                   const QMap<QString, QString>& conditions);
    bool selectData(const QString& tablename,
                    const QMap<QString, QString>& conditions,
                    QVector<xhyrecord>& results);

private:
    QString m_name;
    QList<xhytable> m_tables;
    QList<xhytable> m_transactionCache; // 事务暂存区
    bool m_inTransaction = false;
};

#endif // XHYDATABASE_H
