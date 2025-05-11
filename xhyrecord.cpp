#include "xhyrecord.h"

xhyrecord::xhyrecord() {}

QString xhyrecord::value(const QString& field) const {
    return m_data.value(field);
}

void xhyrecord::insert(const QString& field, const QString& value) {
    m_data.insert(field, value);
}

QMap<QString, QString> xhyrecord::allValues() const { // 新增实现
    return m_data;
}

void xhyrecord::clear() { // 新增实现
    m_data.clear();
}

bool xhyrecord::hasField(const QString& field) const { // 新增实现
    return m_data.contains(field);
}
