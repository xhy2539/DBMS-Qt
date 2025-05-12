#include "xhytable.h"
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QVariant>
#include <QMetaType>
#include <limits> // <--- 确保此行存在
#include <QRegularExpression>
#include "xhydatabase.h"
#include <stdexcept> // 用于 std::runtime_error
#include <QJSEngine>
#include <QRegularExpression>


namespace { // 使用匿名命名空间或设为类的静态私有方法
bool parseDecimalParams(const QStringList& constraints, int& precision, int& scale) {
    bool p_found = false;
    bool s_found = false;
    // SQL 标准：DECIMAL(P) 等同于 DECIMAL(P,0)。如果只有P，S默认为0。
    // 如果P,S都未定义，则行为依赖于具体DBMS实现（可能用默认值，或报错）。
    // 这里我们要求至少P要被定义。
    precision = 0;
    scale = 0;

    for (const QString& c : constraints) {
        if (c.startsWith("PRECISION(", Qt::CaseInsensitive) && c.endsWith(")")) {
            bool conv_ok = false;
            precision = c.mid(10, c.length() - 11).toInt(&conv_ok);
            if (conv_ok) p_found = true;
        } else if (c.startsWith("SCALE(", Qt::CaseInsensitive) && c.endsWith(")")) {
            bool conv_ok = false;
            scale = c.mid(6, c.length() - 7).toInt(&conv_ok);
            if (conv_ok) s_found = true;
        }
    }
    // 如果只找到了 PRECISION 而没有 SCALE，SCALE 默认为 0 是符合SQL标准的。
    return p_found;
}
}
xhytable::xhytable(const QString& name, xhydatabase* parentDb)
    : m_name(name), m_inTransaction(false), m_parentDb(parentDb) {
    // 初始化，如果需要
}



const QList<xhyrecord>& xhytable::records() const {
    return m_inTransaction ? m_tempRecords : m_records;
}

void xhytable::addfield(const xhyfield& field) {
    if (has_field(field.name())) {
        qWarning() << "字段已存在：" << field.name();
        return; // 或者抛出异常
    }
    xhyfield newField = field;

    // Debugging: Print field constraints when adding a field
    qDebug() << "[addfield] Adding field:" << field.name() << "Type:" << field.typestring() << "Constraints:" << field.constraints();


    // 解析主键约束
    bool isPrimaryKey = false;
    for (const QString& constraint : field.constraints()) {
        if (constraint.compare("PRIMARY_KEY", Qt::CaseInsensitive) == 0 ||
            constraint.compare("PRIMARY", Qt::CaseInsensitive) == 0) {
            isPrimaryKey = true;
            break;
        }
    }

    if (isPrimaryKey) {
        if (!m_primaryKeys.contains(field.name(), Qt::CaseInsensitive)) {
            m_primaryKeys.append(field.name());
        }
        // 主键列隐含非空，除非 PRIMARY KEY 约束本身允许在某些DBMS中（但通常不）
        // 为安全起见，在此模型中，主键强制非空。
        qDebug() << "[addfield] Marking PK field as NOT NULL (implied by PRIMARY KEY):" << field.name();
        m_notNullFields.insert(field.name());
    }

    // 解析非空约束
    // 检查字段约束列表中是否包含 "NOT_NULL" (不区分大小写)
    // 已移除 `|| field.constraints().contains("null", Qt::CaseInsensitive)`，因为 "null" 可能是一个笔误检查，不符合标准。
    if (field.constraints().contains("NOT_NULL", Qt::CaseInsensitive)) {
        qDebug() << "[addfield] Found explicit NOT_NULL constraint for field:" << field.name();
        m_notNullFields.insert(field.name());
    }
    // Debugging: Print the content of m_notNullFields after processing constraints
    qDebug() << "[addfield] Current m_notNullFields for table" << m_name << ":" << m_notNullFields;


    // 解析字段级 UNIQUE 约束 (转换为表级单字段唯一约束)
    if (field.constraints().contains("UNIQUE", Qt::CaseInsensitive)) {
        QString constraintName = "UQ_" + m_name.toUpper() + "_" + field.name().toUpper();
        if (!m_uniqueConstraints.contains(constraintName)) {
            m_uniqueConstraints[constraintName] = {field.name()};
        } else {
            qWarning() << "尝试为字段 " << field.name() << " 添加已存在的唯一约束名 " << constraintName;
        }
    }

    // 解析默认值约束
    // 期望格式: constraints 列表中 "DEFAULT" 紧跟着 "<value>"
    int defaultIdx = -1;
    for(int i=0; i < field.constraints().size(); ++i) {
        if (field.constraints().at(i).compare("DEFAULT", Qt::CaseInsensitive) == 0) {
            defaultIdx = i;
            break;
        }
    }
    if (defaultIdx != -1 && defaultIdx + 1 < field.constraints().size()) {
        QString defaultValue = field.constraints().at(defaultIdx + 1);
        // 进一步处理：如果defaultValue是 'some string'，引号应在SQL解析阶段去除。
        // 这里假设传入的是已经准备好的值。
        m_defaultValues[field.name()] = defaultValue;
    }
    m_fields.append(newField);
}

// 新增：检查删除父记录时的外键限制 (RESTRICT)
bool xhytable::checkForeignKeyDeleteRestrictions(const xhyrecord& recordToDelete) const {
    if (!m_parentDb) {
        qWarning() << "警告：表 " << m_name << " 缺少对父数据库的引用，无法执行外键删除检查。";
        return true;
    }

    qDebug() << "[FK Check ON DELETE] Table" << m_name << ": Checking restrictions for deleting record:" << recordToDelete.allValues();

    for (const xhytable& otherTable : m_parentDb->tables()) {
        if (otherTable.name().compare(this->m_name, Qt::CaseInsensitive) == 0) {
            continue; // 跳过自身
        }

        // 注意：这里 otherTable.foreignKeys() 现在返回的是 QList<ForeignKeyDefinition>
        for (const auto& fkDef : otherTable.foreignKeys()) {
            if (fkDef.referenceTable.compare(this->m_name, Qt::CaseInsensitive) == 0) {
                // 这是一个引用当前表的外键，需要检查是否有子记录引用了即将删除的父记录
                QMap<QString, QString> parentReferencedValues; // 即将删除的父记录中被引用的值

                // 收集即将删除的父记录中所有被引用的列的值
                bool allParentRefValuesPresent = true;
                for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
                    const QString& childCol = it.key();
                    const QString& parentRefCol = it.value();
                    QString parentVal = recordToDelete.value(parentRefCol);
                    if (parentVal.isNull()) {
                        allParentRefValuesPresent = false; // 父记录中引用的列有NULL值，这种情况下不会有引用冲突
                        break;
                    }
                    parentReferencedValues[parentRefCol] = parentVal;
                }

                if (!allParentRefValuesPresent || parentReferencedValues.isEmpty()) {
                    continue; // 没有有效的父记录被引用值进行匹配，或者父记录中的FK列为NULL
                }

                const QList<xhyrecord>& childRecords = otherTable.records();
                for (const xhyrecord& childRecord : childRecords) {
                    bool allColumnsMatch = true;
                    bool childFkHasNull = false; // 检查子记录中的外键列是否为NULL

                    // 检查子记录中的所有外键列是否与父记录中对应的值匹配
                    for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
                        const QString& childCol = it.key();
                        const QString& parentRefCol = it.value(); // 父表中对应的列名

                        QString childVal = childRecord.value(childCol);

                        // 如果子记录中的外键列为NULL，则不视为引用冲突（除非该FK列同时有NOT NULL约束，但这通常在插入/更新时检查）
                        if (childVal.isNull()) {
                            childFkHasNull = true;
                            break; // 复合外键中只要有一列是NULL，就认为不匹配
                        }

                        // 比较子记录中的外键值与父记录中被引用的值
                        if (childVal.compare(parentReferencedValues.value(parentRefCol), Qt::CaseSensitive) != 0) {
                            allColumnsMatch = false;
                            break; // 只要有一列不匹配，就不是完整的FK匹配
                        }
                    }

                    if (allColumnsMatch && !childFkHasNull) {
                        QString err = QString("删除操作被限制：表 '%1' 中的记录通过外键 '%2' (列 '%3') "
                                              "引用了表 '%4' (当前表) 中即将删除的记录 (被引用列 '%5' 的值为 '%6')。")
                                          .arg(otherTable.name())
                                          .arg(fkDef.constraintName)
                                          .arg(fkDef.columnMappings.keys().join(", ")) // 显示所有列
                                          .arg(this->m_name)
                                          .arg(fkDef.columnMappings.values().join(", ")) // 显示所有列
                                          .arg(parentReferencedValues.values().join(", ")); // 显示所有值
                        qWarning() << err;
                        throw std::runtime_error(err.toStdString());
                    }
                }
            }
        }
    }
    qDebug() << "[FK Check ON DELETE] Table" << m_name << ": No restrictions found for record:" << recordToDelete.allValues();
    return true;
}


QVariant xhytable::convertStringToType(const QString& str, xhyfield::datatype type) const {
    switch (type) {
    // 整数类型
    case xhyfield::TINYINT:
    case xhyfield::SMALLINT:
    case xhyfield::INT:
        return str.toInt();
    case xhyfield::BIGINT:
        return str.toLongLong();

    // 浮点类型
    case xhyfield::FLOAT:
        return str.toFloat();
    case xhyfield::DOUBLE:
    case xhyfield::DECIMAL:
        return str.toDouble();

    // 字符串类型（直接返回）
    case xhyfield::CHAR:
    case xhyfield::VARCHAR:
    case xhyfield::TEXT:
    case xhyfield::ENUM:
        return str;

    // 布尔类型
    case xhyfield::BOOL:
        return (str.toLower() == "true" || str == "1");

    // 日期时间类型
    case xhyfield::DATE:
        return QDate::fromString(str, Qt::ISODate);
    case xhyfield::DATETIME:
    case xhyfield::TIMESTAMP:
        return QDateTime::fromString(str, Qt::ISODate);

    // 默认返回字符串
    default:
        return str;
    }
}

//解析check语句
bool xhytable::evaluateCheckExpression(const QString& expr, const QVariantMap& fieldValues) const {
    QJSEngine engine;

    // 1. 安全防护：禁止危险JS代码
    if (expr.contains("function") || expr.contains("eval") || expr.contains("script")) {
        qWarning() << "拒绝执行可能危险的CHECK表达式:" << expr;
        return false;
    }

    // 2. 注入字段值到JS引擎（增强类型处理）
    for (auto it = fieldValues.begin(); it != fieldValues.end(); ++it) {
        const QString& fieldName = it.key();
        const QVariant& value = it.value();

        // 处理NULL值
        if (value.isNull()) {
            engine.globalObject().setProperty(fieldName, QJSValue(QJSValue::NullValue));
            continue;
        }

        switch (value.type()) {
        case QVariant::Int:
        case QVariant::Double:
            engine.globalObject().setProperty(fieldName, value.toDouble());
            break;
        case QVariant::Bool:
            engine.globalObject().setProperty(fieldName, value.toBool());
            break;
        default:
            engine.globalObject().setProperty(fieldName, value.toString());
        }
    }

    // 3. 完整SQL→JS语法转换
    QString jsExpr = expr;

    // 处理关键字大小写
    jsExpr.replace(QRegularExpression("\\bAND\\b", QRegularExpression::CaseInsensitiveOption), "&&")
        .replace(QRegularExpression("\\bOR\\b", QRegularExpression::CaseInsensitiveOption), "||")
        .replace(QRegularExpression("\\bNOT\\b", QRegularExpression::CaseInsensitiveOption), "!");

    // 处理特殊比较运算符
    jsExpr.replace("!=", "!==")
        .replace("==", "===")
        .replace("<>", "!==");

    // 处理LIKE（增强通配符支持）
    QRegularExpression likeRe(R"(([\w.]+)\s+LIKE\s+'((?:[^']|'')*)')", QRegularExpression::CaseInsensitiveOption);
    int pos = 0;
    while ((pos = jsExpr.indexOf(likeRe, pos)) != -1) {
        QRegularExpressionMatch match = likeRe.match(jsExpr, pos);
        QString field = match.captured(1);
        QString pattern = match.captured(2)
                              .replace("''", "'")  // 处理SQL转义的单引号
                              .replace("%", ".*")
                              .replace("_", ".")
                              .replace("\\", "\\\\");  // 处理转义字符

        QString replacement = QString("/^%1$/i.test(%2)").arg(pattern, field);
        jsExpr.replace(match.capturedStart(), match.capturedLength(), replacement);
        pos += replacement.length();
    }

    // 处理IN列表（增强字符串支持）
    QRegularExpression inRe(R"(([\w.]+)\s+IN\s*\(((?:'[^']*'(?:\s*,\s*)?)+)\))", QRegularExpression::CaseInsensitiveOption);
    pos = 0;
    while ((pos = jsExpr.indexOf(inRe, pos)) != -1) {
        QRegularExpressionMatch match = inRe.match(jsExpr, pos);
        QString field = match.captured(1);
        QStringList values;

        // 解析IN列表中的每个值
        QRegularExpression valueRe(R"('((?:[^']|'')*)')");
        int valuePos = 0;
        while ((valuePos = match.captured(2).indexOf(valueRe, valuePos)) != -1) {
            QRegularExpressionMatch valueMatch = valueRe.match(match.captured(2), valuePos);
            values << "'" + valueMatch.captured(1).replace("''", "'") + "'";
            valuePos += valueMatch.capturedLength();
        }

        QString replacement = QString("[%1].includes(%2)").arg(values.join(","), field);
        jsExpr.replace(match.capturedStart(), match.capturedLength(), replacement);
        pos += replacement.length();
    }

    // 4. 执行表达式（添加错误防护）
    QJSValue result = engine.evaluate("(function() { try { return !!(" + jsExpr + "); } catch(e) { return false; } })()");

    if (result.isError()) {
        qWarning() << "CHECK约束执行错误:" << result.toString()
            << "\n原始表达式:" << expr
            << "\n转换后JS:" << jsExpr;
        return false;
    }

    return result.toBool();
}



bool xhytable::checkInsertConstraints(const QMap<QString, QString>& fieldValues) const {
    // 验证 CHECK 约束
    QVariantMap fullRecordData;
    for (const QString& key : fieldValues.keys()) {
        fullRecordData[key] = convertStringToType(fieldValues[key],getFieldType(key));
    }
    for (const QString& checkExpr : m_checkConstraints.values()) {
        if (!evaluateCheckExpression(checkExpr, fullRecordData)) {
            qWarning() << "插入失败: 违反表级CHECK约束:" << checkExpr;
            return false;
        }
    }

    return true;
    //暂时不用-----------
    // 验证主键唯一性
    /*if (!m_primaryKeys.isEmpty()) {
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
    }*/
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
                    throw std::runtime_error("更新失败: 唯一性约束违反");
                    return false; // 唯一性冲突
                }
            }

            // 验证 NOT NULL 约束
            for (const QString& field : m_notNullFields) {
                if(updates.contains(field)) {
                    if(updates[field].isEmpty()) {
                        qWarning() << "更新失败: 字段" << field << "不能为空";
                        throw std::runtime_error("更新失败:"+field.toStdString()+"不能为空");
                        return false; // 非空限制违规
                    }
                } else {
                    if(record.value(field).isEmpty()) {
                        qWarning() << "更新失败: 字段" << field << "不能为空";
                        throw std::runtime_error("更新失败:"+field.toStdString()+"不能为空");
                        return false;
                    }
                }
            }
            // 验证 CHECK 约束
            for (const auto& checkConstraint : m_checkConstraints.keys()) {
                const QString& checkExpr = m_checkConstraints[checkConstraint];

                // 构建字段值映射(QVariantMap)
                QVariantMap fieldValues;

                // 获取记录的所有值
                QMap<QString, QString> recordValues = record.allValues();

                // 合并原记录值和更新值
                for (const QString& field : recordValues.keys()) {
                    if (updates.contains(field)) {
                        fieldValues[field] = convertStringToType(updates[field],getFieldType(field)); // 使用更新值
                    } else {
                        fieldValues[field] = convertStringToType(recordValues[field],getFieldType(field)); // 使用原记录值
                    }
                }

                // 执行CHECK约束检查
                if (!evaluateCheckExpression(checkExpr, fieldValues)) {
                    qWarning() << "更新失败: CHECK约束违反 -" << checkExpr;
                    throw std::runtime_error("更新失败: CHECK约束违反 - " + checkExpr.toStdString());
                    return false;
                }
            }
        }
    }

    return true; // 所有约束通过
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
    m_records = table.getCommittedRecords(); // 使用 getter 获取源表的 m_records
    m_primaryKeys = table.primaryKeys();
    m_foreignKeys = table.m_foreignKeys; // 假设可以直接访问或有 getter
    m_uniqueConstraints = table.m_uniqueConstraints;
    m_checkConstraints = table.m_checkConstraints;

    // ---- 开始修复 ----
    m_notNullFields = table.notNullFields(); // 确保 m_notNullFields 被复制
    m_defaultValues = table.defaultValues(); // 确保 m_defaultValues 被复制
    // ---- 结束修复 ----

    m_inTransaction = false;
    m_tempRecords.clear();
    // 同样重要的是，新表实例的父数据库指针 (m_parentDb) 需要被正确设置。
    // 这通常由 xhydatabase 在添加表时处理。
    // 如果这里创建的表实例是最终存储在 xhydatabase 中的实例，
    // 它的 m_parentDb 应该由调用者（例如 xhydatabase::createtable）设置，
    // 或者如果意图在此处设置，则应作为参数传递给此方法。
    // 此处假设 'this' 对象的 m_parentDb 已经是正确的或在别处设置。

    rebuildIndexes();
    return true;
}

void xhytable::add_primary_key(const QStringList& keys) {
    if (keys.isEmpty()) {
        qWarning() << "[add_primary_key] 表 '" << m_name << "'：尝试添加空的主键列表。";
        // 也可以考虑抛出异常
        // throw std::runtime_error("主键列表不能为空。");
        return;
    }

    for (const QString& key : keys) {
        // 1. 检查字段是否存在于表中
        const xhyfield* field_ptr = get_field(key); // 使用 get_field 进行大小写不敏感查找
        if (field_ptr) {
            // 使用从 get_field 获取的、大小写正确的字段名
            const QString& actualFieldName = field_ptr->name();

            // 2. 添加到 m_primaryKeys 列表 (如果尚不存在)
            //    注意：m_primaryKeys 的比较也应该大小写不敏感，或者存储规范化的大小写形式。
            //    假设 contains 和 append 已处理大小写或您已规范化。
            //    为了安全，我们使用 get_field 返回的实际字段名。
            bool foundInPrimaryKeys = false;
            for (const QString& pk : m_primaryKeys) {
                if (pk.compare(actualFieldName, Qt::CaseInsensitive) == 0) {
                    foundInPrimaryKeys = true;
                    break;
                }
            }
            if (!foundInPrimaryKeys) {
                m_primaryKeys.append(actualFieldName);
            }

            // 3. **【核心修改】将主键的组成列添加到 m_notNullFields 集合**
            //    确保字段名的大小写与 m_notNullFields 中存储的名称一致。
            //    m_notNullFields 在 addfield 中使用 field.name() 插入。
            if (!m_notNullFields.contains(actualFieldName)) { // 避免重复插入和日志输出
                m_notNullFields.insert(actualFieldName);
                qDebug() << "[add_primary_key] 表 '" << m_name << "'：将主键字段 '" << actualFieldName << "' 隐式标记为 NOT NULL。";
            }
        } else {
            // 如果主键中指定的字段在表中不存在
            qWarning() << "[add_primary_key] 添加主键失败：字段 '" << key << "' 在表 '" << m_name << "' 中不存在。";
            // 在生产环境中，这里应该抛出异常，以中断 CREATE TABLE 操作
            // throw std::runtime_error(("添加主键失败：主键中指定的字段 '" + key + "' 不存在于表 '" + m_name + "'。").toStdString());
            // 如果不抛出异常，至少要确保操作的原子性，可能需要回滚部分表定义。
            // 为了简单起见，这里仅输出警告。但这意味着可能创建出一个无效的表定义。
        }
    }
    qDebug() << "[add_primary_key] 表 '" << m_name << "' 的主键列更新为：" << m_primaryKeys;
    qDebug() << "[add_primary_key] 表 '" << m_name << "' 的非空字段集合更新为：" << m_notNullFields;
}
void xhytable::add_foreign_key(const QStringList& childColumns,
                               const QString& referencedTable,
                               const QStringList& referencedColumns,
                               const QString& constraintNameIn) {
    if (childColumns.isEmpty() || referencedColumns.isEmpty() || childColumns.size() != referencedColumns.size()) {
        throw std::runtime_error("添加外键失败：列列表不能为空且长度必须匹配。");
    }

    for (const QString& col : childColumns) {
        if (!has_field(col)) {
            throw std::runtime_error("添加外键失败：字段 '" + col.toStdString() + "' 不存在于表 '" + m_name.toStdString() + "'。");
        }
    }

    QString constraintName = constraintNameIn;
    if (constraintName.isEmpty()) {
        // 自动生成约束名，例如 FK_CHILDTABLE_CHILDFIELD1_CHILDFIELD2_PARENTTABLE
        constraintName = QString("FK_%1_%2_%3").arg(m_name.toUpper(),
                                                    childColumns.join("_").toUpper(),
                                                    referencedTable.toUpper());
    }

    // 检查约束名是否已存在
    for(const auto& fk : m_foreignKeys) {
        if (fk.constraintName.compare(constraintName, Qt::CaseInsensitive) == 0) {
            throw std::runtime_error("添加外键失败：约束名 '" + constraintName.toStdString() + "' 已存在于表 '" + m_name.toStdString() + "'。");
        }
    }

    ForeignKeyDefinition newForeignKey;
    newForeignKey.constraintName = constraintName;
    newForeignKey.referenceTable = referencedTable;
    for (int i = 0; i < childColumns.size(); ++i) {
        newForeignKey.columnMappings[childColumns.at(i)] = referencedColumns.at(i);
    }

    m_foreignKeys.append(newForeignKey);
    qDebug() << "外键 '" << constraintName << "' (" << childColumns.join(", ")
             << " REFERENCES " << referencedTable << "(" << referencedColumns.join(", ") << ")) 已添加到表 " << m_name;
}


void xhytable::add_unique_constraint(const QStringList& fields, const QString& constraintNameIn) {
    if (fields.isEmpty()) {
        throw std::runtime_error("添加唯一约束失败：字段列表不能为空。");
    }
    for(const QString& f : fields) {
        if(!has_field(f)) {
            throw std::runtime_error("添加唯一约束失败：字段 '" + f.toStdString() + "' 不存在于表 '" + m_name.toStdString() + "'。");
        }
    }
    QString constraintName = constraintNameIn;
    if (constraintName.isEmpty()) {
        constraintName = "UQ_" + m_name.toUpper();
        for(const QString& f : fields) constraintName += "_" + f.toUpper();
    }

    if (m_uniqueConstraints.contains(constraintName)) {
        throw std::runtime_error("唯一约束 '" + constraintName.toStdString() + "' 已存在。");
    }
    m_uniqueConstraints[constraintName] = fields;
    qDebug() << "唯一约束 '" << constraintName << "' ON (" << fields.join(", ") << ") 已添加到表 " << m_name;
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


bool xhytable::insertData(const QMap<QString, QString>& fieldValuesFromUser) {
    QMap<QString, QString> valuesToValidateAndInsert = fieldValuesFromUser;

    // Step 1: Process all fields to determine their final value for validation and insertion.
    // This loop ensures all fields are represented in the map, with default values or QString() for NULL.
    for (const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();

        // Scenario A: User explicitly provided the 'DEFAULT' keyword for this field
        if (valuesToValidateAndInsert.contains(fieldName) &&
            valuesToValidateAndInsert.value(fieldName).compare("DEFAULT", Qt::CaseInsensitive) == 0) {
            if (m_defaultValues.contains(fieldName)) {
                valuesToValidateAndInsert[fieldName] = m_defaultValues.value(fieldName);
            } else if (!m_notNullFields.contains(fieldName)) {

                // Field is nullable, no default, and user used DEFAULT keyword -> insert SQL NULL
                valuesToValidateAndInsert[fieldName] = QString();

            } else {
                // NOT NULL field, no default, but user used DEFAULT keyword -> illegal
                QString errMessage = QStringLiteral("字段 '%1' (NOT NULL) 无默认值，无法使用 DEFAULT 关键字。").arg(fieldName);
                qWarning() << "插入数据到表 '" << m_name << "' 失败: " << errMessage;
                throw std::runtime_error(errMessage.toStdString());
            }
        }

        // Scenario B: User did NOT provide a value for this field (it's missing from fieldValuesFromUser)
        else if (!valuesToValidateAndInsert.contains(fieldName)) {
            if (m_defaultValues.contains(fieldName)) {
                // Field has a default value -> use it
                valuesToValidateAndInsert[fieldName] = m_defaultValues.value(fieldName);
            } else {
                // Field does NOT have a default value. Whether it's NOT NULL or nullable,
                // if it's missing, its effective value for validation will be SQL NULL.
                // `validateRecord` will then handle the NOT NULL violation if applicable.
                valuesToValidateAndInsert[fieldName] = QString(); // Explicitly set to SQL NULL (QString())

            }
        }
        // Scenario C: User provided an explicit value (already in valuesToValidateAndInsert), no action needed here.
    }

    try {
        // Pass the fully prepared map to validateRecord. This map now contains all fields,
        // with QString() for SQL NULLs (both explicit 'NULL' literal and implicit missing values for nullable/NOT NULL fields without default).
        validateRecord(valuesToValidateAndInsert, nullptr); // nullptr indicates INSERT operation

        xhyrecord new_record_obj;
        for (const xhyfield& fieldDef : m_fields) {
            // After validateRecord, if we reached here, all necessary NOT NULL checks passed.
            // We can confidently insert the values from our prepared map.
            new_record_obj.insert(fieldDef.name(), valuesToValidateAndInsert.value(fieldDef.name()));
        }

        QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;
        if (m_inTransaction && targetRecordsList->isEmpty() && !m_records.isEmpty()) {
            // Ensure m_tempRecords is a copy of m_records at the start of transaction if this is the first modification
            *targetRecordsList = m_records;
        }
        targetRecordsList->append(new_record_obj);

        qWarning() << "成功插入数据到表 '" << m_name << "'";
        return true;
    } catch (const std::runtime_error& e) {
        qWarning() << "插入数据到表 '" << m_name << "' 失败: " << e.what();
        return false;
    }
}
int xhytable::updateData(const QMap<QString, QString>& updates_with_expressions, const ConditionNode & conditions) {
    if(!checkUpdateConstraints(updates_with_expressions,conditions)){
        return 0;
    }

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

    if (m_inTransaction && targetRecordsList->isEmpty() && !m_records.isEmpty()) {
        // 确保事务开始时 tempRecords 被正确初始化
        *targetRecordsList = m_records;
    }

    QList<int> indicesToRemove;
    for (int i = 0; i < targetRecordsList->size(); ++i) {
        const xhyrecord& currentRecord = targetRecordsList->at(i);
        if (matchConditions(currentRecord, conditions)) {
            try {
                // 在实际标记为删除前，检查外键 RESTRICT 约束
                checkForeignKeyDeleteRestrictions(currentRecord); // 如果不符合会抛出异常
                indicesToRemove.append(i);
            } catch (const std::runtime_error& e) {
                qWarning() << "删除表 '" << m_name << "' 的行 (index " << i << ") 失败，违反外键约束: " << e.what();
                // 根据策略，可以中止整个删除操作或仅跳过此行
                // 为了原子性，如果一个行删除失败，最好是整个操作失败（如果这是单个DELETE命令）
                // 这里简单地跳过此行，但会通过 qWarning 提示
                // 如果要中止整个操作，则： throw;
                continue; // 跳过此行
            }
        }
    }

    // 从后往前删除，以保持索引的有效性
    for (int i = indicesToRemove.size() - 1; i >= 0; --i) {
        targetRecordsList->removeAt(indicesToRemove.at(i));
        affectedRows++;
    }

    if (affectedRows > 0) {
        qDebug() << affectedRows << " 行已从表 '" << m_name << "' 删除。";
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
    qDebug() << "[validateRecord] Validating for table '" << m_name << "'. Values:" << values
             << (original_record_for_update ? "(UPDATE operation)" : "(INSERT operation)");

    const QList<xhyrecord>& recordsToCheck = m_inTransaction ? m_tempRecords : m_records;
    //check约束检查
    if(!checkInsertConstraints(values)){
        throw std::runtime_error("check出错");
    }
    // 1. 字段级约束检查 (NOT NULL, Type, CHECK, ENUM)
    for (const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();
        QString valueToValidate; // 这是将要被验证的值
        bool valueProvidedInInput = values.contains(fieldName); // 用户是否在本次操作中提供了这个字段的值

        if (valueProvidedInInput) {
            valueToValidate = values.value(fieldName);
        } else if (original_record_for_update) { // 更新操作，且此字段未在 SET 中指定
            valueToValidate = original_record_for_update->value(fieldName); // 使用旧值进行验证
        } else {
            // 插入操作，字段未提供。如果字段有默认值，它应该已经被 insertData 放入 values 中。
            // 如果没有默认值，且是NOT NULL，下面的检查会处理。
            // 如果允许NULL，valueToValidate 将是默认构造的QString (isNull() == true)
            valueToValidate = QString(); // 表示数据库NULL
        }

        bool isConsideredSqlNull = valueToValidate.isNull() || // 内部用 QString() 代表SQL NULL
                                   (valueProvidedInInput && values.value(fieldName).compare("NULL", Qt::CaseInsensitive) == 0); // 用户显式输入 "NULL"

        // 检查 NOT NULL 约束
        if (m_notNullFields.contains(fieldName)) {
            if (isConsideredSqlNull) {
                // 即使有默认值，如果最终应用的值是NULL（例如用户显式提供了"NULL"覆盖了默认值），对于NOT NULL字段也是错的
                qDebug() << "[validateRecord DEBUG] Triggering NOT NULL (true NULL) error for field:" << fieldName
                         << " Value:" << valueToValidate << " isNull():" << valueToValidate.isNull();
               throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 不能为 NULL。");
            }
            // 可选：检查 "" (空字符串) 是否违反 NOT NULL (取决于数据库策略)
             if (valueToValidate.isEmpty() && !isConsideredSqlNull) {
                qDebug() << "[validateRecord DEBUG] Triggering NOT NULL (empty string) error for field:" << fieldName
                         << " Value:'" << valueToValidate << "'"
                         << " isEmpty():" << valueToValidate.isEmpty()
                         << " isNull():" << valueToValidate.isNull()
                         << " isConsideredSqlNull:" << isConsideredSqlNull;
                 throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 不能为空字符串。");
             }
        }

        // 类型验证, ENUM, CHECK (仅对非SQL NULL值进行)
        if (!isConsideredSqlNull) {
            if (!validateType(fieldDef.type(), valueToValidate, fieldDef.constraints())) {
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueToValidate.toStdString() +
                                         "' 类型错误或不符合长度/格式约束 (定义类型: " + fieldDef.typestring().toStdString() +")。");
            }
            if (fieldDef.type() == xhyfield::ENUM) {
                // valueToValidate 可能为空字符串 ""。如果 ENUM 列表不包含空字符串，则会报错。
                // 如果 ENUM 列表为空，则任何非空值都会报错。
                if (fieldDef.enum_values().isEmpty() && !valueToValidate.isEmpty()) {
                    qWarning() << "警告：字段 '" << fieldName << "' 是 ENUM 类型，但其允许的值列表为空。非空值 '" << valueToValidate << "' 无效。";
                    throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (ENUM) 的允许值列表为空，无法接受非空值。");
                } else if (!valueToValidate.isEmpty() && !fieldDef.enum_values().contains(valueToValidate, Qt::CaseSensitive)) {
                    throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueToValidate.toStdString() +
                                             "' 不是有效的枚举值。允许的值为: " + fieldDef.enum_values().join(", ").toStdString());
                }
            }
            if (fieldDef.hasCheck() && !checkConstraint(fieldDef, valueToValidate)) { // checkConstraint 是占位符
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueToValidate.toStdString() +
                                         "' 违反了 CHECK 约束: " + fieldDef.checkConstraint().toStdString());
            }
        }
    }

    // 2. 主键唯一性检查
    if (!m_primaryKeys.isEmpty()) {
        QMap<QString, QString> pkValuesInCurrentOp; // 本次操作中涉及的主键值
        QStringList pkValStrListForError;
        bool pkHasNull = false;

        for (const QString& pkFieldName : m_primaryKeys) {
            if (!values.contains(pkFieldName) ||
                values.value(pkFieldName).isNull() ||
                values.value(pkFieldName).compare("NULL", Qt::CaseInsensitive) == 0) {
                pkHasNull = true; // 主键的任何部分为NULL
                break;
            }
            pkValuesInCurrentOp[pkFieldName] = values.value(pkFieldName);
            pkValStrListForError.append(pkFieldName + "=" + values.value(pkFieldName));
        }

        if (pkHasNull) {
            throw std::runtime_error("主键字段 (" + m_primaryKeys.join(", ").toStdString() + ") 不能包含NULL值。");
        }

        qDebug() << "  [validateRecord PK Check] Checking PKs:" << pkValuesInCurrentOp;
        for (const xhyrecord& existingRecord : recordsToCheck) {
            if (original_record_for_update != nullptr && (&existingRecord == original_record_for_update)) {
                continue; // 更新操作，跳过与自身原始状态的比较
            }
            bool conflict = true;
            for (const QString& pkFieldName : m_primaryKeys) {
                if (existingRecord.value(pkFieldName).compare(pkValuesInCurrentOp.value(pkFieldName), Qt::CaseSensitive) != 0) {
                    conflict = false;
                    break;
                }
            }
            if (conflict) {
                throw std::runtime_error("主键冲突: (" + pkValStrListForError.join(", ").toStdString() + ") 已存在。");
            }
        }
    }

    // 3. UNIQUE 约束检查
    for (auto it = m_uniqueConstraints.constBegin(); it != m_uniqueConstraints.constEnd(); ++it) {
        const QString& constraintName = it.key();
        const QList<QString>& uniqueFields = it.value();
        QMap<QString, QString> currentUniqueValues;
        QStringList uniqueValsStrListForError;
        bool uniqueKeyHasNull = false;

        for (const QString& uqFieldName : uniqueFields) {
            // 从 'values' (即传入 validateRecord 的map) 中获取要检查的值
            if (!values.contains(uqFieldName) || values.value(uqFieldName).isNull() ||
                values.value(uqFieldName).compare("NULL", Qt::CaseInsensitive) == 0) {
                uniqueKeyHasNull = true; // 唯一约束的某个字段是NULL
                break;
            }
            currentUniqueValues[uqFieldName] = values.value(uqFieldName);
            uniqueValsStrListForError.append(uqFieldName + "=" + values.value(uqFieldName));
        }

        if (uniqueKeyHasNull) {
            // SQL标准：如果唯一约束的任何组成部分为NULL，则该约束不阻止重复（即允许多个NULL）
            // 所以我们跳过对此唯一约束的检查
            qDebug() << "  [validateRecord UQ Check] Skipping UNIQUE constraint '" << constraintName << "' because one of its fields is NULL.";
            continue;
        }
        qDebug() << "  [validateRecord UQ Check] Checking UQ '" << constraintName << "' with values:" << currentUniqueValues;

        for (const xhyrecord& existingRecord : recordsToCheck) {
            if (original_record_for_update != nullptr && (&existingRecord == original_record_for_update)) {
                continue; // 更新操作，跳过与自身原始状态的比较
            }

            bool allExistingUqFieldsMatch = true;
            bool existingUqKeyHasNull = false;
            for (const QString& uqFieldName : uniqueFields) {
                if (existingRecord.value(uqFieldName).isNull() ||
                    existingRecord.value(uqFieldName).compare("NULL", Qt::CaseInsensitive) == 0) {
                    existingUqKeyHasNull = true; // 已存在记录的唯一键部分有NULL，不参与冲突
                    break;
                }
                if (existingRecord.value(uqFieldName).compare(currentUniqueValues.value(uqFieldName), Qt::CaseSensitive) != 0) {
                    allExistingUqFieldsMatch = false;
                    break;
                }
            }

            if (existingUqKeyHasNull) { // 如果现有记录的唯一键部分有NULL，它不与当前记录冲突
                allExistingUqFieldsMatch = false;
            }

            if (allExistingUqFieldsMatch) {
                throw std::runtime_error("唯一约束 '" + constraintName.toStdString() + "' 冲突: 值 (" +
                                         uniqueValsStrListForError.join(", ").toStdString() + ") 已存在。");
            }
        }
    }

    // 4. 外键约束检查 (用于 INSERT 和 UPDATE)
    if (m_parentDb && !m_foreignKeys.isEmpty()) {
        for (const auto& fkDef : m_foreignKeys) { // 遍历 ForeignKeyDefinition 列表
            const QString& refTableName = fkDef.referenceTable;

            // 收集当前操作中（values）待验证的外键值，以及它们在父表中的对应列名
            QMap<QString, QString> currentFkValues; // 子表中的值
            QMap<QString, QString> parentRefColNames; // 父表中对应的列名
            bool fkValueHasNull = false;

            for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
                const QString& childCol = it.key();        // 子表中的外键列名
                const QString& parentRefCol = it.value();   // 父表中的被引用列名

                if (!values.contains(childCol)) {
                    // 如果是更新操作且当前字段未在SET中指定，则使用原始记录的值
                    if (original_record_for_update && original_record_for_update->allValues().contains(childCol)) {
                        currentFkValues[childCol] = original_record_for_update->value(childCol);
                    } else {
                        // 插入操作且未提供值，或者更新操作未提供值且原始记录中也没有（不应该发生）
                        // 如果外键列允许NULL且最终值为NULL，则跳过检查
                        // 但此处为了完整性，我们假设values中已经填充了默认值或QString()
                        fkValueHasNull = true; // 此时该列的值默认为NULL (QString())，不需要匹配
                        break;
                    }
                } else {
                    currentFkValues[childCol] = values.value(childCol);
                }

                if (currentFkValues.value(childCol).isNull() || currentFkValues.value(childCol).compare("NULL", Qt::CaseInsensitive) == 0) {
                    fkValueHasNull = true;
                    break; // 复合外键中只要有一列为NULL，就认为该外键是NULL，不需要匹配
                }
                parentRefColNames[parentRefCol] = currentFkValues.value(childCol); // 父表列名 -> 子表提供的值
            }

            if (fkValueHasNull || currentFkValues.isEmpty()) {
                qDebug() << "  [validateRecord FK Check] FK '" << fkDef.constraintName
                         << "' on columns " << fkDef.columnMappings.keys().join(", ")
                         << " contains NULL values or is empty, skipping check.";
                continue;
            }

            xhytable* referencedTable = m_parentDb->find_table(refTableName);
            if (!referencedTable) {
                throw std::runtime_error("外键约束 '" + fkDef.constraintName.toStdString() +
                                         "' 定义错误: 引用的表 '" + refTableName.toStdString() + "' 在数据库中不存在。");
            }

            // 验证被引用的列在父表中是否是主键或唯一键（这是一个好的实践）
            // 对于复合外键，需要确保所有被引用的列作为一个整体在父表中是唯一的（主键或唯一约束）
            bool isRefCompositePKOrUQ = false;
            if (!referencedTable->primaryKeys().isEmpty() && referencedTable->primaryKeys().size() == fkDef.columnMappings.size()) {
                // 检查引用的列是否是父表的主键的所有组成部分
                bool allRefColsInParentPK = true;
                for(const QString& refCol : fkDef.columnMappings.values()){ // fkDef.columnMappings.values() 是父表列名
                    if(!referencedTable->primaryKeys().contains(refCol, Qt::CaseInsensitive)){
                        allRefColsInParentPK = false;
                        break;
                    }
                }
                if(allRefColsInParentPK) isRefCompositePKOrUQ = true;
            }
            if (!isRefCompositePKOrUQ) { // 如果不是主键，检查是否是某个唯一约束的组成部分
                for (const auto& uqConstFields : referencedTable->uniqueConstraints().values()) { // UQ约束的列列表
                    if (uqConstFields.size() == fkDef.columnMappings.size()) {
                        bool allRefColsInUQ = true;
                        for(const QString& refCol : fkDef.columnMappings.values()){
                            if(!uqConstFields.contains(refCol, Qt::CaseInsensitive)){
                                allRefColsInUQ = false;
                                break;
                            }
                        }
                        if(allRefColsInUQ) {
                            isRefCompositePKOrUQ = true;
                            break;
                        }
                    }
                }
            }
            if (!isRefCompositePKOrUQ) {
                qWarning() << "警告: 外键约束 '" << fkDef.constraintName << "' 引用的复合列 ("
                           << fkDef.columnMappings.values().join(", ") << ") 在表 '" << refTableName
                           << "' 中可能不是主键或唯一键的完整组成部分。这可能导致非预期的行为。";
            }


            bool foundReferencedKey = false;
            const QList<xhyrecord>& parentRecords = referencedTable->records();
            for (const xhyrecord& parentRecord : parentRecords) {
                bool allMappingsMatch = true;
                for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
                    const QString& childCol = it.key();
                    const QString& parentRefCol = it.value(); // 父表中对应的列名

                    if (parentRecord.value(parentRefCol).compare(currentFkValues.value(childCol), Qt::CaseSensitive) != 0) {
                        allMappingsMatch = false;
                        break;
                    }
                }
                if (allMappingsMatch) {
                    foundReferencedKey = true;
                    break;
                }
            }

            if (!foundReferencedKey) {
                throw std::runtime_error("外键约束 '" + fkDef.constraintName.toStdString() +
                                         "' 冲突: 字段 (" + fkDef.columnMappings.keys().join(", ").toStdString() +
                                         ") 的值 (" + currentFkValues.values().join(", ").toStdString() + ") 在引用的表 '" +
                                         refTableName.toStdString() + "' (列 '" + fkDef.columnMappings.values().join(", ").toStdString() + "') 中不存在。");
            }
            qDebug() << "  [validateRecord FK Check] FK '" << fkDef.constraintName << "' on "
                     << fkDef.columnMappings.keys().join(", ") << " to " << refTableName << "("
                     << fkDef.columnMappings.values().join(", ") << ") PASSED.";
        }
    } else if (!m_foreignKeys.isEmpty() && !m_parentDb) {
        qWarning() << "警告: 表 " << m_name << " 有外键定义但缺少对父数据库的引用，无法执行外键检查。";
    }
    qDebug() << "[validateRecord] Validation successful for table '" << m_name << "'.";
}



bool xhytable::validateType(xhyfield::datatype type, const QString& value, const QStringList& constraints) const {
    if (value.isNull()) return true;
    bool ok;
    switch(type) {
    case xhyfield::TINYINT:
    case xhyfield::SMALLINT:
    case xhyfield::INT:
    case xhyfield::BIGINT: {
        qlonglong val_ll;
        bool conversion_successful;

        // 1. 首先用正则表达式检查字符串是否完全是整数格式 (允许负号)
        QRegularExpression int_pattern(R"(^-?\d+$)");
        if (!int_pattern.match(value).hasMatch()) {
            qWarning() << "Value '" << value << "' is not a valid integer format for type " << static_cast<int>(type);
            conversion_successful = false;
        } else {
            // 2. 如果格式正确，再尝试转换为 qlonglong
            val_ll = value.toLongLong(&conversion_successful);
            if (!conversion_successful) {
                // 这个分支理论上不应该进入，如果正则表达式通过了但toLongLong失败，可能是非常大的数超出了qlonglong
                qWarning() << "Value '" << value << "' failed qlonglong conversion despite regex match for type " << static_cast<int>(type);
            }
        }

        if (conversion_successful) {
            // 3. 根据具体类型进行范围检查
            switch(type) {
            case xhyfield::TINYINT:
                // 假设 TINYINT 范围为 signed char: -128 到 127
                if (val_ll < -128 || val_ll > 127) {
                    qWarning() << "Value '" << value << "' (" << val_ll << ") is out of range for TINYINT (-128 to 127).";
                    conversion_successful = false;
                }
                break;
            case xhyfield::SMALLINT:
                // 假设 SMALLINT 范围为 signed short: -32768 到 32767
                if (val_ll < -32768 || val_ll > 32767) {
                    qWarning() << "Value '" << value << "' (" << val_ll << ") is out of range for SMALLINT (-32768 to 32767).";
                    conversion_successful = false;
                }
                break;
            case xhyfield::INT:
                // 使用 std::numeric_limits 获取标准 int 范围
                if (val_ll < std::numeric_limits<int>::min() || val_ll > std::numeric_limits<int>::max()) {
                    qWarning() << "Value '" << value << "' (" << val_ll << ") is out of range for INT ("
                               << std::numeric_limits<int>::min() << " to " << std::numeric_limits<int>::max() << ").";
                    conversion_successful = false;
                }
                break;
            case xhyfield::BIGINT:

                break;
            default: // 不应到达
                conversion_successful = false;
                break;
            }
        }
        return conversion_successful;
    }
    case xhyfield::FLOAT:
    case xhyfield::DOUBLE: { // FLOAT 和 DOUBLE 的基本验证
        // 对于这些类型，QString::toDouble 的校验目前是足够的
        QRegularExpression float_pattern(R"(^-?\d*\.?\d+(?:[eE][-+]?\d+)?$)");
        if (!value.isEmpty() && !float_pattern.match(value).hasMatch()) {
            qWarning() << "Value '" << value << "' is not in a recognizable general number format for FLOAT/DOUBLE.";
            return false;
        }
        value.toDouble(&ok); // ok 会被设置
        if (!ok && !value.isEmpty()) { // 如果非空但转换失败
            qWarning() << "Value '" << value << "' is not a valid floating-point format for type " << static_cast<int>(type);
        }
        return ok || value.isEmpty(); // 允许空字符串通过类型验证（后续由NOT NULL或CHECK处理）
    }
    case xhyfield::DECIMAL: {
        int precision = 0, scale = 0;
        // parseDecimalParams 辅助函数应已在 xhytable.cpp 中定义
        // bool params_defined = parseDecimalParams(constraints, precision, scale);
        // 假设 parseDecimalParams 是之前定义的：
        bool p_found = false; // parseDecimalParams 内部应设置
        precision = 0; scale = 0;
        for (const QString& c : constraints) {
            if (c.startsWith("PRECISION(", Qt::CaseInsensitive) && c.endsWith(")")) {
                bool conv_ok = false;
                precision = c.mid(10, c.length() - 11).toInt(&conv_ok);
                if (conv_ok) p_found = true;
            } else if (c.startsWith("SCALE(", Qt::CaseInsensitive) && c.endsWith(")")) {
                bool conv_ok = false;
                scale = c.mid(6, c.length() - 7).toInt(&conv_ok);
                // s_found 可以在这里设置，但 parseDecimalParams 的返回值主要看 p_found
            }
        }
        bool params_defined = p_found; // DECIMAL 必须有 P，S 可默认为 0

        if (!params_defined) {
            qWarning() << "DECIMAL type for value '" << value << "' lacks PRECISION constraint. Cannot perform full validation. Performing basic number validation.";
            QRegularExpression basic_num_pattern(R"(^-?\d*\.?\d*$)");
            if (!value.isEmpty() && !basic_num_pattern.match(value).hasMatch()) return false;
            bool conv_ok_double;
            value.toDouble(&conv_ok_double);
            return conv_ok_double || value.isEmpty();
        }

        if (precision <= 0) {
            qWarning() << "DECIMAL precision " << precision << " (from constraints) is invalid for value '" << value << "'. Must be > 0.";
            return false;
        }
        if (scale < 0 || scale > precision) {
            qWarning() << "DECIMAL scale " << scale << " (from constraints) is invalid for precision " << precision
                       << " (value: '" << value << "'). Must be 0 <= scale <= precision.";
            return false;
        }

        if (value.isEmpty()) return true; // 空字符串由 NOT NULL 处理

        QRegularExpression decimal_pattern(R"(^-?\d*(\.\d*)?$)");
        if (!decimal_pattern.match(value).hasMatch() || value == "." || value == "-" || value == "-.") {
            qWarning() << "Value '" << value << "' is not a valid DECIMAL format.";
            return false;
        }

        bool temp_conv_ok;
        value.toDouble(&temp_conv_ok); // 进一步检查是否能转为浮点数，排除 "1.2.3"
        if(!temp_conv_ok) {
            qWarning() << "Value '" << value << "' could not be parsed as a number for DECIMAL validation.";
            return false;
        }

        QString num_str = value;
        if (num_str.startsWith('-')) {
            num_str.remove(0, 1);
        }

        int dot_pos = num_str.indexOf('.');
        QString int_part_str = (dot_pos == -1) ? num_str : num_str.left(dot_pos);
        QString frac_part_str = (dot_pos == -1) ? "" : num_str.mid(dot_pos + 1);

        // 1. 验证小数部分的位数 (scale)
        if (frac_part_str.length() > scale) {
            qWarning() << "Value '" << value << "' has " << frac_part_str.length()
            << " decimal places, exceeding defined DECIMAL scale of " << scale << ".";
            return false;
        }

        // 2. 验证整数部分的位数 (P-S)
        int max_int_digits_allowed = precision - scale;
        int current_int_digits = 0;

        if (int_part_str.isEmpty()) { // 例如 ".123"
            current_int_digits = 0;
        } else {
            // 规范化整数部分，例如 "007" -> "7", "0" -> "0"
            bool ok_int_conv;
            qlonglong int_val = int_part_str.toLongLong(&ok_int_conv);
            if (!ok_int_conv && !int_part_str.isEmpty()) { // 如果非空但无法转换为longlong (例如 "abc")
                qWarning() << "Invalid integer part '" << int_part_str << "' for DECIMAL value '" << value << "'.";
                return false;
            }
            if (int_val == 0 && !int_part_str.isEmpty()) { // 如果整数部分是 "0", "00" 等
                current_int_digits = 1; // "0" 本身算一个有效整数位，但特殊处理
            } else if (int_val != 0) {
                current_int_digits = QString::number(qAbs(int_val)).length(); // 非零整数的位数
            } else { // int_part_str 为空或转换后为0但原始字符串也代表0
                current_int_digits = 0;
            }
        }

        // 核心逻辑修正点：
        // 如果 P-S = 0 (小数点前不允许有非0数字), 且实际整数部分仅为 "0", 这是允许的。
        // 例如 DECIMAL(5,5) 和值 "0.12345"。current_int_digits = 1, max_int_digits_allowed = 0.
        // 此时不应因 "1 > 0" 而报错。
        if (current_int_digits > max_int_digits_allowed) {
            if (max_int_digits_allowed == 0 && current_int_digits == 1 && !int_part_str.isEmpty() && int_part_str.toLongLong() == 0) {
                // 这是允许的情况, 如 "0.123" 对于 DECIMAL(3,3)
                // 或者 "000.123" 对于 DECIMAL(3,3) (current_int_digits 会是 1)
            } else {
                qWarning() << "Value '" << value << "' has effective integer part with " << current_int_digits
                           << " digit(s) (from '" << int_part_str << "'), exceeding allowed " << max_int_digits_allowed
                           << " for DECIMAL(" << precision << "," << scale << ").";
                return false;
            }
        }

        // (可选) 也可以增加一个对总有效数字位数的校验，但这通常由 S 和 P-S 的校验间接保证。
        // 例如，去除符号和小数点，然后去除整数部分的前导0（除非整数部分就是0），然后计算总长度。
        // QString all_digits_str = int_part_str + frac_part_str; // 这是一个简化的思路
        // if (all_digits_str.length() > precision && !(int_part_str == "0" && frac_part_str.length() == precision)) { ... }

        return true; // 所有检查通过
    }
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

