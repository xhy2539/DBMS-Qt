#ifndef XHYDATABASE_H
#define XHYDATABASE_H

#include <QString>
#include <QList>
#include "xhytable.h"
#include "ConditionNode.h" // 确保 ConditionNode.h 被包含
#include "xhyindex.h"    // 确保 xhyindex.h 被包含
#include <QVector>       // 确保 QVector 被包含 (用于 selectData)
#include <QMap>          // 确保 QMap 被包含 (用于 insertData/updateData)


class xhydatabase {
public:
    explicit xhydatabase(const QString& name = ""); // 构造函数可以有默认参数

    // 数据库元数据
    QString name() const;
    QList<xhytable>& tables(); // 返回引用以便修改 (如果需要)
    const QList<xhytable>& tables() const; // const 版本
    xhytable* find_table(const QString& tablename);
    const xhytable* find_table(const QString& tablename) const; // const 版本
    bool has_table(const QString& table_name) const;

    // 表操作
    bool createtable(const xhytable& table);
    bool droptable(const QString& tablename);

    // 事务管理
    void beginTransaction();
    void commit();
    void rollback();
    bool isInTransaction() const { return m_inTransaction; }
    void clearTables(); // 添加 clearTables 声明
    void addTable(const xhytable& table); // 添加表到当前数据库实例


    // 数据操作
    bool insertData(const QString& tablename, const QMap<QString, QString>& fieldValues);
    int updateData(const QString& tablename,
                   const QMap<QString, QString>& updates,
                   const ConditionNode &conditions);
    int deleteData(const QString& tablename,
                   const ConditionNode &conditions);
    bool selectData(const QString& tablename,
                    const ConditionNode &conditions,
                    QVector<xhyrecord>& results) const; // 改为 const

    // 索引操作
    bool createIndex(const xhyindex& idx);
    bool dropIndex(const QString& indexName);
    const xhyindex* findIndex(const QString& columnName) const; // 根据列名查找（可能需要调整为更合适的查找方式）
    const xhyindex* findIndexByName(const QString& indexName) const; // 根据索引名查找
    QList<xhyindex> allIndexes() const;


private:
    QString m_name;
    QList<xhytable> m_tables;
    QList<xhytable> m_transactionCache; // 用于事务回滚的表快照
    bool m_inTransaction = false;
    QList<xhyindex> m_indexes; // 索引列表
};

#endif // XHYDATABASE_H
