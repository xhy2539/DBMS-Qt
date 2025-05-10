#include "xhytable.h"
#include <QDate>
#include <QDebug>

xhytable::xhytable(const QString& name) : m_name(name) {}

// 表结构操作
void xhytable::addfield(const xhyfield& field) {
    if (has_field(field.name())) {
        qWarning() << "字段已存在：" << field.name();
        return;
    }
    // 设置字段顺序为当前字段数+1
    xhyfield newField = field;
    newField.setOrder(m_fields.size() + 1);
    // 解析主键约束
    if(field.constraints().contains("PRIMARY_KEY")||field.constraints().contains("primary")) {
        m_primaryKeys.append(field.name());
    }
    //解析非空约束
    if(field.constraints().contains("NOT_NULL")||field.constraints().contains("null")) {
            m_notNullFields.insert(field.name());
        }
    //解析unique
    if(field.constraints().contains("UNIQUE")||field.constraints().contains("unique")) {
        //如果已有，不做操作
            if(m_uniqueConstraints.contains(field.name()));
            else {
                //否则加入
                QList<QString> temp;
                temp.append(field.name());
                m_uniqueConstraints[field.name()]=temp;
            }
    }
    m_fields.append(field);
}
bool xhytable::has_field(const QString& field_name) const {
    for (const auto& field : m_fields) {
        if (field.name() == field_name) {
            return true;
        }
    }
    return false;
}

void xhytable::add_field(const xhyfield& field) {
    m_fields.append(field);
}

void xhytable::remove_field(const QString& field_name) {
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it) {
        if (it->name() == field_name) {
            it = m_fields.erase(it);
            break;
        }
    }
}
const xhyfield* xhytable::get_field(const QString& field_name)  {
    for (const auto& field :m_fields) {
        if (field.name() == field_name) {
            return &field;
        }
    }
    return nullptr;
}
void xhytable::rename(const QString& new_name) {
    m_name = new_name;
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
        validateRecord(fieldValues); // 完整性检查
        xhyrecord new_record;

        for(const xhyfield& field : m_fields) {
            QString value = fieldValues.value(field.name(), "");
            //如果为空且有默认值
            if(value==""&&m_defaultValues.find(field.name())!=m_defaultValues.end())
                value=m_defaultValues[field.name()];//设置默认值
            new_record.insert(field.name(), value);
        }

        m_records.append(new_record);
        return true;
    } catch(const std::exception& e) {
        qDebug() << "Insert Error:" << e.what();
        return false;
    }
}

int xhytable::updateData(const QMap<QString, QString>& updates, const ConditionNode & conditions) {
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

int xhytable::deleteData(const ConditionNode&  conditions) {
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

bool xhytable::selectData(const ConditionNode & conditions, QVector<xhyrecord>& results) {
    results.clear();
    for(const auto& record : m_records) {
        if(matchConditions(record, conditions)) {
            results.append(record);
        }
    }
    return !results.isEmpty();
}
// 检查插入操作时是否违反约束
bool xhytable::checkInsertConstraints(const QMap<QString, QString>& fieldValues) const {
    // 验证主键唯一性
    if (!m_primaryKeys.isEmpty()) {
        for (const auto& record : m_records) {
            bool conflict = true;
            for (const auto& pk : m_primaryKeys) {
                if (record.value(pk) != fieldValues.value(pk)) {
                    conflict = false;
                    break;
                }
            }
            //如果都一样
            if (conflict) {
                qWarning() << "插入失败: 主键冲突";
                return false; // 主键冲突
            }
        }
    }

    // 验证 NOT NULL 约束
    for (const QString& field : m_notNullFields) {
        if (!fieldValues.contains(field)) {
            qWarning() << "插入失败: 缺少必填字段" << field;
            return false; // 缺少必填字段
        }
    }

    // 验证唯一性约束
    for (const auto& uniqueConstraint : m_uniqueConstraints) {
        const QList<QString>& uniqueFields = uniqueConstraint;
        QMap<QString, QString> values;

        for (const QString& field : uniqueFields) {
            values[field] = fieldValues.value(field);
        }

        for (const auto& record : m_records) {
            bool conflict = true;
            for (const QString& field : uniqueFields) {
                if (record.value(field) != values[field]) {
                    conflict = false;
                    break;
                }
            }
            if (conflict) {
                qWarning() << "插入失败: 唯一性约束违反，字段" << uniqueFields.join(", ");
                return false; // 唯一性冲突
            }
        }
    }

    return true; // 所有约束通过
}
// 检查更新操作时是否违反约束
bool xhytable::checkUpdateConstraints(const QMap<QString, QString>& updates, const ConditionNode & conditions) const {
    for (auto &record : m_records) {
        if(matchConditions(record, conditions)) {

            // 验证唯一性约束
            for (const auto& uniqueConstraint : m_uniqueConstraints.keys()) {
                const QList<QString>& uniqueFields = m_uniqueConstraints[uniqueConstraint];

                QMap<QString, QString> updatedValues;

                for (const QString& field : uniqueFields) {
                    updatedValues[field] = updates.value(field, record.value(field));
                }

                bool uniqueConflict = false;

                for (const auto& existingRecord : m_records) {
                    bool conflict = true;

                    for (const QString& field : uniqueFields) {
                        if(existingRecord != record && existingRecord.value(field) == updatedValues[field]) {
                            conflict = false;
                            break;
                        }
                    }

                    if(conflict){
                        uniqueConflict = true;
                        break;
                    }
                }

                if(uniqueConflict){
                    qWarning() << "更新失败: 唯一性约束违反，字段" << uniqueFields.join(", ");
                    return false; // 唯一性冲突
                }
            }

            // 验证 NOT NULL 约束
            for (const QString& field : m_notNullFields) {
                if(updates.contains(field)) {
                    if(updates[field].isEmpty()) {
                        qWarning() << "更新失败: 字段" << field << "不能为空";
                        return false; // 非空限制违规
                    }
                } else {
                    if(record.value(field).isEmpty()) {
                        qWarning() << "更新失败: 字段" << field << "不能为空";
                        return false;
                    }
                }
            }
        }
    }

    return true; // 所有约束通过
}

// 检查删除操作时是否违反外键约束
bool xhytable::checkDeleteConstraints(const ConditionNode & conditions) const{
    return true;
}

// 类型检查
bool xhytable::validateRecord(const QMap<QString, QString>& values) const {
    // 类型检查
    for(const xhyfield& field : m_fields) {
        const QString val = values.value(field.name());
        if(!val.isEmpty() && !validateType(field.type(), val, field.constraints())) {
            return false;
        }
    }
    return true;
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


// 增强条件解析与验证
bool xhytable::matchConditions(const xhyrecord& record, const ConditionNode& condition) const {
    if (condition.type != ConditionNode::LOGIC_OP && condition.type != ConditionNode::COMPARISON) {
        return true;
    }
    // 处理逻辑运算符节点 (AND/OR)
    if (condition.type == ConditionNode::LOGIC_OP) {
        bool result = false;
        const QString op = condition.logicOp.toUpper();

        if (op == "AND") {
            result = true;
            // 遍历所有子条件，全部需满足
            for (const auto& child : condition.children) {
                if (!matchConditions(record, child)) {
                    result = false;
                    break;
                }

            }
        }
        else if (op == "OR") {
            // 任一子条件满足即可
            for (const auto& child : condition.children) {
                if (matchConditions(record, child)) {
                    result = true;
                    break;
                }
            }
        }
        else {
            throw std::runtime_error("未知逻辑运算符: " + op.toStdString());
        }
        return result;
    }

    // 处理原子比较条件节点 (字段比较)
    else if (condition.type == ConditionNode::COMPARISON) {
        const QString& fieldName = condition.comparison.firstKey();
        const QString expr = condition.comparison.value(fieldName);
        if (condition.comparison["1"] == "= 1") {
            return true; // 直接返回 true
        }
        // --- 原有字段存在性检查 ---
        if (!has_field(fieldName)) {
            throw std::runtime_error("字段不存在: " + fieldName.toStdString());
        }

        // --- 获取字段类型和实际值 ---
        xhyfield::datatype fieldType = getFieldType(fieldName);
        QString actualVal = record.value(fieldName);

        // --- 解析操作符和值（完整保留原有逻辑）---
        QRegularExpression re(
            R"(^\s*(!=|<>|>=|<=|>|<|=|LIKE|IN|BETWEEN|IS\s+NOT\s+NULL|IS\s+NULL)\s*(.+)$)",
            QRegularExpression::CaseInsensitiveOption
            );
        QRegularExpressionMatch match = re.match(expr);
        if (!match.hasMatch()) {
            throw std::runtime_error("无效的条件表达式: " + expr.toStdString());
        }

        QString op = match.captured(1).toUpper().replace(" ", "");
        QString val = match.captured(2).trimmed().replace("'", "");

        // --- 根据操作符处理逻辑 ---
        if (op == "ISNULL") {
            return actualVal.isEmpty();
        } else if (op == "ISNOTNULL") {
            return !actualVal.isEmpty();
        }

        // --- 类型检查 ---
        if (!validateTypeForCondition(fieldType, val, op)) {
            throw std::runtime_error("字段类型与条件值不匹配: " + fieldName.toStdString());
        }

        // --- 具体比较逻辑 ---
        if (op == "=") {
            return (actualVal == val);
        } else if (op == "!=" || op == "<>") {
            return (actualVal != val);
        } else if (op == ">") {
            return compareValues(actualVal, val, fieldType, false);
        } else if (op == "<") {
            return compareValues(actualVal, val, fieldType, true);
        } else if (op == ">=") {
            return !compareValues(actualVal, val, fieldType, true);
        } else if (op == "<=") {
            return !compareValues(actualVal, val, fieldType, false);
        } else if (op == "LIKE") {
            QRegularExpression pattern(
                "^" + QRegularExpression::wildcardToRegularExpression(val) + "$",
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
                );
            return pattern.match(actualVal).hasMatch();
        } else if (op == "IN") {
            QStringList options = val.mid(1, val.length()-2).split(',', Qt::SkipEmptyParts);
            for (const QString& opt : options) {
                if (actualVal == opt.trimmed()) {
                    return true;
                }
            }
            return false;
        } else if (op == "BETWEEN") {
            QStringList range = val.split(" AND ");
            if (range.size() != 2) {
                throw std::runtime_error("BETWEEN 需要两个值");
            }
            QString lower = range[0].trimmed();
            QString upper = range[1].trimmed();
            return (compareValues(actualVal, lower, fieldType, false) &&
                    compareValues(actualVal, upper, fieldType, true));
        }

        throw std::runtime_error("不支持的操作符: " + op.toStdString());
    }

    throw std::runtime_error("未知的条件节点类型");
}

// 类型验证辅助函数
bool xhytable::validateTypeForCondition(xhyfield::datatype type, const QString& value, const QString& op) const {
    if (op == "ISNULL" || op == "ISNOTNULL") return true; // 无需值验证

    switch(type) {
    case xhyfield::INT:
        return value.toInt();
    case xhyfield::FLOAT:
        return value.toDouble();
    case xhyfield::DATE:
        return QDate::fromString(value, Qt::ISODate).isValid();
    case xhyfield::BOOL:
        return value == "true" || value == "false" || value == "1" || value == "0";
    default:
        return true; // 字符串类型无需额外验证
    }
}

// 通用值比较函数
bool xhytable::compareValues(const QString& actual, const QString& expected, xhyfield::datatype type, bool lessThan) const {
    switch(type) {
    case xhyfield::INT:
        return lessThan ? (actual.toInt() < expected.toInt()) : (actual.toInt() > expected.toInt());
    case xhyfield::FLOAT:
        return lessThan ? (actual.toDouble() < expected.toDouble()) : (actual.toDouble() > expected.toDouble());
    case xhyfield::DATE: {
        QDate a = QDate::fromString(actual, Qt::ISODate);
        QDate b = QDate::fromString(expected, Qt::ISODate);
        return lessThan ? (a < b) : (a > b);
    }
    default: // 字符串按字典序比较
        return lessThan ? (actual < expected) : (actual > expected);
    }
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

// 添加主键
void xhytable::add_primary_key(const QStringList& columns, const QString& constraint_name) {
    for (const QString& column : columns) {
        QString trimmedColumn = column.trimmed(); // 去除多余空格

        if (!trimmedColumn.isEmpty()) {
            m_primaryKeys.append(trimmedColumn); // 添加到主键列表
        } else {
            qWarning() << "Warning: Primary key column name is empty, not added.";
        }
    }
    if (!constraint_name.isEmpty()) {
        qDebug() << "Primary key constraint name set to:" << constraint_name;
    } else {
        qDebug() << "No primary key constraint name specified.";
    }

    // 输出当前主键列表（调试用）
    qDebug() << "Current primary key field list:" << m_primaryKeys;
}

// 添加外键
void xhytable::add_foreign_key(const QString& column, const QString& referenceTable, const QString& referenceColumn, const QString& constraintName) {
    QMap<QString, QString> foreignKey;
    foreignKey["column"] = column;
    foreignKey["referenceTable"] = referenceTable;
    foreignKey["referenceColumn"] = referenceColumn;
    foreignKey["constraintName"] = constraintName;

    m_foreignKeys.append(foreignKey);
}

// 添加唯一约束
void xhytable::add_unique_constraint(const QList<QString>& keys, const QString& constraintName) {
    if (!m_uniqueConstraints.contains(constraintName)) {
        m_uniqueConstraints[constraintName] = keys;
    }
    else qDebug()<<"约束名已被占用";
}

// 添加检查约束
void xhytable::add_check_constraint(const QString& checkExpression, const QString& constraintName) {
    m_checkConstraints[constraintName] = checkExpression;
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
xhyfield::datatype xhytable::getFieldType(const QString& fieldName) const {
    for (const auto& field : m_fields) {
        if (field.name() == fieldName) {
            return field.type();
        }
    }
    throw std::runtime_error("字段不存在: " + fieldName.toStdString());
}
void xhytable::add_foreign_key(const QString& column, const QString& ref_table,
                               const QString& ref_column, const QString& constraint_name,
                               const QString& on_delete, const QString& on_update) {
    QMap<QString, QString> foreignKey;
    foreignKey["column"] = column;
    foreignKey["referenceTable"] = ref_table;
    foreignKey["referenceColumn"] = ref_column;
    foreignKey["constraintName"] = constraint_name;
    foreignKey["onDelete"] = on_delete;
    foreignKey["onUpdate"] = on_update;

    m_foreignKeys.append(foreignKey);
}

void xhytable::add_not_null_constraint(const QString& column, const QString& constraint_name) {
    if (!has_field(column)) {
        throw std::runtime_error(QString("非空字段 '%1' 不存在").arg(column).toStdString());
    }
    m_notNullFields.insert(column);
}

void xhytable::add_default_constraint(const QString& column, const QString& default_value,
                                      const QString& constraint_name) {
    if (!has_field(column)) {
        throw std::runtime_error(QString("默认值字段 '%1' 不存在").arg(column).toStdString());
    }
    m_defaultValues[column] = default_value;
}
