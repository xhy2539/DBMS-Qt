#ifndef XHYFIELD_H
#define XHYFIELD_H

#include <QString>
#include <QStringList>

class xhyfield {
public:
    enum datatype { INT, VARCHAR, FLOAT, DATE, BOOL, CHAR };

    xhyfield(const QString& name = "",
             datatype type = VARCHAR,
             const QStringList& constraints = {});

    // 字段属性
    QString name() const;
    datatype type() const;
    QStringList constraints() const;

    // 约束解析
    QString checkConstraint() const;
    bool hasCheck() const;
    QString typestring() const;

    // 数据验证
    bool validateValue(const QString& value) const;

private:
    QString m_name;
    datatype m_type;
    QStringList m_constraints;
};

#endif // XHYFIELD_H
