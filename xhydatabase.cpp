#include "xhydatabase.h"
#include <QDebug>

xhydatabase::xhydatabase(const QString& name) : m_name(name) {}

QString xhydatabase::name() const {
    return m_name;
}

QList<xhytable> xhydatabase::tables() const {
    return m_tables;
}

xhytable* xhydatabase::find_table(const QString& tablename) {
    for (auto& table : m_tables) {
        if (table.name().toLower() == tablename.toLower()) {
            return &table;
        }
    }
    return nullptr;
}
bool xhydatabase::has_table(const QString& table_name) const {
    for (const auto& table : m_tables) {
        if (table.name() == table_name) {
            return true;
        }
    }
    return false;
}
bool xhydatabase::createtable(const xhytable& table) {
    for (const auto& existing_table : m_tables) {
        if (existing_table.name().toLower() == table.name().toLower()) {
            return false; // 表已存在
        }
    }

    m_tables.append(table);
    return true;
}

bool xhydatabase::droptable(const QString& tablename) {
    for (auto it = m_tables.begin(); it != m_tables.end();) {
        if (it->name().toLower() == tablename.toLower()) {
            it = m_tables.erase(it); // 正确更新迭代器
            qDebug() << "Table" << tablename << "removed from database" << m_name;
            return true;
        } else {
            ++it;
        }
    }
    qDebug() << "Table" << tablename << "not found in database" << m_name;
    return false;
}
// 事务管理方法
void xhydatabase::beginTransaction() {
    if (!m_inTransaction) {
        m_transactionCache = m_tables;
        m_inTransaction = true;
    }
}

void xhydatabase::commit() {
    if (m_inTransaction) {
        m_transactionCache.clear();
        m_inTransaction = false;
    }
}

void xhydatabase::rollback() {
    if (m_inTransaction) {
        m_tables = m_transactionCache;
        m_transactionCache.clear();
        m_inTransaction = false;
    }
}

bool xhydatabase::insertData(const QString& tablename, const QMap<QString, QString>& fieldValues) {
    try {
        for (auto& table : m_tables) {
            if (table.name().toLower() == tablename.toLower()) {
                return table.insertData(fieldValues);
            }
        }
        throw std::runtime_error("表不存在");
    }
    catch (const std::exception& e) {
        qDebug() << "插入数据失败:" << e.what();
        return false;
    }
}

int xhydatabase::updateData(const QString& tablename, const QMap<QString, QString>& updates, const QMap<QString, QString>& conditions) {
    try {
        for (auto& table : m_tables) {
            if (table.name().toLower() == tablename.toLower()) {
                return table.updateData(updates, conditions);
            }
        }
        return 0;
    }
    catch (const std::exception& e) {
        qDebug() << "更新数据失败:" << e.what();
        return 0;
    }
}

int xhydatabase::deleteData(const QString& tablename, const QMap<QString, QString>& conditions) {
    try {
        for (auto& table : m_tables) {
            if (table.name().toLower() == tablename.toLower()) {
                return table.deleteData(conditions);
            }
        }
        return 0;
    }
    catch (const std::exception& e) {
        qDebug() << "删除数据失败:" << e.what();
        return 0;
    }
}

bool xhydatabase::selectData(const QString& tablename, const QMap<QString, QString>& conditions, QVector<xhyrecord>& results) {
    try {
        for (auto& table : m_tables) {
            if (table.name().toLower() == tablename.toLower()) {
                return table.selectData(conditions, results);
            }
        }
        return false;
    }
    catch (const std::exception& e) {
        qDebug() << "查询数据失败:" << e.what();
        results.clear();
        return false;
    }
}
