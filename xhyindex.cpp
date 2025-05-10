#include "xhyindex.h"

xhyindex::xhyindex(const QString& name, const QString& table, const QStringList& columns, bool unique)
    : m_name(name), m_tableName(table), m_columns(columns), m_unique(unique) {}

QString xhyindex::name() const { return m_name; }
QString xhyindex::tableName() const { return m_tableName; }
QStringList xhyindex::columns() const { return m_columns; }
bool xhyindex::isUnique() const { return m_unique; }
