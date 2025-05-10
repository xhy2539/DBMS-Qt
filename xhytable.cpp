// xhytable.cpp
#include "xhytable.h"
// #include "xhyrecord.h" // xhytable.h 中已包含
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QVariant>
#include <QMetaType> // 确保 QMetaType 被包含 (通常 QVariant 会带入)
#include <QRegularExpression>
#include <limits> // For std::numeric_limits

// --- 构造函数和其他未修改的函数保持原样 ---
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

const xhyfield* xhytable::get_field(const QString& field_name) const {
    for (const auto& field :m_fields) {
        if (field.name().compare(field_name, Qt::CaseInsensitive) == 0) {
            return &field;
        }
    }
    return nullptr;
}

xhyfield::datatype xhytable::getFieldType(const QString& fieldName) const {
    for (const auto& field : m_fields) {
        if (field.name().compare(fieldName, Qt::CaseInsensitive) == 0) {
            return field.type();
        }
    }
    throw std::runtime_error("在表 '" + m_name.toStdString() + "' 中未找到字段定义: " + fieldName.toStdString());
}


void xhytable::rename(const QString& new_name) {
    m_name = new_name;
}

void xhytable::addrecord(const xhyrecord& record) {
    if (m_inTransaction) {
        m_tempRecords.append(record);
    } else {
        m_records.append(record);
    }
}

bool xhytable::createtable(const xhytable& table) {
    m_name = table.name();
    m_fields = table.fields();
    m_records = table.getCommittedRecords();
    m_primaryKeys = table.primaryKeys();
    m_inTransaction = false;
    m_tempRecords.clear();
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

void xhytable::beginTransaction() {
    if (!m_inTransaction) {
        m_tempRecords = m_records;
        m_inTransaction = true;
        qDebug() << "表 '" << m_name << "' 事务开始。";
    } else {
        qWarning() << "表 '" << m_name << "' 已在事务中。";
    }
}

void xhytable::commit() {
    if (m_inTransaction) {
        m_records = m_tempRecords;
        m_tempRecords.clear();
        m_inTransaction = false;
        qDebug() << "表 '" << m_name << "' 事务提交。";
    } else {
        qWarning() << "表 '" << m_name << "' 不在事务中，无法提交。";
    }
}

void xhytable::rollback() {
    if (m_inTransaction) {
        m_tempRecords.clear();
        m_inTransaction = false;
        qDebug() << "表 '" << m_name << "' 事务回滚。";
    } else {
        qWarning() << "表 '" << m_name << "' 不在事务中，无需回滚。";
    }
}

bool xhytable::insertData(const QMap<QString, QString>& fieldValues) {
    try {
        validateRecord(fieldValues, nullptr); // 对于 INSERT，original_record_for_update 为 nullptr

        xhyrecord new_record;
        for(const xhyfield& fieldDef : m_fields) {
            const QString& fieldName = fieldDef.name();
            if (fieldValues.contains(fieldName)) {
                QString value = fieldValues.value(fieldName);
                if (value.compare("NULL", Qt::CaseInsensitive) == 0) {
                    new_record.insert(fieldName, QString());
                } else {
                    new_record.insert(fieldName, value);
                }
            } else {
                if (!fieldDef.constraints().contains("NOT_NULL", Qt::CaseInsensitive)) {
                    new_record.insert(fieldName, QString()); // 可空字段且未提供值，视为 NULL
                } else {
                    throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 缺失值且无默认值。");
                }
            }
        }

        if (m_inTransaction) {
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

void xhytable::validateRecord(const QMap<QString, QString>& values, const xhyrecord* original_record_for_update) const {
    for(const xhyfield& field : m_fields) {
        QString valueToValidate;
        bool valueProvided = values.contains(field.name());

        if (valueProvided) {
            valueToValidate = values.value(field.name());
        }

        bool isExplicitNull = valueProvided && valueToValidate.compare("NULL", Qt::CaseInsensitive) == 0;
        // effectivelyNull: 是显式 "NULL"，或者未提供值且字段可为空
        bool isEffectivelyNull = isExplicitNull || (!valueProvided && !field.constraints().contains("NOT_NULL", Qt::CaseInsensitive));

        if (field.constraints().contains("NOT_NULL", Qt::CaseInsensitive)) {
            if (!valueProvided) {
                throw std::runtime_error("字段 '" + field.name().toStdString() + "' (NOT NULL) 缺失值。");
            }
            if (isExplicitNull) {
                throw std::runtime_error("字段 '" + field.name().toStdString() + "' (NOT NULL) 不能为 NULL。");
            }
            if (valueToValidate.isEmpty() && !isExplicitNull) {
                throw std::runtime_error("字段 '" + field.name().toStdString() + "' (NOT NULL) 不能为空字符串。");
            }
        }

        if (valueProvided && !isExplicitNull && !valueToValidate.isEmpty()) { // 只对实际有值的非"NULL"字符串进行类型验证
            if (!validateType(field.type(), valueToValidate, field.constraints())) {
                throw std::runtime_error("字段 '" + field.name().toStdString() + "' 的值 '" + valueToValidate.toStdString() + "' 类型错误或不符合长度/格式约束。");
            }
        }
    }

    if (!m_primaryKeys.isEmpty()) {
        QMap<QString, QString> new_pk_values;
        for (const QString& pkField : m_primaryKeys) {
            if (!values.contains(pkField) || values.value(pkField).isEmpty() || values.value(pkField).compare("NULL", Qt::CaseInsensitive) == 0 ) {
                throw std::runtime_error("主键字段 '" + pkField.toStdString() + "' 缺失、为空或为NULL。");
            }
            new_pk_values[pkField] = values.value(pkField);
        }

        const QList<xhyrecord>& records_to_check = m_inTransaction ? m_tempRecords : m_records;
        for (const auto& existing_record_ref : records_to_check) {
            if (original_record_for_update && (&existing_record_ref == original_record_for_update)) {
                continue;
            }

            bool conflict = true;
            for (const QString& pkField : m_primaryKeys) {
                if (existing_record_ref.value(pkField).compare(new_pk_values.value(pkField), Qt::CaseSensitive) != 0) {
                    conflict = false;
                    break;
                }
            }
            if (conflict) {
                QString pkValStr;
                for(const QString& pkField : m_primaryKeys) pkValStr += pkField + "=" + new_pk_values.value(pkField) + " ";
                throw std::runtime_error("主键冲突: " + pkValStr.trimmed().toStdString() + " 与现有记录冲突。");
            }
        }
    }
}

int xhytable::updateData(const QMap<QString, QString>& updates_with_expressions, const ConditionNode & conditions) {
    int affectedRows = 0;
    QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;

    for (int i = 0; i < targetRecordsList->size(); ++i) {
        if (matchConditions(targetRecordsList->at(i), conditions)) {
            xhyrecord originalRecordCopy = targetRecordsList->at(i);
            QMap<QString, QString> finalNewValues = originalRecordCopy.allValues();

            qDebug() << "[xhytable::updateData] Matched row with ID (if exists):" << originalRecordCopy.value("id");

            for (auto it_update = updates_with_expressions.constBegin(); it_update != updates_with_expressions.constEnd(); ++it_update) {
                const QString& fieldNameToUpdate = it_update.key();
                const QString& valueExpression = it_update.value();

                qDebug() << "[xhytable::updateData] Processing SET for field:" << fieldNameToUpdate << "Expression/Value:" << valueExpression;

                const xhyfield* fieldSchema = get_field(fieldNameToUpdate);
                if (!fieldSchema) {
                    qWarning() << "更新错误：表 '" << m_name << "' 中字段 " << fieldNameToUpdate << " 不存在。";
                    goto next_row_in_update_data; // 使用不同的标签名
                }

                QString escapedFieldName = QRegularExpression::escape(fieldNameToUpdate);
                QRegularExpression selfArithmeticRe(QString(R"(^\s*%1\s*([+\-*/])\s*(.+?)\s*$)").arg(escapedFieldName), QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch arithmeticMatch = selfArithmeticRe.match(valueExpression);

                if (arithmeticMatch.hasMatch()) {
                    qDebug() << "[xhytable::updateData] Expression '" << valueExpression << "' matched arithmetic pattern for field '" << fieldNameToUpdate << "'.";
                    QString op = arithmeticMatch.captured(1).trimmed();
                    QString operandStr = arithmeticMatch.captured(2).trimmed();
                    QString currentValueStr = originalRecordCopy.value(fieldNameToUpdate);
                    bool conversionOk_current, conversionOk_operand;
                    QString calculatedValueStr;

                    qDebug() << "[xhytable::updateData] Arithmetic: CurrentValue='" << currentValueStr << "', Op='" << op << "', OperandStr='" << operandStr << "'";

                    xhyfield::datatype type = fieldSchema->type();
                    if (type == xhyfield::FLOAT || type == xhyfield::DOUBLE || type == xhyfield::DECIMAL) {
                        double currentValueD = currentValueStr.toDouble(&conversionOk_current);
                        double operandD = operandStr.toDouble(&conversionOk_operand);
                        if (!conversionOk_current || !conversionOk_operand) {
                            qWarning() << "更新错误：字段 '" << fieldNameToUpdate << "' 或其操作数无法转换为浮点数进行算术运算。Current:" << currentValueStr << "Operand:" << operandStr;
                            goto next_update_clause_for_this_row_in_update_data; // 使用不同的标签名
                        }
                        double resultD = 0;
                        if (op == "+") resultD = currentValueD + operandD;
                        else if (op == "-") resultD = currentValueD - operandD;
                        else if (op == "*") resultD = currentValueD * operandD;
                        else if (op == "/") {
                            if (qFuzzyCompare(operandD, 0.0)) {
                                qWarning() << "更新错误：尝试除以零。字段：" << fieldNameToUpdate;
                                goto next_update_clause_for_this_row_in_update_data;
                            }
                            resultD = currentValueD / operandD;
                        } else {
                            qWarning() << "更新错误：不支持的算术运算符 '" << op << "' 对于浮点字段 " << fieldNameToUpdate;
                            goto next_update_clause_for_this_row_in_update_data;
                        }
                        calculatedValueStr = QString::number(resultD);
                        qDebug() << "[xhytable::updateData] Float arithmetic result:" << calculatedValueStr;
                    } else if (type == xhyfield::INT || type == xhyfield::BIGINT || type == xhyfield::SMALLINT || type == xhyfield::TINYINT) {
                        qlonglong currentValueL = currentValueStr.toLongLong(&conversionOk_current);
                        qlonglong operandL = operandStr.toLongLong(&conversionOk_operand);
                        if (!conversionOk_current || !conversionOk_operand) {
                            qWarning() << "更新错误：字段 '" << fieldNameToUpdate << "' 或其操作数无法转换为整数进行算术运算。Current:" << currentValueStr << "Operand:" << operandStr;
                            goto next_update_clause_for_this_row_in_update_data;
                        }
                        qlonglong resultL = 0;
                        if (op == "+") resultL = currentValueL + operandL;
                        else if (op == "-") resultL = currentValueL - operandL;
                        else if (op == "*") resultL = currentValueL * operandL;
                        else if (op == "/") {
                            if (operandL == 0) {
                                qWarning() << "更新错误：尝试除以零。字段：" << fieldNameToUpdate;
                                goto next_update_clause_for_this_row_in_update_data;
                            }
                            resultL = currentValueL / operandL;
                        } else {
                            qWarning() << "更新错误：不支持的算술运算符 '" << op << "' 对于整型字段 " << fieldNameToUpdate;
                            goto next_update_clause_for_this_row_in_update_data;
                        }
                        calculatedValueStr = QString::number(resultL);
                        qDebug() << "[xhytable::updateData] Integer arithmetic result:" << calculatedValueStr;
                    } else {
                        qWarning() << "更新错误：字段 '" << fieldNameToUpdate << "' 类型 (" << fieldSchema->typestring() << ") 不支持算术运算。";
                        goto next_update_clause_for_this_row_in_update_data;
                    }
                    finalNewValues[fieldNameToUpdate] = calculatedValueStr;
                } else {
                    qDebug() << "[xhytable::updateData] Expression '" << valueExpression << "' DID NOT match arithmetic pattern for field '" << fieldNameToUpdate << "'. Treating as literal.";
                    if (valueExpression.compare("NULL", Qt::CaseInsensitive) == 0) {
                        finalNewValues[fieldNameToUpdate] = QString();
                    } else if ((valueExpression.startsWith('\'') && valueExpression.endsWith('\'')) ||
                               (valueExpression.startsWith('"') && valueExpression.endsWith('"'))) {
                        if (valueExpression.length() >= 2) {
                            QString innerStr = valueExpression.mid(1, valueExpression.length() - 2);
                            if (valueExpression.startsWith('\'')) innerStr.replace("''", "'"); else innerStr.replace("\"\"", "\"");
                            finalNewValues[fieldNameToUpdate] = innerStr;
                        } else {
                            finalNewValues[fieldNameToUpdate] = QString("");
                        }
                    } else {
                        finalNewValues[fieldNameToUpdate] = valueExpression;
                    }
                }
            next_update_clause_for_this_row_in_update_data:;
            }

            qDebug() << "[xhytable::updateData] Final values to validate for row ID" << originalRecordCopy.value("id") << ":" << finalNewValues;

            try {
                validateRecord(finalNewValues, &targetRecordsList->at(i));
                (*targetRecordsList)[i].clear();
                for(auto it_final = finalNewValues.constBegin(); it_final != finalNewValues.constEnd(); ++it_final){
                    (*targetRecordsList)[i].insert(it_final.key(), it_final.value());
                }
                affectedRows++;
                qDebug() << "[xhytable::updateData] Row ID" << originalRecordCopy.value("id") << "updated successfully.";
            } catch (const std::runtime_error& e) {
                qWarning() << "更新表 '" << m_name << "' 的行 (ID: " << originalRecordCopy.value("id") << ") 验证失败: " << e.what() << " 更新被跳过。";
            }
        }
    next_row_in_update_data:; // 使用不同的标签名
    }
    return affectedRows;
}

bool xhytable::validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const {
    if (value.isEmpty()) {
        switch(type) {
        case xhyfield::CHAR:
        case xhyfield::VARCHAR:
        case xhyfield::TEXT:
        case xhyfield::ENUM:
            return true;
        default:
            qDebug() << "[validateType] Empty value for non-string-like type:" << static_cast<int>(type);
            return false;
        }
    }

    bool ok;
    switch(type) {
    case xhyfield::INT: case xhyfield::TINYINT: case xhyfield::SMALLINT: case xhyfield::BIGINT:
        value.toLongLong(&ok);
        if (!ok) qDebug() << "[validateType] INT conversion failed for value:" << value;
        return ok;
    case xhyfield::FLOAT: case xhyfield::DOUBLE: case xhyfield::DECIMAL:
        value.toDouble(&ok);
        if (!ok) qDebug() << "[validateType] FLOAT/DOUBLE conversion failed for value:" << value;
        return ok;
    case xhyfield::BOOL:
        ok = (value.compare("true", Qt::CaseInsensitive) == 0 || value.compare("false", Qt::CaseInsensitive) == 0 || value == "1" || value == "0");
        if (!ok) qDebug() << "[validateType] BOOL conversion failed for value:" << value;
        return ok;
    case xhyfield::DATE:
        ok = QDate::fromString(value, "yyyy-MM-dd").isValid();
        if (!ok) qDebug() << "[validateType] DATE conversion failed for value:" << value << "(Expected yyyy-MM-dd)";
        return ok;
    case xhyfield::DATETIME: case xhyfield::TIMESTAMP:
        ok = QDateTime::fromString(value, "yyyy-MM-dd HH:mm:ss").isValid() || QDateTime::fromString(value, Qt::ISODateWithMs).isValid() || QDateTime::fromString(value, Qt::ISODate).isValid();
        if (!ok) qDebug() << "[validateType] DATETIME conversion failed for value:" << value << "(Expected yyyy-MM-dd HH:mm:ss or ISO)";
        return ok;
    case xhyfield::CHAR:
    case xhyfield::VARCHAR: {
        int definedSize = -1;
        bool sizeFound = false;
        for(const QString& c : constraints) {
            if (c.startsWith("SIZE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                bool sizeParseOk;
                definedSize = c.mid(5, c.length() - 6).toInt(&sizeParseOk);
                if (sizeParseOk) sizeFound = true;
                break;
            }
        }
        if (!sizeFound) {
            if (type == xhyfield::CHAR) definedSize = 1;
            else definedSize = 255;
        }
        if (definedSize > 0 && value.length() > definedSize) {
            qDebug() << "[validateType] String value '" << value << "' length " << value.length()
            << "exceeds defined size" << definedSize << "for type" << (type == xhyfield::CHAR ? "CHAR" : "VARCHAR");
            return false;
        }
        return true;
    }
    case xhyfield::TEXT: return true;
    case xhyfield::ENUM: return true;
    default:
        qWarning() << "[validateType] Unknown data type for validation: " << static_cast<int>(type) << "for value:" << value;
        return false;
    }
}

QVariant xhytable::convertToTypedValue(const QString& strValue, xhyfield::datatype type) const {
    if (strValue.isEmpty()) { // 代表 SQL NULL
        return QVariant();
    }

    bool ok;
    switch (type) {
    case xhyfield::INT: case xhyfield::TINYINT: case xhyfield::SMALLINT: case xhyfield::BIGINT: {
        qlonglong val = strValue.toLongLong(&ok);
        if (ok) {
            if (val >= std::numeric_limits<int>::min() && val <= std::numeric_limits<int>::max()) return QVariant(static_cast<int>(val));
            return QVariant(val);
        }
        qDebug() << "[convertToTypedValue] INT conversion FAILED for '" << strValue << "'. Returning original string in QVariant.";
        return QVariant(strValue);
    }
    case xhyfield::FLOAT: case xhyfield::DOUBLE: case xhyfield::DECIMAL: {
        double val = strValue.toDouble(&ok);
        if (ok) return QVariant(val);
        qDebug() << "[convertToTypedValue] FLOAT/DOUBLE conversion FAILED for '" << strValue << "'. Returning original string in QVariant.";
        return QVariant(strValue);
    }
    case xhyfield::BOOL:
        if (strValue.compare("true", Qt::CaseInsensitive) == 0 || strValue == "1") return QVariant(true);
        if (strValue.compare("false", Qt::CaseInsensitive) == 0 || strValue == "0") return QVariant(false);
        qDebug() << "[convertToTypedValue] BOOL conversion FAILED for '" << strValue << "'. Returning original string in QVariant.";
        return QVariant(strValue);
    case xhyfield::DATE: {
        QDate date = QDate::fromString(strValue, "yyyy-MM-dd");
        if(date.isValid()) return QVariant(date);
        qDebug() << "[convertToTypedValue] DATE conversion FAILED for '" << strValue << "' (yyyy-MM-dd). Returning original string in QVariant.";
        return QVariant(strValue);
    }
    case xhyfield::DATETIME: case xhyfield::TIMESTAMP: {
        QDateTime dt = QDateTime::fromString(strValue, "yyyy-MM-dd HH:mm:ss");
        if (!dt.isValid()) dt = QDateTime::fromString(strValue, Qt::ISODateWithMs);
        if (!dt.isValid()) dt = QDateTime::fromString(strValue, Qt::ISODate);
        if(dt.isValid()) return QVariant(dt);
        qDebug() << "[convertToTypedValue] DATETIME conversion FAILED for '" << strValue << "' (yyyy-MM-dd HH:mm:ss or ISO). Returning original string in QVariant.";
        return QVariant(strValue);
    }
    default: // VARCHAR, CHAR, TEXT, ENUM
        return QVariant(strValue);
    }
}

bool xhytable::compareQVariants(const QVariant& left, const QVariant& right, const QString& op) const {
    if (op.compare("IS NULL", Qt::CaseInsensitive) == 0) return !left.isValid() || left.isNull();
    if (op.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) return left.isValid() && !left.isNull();

    if (!left.isValid() || !right.isValid() || left.isNull() || right.isNull()) { // 如果任何一方是SQL NULL
        qDebug() << "[compareQVariants] One or both operands are SQL NULL. Left:" << left << "Right:" << right << "Op:" << op << "-> returning false";
        return false;
    }

    int leftMetaTypeId = left.metaType().id();  // 使用 int 或 QMetaType::Type (enum) if you compare against QMetaType enums
    int rightMetaTypeId = right.metaType().id(); // 使用 int 或 QMetaType::Type


    qDebug() << "[compareQVariants] Left: " << left.toString() << " (TypeId:" << leftMetaTypeId << ", Name:" << left.typeName() << ")"
             << " Op: '" << op << "' "
             << "Right: " << right.toString() << " (TypeId:" << rightMetaTypeId << ", Name:" << right.typeName() << ")";

    // 数字比较 (尝试整数，然后浮点数)
    if ( (leftMetaTypeId == QMetaType::Int || leftMetaTypeId == QMetaType::LongLong || leftMetaTypeId == QMetaType::ULongLong || leftMetaTypeId == QMetaType::UInt) &&
        (rightMetaTypeId == QMetaType::Int || rightMetaTypeId == QMetaType::LongLong || rightMetaTypeId == QMetaType::ULongLong || rightMetaTypeId == QMetaType::UInt) ) {
        qlonglong l_ll = left.toLongLong(); qlonglong r_ll = right.toLongLong();
        qDebug() << "  Integer Comparison Path: l_ll=" << l_ll << ", r_ll=" << r_ll;
        if (op == "=") return l_ll == r_ll; if (op == "!=" || op == "<>") return l_ll != r_ll;
        if (op == ">") return l_ll > r_ll;  if (op == "<") return l_ll < r_ll;
        if (op == ">=") return l_ll >= r_ll; if (op == "<=") return l_ll <= r_ll;
    } else if (left.canConvert<double>() && right.canConvert<double>()) {
        bool lok_d, rok_d;
        double l_double = left.toDouble(&lok_d); double r_double = right.toDouble(&rok_d);
        if (lok_d && rok_d) {
            qDebug() << "  Floating Point Comparison Path: l_double=" << l_double << ", r_double=" << r_double;
            const double epsilon = 1e-9;
            if (op == "=") return qAbs(l_double - r_double) < epsilon;
            if (op == "!=" || op == "<>") return qAbs(l_double - r_double) >= epsilon;
            if (op == ">") return l_double > r_double && qAbs(l_double - r_double) >= epsilon;
            if (op == "<") return l_double < r_double && qAbs(l_double - r_double) >= epsilon;
            if (op == ">=") return l_double > r_double || qAbs(l_double - r_double) < epsilon;
            if (op == "<=") return l_double < r_double || qAbs(l_double - r_double) < epsilon;
        }
    }

    // 日期时间比较 (应在数字比较之后，字符串比较之前)
    if (leftMetaTypeId == QMetaType::QDate && rightMetaTypeId == QMetaType::QDate) {
        QDate l_date = left.toDate(); QDate r_date = right.toDate();
        qDebug() << "  Date Comparison Path: left_date=" << l_date.toString("yyyy-MM-dd") << ", right_date=" << r_date.toString("yyyy-MM-dd");
        if (op == "=") return l_date == r_date; if (op == "!=" || op == "<>") return l_date != r_date;
        if (op == ">") return l_date > r_date;  if (op == "<") return l_date < r_date;
        if (op == ">=") return l_date >= r_date; if (op == "<=") return l_date <= r_date;
    } else if (leftMetaTypeId == QMetaType::QDateTime && rightMetaTypeId == QMetaType::QDateTime) {
        QDateTime l_dt = left.toDateTime(); QDateTime r_dt = right.toDateTime();
        qDebug() << "  DateTime Comparison Path: left_dt=" << l_dt.toString(Qt::ISODate) << ", right_dt=" << r_dt.toString(Qt::ISODate);
        if (op == "=") return l_dt == r_dt; if (op == "!=" || op == "<>") return l_dt != r_dt;
        if (op == ">") return l_dt > r_dt;  if (op == "<") return l_dt < r_dt;
        if (op == ">=") return l_dt >= r_dt; if (op == "<=") return l_dt <= r_dt;
    } else if (leftMetaTypeId == QMetaType::Bool && rightMetaTypeId == QMetaType::Bool) {
        bool l_bool = left.toBool(); bool r_bool = right.toBool();
        qDebug() << "  Bool Comparison Path: left_bool=" << l_bool << ", right_bool=" << r_bool;
        if (op == "=") return l_bool == r_bool; if (op == "!=" || op == "<>") return l_bool != r_bool;
        qDebug() << "    Unsupported operator '" << op << "' for bool comparison."; return false;
    }

    // 如果不能按特定类型比较，则进行字符串比较
    QString sLeft = left.toString();
    QString sRight = right.toString();
    qDebug() << "  String Comparison Path (Fallback): sLeft='" << sLeft << "', sRight='" << sRight << "'";
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
    if (op == "=") return sLeft.compare(sRight, cs) == 0;
    if (op == "!=" || op == "<>") return sLeft.compare(sRight, cs) != 0;
    if (op == ">") return sLeft.compare(sRight, cs) > 0;
    if (op == "<") return sLeft.compare(sRight, cs) < 0;
    if (op == ">=") return sLeft.compare(sRight, cs) >= 0;
    if (op == "<=") return sLeft.compare(sRight, cs) <= 0;

    qWarning() << "[compareQVariants] Fallback: Unhandled operator '" << op << "' for types " << left.typeName() << " and " << right.typeName();
    return false;
}

bool xhytable::matchConditions(const xhyrecord& record, const ConditionNode& condition) const {
    bool result;
    switch (condition.type) {
    case ConditionNode::EMPTY: return true;
    case ConditionNode::LOGIC_OP:
        if (condition.children.isEmpty()) {
            qWarning() << "[matchConditions] Logic_OP node has no children. Op:" << condition.logicOp;
            return condition.logicOp.compare("AND", Qt::CaseInsensitive) == 0;
        }
        if (condition.logicOp.compare("AND", Qt::CaseInsensitive) == 0) {
            result = true;
            for (const auto& child : condition.children) if (!matchConditions(record, child)) { result = false; break; }
        } else if (condition.logicOp.compare("OR", Qt::CaseInsensitive) == 0) {
            result = false;
            for (const auto& child : condition.children) if (matchConditions(record, child)) { result = true; break; }
        } else { throw std::runtime_error("未知逻辑运算符: " + condition.logicOp.toStdString()); }
        break;
    case ConditionNode::NEGATION_OP:
        if (condition.children.isEmpty() || condition.children.size() != 1) throw std::runtime_error("NOT 操作符后缺少或有多于一个条件表达式。");
        result = !matchConditions(record, condition.children.first());
        break;
    case ConditionNode::COMPARISON_OP: {
        const ComparisonDetails& cd = condition.comparison;
        if (!has_field(cd.fieldName)) throw std::runtime_error("在表 '" + m_name.toStdString() + "' 中未找到字段: " + cd.fieldName.toStdString() + " 用于条件比较。");

        xhyfield::datatype schemaType = getFieldType(cd.fieldName);
        QString rawRecordValueStr = record.value(cd.fieldName);
        QVariant actualValue = convertToTypedValue(rawRecordValueStr, schemaType); // actualValue 是记录中字段的QVariant形式
            // cd.value 是条件中字面量的QVariant形式 (由MainWindow::parseLiteralValue创建)
        qDebug() << "[matchConditions] Field:" << cd.fieldName << "Op:" << cd.operation
                 << "RecValRaw:'" << rawRecordValueStr << "' ActualValTyped:" << actualValue << "(Type:" << actualValue.typeName() << ")"
                 << "vs CompValLiteral:" << cd.value << "(Type:" << cd.value.typeName() << ")";

        if (cd.operation.compare("IS NULL", Qt::CaseInsensitive) == 0) result = !actualValue.isValid() || actualValue.isNull();
        else if (cd.operation.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) result = actualValue.isValid() && !actualValue.isNull();
        else if (cd.operation.compare("IN", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT IN", Qt::CaseInsensitive) == 0) {
            if (!actualValue.isValid() || actualValue.isNull()) result = false;
            else {
                bool found = false;
                for (const QVariant& listItemLiteral : cd.valueList) {
                    if (compareQVariants(actualValue, listItemLiteral, "=")) { found = true; break; }
                }
                result = (cd.operation.compare("IN", Qt::CaseInsensitive) == 0) ? found : !found;
            }
        } else if (cd.operation.compare("BETWEEN", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
            if (!actualValue.isValid() || actualValue.isNull()) result = false;
            else {
                bool isBetween = compareQVariants(actualValue, cd.value, ">=") && compareQVariants(actualValue, cd.value2, "<=");
                result = (cd.operation.compare("BETWEEN", Qt::CaseInsensitive) == 0) ? isBetween : !isBetween;
            }
        } else if (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT LIKE", Qt::CaseInsensitive) == 0) {
            if (!actualValue.isValid() || actualValue.isNull()) { result = false; } // NULL LIKE ... is UNKNOWN (false)
            else {
                QString actualString = actualValue.toString();
                // 修正 QVariant::typeId() 的使用，确保 cd.value 是字符串
                if (cd.value.metaType().id() != QMetaType::QString && !cd.value.canConvert<QString>()) { // 使用 metaType().id()
                    throw std::runtime_error("LIKE 操作符的模式必须是字符串或可转换为字符串。");
                }
                QString patternStr = cd.value.toString();
                qDebug() << "  LIKE check: actualString='" << actualString << "' patternStr='" << patternStr << "'";
                // 修正: 移除非标准参数 NutzerdefiniertesWildcarding
                QRegularExpression patternRegex(QRegularExpression::wildcardToRegularExpression(patternStr),
                                                QRegularExpression::CaseInsensitiveOption);
                bool matches = patternRegex.match(actualString).hasMatch();
                result = (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0) ? matches : !matches;
            }
        } else {
            result = compareQVariants(actualValue, cd.value, cd.operation);
        }
        qDebug() << "[matchConditions] Comparison result:" << result;
        break;
    }
    default: throw std::runtime_error("未知的条件节点类型");
    }
    return result;
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

// --- 实现之前缺失或占位的方法 ---
void xhytable::remove_field(const QString& field_name) {
    bool removed = false;
    for (int i = 0; i < m_fields.size(); ++i) {
        if (m_fields[i].name().compare(field_name, Qt::CaseInsensitive) == 0) {
            m_fields.removeAt(i);
            removed = true;
            break;
        }
    }
    if (!removed) {
        qWarning() << "尝试移除不存在的字段:" << field_name << "从表" << m_name;
        return;
    }

    // 从主键列表中移除 (区分大小写移除)
    for (int i = m_primaryKeys.size() - 1; i >= 0; --i) {
        if (m_primaryKeys[i].compare(field_name, Qt::CaseInsensitive) == 0) {
            m_primaryKeys.removeAt(i);
        }
    }

    // 从所有记录中移除该字段的值
    auto remove_from_records_list = [&](QList<xhyrecord>& records_list) {
        for (xhyrecord& rec : records_list) {
            rec.removeValue(field_name); // 假设 xhyrecord 有 removeValue
        }
    };
    remove_from_records_list(m_records);
    remove_from_records_list(m_tempRecords);

    qDebug() << "字段" << field_name << "已从表" << m_name << "移除。";
}

void xhytable::add_unique_constraint(const QStringList& fields, const QString& constraintName) {
    Q_UNUSED(fields); Q_UNUSED(constraintName);
    qWarning() << "xhytable::add_unique_constraint - 未实现";
    // TODO: 实现逻辑，例如将信息存到 m_uniqueConstraints
}

void xhytable::add_check_constraint(const QString& condition, const QString& constraintName) {
    Q_UNUSED(condition); Q_UNUSED(constraintName);
    qWarning() << "xhytable::add_check_constraint - 未实现";
    // TODO: 实现逻辑，例如将信息存到 m_checkConstraints
}

void xhytable::add_foreign_key(const QString& field, const QString& referencedTable, const QString& referencedField, const QString& constraintName) {
    Q_UNUSED(field); Q_UNUSED(referencedTable); Q_UNUSED(referencedField); Q_UNUSED(constraintName);
    qWarning() << "xhytable::add_foreign_key - 未实现";
    // TODO: 实现逻辑，例如将信息存到 m_foreignKeys
}

void xhytable::rebuildIndexes() { /* 占位符 */ }
bool xhytable::checkConstraint(const xhyfield& field, const QString& value) const { Q_UNUSED(field); Q_UNUSED(value); return true; /* 占位符 */ }
