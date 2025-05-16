#include <QApplication>
#include <QStyleFactory>
#include <QDebug> // 用于输出调试信息
#include <QDir>
#include <QTCore>
#include "mainwindow.h" // 确保包含了 MainWindow 的头文件
#include "logindialog.h"    // 确保包含了 LoginDialog 的头文件
#include "userfilemanager.h" // 确保包含了 UserFileManager 的头文件

QString findDataFile();
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create("Fusion"));
    UserFileManager userManager(findDataFile());
    LoginDialog login(&userManager);
    if (login.exec() == QDialog::Accepted) {
        // 登录成功
        QString loggedInUsername = login.getUsername();
        qDebug() << "用户登录成功:" << loggedInUsername;
        MainWindow w(loggedInUsername,findDataFile());
        w.show();

        return a.exec();
    } else {
        // 登录失败或被取消
        qDebug() << "登录失败或取消，程序退出。";
        return 0;
    }
}
QString findDataFile() {
    QStringList possiblePaths = {
        "data/default_userdata.dat",
        "../data/default_userdata.dat",
        "../../data/default_userdata.dat",
        "../../../data/default_userdata.dat",
        QCoreApplication::applicationDirPath() + "/data/default_userdata.dat"
    };

    for (const QString &path : possiblePaths) {
        QFileInfo fileInfo(path);
        if (fileInfo.exists() && fileInfo.isFile()) {
            qDebug() << "Found user data file at:" << fileInfo.absoluteFilePath();
            return fileInfo.absoluteFilePath();
        }
    }
    qWarning() << "User data file not found in candidate paths. Will use/create at default AppDataLocation.";
    QDir appDataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    if (!appDataDir.exists("DBMSData")) {
        appDataDir.mkpath("DBMSData");
    }
    QString defaultPath = appDataDir.filePath("DBMSData/default_userdata.dat");
    qDebug() << "Using default user data file path:" << defaultPath;
    return defaultPath;
}
