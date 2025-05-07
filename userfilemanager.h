#ifndef USERFILEMANAGER_H
#define USERFILEMANAGER_H

#include <QObject>
#include <QVector>
#include <QMap>
#include <QString>

#pragma pack(push, 1)
struct DatabasePermission {
    char db_name[50];       // 数据库名称
    uint8_t permissions;    // 权限标志
};
//数据库结构体
struct UserDatabaseInfo {
    QString dbName;
    uint8_t permissions;
    QString permissionsString() const {
        switch(permissions) {
        case 0: return "ReadOnly";
        case 1: return "ReadWrite";
        case 2: return "FullAccess";
        default: return "Custom";
        }
    }
};
//用户结构体
struct UserRecord {
    char username[50];      // 用户名
    char salt[32];          // 盐值
    char password_hash[64]; // SHA-512哈希
    uint8_t role;           // 0:只读 1:普通 2:管理员
    uint16_t db_count;      // 关联数据库数量
};
#pragma pack(pop)

class UserFileManager
{
public:
    explicit UserFileManager(const QString& filename = "");

    bool loadUsers();
    bool saveUsers();

    // 用户管理
    bool addUser(const QString& username, const QString& password,uint8_t role,const QVector<QPair<QString, uint8_t>>& databases = {});
    bool deleteUser(const QString& username);
    bool validateUser(const QString& username, const QString& password);

    // 权限管理
    uint8_t getUserRole(const QString& username) const;
    bool setUserRole(const QString& username, uint8_t newRole);

    // 数据库权限管理
    QVector<UserDatabaseInfo> getUserDatabaseInfo(const QString& username) const;
    bool addDatabaseToUser(const QString& username, const QString& dbName, uint8_t permissions);
    bool removeDatabaseFromUser(const QString& username, const QString& dbName);
    bool updateDatabasePermissions(const QString& username, const QString& dbName, uint8_t newPermissions);
    bool removeAllDatabasesFromUser(const QString& username);

private:
    QString m_filename;
    QVector<UserRecord> m_users;
    QMap<QString, QVector<DatabasePermission>> m_userDatabases;

    QByteArray hashPassword(const QString& password, const QByteArray& salt) const;
    QByteArray generateSalt() const;
    bool initialize();
};

#endif // USERFILEMANAGER_H
