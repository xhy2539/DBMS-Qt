
// sqlparser.h
#ifndef SQLPARSER_H
#define SQLPARSER_H

#include <QString>
#include <QStringList>

class SQLParser {
public:
    static QStringList parseMultiLineSQL(const QString& input);
};

#endif // SQLPARSER_H
