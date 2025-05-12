// createuserdialog.h
#ifndef CREATEUSERDIALOG_H
#define CREATEUSERDIALOG_H

#include <QDialog>

namespace Ui {
class CreateUserDialog;
}

class CreateUserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateUserDialog(QWidget *parent = nullptr);
    ~CreateUserDialog();

    QString getUsername() const;
    QString getPassword() const;
    int getRole() const;
    QVector<QPair<QString, int>> getDatabasePermissions() const;

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::CreateUserDialog *ui;
};

#endif // CREATEUSERDIALOG_H
