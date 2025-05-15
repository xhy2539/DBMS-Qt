#ifndef TABLESHOW_H
#define TABLESHOW_H

#include <QWidget>
#include "xhydbmanager.h"
#include <QTableWidgetItem>

namespace Ui {
class tableShow;
}

class tableShow : public QWidget
{
    Q_OBJECT

public:
    explicit tableShow(xhydbmanager * dbms, QString dbName , QString tableName , QWidget *parent = nullptr );
    ~tableShow();
    // void setTable(const xhytable& table);
    void resetButton(bool yes);
    void resetShow();
signals:
    void dataChanged(const QString& sql);

    void refresh();
private slots:
    void on_addRecord_released();

    void on_deleteRecord_released();

    void on_comfirm_released();

    void on_cancle_released();

    void on_refresh_released();

private:
    Ui::tableShow *ui;
    QString m_tableName ;
    QString m_dbName;
    xhydbmanager * m_dbms;
    xhytable * m_table;
    bool adding = false;
};

#endif // TABLESHOW_H
