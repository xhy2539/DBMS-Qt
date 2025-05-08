#pragma once
#include <QString>
#include <QStringList>

class xhyindex {
public:
    xhyindex() = default;
    xhyindex(const QString& name, const QString& table, const QStringList& columns, bool unique = false);

    QString name() const;
    QString tableName() const;
    QStringList columns() const;
    bool isUnique() const;

    // 可扩展：序列化/反序列化到磁盘

private:
    QString m_name;
    QString m_tableName;
    QStringList m_columns;
    bool m_unique = false;
};
