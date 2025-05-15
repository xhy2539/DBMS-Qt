#include "xhyfield.h"
#include <QDate>
#include <QRegularExpression>

xhyfield::xhyfield(const QString& name, datatype type, const QStringList& constraints)
    : m_name(name), m_type(type), m_constraints(constraints) {}

QString xhyfield::name() const { return m_name; }
xhyfield::datatype xhyfield::type() const { return m_type; }
QStringList xhyfield::constraints() const {
    return m_constraints;
}

QString xhyfield::checkConstraint() const {
    for(const QString& c : m_constraints) {
        if(c.startsWith("CHECK")) {
            return c.mid(6).replace("(", "").replace(")", "");
        }
    }
    return "";
}
QStringList xhyfield::enum_values() const {
    return m_enumValues;
}
bool xhyfield::hasCheck() const {
    return !checkConstraint().isEmpty();
}
void xhyfield::set_enum_values(const QList<QString>& values) {
    this->m_enumValues = values;
    // 您可以在这里添加一些调试信息，例如：
    // qDebug() << "Enum values for field" << m_name << "set to:" << m_enumValues;
}
// xhyfield.cpp

// xhyfield.cpp

QString xhyfield::typestring() const {
    QString baseTypeStr;
    switch(m_type) {
    case TINYINT:   baseTypeStr = "TINYINT"; break;
    case SMALLINT:  baseTypeStr = "SMALLINT"; break;
    case INT:       baseTypeStr = "INT"; break;
    case BIGINT:    baseTypeStr = "BIGINT"; break;
    case FLOAT:     baseTypeStr = "FLOAT"; break;
    case DOUBLE:    baseTypeStr = "DOUBLE"; break;
    case DECIMAL:   baseTypeStr = "DECIMAL"; break;
    case CHAR:      baseTypeStr = "CHAR"; break;
    case VARCHAR:   baseTypeStr = "VARCHAR"; break;
    case TEXT:      baseTypeStr = "TEXT"; break;
    case DATE:      baseTypeStr = "DATE"; break;
    case DATETIME:  baseTypeStr = "DATETIME"; break;
    case TIMESTAMP: baseTypeStr = "TIMESTAMP"; break;
    case BOOL:      baseTypeStr = "BOOL"; break;
    case ENUM:      baseTypeStr = "ENUM"; break;
    default:        return "UNKNOWN"; // 对于未知类型直接返回
    }

    QString typeAndParams = baseTypeStr; // 初始化为基本类型字符串

    // 为 CHAR, VARCHAR, DECIMAL 添加参数 (L, P, S)
    if (m_type == CHAR || m_type == VARCHAR) {
        QString sizeSuffix; // 用于存储 "(length)"
        for (const QString& c : m_constraints) {
            if (c.startsWith("SIZE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                sizeSuffix = c.mid(4); // 获取 "(length)" 部分
                break;
            }
        }
        if (!sizeSuffix.isEmpty()) {
            typeAndParams += sizeSuffix;
        } else if (m_type == CHAR) {
            // 如果 CHAR 类型没有 SIZE 约束，SQL 标准通常默认为 CHAR(1)
            typeAndParams += "(1)";
        }
        // VARCHAR 如果没有指定长度，typeAndParams 保持为 "VARCHAR"
        // (具体行为可能依赖于数据库实现或后续的默认值处理)

    } else if (m_type == DECIMAL) {
        QString p_str, s_str;
        bool p_found = false;
        for (const QString& c : m_constraints) {
            if (c.startsWith("PRECISION(", Qt::CaseInsensitive) && c.endsWith(")")) {
                p_str = c.mid(10, c.length() - 11);
                p_found = true;
            } else if (c.startsWith("SCALE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                s_str = c.mid(6, c.length() - 7);
            }
        }
        if (p_found) {
            typeAndParams += "(" + p_str;
            if (!s_str.isEmpty()) { // S 明确指定
                typeAndParams += "," + s_str;
            } else {
                // SQL 标准：DECIMAL(P) 等同于 DECIMAL(P,0)
                typeAndParams += ",0";
            }
            typeAndParams += ")";
        }
        // 如果 DECIMAL 未找到 P,S 约束, typeAndParams 保持为 "DECIMAL"

    } else if (m_type == ENUM) {
        // （可选）可以进一步扩展，显示 ENUM 的值列表
        //这会使类型字符串很长，所以默认注释掉
        if (!m_enumValues.isEmpty()) {
            typeAndParams += QString("('%1')").arg(m_enumValues.join("','"));
        }
       // typeAndParams 保持为 "ENUM"
    }

    // 附加字段级别的 CHECK 约束
    // QStringList checkConstraintExpressions;
    // for (const QString& constraint : m_constraints) {
    //     if (constraint.startsWith("CHECK(", Qt::CaseInsensitive) && constraint.endsWith(")")) {
    //         // 添加完整的 "CHECK(...)" 字符串，通常为了统一性转为大写
    //         checkConstraintExpressions.append(constraint.toUpper());
    //     }
    // }

    // if (!checkConstraintExpressions.isEmpty()) {
    //     // 如果有 CHECK 约束，将其附加到类型字符串后面，用空格隔开
    //     typeAndParams += " " + checkConstraintExpressions.join(" "); // 如果有多个（不常见），也一并加入
    // }

    return typeAndParams; // 返回包含类型、参数和CHECK约束的完整字符串
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
