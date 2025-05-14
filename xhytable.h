#ifndef XHYTABLE_H
#define XHYTABLE_H

#include "xhyfield.h"
#include "xhyrecord.h"
#include "ConditionNode.h"
#include <QString>
#include <QList>
#include <QMap>
#include <QSet>
#include <QVariant>

// 前向声明，避免循环依赖
class xhydatabase;
struct ForeignKeyDefinition {
    QString constraintName;          // 约束名称
    QString referenceTable;          // 引用（父）表的名称
    // QMap 来存储子表列名 -> 父表列名 的映射
    QMap<QString, QString> columnMappings;
    // 可选：ON DELETE 和 ON UPDATE 规则，例如 "RESTRICT", "CASCADE", "SET NULL"
    // QString onDeleteAction;
    // QString onUpdateAction;

    // 默认构造函数
    ForeignKeyDefinition() = default;

    // 比较操作符，方便在 QList 中查找或移除
    bool operator==(const ForeignKeyDefinition& other) const {
        return constraintName.compare(other.constraintName, Qt::CaseInsensitive) == 0;
    }
};
class xhytable {

public:
    // 构造函数：增加父数据库指针参数
    xhytable(const QString& name, xhydatabase* parentDb = nullptr);

    const QString& name() const { return m_name; }
    const QList<xhyfield>& fields() const { return m_fields; }
    const QList<xhyrecord>& records() const; // 根据是否在事务中返回 m_tempRecords 或 m_records
    const QList<xhyrecord>& getCommittedRecords() const { return m_records; }

    void addfield(const xhyfield& field);
    bool has_field(const QString& field_name) const;
    xhyfield::datatype getFieldType(const QString& fieldName) const;
    const xhyfield* get_field(const QString& field_name) const;
    const QStringList& primaryKeys() const { return m_primaryKeys; }
    // 同时，修改 xhytable::foreignKeys() 的返回类型
    const QList<ForeignKeyDefinition>& foreignKeys() const { return m_foreignKeys; } // 获取外键定义列表
    const QMap<QString, QList<QString>>& uniqueConstraints() const { return m_uniqueConstraints; }
    const QSet<QString>& notNullFields() const { return m_notNullFields; }
    const QMap<QString, QString>& defaultValues() const { return m_defaultValues; }
    const QMap<QString, QString>& checkConstraints() const { return m_checkConstraints; }


    void add_field(const xhyfield& field); // 等同于 addfield
    void remove_field(const QString& field_name);
    void rename(const QString& new_name);
    void addrecord(const xhyrecord& record); // 内部使用，已验证记录
    bool createtable(const xhytable& table); // 从另一个表结构和数据创建（元数据复制）

    void add_primary_key(const QStringList& keys);
    void add_foreign_key(const QStringList& childColumns,
                                   const QString& referencedTable,
                                   const QStringList& referencedColumns,
                                   const QString& constraintNameIn);
    void add_unique_constraint(const QStringList& fields, const QString& constraintName = "");
    void add_check_constraint(const QString& condition, const QString& constraintName = "");

    void beginTransaction();
    void commit();
    void rollback();
    bool isInTransaction() const { return m_inTransaction; }

    // 数据操作 (CRUD)
    bool insertData(const QMap<QString, QString>& fieldValuesFromUser);
    int updateData(const QMap<QString, QString>& updates_with_expressions, const ConditionNode& conditions);
    int deleteData(const ConditionNode& conditions);
    bool selectData(const ConditionNode& conditions, QVector<xhyrecord>& results) const;

    // 验证方法
    //约束检查
    void checkInsertConstraints(const QMap<QString, QString>& fieldValues) const;
    void checkUpdateConstraints(const xhyrecord& originalRecord, const QMap<QString, QString>& finalProposedUpdates) const ;
    bool checkDeleteConstraints(const ConditionNode & conditions) const;
    bool evaluateCheckExpression(const QString& expr, const QVariantMap& fieldValues) const;//check 语句解析
    QVariant convertStringToType(const QString& str, xhyfield::datatype type) const ;//类型转换

    // 新增：设置父数据库的方法
    void setParentDb(xhydatabase* db) { m_parentDb = db; }

    void validateRecord(const QMap<QString, QString>& values, const xhyrecord* original_record_for_update = nullptr) const;
    bool validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const;
    bool checkConstraint(const xhyfield& field, const QString& value) const; // CHECK 约束 (目前是占位符)

    void rebuildIndexes(); // (占位符)

    QVariant convertToTypedValue(const QString& strValue, xhyfield::datatype type) const;
    bool compareQVariants(const QVariant& left, const QVariant& right, const QString& op) const;
    bool matchConditions(const xhyrecord& record, const ConditionNode& condition) const;



    // 新增：检查删除父记录时的外键限制 (RESTRICT)
    bool checkForeignKeyDeleteRestrictions(const xhyrecord& recordToDelete) const;

    QString m_name;
    QList<xhyfield> m_fields;
    QList<xhyrecord> m_records; // 已提交状态
    QStringList m_primaryKeys;
    QList<ForeignKeyDefinition> m_foreignKeys;  // FK 定义
    QMap<QString, QList<QString>> m_uniqueConstraints; // <ConstraintName, ListOfFields>
    QSet<QString> m_notNullFields;
    QMap<QString, QString> m_defaultValues; // <FieldName, DefaultValue>
    QMap<QString, QString> m_checkConstraints;

    bool m_inTransaction;
    QList<xhyrecord> m_tempRecords; // 事务期间的临时记录

    xhydatabase* m_parentDb; // 指向所属数据库的指针

};

#endif // XHYTABLE_H
