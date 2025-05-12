// createuserdialog.cpp
#include "createuserdialog.h"
#include "ui_createuserdialog.h"
#include <QMessageBox>

CreateUserDialog::CreateUserDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateUserDialog)
{
    ui->setupUi(this);

    // 设置角色选项
    ui->roleComboBox->addItem("只读", 0);
    ui->roleComboBox->addItem("普通用户", 1);
    ui->roleComboBox->addItem("管理员", 2);

    // TODO: 添加数据库列表和权限选择
}

CreateUserDialog::~CreateUserDialog()
{
    delete ui;
}

QString CreateUserDialog::getUsername() const {
    return ui->usernameLineEdit->text().trimmed();
}

QString CreateUserDialog::getPassword() const {
    return ui->passwordLineEdit->text();
}

int CreateUserDialog::getRole() const {
    return ui->roleComboBox->currentData().toInt();
}

QVector<QPair<QString, int>> CreateUserDialog::getDatabasePermissions() const {
    QVector<QPair<QString, int>> permissions;
    // TODO: 实现获取数据库权限
    return permissions;
}

void CreateUserDialog::on_buttonBox_accepted()
{
    if(getUsername().isEmpty() || getPassword().isEmpty()) {
        QMessageBox::warning(this, "错误", "用户名和密码不能为空");
        return;
    }
    accept();
}

void CreateUserDialog::on_buttonBox_rejected()
{
    reject();
}
