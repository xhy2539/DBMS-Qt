#include "userfilemanager.h"
#include <QCryptographicHash>
#include <QFile>
#include <QDataStream>
#include <QRandomGenerator>
#include <QDateTime>
#include <QStandardPaths>
#include <QDebug>
#include <QDir>

UserFileManager::UserFileManager(const QString& filename)
    : m_filename(filename.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/userdata.dat" : filename)
{
    initialize();
}

bool UserFileManager::loadUsers()
{
    QFile file(m_filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open user file:" << file.errorString();
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::BigEndian);

    // 验证文件头
    uint32_t magic;
    in >> magic;
    if (magic != 0x55445246) { // "UDRF"
        file.close();
        qWarning() << "Invalid file format";
        return false;
    }

    uint32_t userCount;
    in >> userCount;

    m_users.clear();
    m_userDatabases.clear();

    for (uint32_t i = 0; i < userCount; ++i) {
        UserRecord user;

        // 分段读取确保安全
        if (in.readRawData(user.username, 50) != 50 ||
            in.readRawData(user.salt, 32) != 32 ||
            in.readRawData(user.password_hash, 64) != 64 ||
            in.readRawData(reinterpret_cast<char*>(&user.role), 1) != 1 ||
            in.readRawData(reinterpret_cast<char*>(&user.db_count), 2) != 2) {
            qWarning() << "Corrupted user record at index" << i;
            return false;
        }

        // 读取关联数据库,db count为3 异常
        QVector<DatabasePermission> dbs;
        for (uint16_t j = 0; j < user.db_count; ++j) {
            DatabasePermission db;
            if (in.readRawData(db.db_name, 50) != 50 ||
                in.readRawData(reinterpret_cast<char*>(&db.permissions), 1) != 1) {
                qWarning() << "Corrupted database record for user" << user.username;
                return false;
            }
            dbs.append(db);
        }

        m_users.append(user);
        m_userDatabases[QString::fromUtf8(user.username, strnlen(user.username, 50))] = dbs;
    }

    file.close();
    return true;
}
UserFileManager::~UserFileManager(){
    m_users.clear();
    m_userDatabases.clear();
}

bool UserFileManager::saveUsers()
{
    QFile file(m_filename);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to save user file:" << file.errorString();
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::BigEndian);

    // 写入文件头
    out << static_cast<uint32_t>(0x55445246); // Magic
    out << static_cast<uint32_t>(m_users.size());

    // 写入用户数据
    for (const UserRecord& user : m_users) {
        out.writeRawData(user.username, 50);
        out.writeRawData(user.salt, 32);
        out.writeRawData(user.password_hash, 64);
        out.writeRawData(reinterpret_cast<const char*>(&user.role), 1);
        out.writeRawData(reinterpret_cast<const char*>(&user.db_count), 2);

        // 写入关联数据库
        const auto& dbs = m_userDatabases[QString::fromLatin1(user.username)];
        for (const DatabasePermission& db : dbs) {
            out.writeRawData(db.db_name, 50);
            out.writeRawData(reinterpret_cast<const char*>(&db.permissions), 1);
        }
    }

    file.close();
    return true;
}

bool UserFileManager::addUser(const QString& username,
                              const QString& password,
                              uint8_t role,
                              const QVector<QPair<QString, uint8_t>>& databases)
{
    // 参数验证
    if (username.isEmpty() || password.isEmpty()) {
        qWarning() << "Username or password cannot be empty";
        return false;
    }

    if (role > 2) {
        qWarning() << "Invalid role level:" << role;
        return false;
    }

    // 检查用户是否存在
    for (const UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
            qWarning() << "User already exists:" << username;
            return false;
        }
    }

    // 创建用户记录
    UserRecord newUser;
    memset(&newUser, 0, sizeof(UserRecord));
    strncpy(newUser.username, username.toUtf8().constData(), 49);

    QByteArray salt = generateSalt();
    memcpy(newUser.salt, salt.constData(), 32);

    // 直接存储二进制哈希
    QByteArray hashed = QCryptographicHash::hash(
        password.toUtf8() + salt,
        QCryptographicHash::Sha512
        );
    memcpy(newUser.password_hash, hashed.constData(), 64);
    //调试使用
    qDebug() << "UserFileManager::addUser - User added:" << username;
    qDebug() << "UserFileManager::addUser - Generated Salt (hex):" << salt.toHex();
    qDebug() << "UserFileManager::addUser - Stored Password Hash (hex):" << hashed.toHex();
    newUser.role = role;
    newUser.db_count = static_cast<uint16_t>(databases.size());

    // 添加数据库权限
    QVector<DatabasePermission> dbPermissions;
    for (const auto& db : databases) {
        DatabasePermission perm;
        memset(&perm, 0, sizeof(DatabasePermission));
        strncpy(perm.db_name, db.first.toUtf8().constData(), 49);
        perm.permissions = db.second;
        dbPermissions.append(perm);
    }

    // 保存到内存
    m_users.append(newUser);
    m_userDatabases[username] = dbPermissions;

    return saveUsers();
}

bool UserFileManager::deleteUser(const QString& username)
{
    for (int i = 0; i < m_users.size(); ++i) {
        if (QString::fromUtf8(m_users[i].username, strnlen(m_users[i].username, 50)) == username) {
            m_users.removeAt(i);
            m_userDatabases.remove(username);
            return saveUsers();
        }
    }
    return false;
}

bool UserFileManager::validateUser(const QString& username, const QString& password)
{
    for (const UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
            QByteArray salt(user.salt, 32); // 从 UserRecord 结构体中取出盐值

            // 修正：直接使用 QByteArray 类型来存储哈希结果，避免 QString 转换导致的损坏
            QByteArray hashedPasswordAttempt = hashPassword(password, salt);

            // 从 UserRecord 结构体中取出存储的哈希值（确保它是 QByteArray 类型）
            QByteArray storedHash(user.password_hash, 64); // 64字节的SHA-512哈希

            // 直接比较两个 QByteArray
            return hashedPasswordAttempt == storedHash;
        }
    }
    return false; // 用户名未找到
}

uint8_t UserFileManager::getUserRole(const QString& username) const
{
    // 参数检查
    if (username.isEmpty()) {
        qWarning() << "Username cannot be empty";
        return 0;
    }

    // 查找用户
    auto it = m_userDatabases.find(username);
    if (it != m_userDatabases.end()) {
        // 通过关联的m_users找到对应记录
        for (const UserRecord& user : m_users) {
            if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
                return (user.role <= 2) ? user.role : 0; // 确保角色值有效
            }
        }
    }

    qWarning() << "User not found:" << username;
    return 0; // 默认返回只读权限
}

bool UserFileManager::setUserRole(const QString& username, uint8_t newRole)
{
    if (newRole > 2) {
        qWarning() << "Invalid role level:" << newRole;
        return false;
    }

    for (UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
            user.role = newRole;
            return saveUsers();
        }
    }
    return false;
}

QVector<UserDatabaseInfo> UserFileManager::getUserDatabaseInfo(const QString& username) const
{
    QVector<UserDatabaseInfo> result;

    // 参数检查
    if (username.isEmpty()) {
        qWarning() << "Username cannot be empty";
        return result;
    }

    // 获取原始权限数据
    const QVector<DatabasePermission>& dbs = m_userDatabases.value(username);

    // 转换为更友好的结构
    for (const DatabasePermission& db : dbs) {
        UserDatabaseInfo info;
        info.dbName = QString::fromUtf8(db.db_name, strnlen(db.db_name, 50));
        info.permissions = db.permissions;
        result.append(info);
    }

    return result;
}

bool UserFileManager::addDatabaseToUser(const QString& username, const QString& dbName, uint8_t permissions)
{
    for (UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
            // 检查是否已存在
            auto& dbs = m_userDatabases[username];
            for (const DatabasePermission& db : dbs) {
                if (QString::fromUtf8(db.db_name, strnlen(db.db_name, 50)) == dbName) {
                    return false; // 已存在
                }
            }

            // 添加新数据库权限
            DatabasePermission newDb;
            memset(&newDb, 0, sizeof(DatabasePermission));
            strncpy(newDb.db_name, dbName.toUtf8().constData(), 49);
            newDb.permissions = permissions;

            dbs.append(newDb);
            user.db_count = dbs.size();
            return saveUsers();
        }
    }
    return false;
}

bool UserFileManager::removeDatabaseFromUser(const QString& username, const QString& dbName)
{
    for (UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
            auto& dbs = m_userDatabases[username];
            for (int i = 0; i < dbs.size(); ++i) {
                if (QString::fromUtf8(dbs[i].db_name, strnlen(dbs[i].db_name, 50)) == dbName) {
                    dbs.removeAt(i);
                    user.db_count = dbs.size();
                    return saveUsers();
                }
            }
            break;
        }
    }
    return false;
}

bool UserFileManager::updateDatabasePermissions(const QString& username, const QString& dbName, uint8_t newPermissions)
{
    for (UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {
            auto& dbs = m_userDatabases[username];
            for (DatabasePermission& db : dbs) {
                if (QString::fromUtf8(db.db_name, strnlen(db.db_name, 50)) == dbName) {
                    db.permissions = newPermissions;
                    return saveUsers();
                }
            }
            break;
        }
    }
    return false;
}

bool UserFileManager::removeAllDatabasesFromUser(const QString& username) {
    // 1. 查找用户
    for (UserRecord& user : m_users) {
        if (QString::fromUtf8(user.username, strnlen(user.username, 50)) == username) {

            // 2. 检查是否已有数据库权限
            if (user.db_count == 0) {
                qDebug() << username << "当前无关联数据库";
                return true;
            }

            // 3. 清除内存中的数据
            m_userDatabases[username].clear();
            user.db_count = 0;

            // 4. 保存到文件
            if (!saveUsers()) {
                qCritical() << "保存用户数据失败";
                return false;
            }
            qDebug() << "已移除" << username << "的所有数据库权限";
            return true;
        }
    }

    qWarning() << "未找到用户:" << username;
    return false;
}

QByteArray UserFileManager::hashPassword(const QString& password, const QByteArray& salt) const {
    return QCryptographicHash::hash(
        password.toUtf8() + salt,
        QCryptographicHash::Sha512  // 直接返回二进制数据
        );
}

QByteArray UserFileManager::generateSalt() const
{
    QByteArray salt(32, 0);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(salt.data()), 8);
    return salt;
}

bool UserFileManager::initialize()
{
    // 确保目录存在
    QFileInfo fi(m_filename);
    QDir dir(fi.path());
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 如果文件不存在，创建默认文件
    if (!fi.exists()) {
        QFile file(m_filename);
        if (!file.open(QIODevice::WriteOnly)) {
            qCritical() << "Failed to create user data file:" << file.errorString();
            return false;
        }

        // 创建默认管理员账户
        UserRecord root;
        memset(&root, 0, sizeof(root));
        strncpy(root.username, "root", 49);

        QByteArray salt = generateSalt();
        memcpy(root.salt, salt.constData(), 32);

        // 直接计算二进制哈希（跳过十六进制转换）
        QByteArray hashed = QCryptographicHash::hash(
            "123456" + QByteArray(root.salt, 32),  // 密码 + 盐值
            QCryptographicHash::Sha512
            );
        memcpy(root.password_hash, hashed.constData(), 64); // 正确复制64字节二进制数据
        // 在 UserFileManager::initialize 中，在 memcpy(root.password_hash, ...) 之后
        qDebug() << "UserFileManager::initialize - root user initialized.";
        qDebug() << "UserFileManager::initialize - Salt (hex):" << QByteArray(root.salt, 32).toHex();
        qDebug() << "UserFileManager::initialize - Password Hash (hex):" << QByteArray(root.password_hash, 64).toHex();

        root.role = 2; // 管理员
        root.db_count = 0;

        QDataStream out(&file);
        out.setByteOrder(QDataStream::BigEndian);
        out << static_cast<uint32_t>(0x55445246); // Magic
        out << static_cast<uint32_t>(1);          // 用户数
        out.writeRawData(reinterpret_cast<const char*>(&root), sizeof(root));

        file.close();

        m_users.append(root);
        m_userDatabases["root"] = QVector<DatabasePermission>();
    } else {
        return loadUsers();
    }

    return true;
}
