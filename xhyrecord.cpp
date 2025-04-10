#include "xhyrecord.h"

xhyrecord::xhyrecord() {}

QString xhyrecord::value(const QString& field) const {
    return m_data.value(field);
}

void xhyrecord::insert(const QString& field, const QString& value) {
    m_data.insert(field, value);
}
