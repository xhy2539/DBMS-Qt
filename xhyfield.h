#ifndef XHYFIELD_H
#define XHYFIELD_H

#include <QString>
#include <QStringList>

class xhyfield {
public:
    enum datatype {
        TINYINT, SMALLINT, INT, BIGINT,
        FLOAT, DOUBLE, DECIMAL,
        CHAR, VARCHAR, TEXT,
        DATE, DATETIME, TIMESTAMP,
        BOOL, ENUM
    };

    xhyfield(const QString& name = "",
             datatype type = VARCHAR,
             const QStringList& constraints = {});
    void set_enum_values(const QStringList& values);
    QStringList enum_values() const;
    // 字段属性
    QString name() const;
    datatype type() const;
    QStringList constraints() const;
    int order() const { return m_order; }
    void setOrder(int order) { m_order = order; }
    // 约束解析
    QString checkConstraint() const;
    bool hasCheck() const;
    QString typestring() const;

    // 数据验证
    bool validateValue(const QString& value) const;

private:
     int m_order = 0;
     QStringList m_enum_values;
    QString m_name;
    datatype m_type;
    QStringList m_constraints;
};

#endif // XHYFIELD_H
