#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
// 前向声明 UserFileManager 类
class UserFileManager;

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    // 修改构造函数，接收 UserFileManager 的指针
    explicit LoginDialog(UserFileManager *userManager, QWidget *parent = nullptr);
    virtual ~LoginDialog();
    bool validateUser(const QString &inputUser, const QString &inputPass);
    QString getUsername() const;

private slots:
    void on_loginButton_clicked();
    void on_cancelButton_clicked();
private:
    QString findDataFile();
    Ui::LoginDialog *ui;
    UserFileManager *m_userManager; // 添加成员变量来存储传入的 UserFileManager 指针
};

#endif // LOGINDIALOG_H
