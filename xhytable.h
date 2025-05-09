#ifndef XHYTABLE_H
#define XHYTABLE_H

#include "xhyfield.h"
#include "xhyrecord.h"
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <stdexcept>
#include <QVector>
#include"ConditionNode.h"
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
    void add_foreign_key(const QString& field, const QString& referencedTable, const QString& referencedField, const QString& constraintName);
    // 数据操作
    bool insertData(const QMap<QString, QString>& fieldValues);
    bool selectData(const ConditionNode &conditions, QVector<xhyrecord>& results);
    bool has_field(const QString& field_name) const;
    void add_field(const xhyfield& field);
    void remove_field(const QString& field_name);
    void rename(const QString& new_name);

    // 事务支持
    void beginTransaction();
    void commit();
    void rollback();

    const xhyfield *get_field(const QString &field_name);
    bool validateTypeForCondition(xhyfield::datatype type, const QString &value, const QString &op) const;
    bool compareValues(const QString &actual, const QString &expected, xhyfield::datatype type, bool lessThan) const;
    xhyfield::datatype getFieldType(const QString &fieldName) const;
    bool matchConditions(const xhyrecord &record, const ConditionNode &condition) const;
    int deleteData(const ConditionNode &conditions);
    int updateData(const QMap<QString, QString> &updates, const ConditionNode &conditions);

    //约束添加
    // 主键约束
    void add_primary_key(const QStringList& columns, const QString& constraint_name = "");

    // 外键约束
    void add_foreign_key(const QString& column, const QString& ref_table,
                         const QString& ref_column, const QString& constraint_name,
                         const QString& on_delete = "NO_ACTION",
                         const QString& on_update = "NO_ACTION");

    // 唯一约束
    void add_unique_constraint(const QStringList& columns, const QString& constraint_name = "");

    // 检查约束
    void add_check_constraint(const QString& condition, const QString& constraint_name = "");

    // 非空约束
    void add_not_null_constraint(const QString& column, const QString& constraint_name = "");

    // 默认值约束
    void add_default_constraint(const QString& column, const QString& default_value,
                                const QString& constraint_name = "");
    //约束检查
    bool checkInsertConstraints(const QMap<QString, QString>& fieldValues) const;
    bool checkUpdateConstraints(const QMap<QString, QString>& updates, const ConditionNode & conditions) const;
    bool checkDeleteConstraints(const ConditionNode & conditions) const;
    // 验证数据类型的方法
    bool validateRecord(const QMap<QString, QString>& values) const;
private:
    //约束存储
    QList<QString> m_primaryKeys;  // 主键字段列表
    QList<QMap<QString, QString>> m_foreignKeys;  // 外键信息列表
    QMap<QString, QList<QString>> m_uniqueConstraints; // 唯一约束映射
    QMap<QString, QString> m_checkConstraints; // 检查约束映射
    QSet<QString> m_notNullFields; // 非空字段集合
    QMap<QString, QString> m_defaultValues; // 默认值映射
    //检查数据类型
    bool validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const;


    // 索引重建
    void rebuildIndexes();

    QString m_name;
    QList<xhyfield> m_fields;
    QList<xhyrecord> m_records;
    QList<xhyrecord> m_tempRecords; // 事务备份
};

#endif // XHYTABLE_H
