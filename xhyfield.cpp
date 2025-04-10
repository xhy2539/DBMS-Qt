#include "xhyfield.h"
#include <QDate>
#include <QRegularExpression>

xhyfield::xhyfield(const QString& name, datatype type, const QStringList& constraints)
    : m_name(name), m_type(type), m_constraints(constraints) {}

QString xhyfield::name() const { return m_name; }
xhyfield::datatype xhyfield::type() const { return m_type; }
QStringList xhyfield::constraints() const {
    return m_constraints.filter(QRegularExpression("^(?!SIZE).*"));
}

QString xhyfield::checkConstraint() const {
    for(const QString& c : m_constraints) {
        if(c.startsWith("CHECK")) {
            return c.mid(6).replace("(", "").replace(")", "");
        }
    }
    return "";
}

bool xhyfield::hasCheck() const {
    return !checkConstraint().isEmpty();
}

QString xhyfield::typestring() const {
    switch(m_type) {
    case INT: return "INT";
    case VARCHAR: return "VARCHAR";
    case FLOAT: return "FLOAT";
    case DATE: return "DATE";
    case BOOL: return "BOOL";
    case CHAR: {
        for(const QString& c : m_constraints) {
            if(c.startsWith("SIZE(")) {
                return QString("CHAR(%1)").arg(c.mid(5, c.indexOf(')')-5));
            }
        }
        return "CHAR(1)";
    }
    default: return "UNKNOWN";
    }
}

bool xhyfield::validateValue(const QString& value) const {
    switch(m_type) {
    case INT:
        return QRegularExpression("^-?\\d+$").match(value).hasMatch();

    case FLOAT:
        return QRegularExpression("^-?\\d+(\\.\\d+)?$").match(value).hasMatch();

    case BOOL:
        return value.compare("true", Qt::CaseInsensitive) == 0 ||
               value.compare("false", Qt::CaseInsensitive) == 0;

    case DATE:
        return QDate::fromString(value, Qt::ISODate).isValid();

    case CHAR: {
        int maxLen = 1;
        for(const QString& c : m_constraints) {
            if(c.startsWith("SIZE(")) {
                maxLen = c.mid(5, c.indexOf(')')-5).toInt();
                break;
            }
        }
        return value.length() <= maxLen;
    }

    case VARCHAR:
        return true;

    default:
        return false;
    }
}
