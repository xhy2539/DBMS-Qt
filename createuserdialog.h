// createuserdialog.h
#ifndef CREATEUSERDIALOG_H
#define CREATEUSERDIALOG_H

#include <QDialog>
// 前向声明：告诉编译器有这些类，但不需要知道它们的完整定义，
class UserFileManager;
class xhydbmanager;

namespace Ui {
class CreateUserDialog;
}

class CreateUserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreateUserDialog(UserFileManager *accountManager, xhydbmanager *dbManager, QWidget *parent = nullptr);
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
    UserFileManager *m_accountManager;
    xhydbmanager *m_dbManager;
};

#endif // CREATEUSERDIALOG_H
