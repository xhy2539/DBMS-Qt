#include "xhydatabase.h"
#include <QDebug>
#include <stdexcept> // For std::runtime_error

xhydatabase::xhydatabase(const QString& name) : m_name(name), m_inTransaction(false) {}

QString xhydatabase::name() const {
    return m_name;
}

QList<xhytable>& xhydatabase::tables() {
    return m_tables;
}

const QList<xhytable>& xhydatabase::tables() const {
    return m_tables;
}

xhytable* xhydatabase::find_table(const QString& tablename) {
    for (auto& table : m_tables) {
        if (table.name().compare(tablename, Qt::CaseInsensitive) == 0) {
            return &table;
        }
    }
    return nullptr;
}

const xhytable* xhydatabase::find_table(const QString& tablename) const {
    for (const auto& table : m_tables) {
        if (table.name().compare(tablename, Qt::CaseInsensitive) == 0) {
            return &table;
        }
    }
    return nullptr;
}

bool xhydatabase::has_table(const QString& table_name) const {
    for (const auto& table : m_tables) {
        if (table.name().compare(table_name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool xhydatabase::createtable(const xhytable& table_data_const) {
    if (has_table(table_data_const.name())) {
        qWarning() << "创建表失败：表 '" << table_data_const.name() << "' 在数据库 '" << m_name << "' 中已存在。";
        return false;
    }
    // 创建表对象时，将 this (当前 xhydatabase 实例) 作为父数据库指针传递
    xhytable newTable(table_data_const.name(), this);
    // 使用 table_data_const 的数据（元数据和记录）来初始化 newTable
    // 假设 xhytable::createtable 是一个深拷贝/元数据复制方法
    if (!newTable.createtable(table_data_const)) {
        qWarning() << "通过元数据复制创建表 '" << table_data_const.name() << "' 内部失败。";
        return false;
    }
    // newTable.setParentDb(this); // 另一种设置方式，如果在构造函数中未设置

    m_tables.append(newTable);
    qDebug() << "表 '" << newTable.name() << "' 已成功创建在数据库 '" << m_name << "' 并设置了父数据库引用。";
    return true;
}

void xhydatabase::addTable(xhytable& table) { // 修改为接收引用
    if (has_table(table.name())) {
        qWarning() << "尝试添加已存在的表 '" << table.name() << "' 到数据库 '" << m_name << "'";
        return;
    }
    table.setParentDb(this); // 关键：设置表的父数据库指针
    m_tables.append(table);
    qDebug() << "表 '" << table.name() << "' 已添加到数据库 '" << m_name << "' 并设置了父数据库引用。";
}

bool xhydatabase::droptable(const QString& tablename) {
    for (auto it = m_tables.begin(); it != m_tables.end(); ++it) {
        if (it->name().compare(tablename, Qt::CaseInsensitive) == 0) {
            it = m_tables.erase(it);
            qDebug() << "表 '" << tablename << "' 已从数据库 '" << m_name << "' 中删除。";
            m_indexes.removeIf([&](const xhyindex& idx){ return idx.tableName().compare(tablename, Qt::CaseInsensitive) == 0; });
            return true;
        }
    }
    qWarning() << "删除表失败：表 '" << tablename << "' 在数据库 '" << m_name << "' 中未找到。";
    return false;
}

void xhydatabase::beginTransaction() {
    if (m_inTransaction) {
        qWarning() << "数据库 '" << m_name << "' 已处于事务中，无法重复开始事务。";
        return;
    }
    m_transactionCache = m_tables;
    for(xhytable& table : m_tables) {
        table.beginTransaction();
    }
    m_inTransaction = true;
    qDebug() << "数据库 '" << m_name << "' 事务开始。";
}

void xhydatabase::commit() {
    if (!m_inTransaction) {
        qWarning() << "数据库 '" << m_name << "' 不在事务中，无法提交。";
        return;
    }
    for(xhytable& table : m_tables) {
        table.commit();
    }
    m_transactionCache.clear();
    m_inTransaction = false;
    qDebug() << "数据库 '" << m_name << "' 事务提交。";
    // 实际持久化由 xhydbmanager 在其 commitTransaction 中统一处理
}

void xhydatabase::rollback() {
    if (!m_inTransaction) {
        qWarning() << "数据库 '" << m_name << "' 不在事务中，无需回滚。";
        return;
    }
    m_tables = m_transactionCache; // 从快照恢复表列表
    // 对于 m_tables 中的每个表，它们在 beginTransaction 时其 m_tempRecords 被 m_records 初始化。
    // 它们自己的 rollback 会清空 m_tempRecords 并将 m_inTransaction 设置为 false。
    // 由于 m_tables 列表本身被恢复了，所以内部的表对象也是事务开始时的状态。
    // 我们仍然需要调用每个表的 rollback，以确保它们内部的 m_inTransaction 标志被正确重置。
    for(xhytable& table : m_tables) { // 这里的 m_tables 已经是回滚后的列表
        table.rollback();
    }
    m_transactionCache.clear();
    m_inTransaction = false;
    qDebug() << "数据库 '" << m_name << "' 事务回滚。";
}

// *** 添加 clearTables 的实现 ***
void xhydatabase::clearTables() {
    m_tables.clear();
    m_indexes.clear(); // 如果表被清空，相关的索引也应该清空
    qDebug() << "数据库 '" << m_name << "' 中的所有表和索引已被清除 (内存中)。";
}

void xhydatabase::addTable(const xhytable& table) {
    if (has_table(table.name())) {
        qWarning() << "尝试添加已存在的表 '" << table.name() << "' 到数据库 '" << m_name << "'";
        return;
    }
    m_tables.append(table);
}


bool xhydatabase::insertData(const QString& tablename, const QMap<QString, QString>& fieldValues) {
    xhytable* table = find_table(tablename);
    if (!table) {
        throw std::runtime_error(("表 '" + tablename + "' 在数据库 '" + m_name + "' 中不存在。").toStdString());
    }
    return table->insertData(fieldValues);
}

int xhydatabase::updateData(const QString& tablename,
                            const QMap<QString, QString>& updates,
                            const ConditionNode &conditions) {
    xhytable* table = find_table(tablename);
    if (!table) {
        throw std::runtime_error(("表 '" + tablename + "' 在数据库 '" + m_name + "' 中不存在。").toStdString());
    }
    return table->updateData(updates, conditions);
}

int xhydatabase::deleteData(const QString& tablename,
                            const ConditionNode &conditions) {
    xhytable* table = find_table(tablename);
    if (!table) {
        throw std::runtime_error(("表 '" + tablename + "' 在数据库 '" + m_name + "' 中不存在。").toStdString());
    }
    return table->deleteData(conditions);
}

bool xhydatabase::selectData(const QString& tablename,
                             const ConditionNode &conditions,
                             QVector<xhyrecord>& results) const {
    const xhytable* table = find_table(tablename);
    if (!table) {
        throw std::runtime_error(("表 '" + tablename + "' 在数据库 '" + m_name + "' 中不存在。").toStdString());
    }
    return table->selectData(conditions, results);
}

bool xhydatabase::createIndex(const xhyindex& idx) {
    for (const auto& existing_idx : m_indexes) {
        if (existing_idx.name().compare(idx.name(), Qt::CaseInsensitive) == 0) {
            qWarning() << "创建索引失败：索引 '" << idx.name() << "' 已存在于数据库 '" << m_name << "'";
            return false;
        }
    }
    const xhytable* table = find_table(idx.tableName());
    if (!table) {
        qWarning() << "创建索引失败：表 '" << idx.tableName() << "' 不存在于数据库 '" << m_name << "'";
        return false;
    }
    for(const QString& colName : idx.columns()) {
        if (!table->has_field(colName)) {
            qWarning() << "创建索引失败：列 '" << colName << "' 不存在于表 '" << idx.tableName() << "'";
            return false;
        }
    }
    m_indexes.append(idx);
    qDebug() << "索引 '" << idx.name() << "' 已在表 '" << idx.tableName() << "' 上创建。";
    return true;
}

bool xhydatabase::dropIndex(const QString& indexName) {
    for (auto it = m_indexes.begin(); it != m_indexes.end(); ++it) {
        if (it->name().compare(indexName, Qt::CaseInsensitive) == 0) {
            qDebug() << "索引 '" << it->name() << "' 已从数据库 '" << m_name << "' 中删除。";
            m_indexes.erase(it);
            return true;
        }
    }
    qWarning() << "删除索引失败：索引 '" << indexName << "' 在数据库 '" << m_name << "' 中未找到。";
    return false;
}

const xhyindex* xhydatabase::findIndex(const QString& columnName) const {
    for (const auto& index : m_indexes) {
        if (index.columns().contains(columnName, Qt::CaseInsensitive)) {
            return &index;
        }
    }
    return nullptr;
}
const xhyindex* xhydatabase::findIndexByName(const QString& indexName) const {
    for (const auto& index : m_indexes) {
        if (index.name().compare(indexName, Qt::CaseInsensitive) == 0) {
            return &index;
        }
    }
    return nullptr;
}

QList<xhyindex> xhydatabase::allIndexes() const {
    return m_indexes;
}
