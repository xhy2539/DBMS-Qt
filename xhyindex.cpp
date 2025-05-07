#include "xhyindex.h"
#include <QDebug>

xhyindex::xhyindex(const QString& indexName, const QString& tableName)
    : m_indexName(indexName), m_tableName(tableName) {}

bool xhyindex::createIndex(const QString& fieldList) {
    m_indexData.clear(); // 清空现有索引
    m_fields = fieldList.split(","); // 将字段列表分割

    // 假设这里有一个函数可以获取与表相关的记录
    QVector<xhyrecord> records; // 假设获取表的所有记录

    for (int i = 0; i < records.size(); ++i) {
        QString key = "";
        for (const QString& field : m_fields) {
            key += records[i].value(field.trimmed()) + "|"; // 用分隔符连接字段值
        }
        m_indexData[key].append(i); // 将记录的位置存储在索引中
    }

    qDebug() << "索引" << m_indexName << "已创建于表" << m_tableName;
    return true;
}

void xhyindex::dropIndex() {
    m_indexData.clear(); // 删除索引
    qDebug() << "索引" << m_indexName << "已删除";
}

QString xhyindex::showIndex() const {
    QString indexInfo = "索引名称: " + m_indexName + "\n字段: " + m_fields.join(", ");
    return indexInfo;
}

QVector<xhyrecord> xhyindex::findWithIndex(const QString& value) {
    QVector<xhyrecord> results;

    // 使用索引进行查找
    if (m_indexData.contains(value)) {
        for (int index : m_indexData[value]) {
            results.append(/* 根据索引获取记录 */);
        }
    } else {
        qWarning() << "没有找到匹配的记录。";
    }

    return results; // 返回找到的结果
}
