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

// zyh的where里的like
namespace { // 匿名命名空间，用于此文件内的辅助函数
QStringList helper_parseSqlValues(const QString& input_raw) {
    QStringList values;
    QString current_value;
    bool in_string_literal = false;
    QChar quote_char = QChar::Null;
    QString input = input_raw.trimmed();

    for (int i = 0; i < input.length(); ++i) {
        QChar ch = input[i];
        if (in_string_literal) {
            if (ch == quote_char) {
                // 处理SQL中的两个连续引号表示一个引号的情况
                if (i + 1 < input.length() && input[i + 1] == quote_char) {
                    current_value += quote_char;
                    i++; // 跳过第二个引号
                } else {
                    in_string_literal = false; // 字符串结束
                    // current_value += ch; // 包含结束引号 (如果 parseLiteralValue 需要)
                    // 或者不加，如果 parseLiteralValue 期望的是内部内容
                }
            } else {
                current_value += ch;
            }
        } else {
            if (ch == '\'' || ch == '"') {
                in_string_literal = true;
                quote_char = ch;
                // current_value += ch; // 包含开始引号 (如果 parseLiteralValue 需要)
            } else if (ch == ',') {
                values.append(current_value.trimmed());
                current_value.clear();
            } else {
                current_value += ch;
            }
        }
    }
    if (!current_value.trimmed().isEmpty() || !values.isEmpty() || !input_raw.trimmed().isEmpty()) {
        values.append(current_value.trimmed()); // 添加最后一个值
    }
    // 清理可能因仅有逗号或末尾逗号产生的空字符串
    values.removeAll(QString(""));
    return values;
}
}
QString sqlLikeToRegex(const QString& likePattern, QChar customEscapeChar = QChar::Null) {
    QString regexPattern;
    regexPattern += '^'; // 锚定字符串的开始

    // 确定实际使用的转义字符
    const QChar escapeChar = (customEscapeChar == QChar::Null) ? QLatin1Char('\\') : customEscapeChar;
    bool nextCharIsEscaped = false;

    for (int i = 0; i < likePattern.length(); ++i) {
        QChar c = likePattern[i];

        if (nextCharIsEscaped) {
            // 当前字符是前一个转义字符的目标，将其作为普通字符处理
            regexPattern += QRegularExpression::escape(QString(c));
            nextCharIsEscaped = false;
        } else {
            if (c == escapeChar) {
                // 遇到了转义字符，标记下一个字符需要被转义
                // 注意：如果转义字符是模式的最后一个字符，这种处理方式会将其忽略。
                // SQL标准通常认为以转义符结尾的模式是错误的。
                // 或者，您可以选择将末尾的转义符也视为普通字符。
                // 为简单起见，这里我们假设它总是用于转义下一个字符。
                if (i + 1 < likePattern.length()) {
                    nextCharIsEscaped = true;
                } else {
                    // 模式以转义字符结尾，将其视为文字处理
                    regexPattern += QRegularExpression::escape(QString(c));
                }
            } else if (c == QLatin1Char('%')) {
                regexPattern += ".*"; // SQL '%' 对应正则表达式 '.*'
            } else if (c == QLatin1Char('_')) {
                regexPattern += ".";  // SQL '_' 对应正则表达式 '.'
            } else {
                // 其他字符按原样转义，以防它们是正则表达式的特殊字符
                regexPattern += QRegularExpression::escape(QString(c));
            }
        }
    }

    // 如果模式以一个有效的转义序列结束，但nextCharIsEscaped仍为true（例如 LIKE 'abc\' ），
    // 之前的逻辑会忽略最后一个字符。为确保正确性，可以添加一个检查，
    // 但更标准的做法是在解析LIKE模式时就认为这种情况非法。
    // 此处保持简单，依赖于 i+1 < length() 的检查。

    regexPattern += '$'; // 锚定字符串的结束
    return regexPattern;
}

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
    // 期望格式: field.constraints() 列表中 "DEFAULT" 紧跟着 "<parsed_value_or_keyword>"
    int defaultIdx = -1;
    for(int i=0; i < field.constraints().size(); ++i) {
        // 注意：这里我们匹配的是由 parseConstraints 添加的 "DEFAULT" 标记
        if (field.constraints().at(i).compare("DEFAULT", Qt::CaseInsensitive) == 0) {
            defaultIdx = i;
            break;
        }
    }
    if (defaultIdx != -1 && defaultIdx + 1 < field.constraints().size()) {
        QString defaultValueOrKeyword = field.constraints().at(defaultIdx + 1);
        m_defaultValues[field.name()] = defaultValueOrKeyword; // 直接存储解析后的值或特殊关键字
        qDebug() << "[表::addfield] 字段 '" << field.name() << "' 设置默认值为 (或关键字): '" << defaultValueOrKeyword << "'";
    }


    // 解析字段级 CHECK 约束 (修正版)
    for (const QString& constraint_item : field.constraints()) { // 遍历该字段的所有约束字符串
        if (constraint_item.startsWith("CHECK", Qt::CaseInsensitive) &&
            constraint_item.contains('(') && constraint_item.endsWith(')')) {

            int startParen = constraint_item.indexOf('(');
            int endParen = constraint_item.lastIndexOf(')');

            if (startParen != -1 && endParen > startParen) {
                QString condition = constraint_item.mid(startParen + 1, endParen - (startParen + 1)).trimmed();
                if (!condition.isEmpty()) {
                    // 这个调用会将 CHECK 约束添加到 this->m_checkConstraints
                    this->add_check_constraint(condition, ""); // 约束名自动生成
                    qDebug() << "[addfield] 字段" << field.name() << ": 已添加 CHECK 约束，条件为:" << condition;
                } else { /* ... warning ... */ }
            } else { /* ... warning ... */ }
            break;
        }
    }
    m_fields.append(newField);
}

// xhytable.cpp
bool xhytable::checkForeignKeyDeleteRestrictions(const xhyrecord& recordToDelete) const {
    if (!m_parentDb) {
        qWarning() << "警告：表 " << m_name << " 缺少对父数据库的引用，无法执行外键删除检查。";
        return true; // 或者根据您的设计决定是否应该抛出异常
    }

    qDebug() << "[FK Check ON DELETE] Table" << m_name << ": Checking restrictions for deleting record:" << recordToDelete.allValues();

    // 遍历数据库中的所有表，检查它们是否有外键引用到当前表 (this->m_name) 中即将被删除的记录
    for (const xhytable& potentialReferencingTable : m_parentDb->tables()) {
        // 对于 potentialReferencingTable 中的每一个外键定义...
        for (const auto& fkDef : potentialReferencingTable.foreignKeys()) {
            // 检查这个外键是否引用了当前表 (this->m_name)
            if (fkDef.referenceTable.compare(this->m_name, Qt::CaseInsensitive) == 0) {
                // 是的，potentialReferencingTable 中的外键 fkDef 引用了 this->m_name 表。
                // 现在我们需要检查 potentialReferencingTable 中是否有任何记录的此外键列的值
                // 等于 recordToDelete (来自 this->m_name 表) 中被引用的主键/唯一键列的值。

                QMap<QString, QString> parentKeyValuesFromRecordToDelete; // 从被删除记录中提取其被引用的键值
                bool canBeReferenced = true;
                for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                    // it_map.key() 是 potentialReferencingTable 中的外键列
                    // it_map.value() 是 this->m_name (父表) 中被引用的列
                    const QString& parentReferencedColumnName = it_map.value();
                    QString val = recordToDelete.value(parentReferencedColumnName);
                    if (val.isNull()) { // 如果被引用的父键部分为NULL，则不能形成有效引用
                        canBeReferenced = false;
                        break;
                    }
                    parentKeyValuesFromRecordToDelete[parentReferencedColumnName] = val;
                }

                if (!canBeReferenced || parentKeyValuesFromRecordToDelete.isEmpty()) {
                    continue; // 被删除记录的被引用键包含NULL，不可能有子记录引用它
                }

                // 遍历 potentialReferencingTable (可能是自身，也可能是其他表) 的所有记录
                const QList<xhyrecord>& referencingRecords = potentialReferencingTable.records();
                for (const xhyrecord& referencingRecord : referencingRecords) {

                    // 如果是自引用检查 (potentialReferencingTable 就是 this)，并且当前检查的 referencingRecord 就是 recordToDelete，则跳过
                    if (potentialReferencingTable.name().compare(this->m_name, Qt::CaseInsensitive) == 0) {
                        bool isSameRecord = true;
                        if (m_primaryKeys.isEmpty()) { // 没有主键，很难精确判断是否是同一条记录，这里做个简单处理
                            if (&referencingRecord == &recordToDelete) { // 仅当是同一个对象时才跳过（可能不准确）
                                // qDebug() << "[FK Check ON DELETE Self-Ref] Skipping comparison of record with itself (pointer check).";
                            } else { isSameRecord = false; } // 不是同一个对象实例，不能简单跳过
                        } else {
                            for(const QString& pkCol : m_primaryKeys) {
                                if(referencingRecord.value(pkCol) != recordToDelete.value(pkCol)) {
                                    isSameRecord = false;
                                    break;
                                }
                            }
                        }
                        if(isSameRecord) {
                            // qDebug() << "[FK Check ON DELETE Self-Ref] Skipping comparison of record with itself (PK check):" << recordToDelete.value(m_primaryKeys.first());
                            continue;
                        }
                    }


                    bool allFkPartsMatch = true;
                    bool childFkHasNull = false;
                    for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                        const QString& childFkColumnName = it_map.key();
                        const QString& parentReferencedColumnName = it_map.value();
                        QString childFkValue = referencingRecord.value(childFkColumnName);

                        if (childFkValue.isNull()) {
                            childFkHasNull = true;
                            break; // 如果子记录的外键部分为NULL，它不构成有效引用
                        }
                        if (childFkValue.compare(parentKeyValuesFromRecordToDelete.value(parentReferencedColumnName), Qt::CaseSensitive) != 0) {
                            allFkPartsMatch = false;
                            break;
                        }
                    }

                    if (allFkPartsMatch && !childFkHasNull) {
                        // 找到了一个引用记录！
                        QString err = QString("删除操作被限制：表 '%1' 中的记录 (例如 %2=%3) 通过外键 '%4' (列 '%5') "
                                              "引用了表 '%6' (当前表) 中即将删除的记录 (%7=%8)。")
                                          .arg(potentialReferencingTable.name())
                                          .arg(potentialReferencingTable.primaryKeys().isEmpty() ? "PK?" : potentialReferencingTable.primaryKeys().first())
                                          .arg(referencingRecord.value(potentialReferencingTable.primaryKeys().isEmpty() ? "" : potentialReferencingTable.primaryKeys().first()))
                                          .arg(fkDef.constraintName)
                                          .arg(fkDef.columnMappings.keys().join(", "))
                                          .arg(this->m_name)
                                          .arg(m_primaryKeys.isEmpty() ? "PK_of_deleted?" : m_primaryKeys.first())
                                          .arg(recordToDelete.value(m_primaryKeys.isEmpty() ? "" : m_primaryKeys.first()));
                        qDebug() << (potentialReferencingTable.name().compare(this->m_name, Qt::CaseInsensitive) == 0 ? "[FK Check ON DELETE Self-Ref] " : "[FK Check ON DELETE OtherTable] ") << err;
                        throw std::runtime_error(err.toStdString());
                    }
                }
            }
        }
    }
    qDebug() << "[FK Check ON DELETE] Table" << m_name << ": No restrictions found for record:" << recordToDelete.allValues();
    return true; // 如果遍历完所有表和所有外键都没有找到引用，则可以安全删除
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
    qDebug() << "[表::CHECK表达式求值] 开始处理表达式: '" << expr << "' 使用字段值: " << fieldValues;

    // 1. 安全防护
    if (expr.contains("function", Qt::CaseInsensitive) || expr.contains("eval", Qt::CaseInsensitive) || expr.contains("script", Qt::CaseInsensitive)) {
        qWarning() << "  拒绝执行可能危险的CHECK表达式（包含禁用词）:" << expr;
        return false;
    }

    // 2. 注入字段值到JS引擎
    for (auto it = fieldValues.constBegin(); it != fieldValues.constEnd(); ++it) {
        const QString& fieldName = it.key();
        const QVariant& qVariantValue = it.value();

        if (qVariantValue.isNull() || qVariantValue.userType() == QMetaType::UnknownType ||
            (qVariantValue.typeId() == QMetaType::QString && qVariantValue.toString().compare("NULL", Qt::CaseInsensitive) == 0) ) {
            engine.globalObject().setProperty(fieldName, QJSValue::NullValue);
            // qDebug() << "  注入字段 '" << fieldName << "' 为 JS null";
            continue;
        }

        // 根据QVariant内部存储的C++类型注入
        switch (qVariantValue.userType()) {
        case QMetaType::Bool:       engine.globalObject().setProperty(fieldName, qVariantValue.toBool()); break;
        case QMetaType::Int:        engine.globalObject().setProperty(fieldName, qVariantValue.toInt()); break;
        case QMetaType::LongLong:   engine.globalObject().setProperty(fieldName, static_cast<double>(qVariantValue.toLongLong())); break;
        case QMetaType::UInt:       engine.globalObject().setProperty(fieldName, qVariantValue.toUInt()); break;
        case QMetaType::ULongLong:  engine.globalObject().setProperty(fieldName, static_cast<double>(qVariantValue.toULongLong())); break;
        case QMetaType::Float:      engine.globalObject().setProperty(fieldName, static_cast<double>(qVariantValue.toFloat())); break;
        case QMetaType::Double:     engine.globalObject().setProperty(fieldName, qVariantValue.toDouble()); break;
        case QMetaType::QDate:      engine.globalObject().setProperty(fieldName, qVariantValue.toDate().toString(Qt::ISODate)); break;
        case QMetaType::QDateTime:  engine.globalObject().setProperty(fieldName, qVariantValue.toDateTime().toString(Qt::ISODateWithMs)); break;
        case QMetaType::QString:
        default:                    engine.globalObject().setProperty(fieldName, qVariantValue.toString()); break;
        }
    }

    // 3. SQL→JS语法转换
    QString jsExpr = expr;
    qDebug() << "  [表::CHECK表达式求值] 原始SQL CHECK表达式 (来自m_checkConstraints): " << jsExpr;

    // 规范化字段名大小写
    for (const xhyfield& fieldDef : m_fields) {
        QString fieldNameInTableDef = fieldDef.name();
        QRegularExpression fieldRegex(QString(R"(\b%1\b)").arg(QRegularExpression::escape(fieldNameInTableDef)),
                                      QRegularExpression::CaseInsensitiveOption);
        jsExpr.replace(fieldRegex, fieldNameInTableDef);
    }
    qDebug() << "  [表::CHECK表达式求值] 规范化字段名大小写后的JS表达式: " << jsExpr;

    // 基本逻辑和比较操作符转换
    jsExpr.replace(QRegularExpression(R"(\bAND\b)", QRegularExpression::CaseInsensitiveOption), "&&");
    jsExpr.replace(QRegularExpression(R"(\bOR\b)", QRegularExpression::CaseInsensitiveOption), "||");
    jsExpr.replace(QRegularExpression(R"(\bNOT\b(?!\s+(?:IN|LIKE|BETWEEN|NULL)))", QRegularExpression::CaseInsensitiveOption), "!"); // 修正NOT以避免干扰IS NOT NULL
    jsExpr.replace(QRegularExpression(R"((?<![!=<>])=(?!=))"), "==");
    jsExpr.replace("<>", "!=");

    QRegularExpression isNotNullRe(R"(\b([\w`\[\]\.]+)\s+IS\s+NOT\s+NULL\b)", QRegularExpression::CaseInsensitiveOption);
    jsExpr.replace(isNotNullRe, "(\\1 !== null && typeof \\1 !== 'undefined')"); // 使用 \\1 代表第一个捕获组

    QRegularExpression isNullRe(R"(\b([\w`\[\]\.]+)\s+IS\s+NULL\b)", QRegularExpression::CaseInsensitiveOption);
    jsExpr.replace(isNullRe, "(\\1 === null || typeof \\1 === 'undefined')"); // 使用 \\1 代表第一个捕获组

    qDebug() << "  [表::CHECK表达式求值] 处理 IS NULL/NOT NULL后的JS表达式: " << jsExpr;
    // 处理 LIKE 和 NOT LIKE
    QRegularExpression likeRe(R"(([\w`\[\]\.]+)\s+(NOT\s+)?LIKE\s+'((?:[^']|'')*)')", QRegularExpression::CaseInsensitiveOption);
    int pos = 0;
    QString tempJsExprForLike = jsExpr;
    jsExpr.clear();
    int lastPos = 0;
    while ((pos = tempJsExprForLike.indexOf(likeRe, lastPos)) != -1) {
        QRegularExpressionMatch match = likeRe.match(tempJsExprForLike, pos);
        jsExpr += tempJsExprForLike.mid(lastPos, pos - lastPos);
        QString field = match.captured(1);
        bool isNotLike = !match.captured(2).isEmpty();
        QString patternStr = match.captured(3).replace("''", "'");
        QString jsRegexPattern =sqlLikeToRegex(patternStr); // 使用辅助函数
        QString escapedJsRegexPattern = jsRegexPattern.replace("\\", "\\\\").replace("'", "\\'");
        jsExpr += QString("%3new RegExp('^%1$', 'i').test(%2)") // 添加 ^ 和 $ 以匹配整个字符串
                      .arg(escapedJsRegexPattern, field, (isNotLike ? "!" : ""));
        lastPos = pos + match.capturedLength();
    }
    jsExpr += tempJsExprForLike.mid(lastPos);
    qDebug() << "  [表::CHECK表达式求值] 处理 LIKE 后的JS表达式: " << jsExpr;

    // 处理 IN 和 NOT IN
    QRegularExpression inRe(R"(([\w`\[\]\.]+)\s+(NOT\s+)?IN\s*\(\s*((?:'[^']*'(?:\s*,\s*'[^']*')*|[\d\s,trufalsetTRUEFALSE\.\-\+Ee]+)?)\s*\))", QRegularExpression::CaseInsensitiveOption);
    pos = 0;
    QString tempJsExprForIn = jsExpr;
    jsExpr.clear();
    lastPos = 0;
    while((pos = tempJsExprForIn.indexOf(inRe, lastPos)) != -1) {
        QRegularExpressionMatch match = inRe.match(tempJsExprForIn, pos);
        jsExpr += tempJsExprForIn.mid(lastPos, pos - lastPos);
        QString field = match.captured(1);
        bool isNotIn = !match.captured(2).isEmpty();
        QString valuesPart = match.captured(3).trimmed();
        QStringList jsValues;
        if (!valuesPart.isEmpty()) {
            QStringList sqlLiterals = helper_parseSqlValues(valuesPart); // 使用为此优化的辅助函数
            for (const QString& sqlLiteralOriginal : sqlLiterals) {
                QString sqlLiteral = sqlLiteralOriginal;
                // 将SQL字面量转换为JS字面量
                if (sqlLiteral.compare("true", Qt::CaseInsensitive) == 0 || sqlLiteral.compare("false", Qt::CaseInsensitive) == 0) {
                    jsValues << sqlLiteral.toLower();
                } else if ((sqlLiteral.startsWith('\'') && sqlLiteral.endsWith('\'')) || (sqlLiteral.startsWith('"') && sqlLiteral.endsWith('"'))) {
                    // 如果 helper_parseSqlValues_for_check 返回的是包含引号的字符串 " 'value' "
                    // 那么JS数组中也应该是 "'value'"
                    // 如果 helper_parseSqlValues_for_check 返回的是去除引号的字符串 "value"
                    // 那么JS数组中应该是 "'value'"
                    // 假设 helper_parseSqlValues_for_check 返回的是SQL字面量字符串本身（即 'value' 或 "value"）
                    jsValues << sqlLiteral;
                } else { // 数字或其他不需要引号的JS字面量
                    bool okNum;
                    sqlLiteral.toDouble(&okNum);
                    if(okNum) jsValues << sqlLiteral; // 数字直接用
                    else jsValues << "'" + QString(sqlLiteral).replace(QLatin1Char('\''), QLatin1String("\\'")) + "'"; // 其他无引号的视为字符串
                }
            }
        }
        jsExpr += QString("%3[%1].includes(%2)")
                      .arg(jsValues.join(", "), field, (isNotIn ? "!" : ""));
        lastPos = pos + match.capturedLength();
    }
    jsExpr += tempJsExprForIn.mid(lastPos);
    qDebug() << "  [表::CHECK表达式求值] 处理 IN 后的JS表达式: " << jsExpr;

    // 4. 执行表达式
    QString fullJsToEvaluate = QString(
                                   "(function() {"
                                   "  try {"
                                   "    let result = (%1);"
                                   "    if (result === null || typeof result === 'undefined') {"
                                   "      return true; /* SQL UNKNOWN means CHECK constraint passes */"
                                   "    }"
                                   "    return !!result; /* Otherwise, must be true */"
                                   "  } catch (e) {"
                                   "    /* console.warn is not available here, error will be caught by C++ */"
                                   "    return false; /* JS error means CHECK constraint fails */"
                                   "  }"
                                   "})()"
                                   ).arg(jsExpr);

    // qDebug() << "  [表::CHECK表达式求值] 待执行的完整JS函数体:\n" << fullJsToEvaluate; // 调试时可以取消注释
    QJSValue result = engine.evaluate(fullJsToEvaluate);

    if (result.isError()) {
        qWarning() << "  CHECK约束 QJSEngine 执行错误 (engine.evaluate() returned error):" << result.toString()
            << "\n    原始SQL表达式:" << expr
            << "\n    转换后JS表达式主体:" << jsExpr;
        return false;
    }

    bool finalOutcome = result.toBool();
    if(!finalOutcome && !result.isError()){ // 如果JS逻辑的catch块返回了false
        qDebug() << "  [表::CHECK表达式求值] JS表达式 (" << jsExpr << ") 在catch块中返回false或计算结果为false.";
    }
    qDebug() << "  [表::CHECK表达式求值] SQL表达式 '" << expr << "' 的最终计算结果: " << (finalOutcome ? "通过" : "失败");
    return finalOutcome;
}
// 确保其声明和定义匹配。这里我提供一个与之前讨论匹配的签名。
void xhytable::checkUpdateConstraints(const xhyrecord& originalRecord, const QMap<QString, QString>& finalProposedUpdates) const {
    qDebug() << "[表::检查更新约束] 开始对表 '"<< m_name << "' 的更新值进行 CHECK 约束检查。";
    QVariantMap updatedRecordData;

    QMap<QString, QString> originalValues = originalRecord.allValues();
    for (const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();
        if (finalProposedUpdates.contains(fieldName)) {
            updatedRecordData[fieldName] = convertToTypedValue(finalProposedUpdates.value(fieldName), fieldDef.type());
        } else {
            updatedRecordData[fieldName] = convertToTypedValue(originalValues.value(fieldName), fieldDef.type());
        }
    }
    for (const QString& key : finalProposedUpdates.keys()) {
        if (!updatedRecordData.contains(key)) {
            const xhyfield* fieldDef = get_field(key); // 【修复】使用 ->
            if (fieldDef) {
                updatedRecordData[key] = convertToTypedValue(finalProposedUpdates.value(key), fieldDef->type());
            } else {
                qWarning() << "  [表::检查更新约束] 警告: 字段 '" << key << "' 在表定义中未找到，但在更新值中存在。作为字符串处理。";
                updatedRecordData[key] = finalProposedUpdates.value(key);
            }
        }
    }
    qDebug() << "  [表::检查更新约束] 用于 CHECK 表达式的更新后记录数据: " << updatedRecordData;

    for (auto it = m_checkConstraints.constBegin(); it != m_checkConstraints.constEnd(); ++it) {
        const QString& constraintName = it.key();
        const QString& checkExpr = it.value();
        qDebug() << "    检查约束 '" << constraintName << "', 表达式: '" << checkExpr << "'";
        if (!evaluateCheckExpression(checkExpr, updatedRecordData)) {
            QString errorMsg = QString("更新失败: 记录更新后将违反 CHECK 约束 '%1' (表达式: %2).")
                                   .arg(constraintName, checkExpr);
            qWarning() << "  " << errorMsg;
            throw std::runtime_error(errorMsg.toStdString());
        }
    }
    qDebug() << "[表::检查更新约束] 所有 CHECK 约束检查通过。";
}


void xhytable::checkInsertConstraints(const QMap<QString, QString>& fieldValues) const {
    qDebug() << "[表::检查插入约束] 开始对表 '"<< m_name << "' 的插入值进行 CHECK 约束检查: " << fieldValues;
    QVariantMap fullRecordData;

    for (const xhyfield& fieldDef : m_fields) { // 【修复】直接遍历 m_fields
        const QString& fieldName = fieldDef.name();
        if (fieldValues.contains(fieldName)) {
            fullRecordData[fieldName] = convertToTypedValue(fieldValues.value(fieldName), fieldDef.type());
        } else {
            fullRecordData[fieldName] = QVariant();
        }
    }
    for (const QString& key : fieldValues.keys()) {
        if (!fullRecordData.contains(key)) {
            const xhyfield* fieldDef = get_field(key);
            if (fieldDef) {
                fullRecordData[key] = convertToTypedValue(fieldValues.value(key), fieldDef->type());
            } else {
                qWarning() << "  [表::检查插入约束] 警告: 字段 '" << key << "' 在表定义中未找到，但在输入值中存在。作为字符串处理。";
                fullRecordData[key] = fieldValues.value(key);
            }
        }
    }
    qDebug() << "  [表::检查插入约束] 用于 CHECK 表达式的记录数据: " << fullRecordData;

    for (auto it = m_checkConstraints.constBegin(); it != m_checkConstraints.constEnd(); ++it) {
        const QString& constraintName = it.key();
        const QString& checkExpr = it.value();
        qDebug() << "    检查约束 '" << constraintName << "', 表达式: '" << checkExpr << "'";
        if (!evaluateCheckExpression(checkExpr, fullRecordData)) {
            QString errorMsg = QString("插入失败: 记录违反了 CHECK 约束 '%1' (表达式: %2).")
                                   .arg(constraintName, checkExpr);
            qWarning() << "  " << errorMsg;
            throw std::runtime_error(errorMsg.toStdString());
        }
    }
    qDebug() << "[表::检查插入约束] 所有 CHECK 约束检查通过。";
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
                               const QString& constraintNameIn,
                               ForeignKeyDefinition::ReferentialAction onDeleteAction,
                               ForeignKeyDefinition::ReferentialAction onUpdateAction) {
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
    newForeignKey.constraintName = constraintName; // 使用已确定的约束名
    newForeignKey.referenceTable = referencedTable;
    for (int i = 0; i < childColumns.size(); ++i) {
        newForeignKey.columnMappings[childColumns.at(i)] = referencedColumns.at(i);
    }
    newForeignKey.onDeleteAction = onDeleteAction; // 保存 ON DELETE 动作
    newForeignKey.onUpdateAction = onUpdateAction; // 保存 ON UPDATE 动作

    m_foreignKeys.append(newForeignKey);
    qDebug() << "外键 '" << newForeignKey.constraintName << "' (" << childColumns.join(", ")
             << " REFERENCES " << referencedTable << "(" << referencedColumns.join(", ") << "))"
             << " ON DELETE " << (onDeleteAction == ForeignKeyDefinition::CASCADE ? "CASCADE" : "NO ACTION") // 示例输出
             << " ON UPDATE " << (onUpdateAction == ForeignKeyDefinition::CASCADE ? "CASCADE" : "NO ACTION") // 示例输出
             << " 已添加到表 " << m_name;
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

namespace DefaultValueKeywords {
const QString SQL_NULL = "##SQL_NULL##"; // 特殊标记代表 SQL NULL
const QString CURRENT_TIMESTAMP_KW = "##CURRENT_TIMESTAMP##"; // 特殊标记代表 CURRENT_TIMESTAMP
const QString CURRENT_DATE_KW = "##CURRENT_DATE##";
}
bool xhytable::insertData(const QMap<QString, QString>& fieldValuesFromUser) {
    qDebug() << "[表::插入数据] 尝试向表 '" << m_name << "' 插入数据，用户提供的值: " << fieldValuesFromUser;
    QMap<QString, QString> valuesToInsert = fieldValuesFromUser; // 创建一个可修改的副本

    // 步骤 1: 处理所有字段，确定用于验证和插入的最终值。
    // 这包括应用默认值（如果用户未提供该字段的值）。
    for (const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();

        if (valuesToInsert.contains(fieldName)) {
            if (valuesToInsert.value(fieldName).compare("DEFAULT", Qt::CaseInsensitive) == 0) {
                if (m_defaultValues.contains(fieldName)) {
                    QString storedDefault = m_defaultValues.value(fieldName).trimmed();;

                    if (storedDefault == DefaultValueKeywords::SQL_NULL) {
                        valuesToInsert[fieldName] = QString(); // SQL NULL
                    } else if (storedDefault == DefaultValueKeywords::CURRENT_TIMESTAMP_KW) {
                        valuesToInsert[fieldName] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs); // 或 Qt::ISODate
                    } else if (storedDefault == DefaultValueKeywords::CURRENT_DATE_KW) { // <--- 新增处理 CURRENT_DATE_KW
                        valuesToInsert[fieldName] = QDate::currentDate().toString(Qt::ISODate); // 使用当前日期
                    } else {
                        valuesToInsert[fieldName] = storedDefault; // 普通字面量
                    }
                    qDebug() << "  字段 '" << fieldName << "' 使用了 DEFAULT 关键字, 应用解析后的默认值: '" << valuesToInsert[fieldName] << "'";
                } else if (!m_notNullFields.contains(fieldName)) {
                    valuesToInsert[fieldName] = QString();
                    qDebug() << "  字段 '" << fieldName << "' 使用了 DEFAULT 关键字, 允许NULL且无显式默认值, 视作 SQL NULL";
                } else {
                    QString errMessage = QString("字段 '%1' (NOT NULL) 没有默认值，不能使用 DEFAULT 关键字。").arg(fieldName);
                    qWarning() << "[表::插入数据] 向表 '" << m_name << "' 插入数据失败: " << errMessage;
                    throw std::runtime_error(errMessage.toStdString());
                }
            }
            // 如果用户提供了显式值 (不是 'DEFAULT')，该值已在 valuesToInsert 中。
        } else {
            // 用户没有为此字段提供值
            if (m_defaultValues.contains(fieldName)) {
                QString storedDefault = m_defaultValues.value(fieldName);
                if (storedDefault == DefaultValueKeywords::SQL_NULL) {
                    valuesToInsert[fieldName] = QString(); // SQL NULL
                } else if (storedDefault == DefaultValueKeywords::CURRENT_TIMESTAMP_KW) {
                    valuesToInsert[fieldName] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
                }  else if (storedDefault == DefaultValueKeywords::CURRENT_DATE_KW) { // <--- 新增处理 CURRENT_DATE_KW
                    valuesToInsert[fieldName] = QDate::currentDate().toString(Qt::ISODate); // 使用当前日期
                }else {
                    valuesToInsert[fieldName] = storedDefault; // 普通字面量
                }
                qDebug() << "  字段 '" << fieldName << "' 用户未提供, 应用解析后的默认值: '" << valuesToInsert[fieldName] << "'";
            } else {
                // 无用户提供值，无默认值 -> 对于验证视为 SQL NULL
                valuesToInsert[fieldName] = QString();
                qDebug() << "  字段 '" << fieldName << "' 用户未提供且无默认值, 验证时视作 SQL NULL";
            }
        }
    }
    qDebug() << "[表::插入数据] 用于验证和插入的最终值映射: " << valuesToInsert;

    try {
        // validateRecord 将执行所有约束检查，包括 NOT NULL, UNIQUE, 外键, 以及 CHECK。
        validateRecord(valuesToInsert, nullptr); // nullptr 表示这是 INSERT 操作

        // 创建记录对象并用处理后的值填充所有定义的字段
        xhyrecord new_record_obj;
        for (const xhyfield& fieldDef : m_fields) {
            // 确保记录对象包含表定义的每个字段，即使其值为SQL NULL
            new_record_obj.insert(fieldDef.name(), valuesToInsert.value(fieldDef.name()));
        }

        // 将新记录添加到正确的列表 (事务中是 m_tempRecords，否则是 m_records)
        QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;
        if (m_inTransaction && targetRecordsList->isEmpty() && !m_records.isEmpty() && targetRecordsList != &m_records) {
            // 如果在事务中，并且这是事务中的第一个DML操作，确保 m_tempRecords 是 m_records 的副本
            *targetRecordsList = m_records;
            qDebug() << "[表::插入数据] 事务开始，m_tempRecords 已从 m_records 初始化。";
        }
        targetRecordsList->append(new_record_obj);

        qDebug() << "[表::插入数据] 成功插入数据到表 '" << m_name << "'";
        return true;
    } catch (const std::runtime_error& e) {
        qWarning() << "[表::插入数据] 向表 '" << m_name << "' 插入数据失败: " << e.what();
        throw;
    }
}


int xhytable::updateData(const QMap<QString, QString>& updates_with_expressions, const ConditionNode& conditions) {
    qDebug() << "[表::更新数据] 尝试更新表 '" << m_name << "', SET 子句: " << updates_with_expressions;
    int totalAffectedRows = 0;
    QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;

    if (m_inTransaction && targetRecordsList->isEmpty() && !m_records.isEmpty() && targetRecordsList != &m_records) {
        *targetRecordsList = m_records;
        qDebug() << "[表::更新数据] 事务开始，m_tempRecords 已从 m_records 初始化。";
    }

    QList<QPair<int, xhyrecord>> pending_parent_table_updates;
    struct CascadeUpdateTriggerInfo {
        xhyrecord original_parent_record_snapshot;
        xhyrecord updated_parent_record_snapshot;
    };
    QList<CascadeUpdateTriggerInfo> cascade_update_triggers;

    // --- 阶段 1: 收集父表自身的更新 和 潜在的级联触发信息 ---
    for (int i = 0; i < targetRecordsList->size(); ++i) {
        const xhyrecord& originalRecord = targetRecordsList->at(i);

        if (matchConditions(originalRecord, conditions)) {
            qDebug() << "  [表::更新数据] 第 " << i << " 行记录符合WHERE条件。PK提示: '"
                     << originalRecord.value(m_primaryKeys.isEmpty() ? (m_fields.isEmpty() ? "" : m_fields.first().name()) : m_primaryKeys.first()) << "'";

            QMap<QString, QString> proposedNewValuesFromSet = originalRecord.allValues();
            bool anyValueChangedInThisRecord = false;

            for (auto it_update = updates_with_expressions.constBegin(); it_update != updates_with_expressions.constEnd(); ++it_update) {
                const QString& fieldNameToUpdate = it_update.key();
                const QString& valueExpression = it_update.value().trimmed();
                const xhyfield* fieldSchema = get_field(fieldNameToUpdate);

                if (!fieldSchema) {
                    qWarning() << "    更新警告: 表 '" << m_name << "' 中字段 '" << fieldNameToUpdate << "' 不存在。跳过此字段的更新。";
                    continue;
                }
                QString calculatedNewValue;
                if (valueExpression.compare("NULL", Qt::CaseInsensitive) == 0) {
                    calculatedNewValue = QString();
                } else if ((valueExpression.startsWith('\'') && valueExpression.endsWith('\'')) ||
                           (valueExpression.startsWith('"') && valueExpression.endsWith('"'))) {
                    if (valueExpression.length() >= 2) {
                        QString innerStr = valueExpression.mid(1, valueExpression.length() - 2);
                        if (valueExpression.startsWith('\'')) innerStr.replace("''", "'");
                        else innerStr.replace("\"\"", "\"");
                        calculatedNewValue = innerStr;
                    } else {
                        calculatedNewValue = QString("");
                    }
                } else {
                    // 您的 SET 表达式求值逻辑 (此处简化，假设直接是值)
                    // 实际应用中，这里需要一个完整的表达式求值器
                    // calculatedNewValue = evaluateComplexExpression(valueExpression, originalRecord, m_fields);
                    calculatedNewValue = valueExpression; // 假设 valueExpression 是最终值字符串
                    // 如果 valueExpression 是其他列名或算术表达式，需要在此处求值
                    bool isColumnReference = false;
                    for(const auto& f : m_fields) {
                        if(f.name().compare(valueExpression, Qt::CaseInsensitive) == 0) {
                            calculatedNewValue = originalRecord.value(f.name());
                            isColumnReference = true;
                            break;
                        }
                    }
                    if (!isColumnReference) {
                        // 尝试作为简单算术表达式（非常基础的示例）
                        // 比如: salary + 100
                        QRegularExpression arithmeticRe(QString(R"(\b(%1)\b\s*([\+\-\*\/])\s*(\d+(?:\.\d+)?))").arg(QRegularExpression::escape(fieldNameToUpdate)), QRegularExpression::CaseInsensitiveOption);
                        QRegularExpressionMatch arithmeticMatch = arithmeticRe.match(valueExpression);
                        if(arithmeticMatch.hasMatch()){
                            // 简单的处理，实际需要更复杂的表达式解析器
                            // QString baseCol = arithmeticMatch.captured(1); // 应该是 fieldNameToUpdate
                            QString op = arithmeticMatch.captured(2);
                            double operand = arithmeticMatch.captured(3).toDouble();
                            double baseVal = originalRecord.value(fieldNameToUpdate).toDouble();
                            if(op == "+") calculatedNewValue = QString::number(baseVal + operand);
                            else if(op == "-") calculatedNewValue = QString::number(baseVal - operand);
                            // ... 其他操作
                            else calculatedNewValue = valueExpression; // 无法解析则保持原样
                        } else {
                            // 不是简单列引用也不是上述简单算术，保持原样
                            calculatedNewValue = valueExpression;
                        }
                    }
                }

                if (originalRecord.value(fieldNameToUpdate) != calculatedNewValue) {
                    anyValueChangedInThisRecord = true;
                }
                proposedNewValuesFromSet[fieldNameToUpdate] = calculatedNewValue;
            }

            if (!anyValueChangedInThisRecord) {
                continue;
            }

            // 验证提议的父表记录更新
            // 注意：这里的 validateRecord 是针对父表自身的约束（如CHECK, NOT NULL, UNIQUE on parent table）
            // 它不应该因为子表的外键而失败，因为子表的级联还没有发生。
            validateRecord(proposedNewValuesFromSet, &originalRecord, false); // 第三个参数 false 表示这不是级联验证

            xhyrecord updatedRecordObject;
            for(const xhyfield& fieldDef : m_fields) {
                updatedRecordObject.insert(fieldDef.name(), proposedNewValuesFromSet.value(fieldDef.name()));
            }
            pending_parent_table_updates.append(qMakePair(i, updatedRecordObject));

            bool potentiallyReferencedKeyActuallyChanged = false;
            if (m_parentDb) {
                for (const xhytable& otherTable : m_parentDb->tables()) {
                    // if (otherTable.name().compare(this->m_name, Qt::CaseInsensitive) == 0 && &otherTable == this) continue; // 自引用情况特殊
                    for (const auto& fkDef : otherTable.foreignKeys()) {
                        if (fkDef.referenceTable.compare(this->m_name, Qt::CaseInsensitive) == 0) {
                            for (const QString& referencedParentColumn : fkDef.columnMappings.values()) {
                                if (originalRecord.value(referencedParentColumn) != proposedNewValuesFromSet.value(referencedParentColumn)) {
                                    potentiallyReferencedKeyActuallyChanged = true;
                                    break;
                                }
                            }
                        }
                        if (potentiallyReferencedKeyActuallyChanged) break;
                    }
                    if (potentiallyReferencedKeyActuallyChanged) break;
                }
            }
            if (potentiallyReferencedKeyActuallyChanged) {
                cascade_update_triggers.append({originalRecord, updatedRecordObject});
            }
        }
    }

    // --- 阶段 2: 应用父表自身的更新到 targetRecordsList (m_tempRecords 或 m_records) ---
    int parentRowsUpdatedThisCall = 0;
    for (const auto& update_pair : pending_parent_table_updates) {
        targetRecordsList->replace(update_pair.first, update_pair.second);
        parentRowsUpdatedThisCall++;
    }
    if (parentRowsUpdatedThisCall > 0) {
        qDebug() << "[表::更新数据] 表 '" << m_name << "' 中直接更新了 " << parentRowsUpdatedThisCall << " 行记录 (在临时列表)。";
    }
    totalAffectedRows += parentRowsUpdatedThisCall;

    // --- 阶段 3: 执行级联操作 (ON UPDATE CASCADE / ON UPDATE SET NULL) ---
    if (!cascade_update_triggers.isEmpty() && m_parentDb) {
        qDebug() << "[表::更新数据] 开始处理 " << cascade_update_triggers.count() << " 个潜在的ON UPDATE级联触发器...";
        for (const auto& trigger : cascade_update_triggers) {
            const xhyrecord& oldParentRecordState = trigger.original_parent_record_snapshot;
            const xhyrecord& newParentRecordState = trigger.updated_parent_record_snapshot;

            for (xhytable& referencingTable : m_parentDb->tables()) {
                for (const auto& fkDef : referencingTable.foreignKeys()) {
                    if (fkDef.referenceTable.compare(this->m_name, Qt::CaseInsensitive) == 0) {
                        QMap<QString, QString> oldParentKeyValuesForThisFK;
                        QMap<QString, QString> newParentKeyValuesForThisFK;
                        bool isRelevantFK = false;

                        for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                            const QString& referencedParentColumn = it_map.value();
                            QString oldVal = oldParentRecordState.value(referencedParentColumn);
                            QString newVal = newParentRecordState.value(referencedParentColumn);
                            if (oldVal != newVal) isRelevantFK = true;
                            oldParentKeyValuesForThisFK[referencedParentColumn] = oldVal;
                            newParentKeyValuesForThisFK[referencedParentColumn] = newVal;
                        }
                        if (!isRelevantFK) continue;

                        ConditionNode conditionForChildRows;
                        conditionForChildRows.type = ConditionNode::LOGIC_OP;
                        conditionForChildRows.logicOp = "AND";
                        for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                            const QString& childFkColumn = it_map.key();
                            const QString& parentRefColumn = it_map.value();
                            QString oldParentValForCol = oldParentKeyValuesForThisFK.value(parentRefColumn);
                            ConditionNode compNode;
                            compNode.type = ConditionNode::COMPARISON_OP;
                            compNode.comparison.fieldName = childFkColumn;
                            compNode.comparison.operation = "=";
                            const xhyfield* childFkFieldDef = referencingTable.get_field(childFkColumn);
                            if (!childFkFieldDef) throw std::runtime_error("...");
                            compNode.comparison.value = referencingTable.convertToTypedValue(oldParentValForCol, childFkFieldDef->type());
                            conditionForChildRows.children.append(compNode);
                        }

                        if (fkDef.onUpdateAction == ForeignKeyDefinition::CASCADE) {
                            qDebug() << "  [ON UPDATE CASCADE] From table '" << this->m_name << "' to '" << referencingTable.name() << "' (Constraint: " << fkDef.constraintName << ").";
                            QMap<QString, QString> childTableUpdates;
                            for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                                childTableUpdates[it_map.key()] = newParentKeyValuesForThisFK.value(it_map.value());
                            }
                            if (!childTableUpdates.isEmpty()) {
                                // **重要**: 调用子表的 updateData，它内部会调用 validateRecord。
                                // 子表的 validateRecord 需要能正确验证外键。
                                int cascaded_rows = referencingTable.updateData(childTableUpdates, conditionForChildRows);
                                qDebug() << "    级联更新导致表 '" << referencingTable.name() << "' 中更新了 " << cascaded_rows << " 行。";
                                // totalAffectedRows += cascaded_rows; // 考虑是否计入
                            }
                        } else if (fkDef.onUpdateAction == ForeignKeyDefinition::SET_NULL) {
                            qDebug() << "  [ON UPDATE SET NULL] From table '" << this->m_name << "' to '" << referencingTable.name() << "' (Constraint: " << fkDef.constraintName << ").";
                            QMap<QString, QString> childTableUpdatesToNull;
                            bool canSetNull = true;
                            for (const QString& childFkColumn : fkDef.columnMappings.keys()) {
                                const xhyfield* childFkFieldDef = referencingTable.get_field(childFkColumn);
                                if (childFkFieldDef && referencingTable.notNullFields().contains(childFkFieldDef->name())) {
                                    qWarning() << "    ON UPDATE SET NULL 失败: 子表 '" << referencingTable.name() << "' 的外键列 '" << childFkColumn << "' 不允许为NULL。约束 '" << fkDef.constraintName << "'";
                                    throw;
                                    canSetNull = false;
                                    break;
                                }
                                childTableUpdatesToNull[childFkColumn] = QString(); // SQL NULL
                            }
                            if (canSetNull && !childTableUpdatesToNull.isEmpty()) {
                                int set_null_rows = referencingTable.updateData(childTableUpdatesToNull, conditionForChildRows);
                                qDebug() << "    ON UPDATE SET NULL 导致表 '" << referencingTable.name() << "' 中更新了 " << set_null_rows << " 行。";
                            }
                        } else { // NO_ACTION, RESTRICT, SET_DEFAULT (简化)

                            qDebug() << "  [ON UPDATE " << (fkDef.onUpdateAction == ForeignKeyDefinition::NO_ACTION ? "NO ACTION" : "RESTRICT/SET_DEFAULT")
                                     << "] 在表 '" << referencingTable.name() << "' 上对于约束 '" << fkDef.constraintName << "' 无显式级联动作或限制性检查。";
                            // 为确保 RESTRICT 语义，可以主动检查：
                            if (fkDef.onUpdateAction == ForeignKeyDefinition::NO_ACTION) { // 或 RESTRICT
                                const QList<xhyrecord>& childRecords = referencingTable.records(); // 获取子表的当前记录
                                for(const xhyrecord& childRec : childRecords) {
                                    bool matchesOldParentKey = true;
                                    bool childFkHasNull = false;
                                    for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                                        const QString& childFkColumn = it_map.key();
                                        const QString& parentRefColumn = it_map.value();
                                        QString childFkValue = childRec.value(childFkColumn);
                                        if(childFkValue.isNull()) { childFkHasNull = true; break;}
                                        if(childFkValue.compare(oldParentKeyValuesForThisFK.value(parentRefColumn), Qt::CaseSensitive) != 0) {
                                            matchesOldParentKey = false;
                                            break;
                                        }
                                    }
                                    if(childFkHasNull) continue; // 子外键含NULL，不构成有效引用

                                    if(matchesOldParentKey) {
                                        // 找到了一个子记录仍然引用旧的父键，但父键已经改变，且没有级联动作
                                        // 这通常意味着违反了参照完整性（对于 RESTRICT/NO ACTION）
                                        QString err = QString("更新父表操作失败 (ON UPDATE %1): 表 '%2' (子表) 中的记录 (如 %3='%4') 仍引用表 '%5' (父表) 中已更改的键值 (原父键值相关列: %6)。约束: '%7'")
                                                          .arg(fkDef.onUpdateAction == ForeignKeyDefinition::NO_ACTION ? "NO ACTION" : "RESTRICT")
                                                          .arg(referencingTable.name())
                                                          .arg(referencingTable.primaryKeys().isEmpty() ? referencingTable.fields().first().name() : referencingTable.primaryKeys().first())
                                                          .arg(childRec.value(referencingTable.primaryKeys().isEmpty() ? referencingTable.fields().first().name() : referencingTable.primaryKeys().first()))
                                                          .arg(this->m_name)
                                                          .arg(oldParentKeyValuesForThisFK.keys().join(","))
                                                          .arg(fkDef.constraintName);
                                        qWarning() << err;
                                        throw std::runtime_error(err.toStdString());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    qDebug() << "[表::更新数据] 表 '" << m_name << "' 更新操作完成。总影响（直接或间接）大约 " << totalAffectedRows << " 行。";
    return totalAffectedRows;
}






// xhytable.cpp
int xhytable::deleteData(const ConditionNode& conditions) {
    int affectedRows = 0;
    QList<xhyrecord>* targetRecordsList = m_inTransaction ? &m_tempRecords : &m_records;

    if (m_inTransaction && targetRecordsList->isEmpty() && !m_records.isEmpty() && targetRecordsList != &m_records) { // 修正条件
        *targetRecordsList = m_records;
        qDebug() << "[表::删除数据] 事务开始，m_tempRecords 已从 m_records 初始化。";
    }

    QList<int> indicesToRemove;
    QList<xhyrecord> recordsToDeleteForCascadeCheck; // 存储将要删除的记录的副本

    for (int i = 0; i < targetRecordsList->size(); ++i) {
        const xhyrecord& currentRecord = targetRecordsList->at(i);
        if (matchConditions(currentRecord, conditions)) {
            recordsToDeleteForCascadeCheck.append(currentRecord); // 先收集，用于后续处理
            indicesToRemove.append(i);
        }
    }

    // 在实际从当前表删除之前，处理级联删除和 RESTRICT 检查
    if (!m_parentDb) {
        qWarning() << "警告：表 " << m_name << " 缺少对父数据库的引用，无法执行外键删除检查/级联。";
        // 根据您的设计，这里可能应该抛出异常或返回0
        return 0;
    }

    for (const xhyrecord& recordToDelete : recordsToDeleteForCascadeCheck) {
        // 遍历数据库中的所有表，检查它们是否有外键引用到当前表 (this->m_name)
        for (xhytable& potentialReferencingTable : m_parentDb->tables()) { // 需要非 const 引用以调用其 deleteData
            if (potentialReferencingTable.name().compare(this->m_name, Qt::CaseInsensitive) == 0 && &potentialReferencingTable == this) {
                // 不能对自己表有非自引用的外键进行普通级联，自引用级联是特例，但需要小心
                // 此处简化：如果 potentialReferencingTable 就是当前表，我们通常处理自引用约束
                // 但这里主要考虑其他表对本表的引用
            }

            for (const auto& fkDef : potentialReferencingTable.foreignKeys()) {
                if (fkDef.referenceTable.compare(this->m_name, Qt::CaseInsensitive) == 0) {
                    // potentialReferencingTable 中的外键 fkDef 引用了 this->m_name 表
                    // 提取被删除父记录中被引用的键值
                    QMap<QString, QString> parentKeyValuesFromRecordToDelete;
                    bool canBeReferenced = true;
                    for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                        const QString& parentReferencedColumnName = it_map.value();
                        QString val = recordToDelete.value(parentReferencedColumnName);
                        if (val.isNull()) {
                            canBeReferenced = false;
                            break;
                        }
                        parentKeyValuesFromRecordToDelete[parentReferencedColumnName] = val;
                    }
                    if (!canBeReferenced || parentKeyValuesFromRecordToDelete.isEmpty()) {
                        continue;
                    }

                    // 构建子表中需要匹配的条件
                    ConditionNode cascadeCondition;
                    cascadeCondition.type = ConditionNode::LOGIC_OP;
                    cascadeCondition.logicOp = "AND";

                    for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                        const QString& childFkColumnName = it_map.key();
                        const QString& parentReferencedColumnName = it_map.value();
                        QString parentValue = parentKeyValuesFromRecordToDelete.value(parentReferencedColumnName);

                        ConditionNode compNode;
                        compNode.type = ConditionNode::COMPARISON_OP;
                        compNode.comparison.fieldName = childFkColumnName;
                        compNode.comparison.operation = "=";
                        // 注意：这里需要将 parentValue 转换为 QVariant，parseLiteralValue 可能不适用
                        // 假设您的 ConditionNode 和 matchConditions 能正确处理直接的字符串值
                        const xhyfield* childFieldDef = potentialReferencingTable.get_field(childFkColumnName);
                        if (!childFieldDef) {
                            QString errorMsg = QString("级联删除错误：在子表 %1 中未找到外键列 %2")
                                                   .arg(potentialReferencingTable.name())
                                                   .arg(childFkColumnName);
                            throw std::runtime_error(errorMsg.toStdString());
                        }
                        compNode.comparison.value = potentialReferencingTable.convertToTypedValue(parentValue, childFieldDef->type());
                        cascadeCondition.children.append(compNode);
                    }

                    if (fkDef.onDeleteAction == ForeignKeyDefinition::CASCADE) {
                        qDebug() << "[表::删除数据] 检测到 ON DELETE CASCADE 从表 '" << this->m_name << "' 到表 '" << potentialReferencingTable.name() << "' (约束: " << fkDef.constraintName << ").";
                        qDebug() << "  级联删除条件 for '" << potentialReferencingTable.name() << "': " << parentKeyValuesFromRecordToDelete; // 简化输出
                        // 递归删除子表中的匹配记录
                        // 注意：这里直接调用 deleteData，它内部也应该处理级联
                        // 这要求您的事务管理能够覆盖这些嵌套的删除操作
                        int cascaded_deletes = potentialReferencingTable.deleteData(cascadeCondition);
                        qDebug() << "  级联删除导致表 '" << potentialReferencingTable.name() << "' 中删除了 " << cascaded_deletes << " 行。";
                        // affectedRows += cascaded_deletes; // 是否将级联删除的行数计入主删除操作？取决于需求

                    } else if (fkDef.onDeleteAction == ForeignKeyDefinition::SET_NULL) {
                        // 实现 ON DELETE SET NULL
                        qDebug() << "[表::删除数据] 检测到 ON DELETE SET NULL 从表 '" << this->m_name << "' 到表 '" << potentialReferencingTable.name() << "' (约束: " << fkDef.constraintName << ").";
                        QMap<QString, QString> updatesForSetNull;
                        for (const QString& childCol : fkDef.columnMappings.keys()) {
                            updatesForSetNull[childCol] = "NULL"; // 特殊标记或实际的NULL表示
                        }
                        int set_null_updates = potentialReferencingTable.updateData(updatesForSetNull, cascadeCondition);
                        qDebug() << "  ON DELETE SET NULL 导致表 '" << potentialReferencingTable.name() << "' 中更新了 " << set_null_updates << " 行。";

                    } else { // NO_ACTION or RESTRICT (默认行为)
                        // 检查是否存在引用记录
                        QVector<xhyrecord> referencingRecords;
                        potentialReferencingTable.selectData(cascadeCondition, referencingRecords);
                        if (!referencingRecords.isEmpty()) {
                            QString err = QString("删除操作被限制 (ON DELETE %1)：表 '%2' 中的记录通过外键 '%3' 引用了表 '%4' 中即将删除的记录。")
                                              .arg(fkDef.onDeleteAction == ForeignKeyDefinition::NO_ACTION ? "NO ACTION" : "RESTRICT")
                                              .arg(potentialReferencingTable.name())
                                              .arg(fkDef.constraintName)
                                              .arg(this->m_name);
                            throw std::runtime_error(err.toStdString());
                        }
                    }
                }
            }
        }
    }


    // 所有检查和级联操作完成后，再从当前表删除记录
    // 按索引倒序删除，避免因删除导致后续索引失效
    std::sort(indicesToRemove.begin(), indicesToRemove.end(), std::greater<int>());
    for (int index : indicesToRemove) {
        targetRecordsList->removeAt(index);
        affectedRows++;
    }

    if (affectedRows > 0) {
        qDebug() << "[表::删除数据] 表 '" << m_name << "' 中直接删除了 " << affectedRows << " 行。";
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

void xhytable::validateRecord(const QMap<QString, QString>& valuesToValidate,
                              const xhyrecord* original_record_for_update,
                              bool isBeingValidatedDueToCascade) const { // 新增参数，默认为false
    qDebug() << "[表::记录验证] 开始验证表 '" << m_name << "'. 提议的值: " << valuesToValidate
             << (original_record_for_update ? " (更新操作" : " (插入操作")
             << (isBeingValidatedDueToCascade ? ", 由级联触发)" : ")");


    const QList<xhyrecord>& recordsToCheckAgainst = m_inTransaction ? m_tempRecords : m_records;

    // 步骤 1: 字段级固有约束检查 (NOT NULL, 数据类型, ENUM)
    for (const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();
        QString valueForThisField;
        bool valueExplicitlyProvidedOrUpdated = valuesToValidate.contains(fieldName);

        if (valueExplicitlyProvidedOrUpdated) {
            valueForThisField = valuesToValidate.value(fieldName);
        } else if (original_record_for_update) {
            valueForThisField = original_record_for_update->value(fieldName);
        } else {
            if (!valuesToValidate.contains(fieldName)) {
                valueForThisField = QString(); // SQL NULL
            } else {
                valueForThisField = valuesToValidate.value(fieldName);
            }
        }
        bool isConsideredSqlNull = valueForThisField.isNull() ||
                                   (valueExplicitlyProvidedOrUpdated && valuesToValidate.value(fieldName).compare("NULL", Qt::CaseInsensitive) == 0);

        if (m_notNullFields.contains(fieldName)) {
            if (isConsideredSqlNull) {
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (NOT NULL) 不能为 NULL。");
            }
        }
        if (!isConsideredSqlNull) {
            if (!validateType(fieldDef.type(), valueForThisField, fieldDef.constraints())) {
                throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueForThisField.toStdString() +
                                         "' 类型错误或不符合长度/格式约束 (定义类型: " + fieldDef.typestring().toStdString() +")。");
            }
            if (fieldDef.type() == xhyfield::ENUM) {
                if (fieldDef.enum_values().isEmpty() && !valueForThisField.isEmpty()) {
                    throw std::runtime_error("字段 '" + fieldName.toStdString() + "' (ENUM) 的允许值列表为空，无法接受非空值 '" + valueForThisField.toStdString() + "'。");
                } else if (!valueForThisField.isEmpty() && !fieldDef.enum_values().contains(valueForThisField, Qt::CaseSensitive)) {
                    throw std::runtime_error("字段 '" + fieldName.toStdString() + "' 的值 '" + valueForThisField.toStdString() +
                                             "' 不是有效的枚举值。允许的值为: " + fieldDef.enum_values().join(", ").toStdString());
                }
            }
        }
    }

    // 步骤 2: 主键唯一性检查
    if (!m_primaryKeys.isEmpty()) {
        QMap<QString, QString> pkValuesInCurrentOp;
        bool pkHasNull = false;
        bool pkFieldsWereModified = false;

        for (const QString& pkFieldName : m_primaryKeys) {
            // 确保从 valuesToValidate 获取值，因为它包含了最新的提议值
            QString pkFieldValue;
            if (valuesToValidate.contains(pkFieldName)) {
                pkFieldValue = valuesToValidate.value(pkFieldName);
            } else if (original_record_for_update) { // 如果不在更新集合中，则取原值
                pkFieldValue = original_record_for_update->value(pkFieldName);
            } // else 插入时字段未提供，应已被默认值或NULL处理，validateRecord前就已在valuesToValidate

            if (pkFieldValue.isNull() || pkFieldValue.compare("NULL", Qt::CaseInsensitive) == 0) {
                pkHasNull = true; break;
            }
            pkValuesInCurrentOp[pkFieldName] = pkFieldValue;

            if (original_record_for_update != nullptr) {
                if (original_record_for_update->value(pkFieldName).compare(pkValuesInCurrentOp.value(pkFieldName), Qt::CaseSensitive) != 0) {
                    pkFieldsWereModified = true;
                }
            }
        }
        if (pkHasNull) throw std::runtime_error("主键字段 (" + m_primaryKeys.join(", ").toStdString() + ") 不能包含NULL值。");

        int matchCount = 0;
        for (const xhyrecord& existingRecord : recordsToCheckAgainst) {
            // 如果是更新操作，并且当前检查的 existingRecord 就是正在被更新的原始记录，则跳过与自身的比较，
            // 除非主键字段本身被修改了（这种情况下，新主键值不能与任何其他记录冲突）。
            if (original_record_for_update != nullptr && (&existingRecord == original_record_for_update) && !pkFieldsWereModified) {
                continue;
            }

            bool currentExistingMatchesProposedPk = true;
            for (const QString& pkFieldName : m_primaryKeys) {
                if (existingRecord.value(pkFieldName).compare(pkValuesInCurrentOp.value(pkFieldName), Qt::CaseSensitive) != 0) {
                    currentExistingMatchesProposedPk = false;
                    break;
                }
            }
            if (currentExistingMatchesProposedPk) {
                // 无论插入还是更新（且主键改变），只要找到一个匹配就是冲突
                QStringList pkValsForError;
                for(const QString& pkName : m_primaryKeys) pkValsForError << pkValuesInCurrentOp.value(pkName);
                throw std::runtime_error("主键冲突: 值 (" + pkValsForError.join(",").toStdString() + ") 已存在。");
            }
        }
    }


    // 步骤 3: UNIQUE 约束检查
    for (auto it_uq_constr = m_uniqueConstraints.constBegin(); it_uq_constr != m_uniqueConstraints.constEnd(); ++it_uq_constr) {
        const QString& constraintName = it_uq_constr.key();
        const QList<QString>& uniqueFields = it_uq_constr.value();
        QMap<QString, QString> currentUniqueValues;
        bool uniqueKeyHasNull = false;
        bool uniqueKeyFieldsWereModified = false;

        for (const QString& uqFieldName : uniqueFields) {
            QString uqFieldValue;
            if (valuesToValidate.contains(uqFieldName)) {
                uqFieldValue = valuesToValidate.value(uqFieldName);
            } else if (original_record_for_update) {
                uqFieldValue = original_record_for_update->value(uqFieldName);
            }

            if (uqFieldValue.isNull() || uqFieldValue.compare("NULL", Qt::CaseInsensitive) == 0) {
                uniqueKeyHasNull = true; break;
            }
            currentUniqueValues[uqFieldName] = uqFieldValue;
            if (original_record_for_update != nullptr) {
                if (original_record_for_update->value(uqFieldName).compare(currentUniqueValues.value(uqFieldName), Qt::CaseSensitive) != 0) {
                    uniqueKeyFieldsWereModified = true;
                }
            }
        }
        if (uniqueKeyHasNull) continue; // SQL标准：唯一约束允许列中包含多个NULL（除非唯一键是主键）

        for (const xhyrecord& existingRecord : recordsToCheckAgainst) {
            if (original_record_for_update != nullptr && (&existingRecord == original_record_for_update) && !uniqueKeyFieldsWereModified) {
                continue;
            }
            bool currentExistingMatchesProposedUQ = true;
            bool existingUqKeyInDbHasNull = false;
            for (const QString& uqFieldName : uniqueFields) {
                if (existingRecord.value(uqFieldName).isNull() ||
                    existingRecord.value(uqFieldName).compare("NULL", Qt::CaseInsensitive) == 0) {
                    existingUqKeyInDbHasNull = true; break;
                }
                if (existingRecord.value(uqFieldName).compare(currentUniqueValues.value(uqFieldName), Qt::CaseSensitive) != 0) {
                    currentExistingMatchesProposedUQ = false; break;
                }
            }
            if (existingUqKeyInDbHasNull) continue;
            if (currentExistingMatchesProposedUQ) {
                QStringList uqValsForError;
                for(const QString& uqName : uniqueFields) uqValsForError << currentUniqueValues.value(uqName);
                throw std::runtime_error("唯一约束 '" + constraintName.toStdString() + "' 冲突: 值 (" +
                                         uqValsForError.join(",").toStdString() + ") 已存在。");
            }
        }
    }

    // 步骤 4: 外键约束检查
    if (m_parentDb && !m_foreignKeys.isEmpty()) {
        for (const auto& fkDef : m_foreignKeys) {
            const QString& referencedTableName = fkDef.referenceTable;
            QMap<QString, QString> childFkValues;
            bool childFkHasNull = false;

            for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                const QString& childColumnName = it_map.key();
                QString fkValueForChildCol;
                // 从 valuesToValidate 获取最新的提议值用于外键检查
                if (valuesToValidate.contains(childColumnName)) {
                    fkValueForChildCol = valuesToValidate.value(childColumnName);
                } else if (original_record_for_update) { // 如果是更新且该外键列未被SET，则用原值
                    fkValueForChildCol = original_record_for_update->value(childColumnName);
                } // else 插入时字段未提供，则为NULL或默认值，已在valuesToValidate中

                if (fkValueForChildCol.isNull() || fkValueForChildCol.compare("NULL", Qt::CaseInsensitive) == 0) {
                    childFkHasNull = true; break;
                }
                childFkValues[childColumnName] = fkValueForChildCol;
            }
            if (childFkHasNull) continue;

            xhytable* referencedTable = m_parentDb->find_table(referencedTableName); // 父表
            if (!referencedTable) {
                throw std::runtime_error("外键约束 '" + fkDef.constraintName.toStdString() +
                                         "' 定义错误: 引用的父表 '" + referencedTableName.toStdString() + "' 在数据库中不存在。");
            }

            bool foundMatchingParentKey = false;
            // **关键修改**: 查询父表的当前事务状态记录 (m_tempRecords if in transaction)
            const QList<xhyrecord>& parentRecords = referencedTable->records(); // 使用 records() 而不是 getCommittedRecords()

            for (const xhyrecord& parentRecord : parentRecords) {
                bool allParentColsMatch = true;
                for (auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                    const QString& childColumnName = it_map.key();
                    const QString& parentColumnName = it_map.value();
                    if (parentRecord.value(parentColumnName).compare(childFkValues.value(childColumnName), Qt::CaseSensitive) != 0) {
                        allParentColsMatch = false;
                        break;
                    }
                }
                if (allParentColsMatch) {
                    foundMatchingParentKey = true;
                    break;
                }
            }

            if (!foundMatchingParentKey) {
                QStringList childColNamesList, childColValuesList, parentColNamesList;
                for(auto it_map = fkDef.columnMappings.constBegin(); it_map != fkDef.columnMappings.constEnd(); ++it_map) {
                    childColNamesList.append(it_map.key());
                    childColValuesList.append(childFkValues.value(it_map.key()));
                    parentColNamesList.append(it_map.value());
                }
                throw std::runtime_error("外键约束 '" + fkDef.constraintName.toStdString() + "' 冲突: "
                                                                                             "子表字段 (" + childColNamesList.join(", ").toStdString() + ") 的值 (" + childColValuesList.join(", ").toStdString() + ") "
                                                                                                                                                   "在引用的父表 '" + referencedTableName.toStdString() + "' (列 '" + parentColNamesList.join(", ").toStdString() + "') 中不存在对应记录。");
            }
        }
    }

    // 步骤 5: 所有 CHECK 约束检查
    QVariantMap recordDataForCheck;
    for(const xhyfield& fieldDef : m_fields) {
        const QString& fieldName = fieldDef.name();
        QString valueStr;
        if (valuesToValidate.contains(fieldName)) {
            valueStr = valuesToValidate.value(fieldName);
        } else if (original_record_for_update) {
            valueStr = original_record_for_update->value(fieldName);
        } else {
            valueStr = QString();
        }
        recordDataForCheck[fieldName] = convertToTypedValue(valueStr, fieldDef.type());
    }
    for(const QString& key_in_values : valuesToValidate.keys()){
        if(!recordDataForCheck.contains(key_in_values)){
            const xhyfield* fieldDef = get_field(key_in_values);
            if(fieldDef) recordDataForCheck[key_in_values] = convertToTypedValue(valuesToValidate.value(key_in_values), fieldDef->type());
            else recordDataForCheck[key_in_values] = valuesToValidate.value(key_in_values);
        }
    }

    if (!m_checkConstraints.isEmpty()) {
        for (auto it_check = m_checkConstraints.constBegin(); it_check != m_checkConstraints.constEnd(); ++it_check) {
            const QString& constraintName = it_check.key();
            const QString& checkExpr = it_check.value();
            if (!evaluateCheckExpression(checkExpr, recordDataForCheck)) {
                QString opType = original_record_for_update ? "更新" : "插入";
                QString errorMsg = QString("%1操作失败: 记录违反了 CHECK 约束 '%2' (表达式: %3).")
                                       .arg(opType, constraintName, checkExpr);
                throw std::runtime_error(errorMsg.toStdString());
            }
        }
    }
    qDebug() << "[表::记录验证] 表 '" << m_name << "' 的所有约束验证成功。";
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
        } // zyh修改了以下这一段like，然后就能在where里模式匹配了
        else if (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT LIKE", Qt::CaseInsensitive) == 0) {
            QString actualString = actualValue.toString();
            // 确保 cd.value 是字符串或者可以无歧义地转为字符串模式
            if (cd.value.typeId() != QMetaType::QString && !cd.value.canConvert<QString>()) {
                throw std::runtime_error("LIKE 操作符的模式必须是字符串或可转换为字符串。");
            }
            QString likePatternStr = cd.value.toString(); // 这是原始的 SQL LIKE 模式，例如 'a%' 或 '_r%'
            qDebug() << "  LIKE check: actualString='" << actualString << "' likePatternStr='" << likePatternStr << "'";

            // 使用新的辅助函数将 SQL LIKE 模式转换为正确的正则表达式模式
            QString regexPatternStr = sqlLikeToRegex(likePatternStr);
            qDebug() << "    Converted regex pattern: '" << regexPatternStr << "'";

            QRegularExpression pattern(regexPatternStr, QRegularExpression::CaseInsensitiveOption);
            bool matches = pattern.match(actualString).hasMatch(); // match() 会检查整个字符串是否匹配锚定的正则表达式

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

