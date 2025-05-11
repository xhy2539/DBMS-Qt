#include "xhyfield.h"
#include <QDate>
#include <QRegularExpression>

xhyfield::xhyfield(const QString& name, datatype type, const QStringList& constraints)
    : m_name(name), m_type(type), m_constraints(constraints) {}

QString xhyfield::name() const { return m_name; }
xhyfield::datatype xhyfield::type() const { return m_type; }
QStringList xhyfield::constraints() const {
    return m_constraints; // 直接返回 m_constraints
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

QString xhyfield::typestring() const {
    QString baseTypeStr;
    switch(m_type) {
    case TINYINT:   baseTypeStr = "TINYINT"; break;
    case SMALLINT:  baseTypeStr = "SMALLINT"; break;
    case INT:       baseTypeStr = "INT"; break;
    case BIGINT:    baseTypeStr = "BIGINT"; break;
    case FLOAT:     baseTypeStr = "FLOAT"; break;
    case DOUBLE:    baseTypeStr = "DOUBLE"; break;
    case DECIMAL:   baseTypeStr = "DECIMAL"; break; // 参数将在下面添加
    case CHAR:      baseTypeStr = "CHAR"; break;    // 参数将在下面添加
    case VARCHAR:   baseTypeStr = "VARCHAR"; break;  // 参数将在下面添加
    case TEXT:      baseTypeStr = "TEXT"; break;
    case DATE:      baseTypeStr = "DATE"; break;
    case DATETIME:  baseTypeStr = "DATETIME"; break;
    case TIMESTAMP: baseTypeStr = "TIMESTAMP"; break;
    case BOOL:      baseTypeStr = "BOOL"; break;
    case ENUM:      baseTypeStr = "ENUM"; break;      // 参数（枚举值）可以考虑添加
    default:        return "UNKNOWN";
    }

    // 为 CHAR, VARCHAR, DECIMAL 添加参数 (L, P, S)
    if (m_type == CHAR || m_type == VARCHAR) {
        for (const QString& c : m_constraints) {
            if (c.startsWith("SIZE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                // c 的格式是 "SIZE(length)"
                return QString("%1%2").arg(baseTypeStr).arg(c.mid(4)); // 例如 CHAR(10)
            }
        }
        // 如果 CHAR 类型没有 SIZE 约束，SQL 标准通常默认为 CHAR(1)
        if (m_type == CHAR) return QString("%1(1)").arg(baseTypeStr);
        // VARCHAR 通常需要指定长度，如果没有，则可能是一个定义问题或应有默认
        return baseTypeStr; // 或者 baseTypeStr + "(<default_or_undefined>)"
    } else if (m_type == DECIMAL) {
        QString p_str, s_str;
        bool p_found = false;
        // bool s_found = false; // s_str 为空表示 S 未显式定义（可能默认为0）
        for (const QString& c : m_constraints) {
            if (c.startsWith("PRECISION(", Qt::CaseInsensitive) && c.endsWith(")")) {
                p_str = c.mid(10, c.length() - 11);
                p_found = true;
            } else if (c.startsWith("SCALE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                s_str = c.mid(6, c.length() - 7);
                // s_found = true;
            }
        }
        if (p_found) {
            if (!s_str.isEmpty()) { // S 明确指定
                return QString("%1(%2,%3)").arg(baseTypeStr, p_str, s_str);
            }
            return QString("%1(%2)").arg(baseTypeStr, p_str); // DECIMAL(P) 形式, S默认为0
        }
        return baseTypeStr; // DECIMAL 但未找到P,S约束 (可能定义不完整)
    } else if (m_type == ENUM) {
        // （可选）可以进一步扩展，显示 ENUM 的值列表
        if (!m_enumValues.isEmpty()) {
            return QString("ENUM('%1')").arg(m_enumValues.join("','"));
        }
        return baseTypeStr; // "ENUM"
    }

    return baseTypeStr;
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
