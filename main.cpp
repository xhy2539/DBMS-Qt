#include "mainwindow.h"
#include "logindialog.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create("Fusion"));
    // 登录界面
    LoginDialog login;
    if(login.exec() == QDialog::Accepted) {
        MainWindow w(login.getUsername());
        w.show();
        return a.exec();
    }

    return 0;  // 如果登录失败或取消，直接退出程序
}
