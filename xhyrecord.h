#ifndef XHYRECORD_H
#define XHYRECORD_H

#include <QString>
#include <QMap>

class xhyrecord {
public:
    xhyrecord();
    QString value(const QString& field) const;
    void insert(const QString& field, const QString& value);

private:
    QMap<QString, QString> m_data;
};

#endif // XHYRECORD_H
