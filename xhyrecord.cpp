// xhyrecord.cpp
#include "xhyrecord.h"

xhyrecord::xhyrecord() {}

QString xhyrecord::value(const QString& field) const {
    return m_data.value(field);
}

void xhyrecord::insert(const QString& field, const QString& value) {
    m_data.insert(field, value);
}

QMap<QString, QString> xhyrecord::allValues() const {
    return m_data;
}

void xhyrecord::clear() {
    m_data.clear();
}

// 新增实现
void xhyrecord::removeValue(const QString& field) {
    m_data.remove(field);
}
