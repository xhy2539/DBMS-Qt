#ifndef TABLEDESIGN_H
#define TABLEDESIGN_H

#include <QWidget>

namespace Ui {
class tableDesign;
}

class tableDesign : public QWidget
{
    Q_OBJECT

public:
    explicit tableDesign(QString tableName, QWidget *parent = nullptr);
    ~tableDesign();

signals:
    void tableCreate(QString sql);

private slots:
    void on_deleteField_released();

    void on_addField_released();

    void on_comfirm_released();

private:
    Ui::tableDesign *ui;
    QString dbName;
    QString tableName;
    QStringList typelist={"INT","BIGINT","VARCHAR","CHAR","DECIMAL","DOUBLE","DATE","TIMESTAMP","BOOL"};
    // TINYINT, SMALLINT, INT, BIGINT,
    //     FLOAT, DOUBLE, DECIMAL,
    //     CHAR, VARCHAR, TEXT,
    //     DATE, DATETIME, TIMESTAMP,
    //     BOOL, ENUM
    void setupCheckBoxInTableCell( int row, int column);
    bool getCheckBoxFromCell(int row, int column);
    void addRow(int row);
};

#endif // TABLEDESIGN_H
