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

    // 表结构操作
    QString name() const;
    QList<xhyfield> fields() const;
    QList<xhyrecord> records() const;
    void addfield(const xhyfield& field);
    void addrecord(const xhyrecord& record);
    bool createtable(const xhytable& table);

    // 数据操作
    bool insertData(const QMap<QString, QString>& fieldValues);
    int updateData(const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions);
    int deleteData(const QMap<QString, QString>& conditions);
    bool selectData(const QMap<QString, QString>& conditions, QVector<xhyrecord>& results);

    // 事务支持
    void beginTransaction();
    void commit();
    void rollback();

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
    QStringList m_primaryKeys;
};

#endif // XHYTABLE_H
