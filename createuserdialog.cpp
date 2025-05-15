// createuserdialog.cpp
#include "createuserdialog.h"
#include "ui_createuserdialog.h"
#include "userfilemanager.h"
#include "xhydbmanager.h"
#include <QMessageBox>

CreateUserDialog::CreateUserDialog(UserFileManager *accountManager, xhydbmanager *dbManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateUserDialog),
    m_accountManager(accountManager), // 将传入的 Account 指针存储到成员变量
    m_dbManager(dbManager)           // 将传入的 DatabaseManager 指针存储到成员变量
{
    ui->setupUi(this);

    // 设置角色选项
    ui->roleComboBox->addItem("只读", 0);
    ui->roleComboBox->addItem("普通用户", 1);
    ui->roleComboBox->addItem("管理员", 2);
    // TODO: 添加数据库列表和权限选择
    // 使用传入的 m_dbManager 获取数据库列表并填充到 QTreeWidget
    if (m_dbManager) { // 检查 m_dbManager 指针是否有效
        // 调用 xhydbmanager::databases() 方法获取数据库列表
        QList<xhydatabase> databases = m_dbManager->databases();

        // 遍历数据库列表，为每个数据库创建 QTreeWidget 项
        for(const xhydatabase &db : databases) {
            QTreeWidgetItem *item = new QTreeWidgetItem(ui->databaseTreeWidget);
            item->setText(0, db.name()); // 设置第一列为数据库名称
            item->setCheckState(0, Qt::Unchecked); // 默认不勾选该数据库

            // 为每个数据库项添加权限组合框
            QComboBox *permCombo = new QComboBox();
            permCombo->addItem("只读", 0);           // 对应数据 0
            permCombo->addItem("读写", 1);           // 对应数据 1
            permCombo->addItem("完全控制", 2);       // 对应数据 2
            ui->databaseTreeWidget->setItemWidget(item, 1, permCombo); // 将组合框设置到第二列
        }
    } else {
        qWarning() << "错误: xhydbmanager 指针为空，无法加载数据库列表。";
        // 在这种情况下，您可以考虑向用户显示一个错误消息
        QMessageBox::critical(this, "初始化错误", "无法获取数据库列表。请联系管理员或重启应用程序。");
    }
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

QVector<QPair<QString, int>> CreateUserDialog::getDatabasePermissions() const
{
    QVector<QPair<QString, int>> permissions; // 用于存储最终的数据库权限列表

    // 遍历 QTreeWidget 中的所有顶级项 (也就是每个数据库项)
    for (int i = 0; i < ui->databaseTreeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = ui->databaseTreeWidget->topLevelItem(i); // 获取当前的顶级项 (数据库项)

        // 检查第一列 (索引 0) 的复选框是否被勾选
        if (item->checkState(0) == Qt::Checked) {
            // 如果被勾选，说明用户想为这个数据库设置权限

            QString dbName = item->text(0); // 获取第一列的文本，即数据库名称

            // 获取第二列 (索引 1) 中的控件，它应该是我们之前添加的 QComboBox
            QWidget *widget = ui->databaseTreeWidget->itemWidget(item, 1);
            QComboBox *permCombo = qobject_cast<QComboBox*>(widget); // 尝试将其转换为 QComboBox 指针

            // 检查转换是否成功，以及 QComboBox 是否有效
            if (permCombo) {
                // 如果成功获取了 QComboBox，获取当前选中的项关联的数据
                // 我们在添加 QComboBox 选项时，将权限级别 (0, 1, 2) 存储在了数据中
                int permLevel = permCombo->currentData().toInt();
                // 将数据库名称和选中的权限级别添加到结果列表中
                permissions.append(qMakePair(dbName, permLevel));
            } else {
                qWarning() << "错误: 无法获取数据库 '" << dbName << "' 的权限组合框。";
            }
        }
    }

    return permissions; // 返回收集到的数据库权限列表
}

void CreateUserDialog::on_buttonBox_accepted()
{
    // 1. 输入验证：检查用户名和密码是否为空
    if(getUsername().isEmpty() || getPassword().isEmpty()) {
        QMessageBox::warning(this, "错误", "用户名和密码不能为空");
        return; // 返回，不关闭对话框
    }

    // 2. 获取用户输入数据
    QString username = getUsername();
    QString password = getPassword();

    // 角色：从 int 转换为 uint8_t
    uint8_t role = static_cast<uint8_t>(getRole());

    // 数据库权限：获取 UI 中的权限数据 (QVector<QPair<QString, int>>)
    QVector<QPair<QString, int>> uiPermissions = getDatabasePermissions();

    // 将 UI 权限数据转换为 UserFileManager 所需的格式 (QVector<QPair<QString, uint8_t>>)
    QVector<QPair<QString, uint8_t>> dbPermissionsForUserFileManager;

    for (const QPair<QString, int>& perm : uiPermissions) {
        QString dbName = perm.first;
        int uiPermLevel = perm.second; // UI 中的权限值 (0, 1, 2)
        // 直接将 int 转换为 uint8_t，因为它们现在是直接对应的
        uint8_t userFileManagerPermLevel = static_cast<uint8_t>(uiPermLevel);

        dbPermissionsForUserFileManager.append(qMakePair(dbName, userFileManagerPermLevel));
    }

    // 3. 添加用户
    if (m_accountManager) {
        bool success = m_accountManager->addUser(
            username,
            password,
            role,
            dbPermissionsForUserFileManager // 传递转换后的数据库权限列表
);

        // 4. 根据添加结果显示消息并决定是否关闭对话框
        if (success) {
            QMessageBox::information(this, "成功", "用户注册成功！");
            accept(); // 用户添加成功，关闭对话框
        } else {
            QMessageBox::critical(this, "错误", "用户注册失败，可能原因：用户名已存在、权限设置有误或内部错误。");
            // 注册失败，不关闭对话框，允许用户修改
        }
    } else {
        QMessageBox::critical(this, "错误", "用户管理系统未正确初始化，无法注册用户。");
        // 如果 UserFileManager 未初始化，不关闭对话框
    }
}

void CreateUserDialog::on_buttonBox_rejected()
{
    reject();
}
