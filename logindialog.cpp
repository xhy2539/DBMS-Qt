#include "logindialog.h"
#include "ui_logindialog.h"
#include "userfilemanager.h"
#include <QFile>
#include <QDataStream>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QDir>

// 二进制文件格式的结构体
/*#pragma pack(push, 1)
struct UserRecord {
    char username[50];      // 用户名 (固定50字节)
    char salt[32];          // 盐值 (固定32字节)
    char password_hash[64]; // 密码哈希 (固定64字节)
    uint8_t role;           // 用户角色
    uint16_t db_count;      // 关联数据库数
};
#pragma pack(pop)*/

LoginDialog::LoginDialog(UserFileManager *userManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoginDialog),
    m_userManager(userManager)
{
    ui->setupUi(this);
    ui->passwordLineEdit->setEchoMode(QLineEdit::Password);
}
//寻找用户文件
QString LoginDialog::findDataFile() {
    QStringList possiblePaths = {
        "data/default_userdata.dat",          // 开发环境
        "../data/default_userdata.dat",       // 一级构建目录
        "../../data/default_userdata.dat",    // 二级构建目录
        "../../../data/default_userdata.dat"  // 三级构建目录
    };

    for (const QString &path : possiblePaths) {
        if (QFile::exists(path)) {
            qDebug() << "Found file at:" << QFileInfo(path).absoluteFilePath();
            return path;
        }
    }
    qWarning() << "File not found in any candidate paths";
    return "";
}

bool LoginDialog::validateUser(const QString &inputUser, const QString &inputPass) {
    // 使用方式
    QString filePath = findDataFile();
    if (filePath.isEmpty()) return false;
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Error: Cannot open file" << filePath;
        qDebug() << "Error detail:" << file.errorString();
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::BigEndian);

    // 读取文件头
    uint32_t magic, userCount;
    in >> magic >> userCount;
    qDebug() << "Magic:" << Qt::hex << magic << "UserCount:" << userCount;

    if (magic != 0x55445246) {
        qDebug() << "Invalid magic number!";
        file.close();
        return false;
    }

    // 读取用户数据
    for (uint32_t i = 0; i < userCount; ++i) {
        char username[50];
        char salt[32];
        char passwordHash[64];  // SHA-512 哈希是64字节
        uint8_t role;
        uint16_t dbCount;

        if (in.readRawData(username, 50) != 50 ||
            in.readRawData(salt, 32) != 32 ||
            in.readRawData(passwordHash, 64) != 64 ||
            in.readRawData(reinterpret_cast<char*>(&role), 1) != 1 ||
            in.readRawData(reinterpret_cast<char*>(&dbCount), 2) != 2) {
            qDebug() << "Error reading user data at index" << i;
            file.close();
            return false;
        }

        QString storedUser = QString::fromUtf8(username, strnlen(username, 50));
        if (storedUser == inputUser) {
            // 关键修改：使用 SHA-512 计算哈希
            QByteArray inputHash = QCryptographicHash::hash(
                inputPass.toUtf8() + QByteArray(salt, 32),
                QCryptographicHash::Sha512  // 改为 SHA-512
                );

            // 调试输出
            qDebug() << "--- Debug Info ---";
            qDebug() << "Username:" << storedUser;
            qDebug() << "Salt (hex):" << QByteArray(salt, 32).toHex();
            qDebug() << "Stored Hash (hex):" << QByteArray(passwordHash, 64).toHex();
            qDebug() << "Computed Hash (hex):" << inputHash.toHex();

            // 比较64字节哈希
            if (inputHash == QByteArray(passwordHash, 64)) {
                qDebug() << "Password is correct!";
                file.close();
                return true;
            } else {
                qDebug() << "Password is incorrect!";
                file.close();
                return false;
            }
        }
    }

    file.close();
    return false;
}
LoginDialog::~LoginDialog()
{
    delete ui;  // 删除UI对象
}
void LoginDialog::on_loginButton_clicked()
{
    QString username = ui->usernameLineEdit->text().trimmed();
    QString password = ui->passwordLineEdit->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "错误", "用户名和密码不能为空");
        return;
    }

    // 调用传入的 UserFileManager 实例的 validateUser 方法进行验证
    if (m_userManager && m_userManager->validateUser(username, password)) {
        QMessageBox::information(this, "登录成功", "欢迎, " + username + "!"); // 可选的成功提示
        accept(); // 登录成功，关闭对话框并返回 Accepted
    } else {
        QMessageBox::warning(this, "登录失败", "用户名或密码错误");
        ui->passwordLineEdit->clear();
        ui->passwordLineEdit->setFocus(); // 让密码输入框获得焦点，方便用户重新输入
    }
}
void LoginDialog::on_cancelButton_clicked()
{
    reject();  // 关闭对话框并返回 QDialog::Rejected
}
QString LoginDialog::getUsername() const
{
    return ui->usernameLineEdit->text();
}
