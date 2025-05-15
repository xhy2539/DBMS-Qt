#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    virtual ~LoginDialog();
    bool validateUser(const QString &inputUser, const QString &inputPass);
    QString getUsername() const;

private slots:
    void on_loginButton_clicked();
    void on_cancelButton_clicked();
private:
    QString findDataFile();
    Ui::LoginDialog *ui;
};

#endif // LOGINDIALOG_H
