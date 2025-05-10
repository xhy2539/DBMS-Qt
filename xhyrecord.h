#ifndef XHYRECORD_H
#define XHYRECORD_H

#include <QString>
#include <QMap>

class xhyrecord {
public:
    xhyrecord();
    QString value(const QString& field) const;
    void insert(const QString& field, const QString& value);
    //重载比较函数
    bool operator!=(const xhyrecord& other) const {
        return this->m_data != other.m_data;
    }

private:
    QMap<QString, QString> m_data;
};

#endif // XHYRECORD_H
