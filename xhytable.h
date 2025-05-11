#ifndef XHYTABLE_H
#define XHYTABLE_H

#include "xhyfield.h"
#include "xhyrecord.h"
#include "ConditionNode.h"
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <stdexcept>
#include <QVector>
#include <QVariant>
#include <QDateTime>

class xhytable {
public:
    explicit xhytable(const QString& name = "");
    const QList<QString>& primaryKeys() const { return m_primaryKeys; }
    const QList<xhyfield>& fields() const { return m_fields; }
    const QList<xhyrecord>& records() const;
    const QList<xhyrecord>& getCommittedRecords() const { return m_records; } // 用于 createtable
    const QString& name() const { return m_name; }

    void addfield(const xhyfield& field);
    void addrecord(const xhyrecord& record);
    bool createtable(const xhytable& table);
    void add_primary_key(const QStringList& keys);
    void add_foreign_key(const QString& field, const QString& referencedTable, const QString& referencedField, const QString& constraintName);
    void add_unique_constraint(const QStringList& fields, const QString& constraintName);
    void add_check_constraint(const QString& condition, const QString& constraintName);
    bool has_field(const QString& field_name) const;
    void add_field(const xhyfield& field);
    void remove_field(const QString& field_name);
    void rename(const QString& new_name);
    const xhyfield* get_field(const QString &field_name) const;

    bool insertData(const QMap<QString, QString>& fieldValues);
    int updateData(const QMap<QString, QString> &updates, const ConditionNode &conditions);
    int deleteData(const ConditionNode &conditions);
    bool selectData(const ConditionNode &conditions, QVector<xhyrecord>& results) const;

    void beginTransaction();
    void commit();
    void rollback();
    bool isInTransaction() const { return m_inTransaction; }

    xhyfield::datatype getFieldType(const QString &fieldName) const;
    bool matchConditions(const xhyrecord &record, const ConditionNode &condition) const;

    //约束检查
    bool checkInsertConstraints(const QMap<QString, QString>& fieldValues) const;
    bool checkUpdateConstraints(const QMap<QString, QString>& updates, const ConditionNode & conditions) const;
    bool checkDeleteConstraints(const ConditionNode & conditions) const;
private:
    void validateRecord(const QMap<QString, QString>& values, const xhyrecord* original_record_for_update = nullptr) const;
    bool validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const;
    bool checkConstraint(const xhyfield& field, const QString& value) const;
    void rebuildIndexes();

    QVariant convertToTypedValue(const QString& strValue, xhyfield::datatype type) const;
    bool compareQVariants(const QVariant& left, const QVariant& right, const QString& op) const;

    QString m_name;
    QList<xhyfield> m_fields;
    QList<xhyrecord> m_records;
    QList<xhyrecord> m_tempRecords;
    bool m_inTransaction; // 已声明


    //约束存储
    QList<QString> m_primaryKeys;  // 主键字段列表
    QList<QMap<QString, QString>> m_foreignKeys;  // 外键信息列表
    QMap<QString, QList<QString>> m_uniqueConstraints; // 唯一约束映射
    QMap<QString, QString> m_checkConstraints; // 检查约束映射
    QSet<QString> m_notNullFields; // 非空字段集合
    QMap<QString, QString> m_defaultValues; // 默认值映射
};

#endif // XHYTABLE_H
