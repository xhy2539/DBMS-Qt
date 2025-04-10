#include "xhytable.h"
#include <QDate>
#include <QDebug>

xhytable::xhytable(const QString& name) : m_name(name) {}

// 表结构操作
QString xhytable::name() const { return m_name; }
QList<xhyfield> xhytable::fields() const { return m_fields; }
QList<xhyrecord> xhytable::records() const { return m_records; }

void xhytable::addfield(const xhyfield& field) {
    // 解析主键约束
    if(field.constraints().contains("PRIMARY_KEY")) {
        m_primaryKeys.append(field.name());
    }
    m_fields.append(field);
    rebuildIndexes();
}

void xhytable::addrecord(const xhyrecord& record) {
    m_records.append(record);
}

bool xhytable::createtable(const xhytable& table) {
    m_name = table.name();
    m_fields = table.fields();
    m_records = table.records();
    rebuildIndexes();
    return true;
}

// 数据操作
bool xhytable::insertData(const QMap<QString, QString>& fieldValues) {
    try {
        validateRecord(fieldValues);
        xhyrecord new_record;

        for(const xhyfield& field : m_fields) {
            QString value = fieldValues.value(field.name(), "");
            if(value.isEmpty() && field.constraints().contains("NOT_NULL")) {
                throw std::runtime_error("NOT NULL约束违反");
            }
            new_record.insert(field.name(), value);
        }

        m_records.append(new_record);
        return true;
    } catch(const std::exception& e) {
        qDebug() << "Insert Error:" << e.what();
        return false;
    }
}

int xhytable::updateData(const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions) {
    int affected = 0;
    for(auto& record : m_records) {
        if(matchConditions(record, conditions)) {
            for(auto it = updates.begin(); it != updates.end(); ++it) {
                record.insert(it.key(), it.value());
            }
            affected++;
        }
    }
    return affected;
}

int xhytable::deleteData(const QMap<QString, QString>& conditions) {
    int affected = 0;
    for(auto it = m_records.begin(); it != m_records.end();) {
        if(matchConditions(*it, conditions)) {
            it = m_records.erase(it);
            affected++;
        } else {
            ++it;
        }
    }
    return affected;
}

bool xhytable::selectData(const QMap<QString, QString>& conditions, QVector<xhyrecord>& results) {
    results.clear();
    for(const auto& record : m_records) {
        if(matchConditions(record, conditions)) {
            results.append(record);
        }
    }
    return !results.isEmpty();
}

// 验证方法
void xhytable::validateRecord(const QMap<QString, QString>& values) const {
    // 字段完整性检查
    for(const xhyfield& field : m_fields) {
        if(!values.contains(field.name()) && field.constraints().contains("NOT_NULL")) {
            throw std::runtime_error("缺少必填字段: " + field.name().toStdString());
        }
    }

    // 类型和约束检查
    for(const xhyfield& field : m_fields) {
        const QString val = values.value(field.name());
        if(!val.isEmpty() && !validateType(field.type(), val, field.constraints())) {
            throw std::runtime_error("类型错误: " + field.name().toStdString());
        }
        if(!val.isEmpty() && field.hasCheck() && !checkConstraint(field, val)) {
            throw std::runtime_error("约束违反: " + field.checkConstraint().toStdString());
        }
    }

    // 主键唯一性检查
    if(!m_primaryKeys.isEmpty()) {
        for(const auto& record : m_records) {
            bool conflict = true;
            for(const QString& pk : m_primaryKeys) {
                if(record.value(pk) != values.value(pk)) {
                    conflict = false;
                    break;
                }
            }
            if(conflict) {
                throw std::runtime_error("主键冲突");
            }
        }
    }
}

bool xhytable::validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const {
    if(value.isEmpty()) return true; // 空值默认通过类型验证

    switch(type) {
    case xhyfield::INT:
        return QRegularExpression("^-?\\d+$").match(value).hasMatch();

    case xhyfield::FLOAT:
        return QRegularExpression("^-?\\d+(\\.\\d+)?$").match(value).hasMatch();

    case xhyfield::BOOL:
        return value.compare("true", Qt::CaseInsensitive) == 0 ||
               value.compare("false", Qt::CaseInsensitive) == 0 ||
               value == "1" || value == "0";

    case xhyfield::DATE:
        return QDate::fromString(value, Qt::ISODate).isValid();

    case xhyfield::CHAR: {
        int maxLen = 255;
        for(const QString& c : constraints) {
            if(c.startsWith("SIZE(")) {
                maxLen = c.mid(5, c.indexOf(')')-5).toInt();
                break;
            }
        }
        return value.length() <= maxLen;
    }

    case xhyfield::VARCHAR:
        return true;

    default:
        return false;
    }
}

bool xhytable::checkConstraint(const xhyfield& field, const QString& value) const {
    const QString check = field.checkConstraint();
    if(check.isEmpty()) return true;

    QRegularExpression re(
        QString("^%1$").arg(check)
            .replace(" IN ", "\\s+IN\\s+")
            .replace(" LIKE ", "\\s+LIKE\\s+")
            .replace("%", ".*")
            .replace("_", ".")
        );

    return re.match(value).hasMatch();
}

// 条件解析
bool xhytable::matchConditions(const xhyrecord& record, const QMap<QString, QString>& conditions) const {
    if(conditions.isEmpty()) return true; // 没有条件自动匹配

    for(auto it = conditions.begin(); it != conditions.end(); ++it) {
        const QString& field = it.key();
        const QString expr = it.value();

        QRegularExpression re(R"(([!=<>]+|LIKE|IN|BETWEEN)\s*(.+))");
        QRegularExpressionMatch match = re.match(expr);

        if(!match.hasMatch()) continue;

        QString op = match.captured(1);
        QString val = match.captured(2).replace("'", "");
        QString actualVal = record.value(field);

        if(op == "=") {
            if(actualVal != val) return false;
        }
        else if(op == "!=" || op == "<>") {
            if(actualVal == val) return false;
        }
        else if(op == ">") {
            if(actualVal.toDouble() <= val.toDouble()) return false;
        }
        else if(op == "<") {
            if(actualVal.toDouble() >= val.toDouble()) return false;
        }
        else if(op == "LIKE") {
            QRegularExpression pattern(
                "^" + val.replace("%", ".*").replace("_", ".") + "$",
                QRegularExpression::CaseInsensitiveOption
                );
            if(!pattern.match(actualVal).hasMatch()) return false;
        }
        else if(op == "IN") {
            QStringList options = val.mid(1, val.length()-2).split(',');
            if(!options.contains(actualVal)) return false;
        }
    }
    return true;
}

// 索引重建方法
void xhytable::rebuildIndexes() {
    // 这里实现索引重建逻辑
    // 目前是简单实现，可以根据需要扩展
    m_primaryKeys.clear();
    for(const auto& field : m_fields) {
        if(field.constraints().contains("PRIMARY_KEY")) {
            m_primaryKeys.append(field.name());
        }
    }
}

// 事务支持
void xhytable::beginTransaction() {
    m_tempRecords = m_records;
}

void xhytable::commit() {
    m_tempRecords.clear();
}

void xhytable::rollback() {
    m_records = m_tempRecords;
}
