#ifndef XHYINDEX_H
#define XHYINDEX_H
#include <QString>
#include <QMap>
#include <QVector>
#include <QPair>
#include "xhyrecord.h"

class xhyindex {
public:
    xhyindex(const QString& indexName, const QString& tableName);

    // 创建索引
    bool createIndex(const QString& fieldList);

    // 删除索引
    void dropIndex();

    // 显示索引信息
    QString showIndex() const;

    // 使用索引查找
    QVector<xhyrecord> findWithIndex(const QString& value);

private:
    QString m_indexName;       // 索引名称
    QString m_tableName;       // 表名称
    QMap<QString, QVector<int>> m_indexData; // 存储索引数据
    QStringList m_fields;      // 存储索引字段
};


#endif // XHYINDEX_H
