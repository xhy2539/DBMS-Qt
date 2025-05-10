#include "xhytable.h"
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QVariant>
#include <QMetaType>

xhytable::xhytable(const QString& name) : m_name(name), m_inTransaction(false) {}

const QList<xhyrecord>& xhytable::records() const {
    return m_inTransaction ? m_tempRecords : m_records;
}

void xhytable::addfield(const xhyfield& field) {
    if (has_field(field.name())) {
        qWarning() << "字段已存在：" << field.name();
        return;
    }
    xhyfield newField = field;
    // 确保 setOrder 是 xhyfield 的 public 方法或在构造时设置
    // newField.setOrder(m_fields.size() + 1);
    if(field.constraints().contains("PRIMARY_KEY", Qt::CaseInsensitive)) {
        if (!m_primaryKeys.contains(field.name(), Qt::CaseInsensitive)) {
            m_primaryKeys.append(field.name());
        }
    }
    m_fields.append(newField);
}

bool xhytable::has_field(const QString& field_name) const {
    for (const auto& field : m_fields) {
        if (field.name().compare(field_name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

xhyfield::datatype xhytable::getFieldType(const QString& fieldName) const {
    for (const auto& field : m_fields) {
        if (field.name().compare(fieldName, Qt::CaseInsensitive) == 0) {
            return field.type();
        }
    }
    throw std::runtime_error("在表 '" + m_name.toStdString() + "' 中未找到字段: " + fieldName.toStdString());
}

void xhytable::add_field(const xhyfield& field) {
    addfield(field);
}

void xhytable::remove_field(const QString& field_name) {
    m_fields.removeIf([&](const xhyfield& f){ return f.name().compare(field_name, Qt::CaseInsensitive) == 0; });
    m_primaryKeys.removeAll(field_name);
}

const xhyfield* xhytable::get_field(const QString& field_name) const {
    for (const auto& field :m_fields) {
        if (field.name().compare(field_name, Qt::CaseInsensitive) == 0) {
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
    m_records = table.getCommittedRecords(); // 使用getter获取源表的m_records
    m_primaryKeys = table.primaryKeys();
    m_foreignKeys = table.m_foreignKeys; // 假设可以直接访问或有getter
    m_uniqueConstraints = table.m_uniqueConstraints;
    m_checkConstraints = table.m_checkConstraints;
    m_inTransaction = false;
    m_tempRecords.clear();
    rebuildIndexes();
    return true;
}

void xhytable::add_primary_key(const QStringList& keys) {
    for (const QString& key : keys) {
        if (has_field(key) && !m_primaryKeys.contains(key, Qt::CaseInsensitive)) {
            m_primaryKeys.append(key);
        } else if (!has_field(key)) {
            qWarning() << "添加主键失败：字段 " << key << " 不存在于表 " << m_name;
        }
    }
}
void xhytable::add_foreign_key(const QString& field, const QString& referencedTable, const QString& referencedField, const QString& constraintName) {
    if (!has_field(field)) {
        qWarning() << "添加外键失败：字段 " << field << " 不存在于表 " << m_name;
        return;
    }
    QMap<QString, QString> foreignKey;
    foreignKey["column"] = field;
    foreignKey["referenceTable"] = referencedTable;
    foreignKey["referenceColumn"] = referencedField;
    foreignKey["constraintName"] = constraintName.isEmpty() ? ("FK_" + m_name + "_" + field) : constraintName;
    m_foreignKeys.append(foreignKey);
}
void xhytable::add_unique_constraint(const QStringList& fields, const QString& constraintName) {
    for(const QString& f : fields) {
        if(!has_field(f)) {
            qWarning() << "添加唯一约束失败：字段 " << f << " 不存在于表 " << m_name;
            return;
        }
    }
    QString actualConstraintName = constraintName.isEmpty() ? ("UQ_" + m_name + "_" + fields.join("_")) : constraintName;
    if (!m_uniqueConstraints.contains(actualConstraintName)) {
        m_uniqueConstraints[actualConstraintName] = fields;
    } else {
        qWarning() << "唯一约束 " << actualConstraintName << " 已存在。";
    }
}
void xhytable::add_check_constraint(const QString& condition, const QString& constraintName) {
    QString actualConstraintName = constraintName.isEmpty() ? ("CK_" + m_name + "_cond" + QString::number(m_checkConstraints.size()+1) ) : constraintName;
    if(!m_checkConstraints.contains(actualConstraintName)){
        m_checkConstraints[actualConstraintName] = condition;
    } else {
        qWarning() << "检查约束 " << actualConstraintName << " 已存在。";
    }
}

void xhytable::beginTransaction() {
    if (!m_inTransaction) {
        m_tempRecords = m_records;
        m_inTransaction = true;
    }
}

void xhytable::commit() {
    if (m_inTransaction) {
        m_records = m_tempRecords;
        m_tempRecords.clear();
        m_inTransaction = false;
    }
}

void xhytable::rollback() {
    if (m_inTransaction) {
        m_tempRecords.clear();
        m_inTransaction = false;
    }
}

bool xhytable::insertData(const QMap<QString, QString>& fieldValues) {
    try {
        validateRecord(fieldValues, nullptr);
        xhyrecord new_record;
        for(const xhyfield& field : m_fields) {
            QString value = fieldValues.value(field.name());
            if (value.compare("NULL", Qt::CaseInsensitive) == 0 && !field.constraints().contains("NOT_NULL", Qt::CaseInsensitive)) {
                new_record.insert(field.name(), QString());
            } else {
                new_record.insert(field.name(), value);
            }
        }
        if (m_inTransaction) { // 现在 m_inTransaction 是成员变量
            m_tempRecords.append(new_record);
        } else {
            m_records.append(new_record);
        }
        return true;
    } catch(const std::runtime_error& e) {
        qWarning() << "插入数据到表 '" << m_name << "' 失败: " << e.what();
        return false;
    }
}

// xhytable.cpp
int xhytable::updateData(const QMap<QString, QString>& updates_with_expressions, const ConditionNode & conditions) {
    int affectedRows = 0;
    QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;

    for (int i = 0; i < targetRecordsList->size(); ++i) {
        const xhyrecord& originalRecordRef = targetRecordsList->at(i); // 当前行的引用

        if (matchConditions(originalRecordRef, conditions)) {
            qDebug() << "[xhytable::updateData] Row at index" << i << "matched conditions. PK hint:" << originalRecordRef.value(m_primaryKeys.isEmpty() ? "some_field" : m_primaryKeys.first());

            QMap<QString, QString> finalNewValues = originalRecordRef.allValues(); // Start with current values

            // 应用 SET 子句中的所有更新
            for (auto it_update = updates_with_expressions.constBegin(); it_update != updates_with_expressions.constEnd(); ++it_update) {
                const QString& fieldNameToUpdate = it_update.key();
                const QString& valueExpression = it_update.value().trimmed(); // "quantity - 10" or "'active'" or "50"

                qDebug() << "[xhytable::updateData] Processing SET for field:'" << fieldNameToUpdate
                         << "' with valueExpression:'" << valueExpression << "'";

                const xhyfield* fieldSchema = get_field(fieldNameToUpdate);
                if (!fieldSchema) {
                    qWarning() << "更新错误：表 '" << m_name << "' 中字段 '" << fieldNameToUpdate << "' 不存在。跳过此字段更新。";
                    continue; // 跳过这个SET子句，处理下一个
                }

                // 尝试匹配算术表达式: fieldName op literalValue (e.g., quantity = quantity - 10)
                QString escapedFieldNameForRegex = QRegularExpression::escape(fieldNameToUpdate);
                QString arithmeticPatternStr = QString(R"(^\s*%1\s*([+\-*/])\s*(.+?)\s*$)").arg(escapedFieldNameForRegex);
                QRegularExpression selfArithmeticRe(arithmeticPatternStr, QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch arithmeticMatch = selfArithmeticRe.match(valueExpression);

                if (arithmeticMatch.hasMatch()) { // 是算术表达式，如 quantity = quantity - 10
                    qDebug() << "  Arithmetic expression detected. Pattern:'" << arithmeticPatternStr << "' matched on '" << valueExpression << "'";
                    QString op = arithmeticMatch.captured(1).trimmed(); // +, -, *, /
                    QString operandStr = arithmeticMatch.captured(2).trimmed(); // 10

                    QString currentValueStr = originalRecordRef.value(fieldNameToUpdate); // 从原始记录获取当前值
                    if (currentValueStr.isNull() && (op == "+" || op == "-" || op == "*" || op == "/")) {
                        qWarning() << "更新警告：字段 '" << fieldNameToUpdate << "' 当前值为 NULL，无法进行算术运算 '" << op << "'. SET value for this field will be NULL.";
                        finalNewValues[fieldNameToUpdate] = QString(); // 算术运算结果为 NULL
                        continue; // 处理下一个SET子句
                    }


                    bool conversionOk_current, conversionOk_operand;
                    QString calculatedValueStr;

                    xhyfield::datatype type = fieldSchema->type();
                    if (type == xhyfield::INT || type == xhyfield::BIGINT || type == xhyfield::SMALLINT || type == xhyfield::TINYINT) {
                        qlonglong currentValueL = currentValueStr.toLongLong(&conversionOk_current);
                        qlonglong operandL = operandStr.toLongLong(&conversionOk_operand);
                        if (!conversionOk_current || !conversionOk_operand) {
                            qWarning() << "更新错误：字段 '" << fieldNameToUpdate << "' (" << currentValueStr
                                       << ") 或其操作数 '" << operandStr << "' 无法转换为整数进行算术运算。跳过此字段更新。";
                            continue; // 跳过这个SET子句
                        }
                        qlonglong resultL = 0;
                        if (op == "+") resultL = currentValueL + operandL;
                        else if (op == "-") resultL = currentValueL - operandL;
                        else if (op == "*") resultL = currentValueL * operandL;
                        else if (op == "/") {
                            if (operandL == 0) {
                                qWarning() << "更新错误：尝试除以零。字段：" << fieldNameToUpdate << "。跳过此字段更新。";
                                continue;
                            }
                            resultL = currentValueL / operandL;
                        } else { // 不应发生，因为 regex 已限制操作符
                            qWarning() << "更新错误：不支持的算术运算符 '" << op << "' 对于整型字段 " << fieldNameToUpdate;
                            continue;
                        }
                        calculatedValueStr = QString::number(resultL);
                        qDebug() << "  Integer arithmetic: " << currentValueL << op << operandL << "=" << calculatedValueStr;
                    } else if (type == xhyfield::FLOAT || type == xhyfield::DOUBLE || type == xhyfield::DECIMAL) {
                        double currentValueD = currentValueStr.toDouble(&conversionOk_current);
                        double operandD = operandStr.toDouble(&conversionOk_operand);
                        if (!conversionOk_current || !conversionOk_operand) {
                            qWarning() << "更新错误：字段 '" << fieldNameToUpdate << "' (" << currentValueStr
                                       << ") 或其操作数 '" << operandStr << "' 无法转换为浮点数进行算术运算。跳过此字段更新。";
                            continue;
                        }
                        double resultD = 0;
                        if (op == "+") resultD = currentValueD + operandD;
                        else if (op == "-") resultD = currentValueD - operandD;
                        else if (op == "*") resultD = currentValueD * operandD;
                        else if (op == "/") {
                            if (qAbs(operandD) < 1e-9) { // 检查除以零
                                qWarning() << "更新错误：尝试除以零。字段：" << fieldNameToUpdate << "。跳过此字段更新。";
                                continue;
                            }
                            resultD = currentValueD / operandD;
                        } else {
                            qWarning() << "更新错误：不支持的算术运算符 '" << op << "' 对于浮点字段 " << fieldNameToUpdate;
                            continue;
                        }
                        calculatedValueStr = QString::number(resultD);
                        qDebug() << "  Float arithmetic: " << currentValueD << op << operandD << "=" << calculatedValueStr;
                    } else {
                        qWarning() << "更新错误：字段 '" << fieldNameToUpdate << "' 类型 (" << fieldSchema->typestring()
                            << ") 不支持算术运算。将表达式视为字面量（可能导致类型错误）。";
                        // 如果类型不支持算术，但表达式看起来像，我们选择报错并跳过，
                        // 而不是错误地将 "field op val" 赋给字段。
                        // 或者，我们可以让它掉入下面的字面量处理，但那会导致之前的类型错误。
                        // 更安全的做法是明确跳过这个SET子句。
                        continue;
                    }
                    finalNewValues[fieldNameToUpdate] = calculatedValueStr;
                } else { // 不是 "field = field op value" 形式的算术表达式，按字面量处理
                    qDebug() << "  Expression '" << valueExpression << "' is NOT an arithmetic self-expression. Treating as literal.";
                    if (valueExpression.compare("NULL", Qt::CaseInsensitive) == 0) {
                        finalNewValues[fieldNameToUpdate] = QString(); // 代表 SQL NULL
                    } else if ((valueExpression.startsWith('\'') && valueExpression.endsWith('\'')) ||
                               (valueExpression.startsWith('"') && valueExpression.endsWith('"'))) {
                        if (valueExpression.length() >= 2) {
                            QString innerStr = valueExpression.mid(1, valueExpression.length() - 2);
                            // SQL标准：单引号内用两个单引号表示一个单引号
                            if (valueExpression.startsWith('\'')) innerStr.replace("''", "'");
                            else innerStr.replace("\"\"", "\""); // 如果也支持双引号字符串
                            finalNewValues[fieldNameToUpdate] = innerStr;
                        } else { // 只有引号，例如 '' 或 ""
                            finalNewValues[fieldNameToUpdate] = QString(""); // 空字符串
                        }
                    } else { // 非引号、非NULL的字面量 (可能是数字, 布尔值, 或未加引号的字符串)
                        finalNewValues[fieldNameToUpdate] = valueExpression;
                    }
                    qDebug() << "  Literal assignment: field '" << fieldNameToUpdate << "' set to literal parsed as '" << finalNewValues[fieldNameToUpdate] << "'";
                }
            } // 结束 SET 子句循环

            // 现在 finalNewValues 包含了所有提议的更改
            try {
                qDebug() << "[xhytable::updateData] Validating finalNewValues:" << finalNewValues << "for original record (PK hint:" << originalRecordRef.value(m_primaryKeys.isEmpty() ? "" : m_primaryKeys.first()) << ")";
                validateRecord(finalNewValues, &originalRecordRef); // 传入原始记录指针

                // 如果验证通过，则实际更新列表中的记录
                targetRecordsList->operator[](i).clear();
                for(auto it_final = finalNewValues.constBegin(); it_final != finalNewValues.constEnd(); ++it_final){
                    targetRecordsList->operator[](i).insert(it_final.key(), it_final.value());
                }
                affectedRows++;
                qDebug() << "[xhytable::updateData] Successfully updated row at index " << i;
            } catch (const std::runtime_error& e) {
                qWarning() << "更新表 '" << m_name << "' 的行 (index " << i << ", PK hint: " << originalRecordRef.value(m_primaryKeys.isEmpty() ? "" : m_primaryKeys.first()) << ")"
                           << " 验证失败: " << e.what() << " 更新被跳过。";
                // 此处不应回滚整个事务，只跳过当前行的更新
            }
        } // 结束 if matchConditions
    } // 结束记录循环
    return affectedRows;
}

int xhytable::deleteData(const ConditionNode& conditions) {
    int affectedRows = 0;
    QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;
    for (int i = targetRecordsList->size() - 1; i >= 0; --i) {
        if (matchConditions(targetRecordsList->at(i), conditions)) {
            targetRecordsList->removeAt(i);
            affectedRows++;
        }
    }
    return affectedRows;
}

bool xhytable::selectData(const ConditionNode & conditions, QVector<xhyrecord>& results) const {
    results.clear();
    const QList<xhyrecord>& sourceRecords = m_inTransaction ? m_tempRecords : m_records;
    try {
        for(const auto& record : sourceRecords) {
            if(matchConditions(record, conditions)) {
                results.append(record);
            }
        }
    } catch (const std::runtime_error& e) {
        qWarning() << "查询表 '" << m_name << "' 数据时出错: " << e.what();
        return false;
    }
    return true;
}

void xhytable::validateRecord(const QMap<QString, QString>& values, const xhyrecord* original_record_for_update) const {
    qDebug() << "[validateRecord] Validating values:" << values << (original_record_for_update ? "(UPDATE operation)" : "(INSERT operation)");

    // 1. 字段级约束检查 (NOT NULL, Type, CHECK)
    for(const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();
        QString valueToValidate;
        bool valueProvided = values.contains(fieldName);

        if (valueProvided) {
            valueToValidate = values.value(fieldName);
        }

        // 检查 NOT NULL 约束
        // SQL NULL 用空 QString 表示。 "" (空字符串) 不是 SQL NULL。
        bool isConsideredSqlNull = valueProvided && valueToValidate.isNull(); //isNull() for QString means it's a null QString object, not empty. For QVariant, !isValid() or isNull()
            // Our records store QString. An empty QString ("") means SQL NULL.
        if (valueProvided && valueToValidate.compare("NULL", Qt::CaseInsensitive) == 0) { // User explicitly typed NULL
            isConsideredSqlNull = true;
        }


        if (fieldDef.constraints().contains("NOT_NULL", Qt::CaseInsensitive)) {
            if (!valueProvided) { // 值未在 SET/VALUES 中提供
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 缺失值。");
            }
            if (isConsideredSqlNull) { // 值是显式的 "NULL" 或内部表示的NULL(空QString)
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 不能为 NULL。");
            }
            // 对于某些数据库，NOT NULL 也意味着非空字符串。如果需要此行为:
            // if (valueToValidate.isEmpty() && !isConsideredSqlNull) { // 是 "" 但不是 SQL NULL
            //     throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 不能为空字符串。");
            // }
        }

        // 类型验证 和 CHECK 约束 (仅对非SQL NULL值进行)
        if (valueProvided && !isConsideredSqlNull && !valueToValidate.isEmpty()) { // 跳过SQL NULL和实际的空字符串""的类型验证 (除非类型本身不允许空串)
            if (!validateType(fieldDef.type(), valueToValidate, fieldDef.constraints())) {
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueToValidate.toStdString() + "' 类型错误或不符合长度/格式约束。");
            }
            // CHECK 约束 (您提供的版本中 checkConstraint 是占位符)
            if (fieldDef.hasCheck() && !checkConstraint(fieldDef, valueToValidate)) {
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueToValidate.toStdString() + "' 违反了 CHECK 约束: " + fieldDef.checkConstraint().toStdString());
            }
        }
    }

    // 2. 主键唯一性检查
    if(!m_primaryKeys.isEmpty()){
        QMap<QString, QString> pkValuesInNewRecord;
        QStringList pkValStrListForError; // 用于错误信息

        for(const QString& pkFieldName : m_primaryKeys){
            if(!values.contains(pkFieldName) ||
                values.value(pkFieldName).isNull() || // 内部NULL表示
                values.value(pkFieldName).compare("NULL", Qt::CaseInsensitive) == 0) { // 用户写的 "NULL"
                throw std::runtime_error("主键字段 '" + pkFieldName.toStdString() + "' 缺失或为NULL。");
            }
            // 有些DBMS不允许主键字段为空字符串，如果需要此检查：
            // if (values.value(pkFieldName).isEmpty()) {
            //    throw std::runtime_error("主键字段 '" + pkFieldName.toStdString() + "' 不能为空字符串。");
            // }
            pkValuesInNewRecord[pkFieldName] = values.value(pkFieldName);
            pkValStrListForError.append(pkFieldName + "=" + values.value(pkFieldName));
        }
        qDebug() << "[validateRecord] Checking PK uniqueness for proposed PKs:" << pkValuesInNewRecord;

        const QList<xhyrecord>& recordsToCheck = m_inTransaction ? m_tempRecords : m_records;
        for(const xhyrecord& existingRecord : recordsToCheck){
            bool isSelf = false;
            if (original_record_for_update != nullptr && (&existingRecord == original_record_for_update)) {
                isSelf = true;
            }

            qDebug() << "  [validateRecord PK Loop] Comparing with existing record (PKs:"
                     // 打印现有记录的主键以帮助调试
                     << (m_primaryKeys.isEmpty() ? "N/A" : existingRecord.value(m_primaryKeys.first())) << "). Is self? " << isSelf;

            if (isSelf) {
                qDebug() << "    --> Skipping self-comparison for PK check.";
                continue;
            }

            bool conflict = true; // Assume conflict until a non-matching PK field is found
            for(const QString& pkFieldName : m_primaryKeys){
                // 主键比较通常是区分大小写的
                if(existingRecord.value(pkFieldName).compare(pkValuesInNewRecord.value(pkFieldName), Qt::CaseSensitive) != 0){
                    conflict = false;
                    break;
                }
            }

            if(conflict){
                qWarning() << "[validateRecord PK Check] Conflict detected! Proposed PKs:" << pkValuesInNewRecord
                           << "conflicted with an existing record.";
                throw std::runtime_error("主键冲突: " + pkValStrListForError.join(" ").toStdString());
            }
        }
    }
    // 3. UNIQUE 约束检查 (您已有 m_uniqueConstraints 成员，需要实现检查逻辑)
    // ...

    // 4. 外键约束检查 (您已有 m_foreignKeys 成员，需要实现检查逻辑，可能需要访问其他表)
    // ...

    qDebug() << "[validateRecord] Validation successful for values:" << values;
}

bool xhytable::validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const {
    if (value.isNull()) return true;
    bool ok;
    switch(type) {
    case xhyfield::INT: case xhyfield::TINYINT: case xhyfield::SMALLINT: case xhyfield::BIGINT:
        value.toLongLong(&ok); return ok;
    case xhyfield::FLOAT: case xhyfield::DOUBLE: case xhyfield::DECIMAL:
        value.toDouble(&ok); return ok;
    case xhyfield::BOOL:
        return (value.compare("true", Qt::CaseInsensitive) == 0 || value.compare("false", Qt::CaseInsensitive) == 0 || value == "1" || value == "0");
    case xhyfield::DATE:
        return QDate::fromString(value, Qt::ISODate).isValid() || QDate::fromString(value, "yyyy-MM-dd").isValid();
    case xhyfield::DATETIME: case xhyfield::TIMESTAMP:
        return QDateTime::fromString(value, Qt::ISODate).isValid() || QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss").isValid();
    case xhyfield::CHAR:
    case xhyfield::VARCHAR: {
        int definedSize = -1;
        for(const QString& c : constraints) {
            if (c.startsWith("SIZE(", Qt::CaseInsensitive)) {
                bool sizeOk; definedSize = c.mid(5, c.length() - 6).toInt(&sizeOk);
                if (!sizeOk) definedSize = (type == xhyfield::CHAR ? 1 : 255); break;
            }
        }
        if (type == xhyfield::CHAR && definedSize <= 0) definedSize = 1;
        if (type == xhyfield::VARCHAR && definedSize <= 0) definedSize = 255;
        if (definedSize > 0 && value.length() > definedSize) {
            qWarning() << "值 '" << value << "' 对于 " << (type == xhyfield::CHAR ? "CHAR" : "VARCHAR") << " 类型超过定义长度 " << definedSize;
            return false;
        }
        return true;
    }
    case xhyfield::TEXT: case xhyfield::ENUM: return true;
    default: qWarning() << "未知的类型用于验证: " << static_cast<int>(type); return false;
    }
}

bool xhytable::checkConstraint(const xhyfield& field, const QString& value) const {
    Q_UNUSED(field); Q_UNUSED(value);
    return true;
}

void xhytable::rebuildIndexes() { /* 占位符 */ }
QVariant xhytable::convertToTypedValue(const QString& strValue, xhyfield::datatype type) const {
    if (strValue.isNull() || strValue.compare("NULL", Qt::CaseInsensitive) == 0) {
        qDebug() << "[convertToTypedValue] Input '" << strValue << "' is NULL, returning invalid QVariant.";
        return QVariant(); // SQL NULL
    }

    bool ok;
    switch (type) {
    case xhyfield::INT:
    case xhyfield::TINYINT:
    case xhyfield::SMALLINT:
    case xhyfield::BIGINT: {
        qlonglong val = strValue.toLongLong(&ok);
        if (ok) {
            // 优先返回int如果可能，保持类型精确性
            if (val >= std::numeric_limits<int>::min() && val <= std::numeric_limits<int>::max()) {
                qDebug() << "[convertToTypedValue] INT path for '" << strValue << "' -> QVariant(int(" << static_cast<int>(val) << "))";
                return QVariant(static_cast<int>(val));
            }
            qDebug() << "[convertToTypedValue] INT path for '" << strValue << "' -> QVariant(qlonglong(" << val << "))";
            return QVariant(val);
        }
        qDebug() << "[convertToTypedValue] INT path FAILED for '" << strValue << "'. Returning as string.";
        return QVariant(strValue); // 转换失败，返回原字符串
    }
    case xhyfield::FLOAT:
    case xhyfield::DOUBLE:
    case xhyfield::DECIMAL: {
        double val = strValue.toDouble(&ok);
        if (ok) {
            qDebug() << "[convertToTypedValue] FLOAT/DOUBLE path for '" << strValue << "' -> QVariant(double(" << val << "))";
            return QVariant(val);
        }
        qDebug() << "[convertToTypedValue] FLOAT/DOUBLE path FAILED for '" << strValue << "'. Returning as string.";
        return QVariant(strValue);
    }
    case xhyfield::BOOL:
        if (strValue.compare("true", Qt::CaseInsensitive) == 0 || strValue == "1") {
            qDebug() << "[convertToTypedValue] BOOL path for '" << strValue << "' -> QVariant(true)";
            return QVariant(true);
        }
        if (strValue.compare("false", Qt::CaseInsensitive) == 0 || strValue == "0") {
            qDebug() << "[convertToTypedValue] BOOL path for '" << strValue << "' -> QVariant(false)";
            return QVariant(false);
        }
        qDebug() << "[convertToTypedValue] BOOL path FAILED for '" << strValue << "'. Returning as string.";
        return QVariant(strValue);
    case xhyfield::DATE: {
        QDate date = QDate::fromString(strValue, "yyyy-MM-dd");
        if (!date.isValid()) date = QDate::fromString(strValue, Qt::ISODate);
        if(date.isValid()) {
            qDebug() << "[convertToTypedValue] DATE path for '" << strValue << "' -> QVariant(QDate(" << date.toString("yyyy-MM-dd") << "))";
            return QVariant(date);
        }
        qDebug() << "[convertToTypedValue] DATE path FAILED for '" << strValue << "'. Returning as string.";
        return QVariant(strValue);
    }
    case xhyfield::DATETIME:
    case xhyfield::TIMESTAMP: {
        QDateTime dt = QDateTime::fromString(strValue, "yyyy-MM-dd HH:mm:ss");
        if (!dt.isValid()) dt = QDateTime::fromString(strValue, Qt::ISODate);
        if(dt.isValid()){
            qDebug() << "[convertToTypedValue] DATETIME path for '" << strValue << "' -> QVariant(QDateTime(" << dt.toString("yyyy-MM-dd HH:mm:ss") << "))";
            return QVariant(dt);
        }
        qDebug() << "[convertToTypedValue] DATETIME path FAILED for '" << strValue << "'. Returning as string.";
        return QVariant(strValue);
    }
    default: // VARCHAR, CHAR, TEXT, ENUM 等
        qDebug() << "[convertToTypedValue] String path (default) for '" << strValue << "' -> QVariant(QString(" << strValue << "))";
        return QVariant(strValue);
    }
}

bool xhytable::compareQVariants(const QVariant& left, const QVariant& right, const QString& op) const {
    // IS NULL 和 IS NOT NULL 通常在 matchConditions 中由调用者直接处理，因为它们是单操作数
    // 但为了健壮性，如果它们被传到这里：
    if (op == "IS NULL") {
        bool isLeftNull = !left.isValid() || left.isNull();
        qDebug() << "[compareQVariants] Op: IS NULL, Left: " << left << " (isNull:" << isLeftNull << ") -> " << isLeftNull;
        return isLeftNull;
    }
    if (op == "IS NOT NULL") {
        bool isLeftNotNull = left.isValid() && !left.isNull();
        qDebug() << "[compareQVariants] Op: IS NOT NULL, Left: " << left << " (isNotNull:" << isLeftNotNull << ") -> " << isLeftNotNull;
        return isLeftNotNull;
    }

    // 对于二元操作符，如果任一方是SQL NULL，则结果通常是false (UNKNOWN)
    if (!left.isValid() || left.isNull() || !right.isValid() || right.isNull()) {
        qDebug() << "[compareQVariants] One or both operands are NULL. Left valid:" << left.isValid() << "isNull:" << left.isNull()
        << "Right valid:" << right.isValid() << "isNull:" << right.isNull() << "Op:" << op << "-> returning false";
        return false;
    }

    // 使用 typeId() 获取 QVariant 的内部类型 ID
    int leftTypeId = left.typeId();
    int rightTypeId = right.typeId();

    qDebug() << "[compareQVariants] Left: " << left.toString() << " (TypeID:" << leftTypeId << ", Name:" << left.typeName() << ")"
             << " Op: '" << op << "' "
             << "Right: " << right.toString() << " (TypeID:" << rightTypeId << ", Name:" << right.typeName() << ")";

    // 优先处理原生数字类型之间的精确比较
    if ((leftTypeId == QMetaType::Int || leftTypeId == QMetaType::LongLong || leftTypeId == QMetaType::ULongLong) &&
        (rightTypeId == QMetaType::Int || rightTypeId == QMetaType::LongLong || rightTypeId == QMetaType::ULongLong)) {
        qlonglong l_ll = left.toLongLong();
        qlonglong r_ll = right.toLongLong();
        qDebug() << "  Integer Comparison Path: l_ll=" << l_ll << ", r_ll=" << r_ll;
        if (op == "=") return l_ll == r_ll;
        if (op == "!=" || op == "<>") return l_ll != r_ll;
        if (op == ">") return l_ll > r_ll;
        if (op == "<") return l_ll < r_ll;
        if (op == ">=") return l_ll >= r_ll;
        if (op == "<=") return l_ll <= r_ll;
    }
    // 浮点数或可转换为浮点数的类型之间的比较
    else if (left.canConvert<double>() && right.canConvert<double>()) {
        bool lok, rok;
        double l_double = left.toDouble(&lok);
        double r_double = right.toDouble(&rok);
        qDebug() << "  Numeric Convertible Path: l_double=" << l_double << "(ok:" << lok << ")"
                 << ", r_double=" << r_double << "(ok:" << rok << ")";
        if (lok && rok) { // 确保双方都成功转换为double
            // 检查是否实际上是整数的比较（避免不必要的浮点精度问题）
            // 这种情况应该被上面的分支覆盖，但作为双重检查
            bool leftIsActuallyInt = (leftTypeId == QMetaType::Int || leftTypeId == QMetaType::LongLong || leftTypeId == QMetaType::ULongLong);
            bool rightIsActuallyInt = (rightTypeId == QMetaType::Int || rightTypeId == QMetaType::LongLong || rightTypeId == QMetaType::ULongLong);

            if(leftIsActuallyInt && rightIsActuallyInt && qAbs(l_double - left.toLongLong()) < 0.0000001 && qAbs(r_double - right.toLongLong()) < 0.0000001){
                qlonglong l_ll_f = left.toLongLong();
                qlonglong r_ll_f = right.toLongLong();
                qDebug() << "    Integer Sub-Path (from float convert): l_ll=" << l_ll_f << ", r_ll=" << r_ll_f;
                if (op == "=") return l_ll_f == r_ll_f;
                if (op == "!=" || op == "<>") return l_ll_f != r_ll_f;
                // ... 其他操作符 ...
                if (op == ">") return l_ll_f > r_ll_f;
                if (op == "<") return l_ll_f < r_ll_f;
                if (op == ">=") return l_ll_f >= r_ll_f;
                if (op == "<=") return l_ll_f <= r_ll_f;
            } else { // 至少一个是真正的浮点数，或从字符串转来的数字
                qDebug() << "    Floating Point Comparison Sub-Path: l_double=" << l_double << ", r_double=" << r_double;
                const double epsilon = 0.000001;
                if (op == "=") return qAbs(l_double - r_double) < epsilon;
                if (op == "!=" || op == "<>") return qAbs(l_double - r_double) >= epsilon;
                if (op == ">") return l_double > r_double && qAbs(l_double - r_double) >= epsilon;
                if (op == "<") return l_double < r_double && qAbs(l_double - r_double) >= epsilon;
                if (op == ">=") return l_double > r_double || qAbs(l_double - r_double) < epsilon;
                if (op == "<=") return l_double < r_double || qAbs(l_double - r_double) < epsilon;
            }
        } else {
            qWarning() << "  Numeric conversion failed: left_ok=" << lok << ", right_ok=" << rok << ". Falling to string comparison.";
            // 如果数字转换失败，退回到字符串比较
        }
    }
    // 日期比较
    else if (leftTypeId == QMetaType::QDate && rightTypeId == QMetaType::QDate) {
        QDate l_date = left.toDate(); QDate r_date = right.toDate();
        qDebug() << "  Date Comparison Path: left_date=" << l_date.toString("yyyy-MM-dd") << ", right_date=" << r_date.toString("yyyy-MM-dd");
        if (op == "=") return l_date == r_date; if (op == "!=" || op == "<>") return l_date != r_date;
        if (op == ">") return l_date > r_date;  if (op == "<") return l_date < r_date;
        if (op == ">=") return l_date >= r_date; if (op == "<=") return l_date <= r_date;
    }
    // 日期时间比较
    else if (leftTypeId == QMetaType::QDateTime && rightTypeId == QMetaType::QDateTime) {
        QDateTime l_dt = left.toDateTime(); QDateTime r_dt = right.toDateTime();
        qDebug() << "  DateTime Comparison Path: left_dt=" << l_dt.toString(Qt::ISODate) << ", right_dt=" << r_dt.toString(Qt::ISODate);
        if (op == "=") return l_dt == r_dt; if (op == "!=" || op == "<>") return l_dt != r_dt;
        if (op == ">") return l_dt > r_dt;  if (op == "<") return l_dt < r_dt;
        if (op == ">=") return l_dt >= r_dt; if (op == "<=") return l_dt <= r_dt;
    }
    // 布尔比较
    else if (leftTypeId == QMetaType::Bool && rightTypeId == QMetaType::Bool) {
        bool l_bool = left.toBool(); bool r_bool = right.toBool();
        qDebug() << "  Bool Comparison Path: left_bool=" << l_bool << ", right_bool=" << r_bool;
        if (op == "=") return l_bool == r_bool;
        if (op == "!=" || op == "<>") return l_bool != r_bool;
        // 布尔值不支持 >, < 等
        qDebug() << "    Unsupported operator '" << op << "' for bool comparison.";
        return false;
    }

    // 如果以上类型都不匹配，或者数字转换失败，最后尝试字符串比较
    // （注意：如果上面数字转换失败，lok或rok为false，会直接跳到这里）
    // 如果一个明确是数字类型，另一个是字符串，标准SQL通常会尝试将字符串转为数字，
    // 或者如果无法转换则类型不匹配。这里的实现是，如果不能按数字比较，就按字符串比较。
    // 这可能需要根据您期望的隐式转换规则调整。

    QString sLeft = left.toString();
    QString sRight = right.toString();
    qDebug() << "  String Comparison Path (Fallback or Explicit String Types): sLeft='" << sLeft << "', sRight='" << sRight << "'";

    Qt::CaseSensitivity cs = Qt::CaseSensitive; // 标准SQL字符串比较通常区分大小写
    // Qt::CaseSensitivity cs = Qt::CaseInsensitive; // 如果您的字段设计为不区分大小写

    if (op == "=") return sLeft.compare(sRight, cs) == 0;
    if (op == "!=" || op == "<>") return sLeft.compare(sRight, cs) != 0;
    if (op == ">") return sLeft.compare(sRight, cs) > 0;
    if (op == "<") return sLeft.compare(sRight, cs) < 0;
    if (op == ">=") return sLeft.compare(sRight, cs) >= 0;
    if (op == "<=") return sLeft.compare(sRight, cs) <= 0;

    qWarning() << "[compareQVariants] Unhandled comparison or operator '" << op << "' not supported for these types. "
               << "Left Type:" << left.typeName() << ", Right Type:" << right.typeName();
    return false;
}


// matchConditions 函数保持您提供的版本（已移除 isNegated 相关逻辑）
bool xhytable::matchConditions(const xhyrecord& record, const ConditionNode& condition) const {
    bool result;
    switch (condition.type) {
    case ConditionNode::EMPTY:
        return true;
    case ConditionNode::LOGIC_OP:
        if (condition.children.isEmpty()) {
            return condition.logicOp.compare("AND", Qt::CaseInsensitive) == 0;
        }
        if (condition.logicOp.compare("AND", Qt::CaseInsensitive) == 0) {
            result = true;
            for (const auto& child : condition.children) {
                if (!matchConditions(record, child)) { result = false; break; }
            }
        } else if (condition.logicOp.compare("OR", Qt::CaseInsensitive) == 0) {
            result = false;
            for (const auto& child : condition.children) {
                if (matchConditions(record, child)) { result = true; break; }
            }
        } else {
            throw std::runtime_error("未知逻辑运算符: " + condition.logicOp.toStdString());
        }
        break;
    case ConditionNode::NEGATION_OP:
        if (condition.children.isEmpty()) throw std::runtime_error("NOT 操作符后缺少条件");
        result = !matchConditions(record, condition.children.first());
        break;
    case ConditionNode::COMPARISON_OP: {
        const ComparisonDetails& cd = condition.comparison;
        if (!has_field(cd.fieldName)) {
            throw std::runtime_error("在表 '" + m_name.toStdString() + "' 中未找到字段: " + cd.fieldName.toStdString());
        }
        xhyfield::datatype schemaType = getFieldType(cd.fieldName);
        QVariant actualValue = convertToTypedValue(record.value(cd.fieldName), schemaType); // 使用增强的转换函数

        qDebug() << "[matchConditions] Field:" << cd.fieldName << "Op:" << cd.operation << "RecValRaw:" << record.value(cd.fieldName)
                 << "ActualValTyped:" << actualValue << "CompVal:" << cd.value;


        if (cd.operation.compare("IS NULL", Qt::CaseInsensitive) == 0) {
            result = !actualValue.isValid() || actualValue.isNull();
        } else if (cd.operation.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) {
            result = actualValue.isValid() && !actualValue.isNull();
        } else if (cd.operation.compare("IN", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT IN", Qt::CaseInsensitive) == 0) {
            bool found = false;
            if (!cd.valueList.isEmpty()) {
                for (const QVariant& listItem : cd.valueList) {
                    qDebug() << "  IN check: actual=" << actualValue << "vs list_item=" << listItem;
                    if (compareQVariants(actualValue, listItem, "=")) { // IN 使用等号比较
                        found = true; break;
                    }
                }
            }
            result = (cd.operation.compare("IN", Qt::CaseInsensitive) == 0) ? found : !found;
        } else if (cd.operation.compare("BETWEEN", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
            bool isBetween = compareQVariants(actualValue, cd.value, ">=") && compareQVariants(actualValue, cd.value2, "<=");
            result = (cd.operation.compare("BETWEEN", Qt::CaseInsensitive) == 0) ? isBetween : !isBetween;
        } else if (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT LIKE", Qt::CaseInsensitive) == 0) {
            QString actualString = actualValue.toString();
            // 确保 cd.value 是字符串或者可以无歧义地转为字符串模式
            if (cd.value.typeId() != QMetaType::QString && !cd.value.canConvert<QString>()) {
                throw std::runtime_error("LIKE 操作符的模式必须是字符串或可转换为字符串。");
            }
            QString patternStr = cd.value.toString();
            qDebug() << "  LIKE check: actualString='" << actualString << "' patternStr='" << patternStr << "'";
            QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(patternStr), QRegularExpression::CaseInsensitiveOption);
            bool matches = pattern.match(actualString).hasMatch();
            result = (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0) ? matches : !matches;
        } else { // 其他二元比较操作符: =, !=, >, <, >=, <=
            result = compareQVariants(actualValue, cd.value, cd.operation);
        }
        qDebug() << "[matchConditions] ComparisonOp result for field " << cd.fieldName << ": " << result;
        break;
    }
    default:
        throw std::runtime_error("未知的条件节点类型");
    }
    return result;
}
