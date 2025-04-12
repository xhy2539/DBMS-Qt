#ifndef XHYTABLE_H
#define XHYTABLE_H

#include "xhyfield.h"
#include "xhyrecord.h"
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <stdexcept>
#include <QVector>

class xhytable {
public:
    explicit xhytable(const QString& name = "");
    const QList<QString>& primaryKeys() const { return m_primaryKeys; }
    const QList<xhyfield>& fields() const { return m_fields; }
    const QList<xhyrecord>& records() const { return m_records; }
    const QString& name() const { return m_name; }
    // 表结构操作

    void addfield(const xhyfield& field);
    void addrecord(const xhyrecord& record);
    bool createtable(const xhytable& table);
    void add_primary_key(const QStringList& fields);
    void add_foreign_key(const QString& field, const QString& referencedTable, const QString& referencedField, const QString& constraintName);
    void add_unique_constraint(const QStringList& fields, const QString& constraintName);
    void add_check_constraint(const QString& condition, const QString& constraintName);
    // 数据操作
    bool insertData(const QMap<QString, QString>& fieldValues);
    int updateData(const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions);
    int deleteData(const QMap<QString, QString>& conditions);
    bool selectData(const QMap<QString, QString>& conditions, QVector<xhyrecord>& results);
    bool has_field(const QString& field_name) const;
    void add_field(const xhyfield& field);
    void remove_field(const QString& field_name);
    void rename(const QString& new_name);

    // 事务支持
    void beginTransaction();
    void commit();
    void rollback();

    const xhyfield *get_field(const QString &field_name);
private:
    // 验证方法
    void validateRecord(const QMap<QString, QString>& values) const;
    bool validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const;
    bool checkConstraint(const xhyfield& field, const QString& value) const;
    bool matchConditions(const xhyrecord& record, const QMap<QString, QString>& conditions) const;

    // 索引重建
    void rebuildIndexes();

    QString m_name;
    QList<xhyfield> m_fields;
    QList<xhyrecord> m_records;
    QList<xhyrecord> m_tempRecords; // 事务备份
    QList<QString> m_primaryKeys;  // 存储主键字段名称
    QList<QMap<QString, QString>> m_foreignKeys;  // 存储外键信息，格式 {'column': <column name>, 'referenceTable': <ref table>, 'referenceColumn': <ref column>, 'constraintName': <constraint name>}
    QMap<QString, QList<QString>> m_uniqueConstraints; // 存储唯一约束，格式 {'constraintName': <keys list>}
    QMap<QString, QString> m_checkConstraints; // 存储检查约束，格式 {'constraintName': <check expression>}
};

#endif // XHYTABLE_H
