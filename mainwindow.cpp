#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "xhyfield.h"
#include "xhyrecord.h"
#include "xhytable.h"
#include "xhydatabase.h"
#include "xhyindex.h"
#include "ConditionNode.h" // 确保这个 include 存在
#include "createuserdialog.h" // <-- 如果要打开注册用户对话框，需要包含此头文件
#include <QMessageBox>
#include <QRegularExpression>
#include <QDebug>
#include <QStack>
#include <QStringView>
#include <QVariant>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <stdexcept> // 包含 stdexcept
#include <limits>    // 用于 std::numeric_limits
#include <QRegularExpressionMatchIterator>
#include <QMenu>          // <-- 添加这一行
#include <QMenuBar>       // <-- 添加这一行
#include <QAction>        // <-- 添加这一行
#include <QInputDialog>
#include "tabledesign.h"


MainWindow::MainWindow(const QString &name,QString path,QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , Account(path)
    , username(name)
{
    ui->setupUi(this);
    setWindowTitle("Mini DBMS");
    userDatabaseInfo=Account.getUserDatabaseInfo(username);
    db_manager.load_databases_from_files();

    //菜单栏（注册账号）
    // 获取或创建主窗口的菜单栏
    QMenuBar *menuBar = this->menuBar(); // QMainWindow::menuBar() 会返回菜单栏指针，如果没有则会自动创建

    // 1. 添加“帮助”菜单及其子项
    QMenu *helpMenu = menuBar->addMenu("帮助(&H)"); // "&H" 是快捷键提示，按Alt+H可以快速打开
    QAction *aboutAction = new QAction("相关(&A)...", this); // <-- 修正后的代码
    helpMenu->addAction(aboutAction);

    // 连接“相关”菜单项的触发信号到显示“关于”对话框的槽函数
    connect(aboutAction, &QAction::triggered, this, [this](){
        QMessageBox::about(this, "关于 Mini DBMS", "Mini DBMS 是一个迷你数据库管理系统。\n\n版本: 1.0\。");
    });

    // 2. 添加“账户”菜单及其子项
    QMenu *accountMenu = menuBar->addMenu("账户(&C)"); // "&C" 是快捷键提示
    QAction *registerAccountAction = new QAction("注册账户(&R)...", this); // <-- 修正后的代码
    accountMenu->addAction(registerAccountAction);

    if (Account.getUserRole(username) < 2) { // 只有权限等于2（管理员）的用户才能看到“注册账户”
        registerAccountAction->setVisible(false); // 隐藏该菜单项
    }

    // 连接“注册账户”菜单项的触发信号到打开注册用户对话框的槽函数
    connect(registerAccountAction, &QAction::triggered, this, &MainWindow::openRegisterUserDialog);

    // ====================== 结束添加菜单栏代码 ======================


    //GUI
    ui->linkButton->setEnabled(false);
    ui->tableButton->setEnabled(false);

    ui->tabWidget->setStyleSheet("QTabWidget::pane { margin: 0px; border: 0px; }");
    tablelist = new tableList();
    viewlist = new viewList();
    functionlist = new functionList();
    querylist = new queryList();
    ui->tabWidget->addTab(tablelist,"对象");

    tabBar = ui->tabWidget->tabBar();

    // 隐藏第一个标签页的关闭按钮（索引为 0）
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }

    dataSearch();
    buildTree();
    // popup = new popupWidget(ui->tabWidget);

    // QHBoxLayout *layout = new QHBoxLayout(ui->widget);
    // QListWidget *list = new QListWidget(ui->widget);
    // layout->addWidget(list);
    // ui->widget->setFixedHeight(ui->tabWidget->height()*2);
    // ui->widget->raise();
    // popup->showPopup();
    connect(ui->treeWidget, &QTreeWidget::itemClicked, this, &MainWindow::handleItemClicked);
    connect(ui->treeWidget, &QTreeWidget::itemDoubleClicked, this, &MainWindow::handleItemDoubleClicked);
    connect(tablelist,&tableList::tableOpen,this ,&MainWindow::openTable);
    connect(tablelist,&tableList::tableDrop,[=](QString dbName, QString sql){
        if(!current_GUI_Db.isEmpty()){
            db_manager.use_database(dbName);
            handleString(sql);
            textBuffer.clear();
            dataSearch();
            buildTree();
            updateList(current_GUI_Db);
        }
    });
    connect(tablelist,&tableList::tableCreate,[=]{
        if(current_GUI_Db.isEmpty()) return;
        QString dbName = current_GUI_Db;
        bool ok;
        QString inputText = QInputDialog::getText(
            this,
            tr("创建表"),
            tr("请输入表名:"),
            QLineEdit::Normal,
            "",
            &ok
        );
        if (ok && !inputText.isEmpty()) {
            // 用户点击确定，且输入不为空
            // qDebug() << "输入内容：" << inputText;
            tableDesign* tabledesign = new tableDesign(inputText);
            ui->tabWidget->addTab(tabledesign,inputText+" @"+dbName);
            ui->tabWidget->setCurrentWidget(tabledesign);
            connect(tabledesign,&tableDesign::tableCreate,[=](QString sql){
                db_manager.use_database(dbName);
                handleString(sql+"\n");

                QString ass = textBuffer.join("");
                qDebug()<<ass;
                if(!ass.isEmpty()){
                    // qDebug()<<ass;
                    if(!ass.contains("创建成功")) {
                        QMessageBox msg;
                        msg.setWindowTitle("错误");
                        msg.setText(ass);
                        msg.setStandardButtons(QMessageBox::Ok);
                        msg.exec();
                    }else{
                        ui->tabWidget->removeTab(ui->tabWidget->currentIndex());
                        ui->tabWidget->setCurrentIndex(0);
                        tabledesign->deleteLater();
                    }
                }

                textBuffer.clear();
                dataSearch();
                buildTree();
                updateList(dbName);
            });
        }
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

//join
QPair<QString, QString> MainWindow::parseQualifiedColumn(const QString& qualifiedName)  {
    QStringList parts = qualifiedName.split('.');
    if (parts.size() == 2) {
        return qMakePair(cleanIdentifier(parts[0].trimmed()), cleanIdentifier(parts[1].trimmed()));
    }
    return qMakePair(QString(), cleanIdentifier(qualifiedName.trimmed()));
}
int MainWindow::visualLength(const QString& str) const { // 注意这里也有 const
    int length = 0;
    for (QChar qc : str) {
        ushort ucs = qc.unicode();
        // CJK 和其他宽字符的 Unicode 范围 (与您在 show_schema 中使用的逻辑一致)
        if ((ucs >= 0x1100 && ucs <= 0x11FF) || // Hangul Jamo
            (ucs >= 0x2E80 && ucs <= 0x2EFF) || // CJK Radicals Supplement
            (ucs >= 0x3000 && ucs <= 0x303F) || // CJK Symbols and Punctuation
            (ucs >= 0x3040 && ucs <= 0x309F) || // Hiragana
            (ucs >= 0x30A0 && ucs <= 0x30FF) || // Katakana
            (ucs >= 0x3130 && ucs <= 0x318F) || // Hangul Compatibility Jamo
            (ucs >= 0x31C0 && ucs <= 0x31EF) || // CJK Strokes
            (ucs >= 0x3200 && ucs <= 0x32FF) || // Enclosed CJK Letters and Months
            (ucs >= 0x3400 && ucs <= 0x4DBF) || // CJK Unified Ideographs Extension A
            (ucs >= 0x4DC0 && ucs <= 0x4DFF) || // Yijing Hexagram Symbols
            (ucs >= 0x4E00 && ucs <= 0x9FFF) || // CJK Unified Ideographs
            (ucs >= 0xA960 && ucs <= 0xA97F) || // Hangul Jamo Extended-A
            (ucs >= 0xAC00 && ucs <= 0xD7AF) || // Hangul Syllables
            (ucs >= 0xF900 && ucs <= 0xFAFF) || // CJK Compatibility Ideographs
            (ucs >= 0xFE30 && ucs <= 0xFE4F) || // CJK Compatibility Forms
            (ucs >= 0xFF00 && ucs <= 0xFFEF)    // Fullwidth Forms
           ) {
            length += 2;
        } else {
            length += 1;
        }
    }
    return length;
}

// 新增一个辅助函数，用于从合并后的记录中安全地获取字段类型
// 这个函数需要在 JOIN 逻辑中被调用，以便 WHERE 子句和 ORDER BY 子句能够正确比较值
xhyfield::datatype MainWindow::getFieldTypeFromJoinedRecord(
    const QString& columnNameOrAlias,
    const xhyrecord& joinedRecord,
    xhytable* table1,
    const QString& table1DisplayName,
    xhytable* table2,
    const QString& table2DisplayName
    )  { // 添加 const
    if (!table1 || !table2) return xhyfield::VARCHAR;

    QPair<QString, QString> pqc = parseQualifiedColumn(columnNameOrAlias);
    QString qualifier = pqc.first;
    QString colName = pqc.second;

    if (!qualifier.isEmpty()) {
        if ((qualifier.compare(table1DisplayName, Qt::CaseInsensitive) == 0 || qualifier.compare(table1->name(), Qt::CaseInsensitive) == 0) && table1->has_field(colName)) {
            return table1->getFieldType(colName);
        } else if ((qualifier.compare(table2DisplayName, Qt::CaseInsensitive) == 0 || qualifier.compare(table2->name(), Qt::CaseInsensitive) == 0) && table2->has_field(colName)) {
            return table2->getFieldType(colName);
        }
    } else { // 无限定符
        bool inT1 = table1->has_field(colName);
        bool inT2 = table2->has_field(colName);
        if (inT1 && !inT2) return table1->getFieldType(colName);
        if (!inT1 && inT2) return table2->getFieldType(colName);
        if (inT1 && inT2) {
            qWarning() << "[getFieldTypeFR] 警告: 列 '" << colName << "' 在两个表中都存在且未限定。返回VARCHAR。";
            return xhyfield::VARCHAR; // 歧义
        }
    }
    // 如果列名本身就是合并后的键名 (如 SELECT * 产生 "t1.col")
    if (joinedRecord.allValues().contains(columnNameOrAlias)) {
        // 再次尝试用 columnNameOrAlias 作为限定名去解析
        QPair<QString, QString> pqcDirect = parseQualifiedColumn(columnNameOrAlias);
        if (!pqcDirect.first.isEmpty()) {
            if ((pqcDirect.first.compare(table1DisplayName, Qt::CaseInsensitive) == 0 || pqcDirect.first.compare(table1->name(), Qt::CaseInsensitive) == 0) && table1->has_field(pqcDirect.second)) {
                return table1->getFieldType(pqcDirect.second);
            } else if ((pqcDirect.first.compare(table2DisplayName, Qt::CaseInsensitive) == 0 || pqcDirect.first.compare(table2->name(), Qt::CaseInsensitive) == 0) && table2->has_field(pqcDirect.second)) {
                return table2->getFieldType(pqcDirect.second);
            }
        }
    }

    qWarning() << "[getFieldTypeFR] 警告: 无法确定列 '" << columnNameOrAlias << "' 的原始类型。返回VARCHAR。";
    return xhyfield::VARCHAR;
}
QString MainWindow::sqlLikeToRegex(const QString& likePattern, QChar customEscapeChar) const {
    // ... (具体实现如前一个回复所示) ...
    QString regexPattern;
    regexPattern += '^';
    const QChar escapeChar = (customEscapeChar == QChar::Null) ? QLatin1Char('\\') : customEscapeChar;
    bool nextCharIsEscaped = false;
    for (int i = 0; i < likePattern.length(); ++i) {
        QChar c = likePattern[i];
        if (nextCharIsEscaped) {
            regexPattern += QRegularExpression::escape(QString(c));
            nextCharIsEscaped = false;
        } else {
            if (c == escapeChar) {
                if (i + 1 < likePattern.length()) nextCharIsEscaped = true;
                else regexPattern += QRegularExpression::escape(QString(c));
            } else if (c == QLatin1Char('%')) regexPattern += ".*";
            else if (c == QLatin1Char('_')) regexPattern += ".";
            else regexPattern += QRegularExpression::escape(QString(c));
        }
    }
    regexPattern += '$';
    return regexPattern;
}



bool MainWindow::matchJoinedRecordConditions(
    const xhyrecord& joinedRecord,
    const ConditionNode& condition,
    xhytable* table1,
    const QString& table1DisplayName,
    xhytable* table2,
    const QString& table2DisplayName
    )  { // 添加 const
    if (!table1 || !table2) {
        qWarning() << "[matchJoinedRecordConditions] 错误: table1 或 table2 指针为空。";
        return false;
    }

    bool result;
    switch (condition.type) {
    case ConditionNode::EMPTY:
        return true;

    case ConditionNode::LOGIC_OP:
        if (condition.children.isEmpty()) {
            return condition.logicOp.compare("AND", Qt::CaseInsensitive) == 0;
        }
        if (condition.logicOp.compare("AND", Qt::CaseInsensitive) == 0) {
            result = true;
            for (const auto& child : condition.children) {
                if (!matchJoinedRecordConditions(joinedRecord, child, table1, table1DisplayName, table2, table2DisplayName)) {
                    result = false;
                    break;
                }
            }
        } else if (condition.logicOp.compare("OR", Qt::CaseInsensitive) == 0) {
            result = false;
            for (const auto& child : condition.children) {
                if (matchJoinedRecordConditions(joinedRecord, child, table1, table1DisplayName, table2, table2DisplayName)) {
                    result = true;
                    break;
                }
            }
        } else {
            // textBuffer.append("错误: 未知的逻辑运算符: " + condition.logicOp); // textBuffer 是非 const 成员，不能在 const 方法中直接修改
            qWarning() << "错误: 未知的逻辑运算符: " << condition.logicOp;
            throw std::runtime_error("未知的逻辑运算符: " + condition.logicOp.toStdString());
        }
        break;

    case ConditionNode::NEGATION_OP:
        if (condition.children.isEmpty()) {
            qWarning() << "错误: NOT 操作符后缺少条件。";
            throw std::runtime_error("NOT 操作符后缺少条件");
        }
        result = !matchJoinedRecordConditions(joinedRecord, condition.children.first(), table1, table1DisplayName, table2, table2DisplayName);
        break;

    case ConditionNode::COMPARISON_OP: {
        const ComparisonDetails& cd = condition.comparison;
        QString fieldNameInCondition = cleanIdentifier(cd.fieldName); // 清理 cd.fieldName

        QString valueStrFromRecord = joinedRecord.value(fieldNameInCondition); // joinedRecord 的键应该是 "t1.col" 格式
        bool keyExistsInJoinedRecord = joinedRecord.allValues().contains(fieldNameInCondition);

        if (!keyExistsInJoinedRecord) { // 严格检查键是否存在
            qDebug() << "[matchJoinedCond] 字段 '" << fieldNameInCondition << "' 在合并记录的键中未找到。";
            if (cd.operation.compare("IS NULL", Qt::CaseInsensitive) == 0) return true;
            if (cd.operation.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) return false;
            return false;
        }

        xhyfield::datatype schemaType = getFieldTypeFromJoinedRecord(
            fieldNameInCondition, joinedRecord, table1, table1DisplayName, table2, table2DisplayName
            );
        QVariant actualValueFromRecord = table1->convertToTypedValue(valueStrFromRecord, schemaType);

        if (cd.operation.compare("IS NULL", Qt::CaseInsensitive) == 0) {
            result = !actualValueFromRecord.isValid() || actualValueFromRecord.isNull() ||
                     (actualValueFromRecord.typeId() == QMetaType::QString && actualValueFromRecord.toString().compare("NULL", Qt::CaseInsensitive) == 0);
        } else if (cd.operation.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) {
            result = actualValueFromRecord.isValid() && !actualValueFromRecord.isNull() &&
                     !(actualValueFromRecord.typeId() == QMetaType::QString && actualValueFromRecord.toString().compare("NULL", Qt::CaseInsensitive) == 0);
        } else {
            if (!actualValueFromRecord.isValid() || actualValueFromRecord.isNull() ||
                (actualValueFromRecord.typeId() == QMetaType::QString && actualValueFromRecord.toString().compare("NULL", Qt::CaseInsensitive) == 0) ) {
                result = false; // 与SQL NULL的比较（非IS NULL/IS NOT NULL）为false
            } else if (cd.operation.compare("IN", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT IN", Qt::CaseInsensitive) == 0) {
                bool found = false;
                for (const QVariant& listItem : cd.valueList) {
                    if (table1->compareQVariants(actualValueFromRecord, listItem, "=")) {
                        found = true; break;
                    }
                }
                result = (cd.operation.compare("IN", Qt::CaseInsensitive) == 0) ? found : !found;
            } else if (cd.operation.compare("BETWEEN", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
                bool isBetween = table1->compareQVariants(actualValueFromRecord, cd.value, ">=") &&
                                 table1->compareQVariants(actualValueFromRecord, cd.value2, "<=");
                result = (cd.operation.compare("BETWEEN", Qt::CaseInsensitive) == 0) ? isBetween : !isBetween;
            } else if (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0 || cd.operation.compare("NOT LIKE", Qt::CaseInsensitive) == 0) {
                QString actualString = actualValueFromRecord.toString();
                if (cd.value.typeId() != QMetaType::QString && !cd.value.canConvert<QString>()) {
                    qWarning() << "错误: LIKE 操作符的模式必须是字符串。";
                    throw std::runtime_error("LIKE 操作符的模式必须是字符串。");
                }
                QString likePatternStr = cd.value.toString();
                QString regexPatternStr = this->sqlLikeToRegex(likePatternStr);
                QRegularExpression pattern(regexPatternStr, QRegularExpression::CaseInsensitiveOption);
                bool matches = pattern.match(actualString).hasMatch();
                result = (cd.operation.compare("LIKE", Qt::CaseInsensitive) == 0) ? matches : !matches;
            } else {
                result = table1->compareQVariants(actualValueFromRecord, cd.value, cd.operation);
            }
        }
        break;
    }
    default:
        qWarning() << "错误: 未知的条件节点类型。";
        throw std::runtime_error("未知的条件节点类型");
    }
    return result;
}


int MainWindow::getDatabaseRole(QString dbname){
    int role=-1;
    QVector<UserDatabaseInfo> userDatabaseInfo;
    userDatabaseInfo=Account.getUserDatabaseInfo(username);
    for(auto it :userDatabaseInfo){
        if(dbname==it.dbName) role=it.permissions;
    }
    return role;
}

QString MainWindow::findDataFile() {
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

void MainWindow::execute_command(const QString& command)
{
    if (command.isEmpty()) return;

    try {
        QString cmdUpper = command.toUpper();

        if (cmdUpper.startsWith("EXPLAIN SELECT")) {
            handleExplainSelect(command);
        } else if (cmdUpper.startsWith("CREATE UNIQUE INDEX")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleCreateIndex(command);
            else textBuffer.append(QString("权限不足"));
        }else if (cmdUpper.startsWith("CREATE INDEX")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleCreateIndex(command);
            else textBuffer.append(QString("权限不足"));
        } else if (cmdUpper.startsWith("DROP INDEX")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleDropIndex(command);
            else textBuffer.append(QString("权限不足"));
        } else if (cmdUpper.startsWith("SHOW INDEXES")) {
            handleShowIndexes(command);
        }else if (cmdUpper.startsWith("CREATE DATABASE")) {
            handleCreateDatabase(command);
        }
        else if (cmdUpper.startsWith("SHOW DATABASES")) {
            show_databases();
        }
        else if (cmdUpper.startsWith("USE")) {
            handleUseDatabase(command);
        }
        else if (cmdUpper.startsWith("CREATE TABLE")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2){
            QString mutableCommand = command;
                handleCreateTable(mutableCommand);}
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("INSERT INTO")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleInsert(command);
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("SHOW TABLES")) {
            show_tables(db_manager.get_current_database());
        }
        else if (cmdUpper.startsWith("DESCRIBE ") || cmdUpper.startsWith("DESC ")) {
            handleDescribe(command);
        }
        else if (cmdUpper.startsWith("DROP DATABASE")) {
            if(Account.getUserRole(username)>0)
            handleDropDatabase(command);
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("DROP TABLE")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleDropTable(command);
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("UPDATE")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleUpdate(command);
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("DELETE FROM")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleDelete(command);
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("SELECT")) {
            handleSelect(command);
        }
        else if (cmdUpper.startsWith("ALTER TABLE")) {
            if(getDatabaseRole(current_db)>0||Account.getUserRole(username)==2)
            handleAlterTable(command);
            else textBuffer.append(QString("权限不足"));
        }
        else if (cmdUpper.startsWith("BEGIN TRANSACTION") || cmdUpper.startsWith("BEGIN")) {
            if (db_manager.beginTransaction()) {
                textBuffer.append("事务开始。");
            } else {
                textBuffer.append("错误: 无法开始事务 (可能已在事务中或当前数据库不支持)。");
            }
        }
        else if (cmdUpper.startsWith("COMMIT")) {
            if (db_manager.commitTransaction()) {
                textBuffer.append("事务提交成功。");
            } else {

                textBuffer.append("错误: 事务提交失败 (可能不在事务中或没有更改)。");
            }
        }
        else if (cmdUpper.startsWith("ROLLBACK")) {
            db_manager.rollbackTransaction();
            textBuffer.append("事务已回滚 (如果存在活动事务)。");
        }
        else {
            textBuffer.append("无法识别的命令: " + command);
        }
    } catch (const std::runtime_error& e) {
        QString errMsg = "运行时错误: " + QString::fromStdString(e.what());
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
            errMsg += " (事务已回滚)";
        }
        textBuffer.append(errMsg);
    } catch (...) {
        QString errMsg = "发生未知类型的严重错误。";
        if (db_manager.isInTransaction()) {
            db_manager.rollbackTransaction();
            errMsg += " (事务已回滚)";
        }
        textBuffer.append(errMsg);
    }
}

void MainWindow::handleCreateDatabase(const QString& command) {
    QRegularExpression re(R"(CREATE\s+DATABASE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        //判断权限
        if(Account.getUserRole(username)<1) {
            textBuffer.clear();
            textBuffer.append(QString("权限不足"));
            return;
        }

        if (db_manager.createdatabase(db_name)) {
            textBuffer.append(QString("数据库 '%1' 创建成功。").arg(db_name));
            //添加数据库
            if(Account.getUserRole(username)!=2){
                Account.addDatabaseToUser(username,db_name,2);
            }
        } else {
            textBuffer.append(QString("错误: 创建数据库 '%1' 失败 (可能已存在或名称无效)。").arg(db_name));
        }
    } else {
        textBuffer.append("语法错误: CREATE DATABASE <数据库名>");
    }
}

void MainWindow::handleUseDatabase(const QString& command) {
    QRegularExpression re(R"(USE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        int role=-1;
        role=getDatabaseRole(db_name);
        if(role<0&&Account.getUserRole(username)<2){
            textBuffer.append(QString("权限不足"));
            return;
        }
        if (db_manager.use_database(db_name)) {
            current_db = db_name;
            textBuffer.append(QString("已切换到数据库 '%1'。").arg(db_name));
        } else {
            textBuffer.append(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        }
    } else {
        textBuffer.append("语法错误: USE <数据库名>");
    }
}

void MainWindow::handleDropDatabase(const QString& command) {
    QRegularExpression re(R"(DROP\s+DATABASE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString db_name = match.captured(1);
        //权限不足
        if(getDatabaseRole(db_name)!=2&&Account.getUserRole(username)!=2){
            textBuffer.append(QString("权限不足"));
            return;
        }
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "确认删除",
                                                                 QString("确定要永久删除数据库 '%1' 及其所有数据吗? 此操作不可恢复!").arg(db_name),
                                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (db_manager.dropdatabase(db_name)) {
                textBuffer.append(QString("数据库 '%1' 已删除。").arg(db_name));
                if (current_db.compare(db_name, Qt::CaseInsensitive) == 0) {
                    current_db.clear();
                    textBuffer.append("注意：当前使用的数据库已被删除。");
                }
            } else {
                textBuffer.append(QString("错误: 删除数据库 '%1' 失败 (可能不存在或文件删除失败)。").arg(db_name));
            }
        } else {
            textBuffer.append("删除数据库操作已取消。");
        }
    } else {
        textBuffer.append("语法错误: DROP DATABASE <数据库名>");
    }
}


// Helper function definition: Extracts parameters like (L), (P), or (P,S)
bool MainWindow::extractParenthesizedParams(const QString& params_str_with_parens, int& p_val, int& s_val, bool& s_specified) {
    p_val = 0;
    s_val = 0;
    s_specified = false;

    QString trimmed_params = params_str_with_parens.trimmed();
    if (!trimmed_params.startsWith('(') || !trimmed_params.endsWith(')')) {
        return false; // 不是以括号包围
    }

    QString inner_params = trimmed_params.mid(1, trimmed_params.length() - 2).trimmed();
    if (inner_params.isEmpty()) {
        return false; // 例如 "()" 括号内为空
    }

    QStringList param_parts = inner_params.split(',', Qt::SkipEmptyParts);
    bool conv_ok_p = false;
    bool conv_ok_s = false;

    if (param_parts.size() >= 1) {
        QString p_str = param_parts[0].trimmed();
        if (!p_str.isEmpty()) {
            p_val = p_str.toInt(&conv_ok_p);
        } else {
            conv_ok_p = false; // 空的部分不是有效数字
        }
    }

    if (param_parts.size() >= 2) {
        QString s_str = param_parts[1].trimmed();
        if (!s_str.isEmpty()) {
            s_val = s_str.toInt(&conv_ok_s);
            if (conv_ok_s) {
                s_specified = true;
            }
        } else {
            conv_ok_s = false; // 空的部分不是有效数字 (对于S)
        }
    }
    return conv_ok_p; // 返回 P 是否成功解析为一个整数
}
// mainwindow.cpp

xhyfield::datatype MainWindow::parseDataTypeAndParams(
    const QString& type_str_input,
    QStringList& auto_generated_constraints,
    QString& out_error_message)
{
    auto_generated_constraints.clear();
    out_error_message.clear();

    QString type_def_str = type_str_input.trimmed();
    QString type_name_upper;
    QString params_in_paren_str; // 存储括号及其内部内容，如 "(10,2)" 或 "('A','B')"

    int paren_start = type_def_str.indexOf('(');
    if (paren_start != -1) {
        type_name_upper = type_def_str.left(paren_start).trimmed().toUpper();
        // 简单检查末尾是否有括号。对于 ENUM，其参数可能复杂，所以允许更宽松的括号内容。
        if (type_def_str.endsWith(')')) {
            params_in_paren_str = type_def_str.mid(paren_start); // 包括括号，例如 "(10,2)"
        } else {
            // 如果类型是 ENUM，允许这种情况，因为 ENUM 的值列表可能包含未正确匹配的括号（由后续 ENUM 值解析处理）
            // 对于其他需要参数的类型，如果括号不匹配，则 params_in_paren_str 将为空，后续逻辑会报错。
            if (type_name_upper != "ENUM") {
                out_error_message = QString("类型 '%1' 的定义中括号不匹配: '%2'").arg(type_name_upper, type_str_input);
                // 让后续逻辑基于空的 params_in_paren_str 来判断是否缺少必要参数
            }
            // params_in_paren_str 保持为空，或者可以尝试更复杂的括号匹配
        }
    } else {
        type_name_upper = type_def_str.toUpper();
    }

    if (type_name_upper.isEmpty()) {
        out_error_message = QString("类型定义为空: '%1'").arg(type_str_input);
        return xhyfield::VARCHAR; // 默认回退
    }

    int p_val = 0;
    int s_val = 0;
    bool s_is_specified = false; // s_val 是否在括号中被明确指定

    if (type_name_upper != "ENUM" && !params_in_paren_str.isEmpty()) {
        if (!extractParenthesizedParams(params_in_paren_str, p_val, s_val, s_is_specified)) {
            // extractParenthesizedParams 返回false意味着括号内不是有效的数字参数P
            // (或者是空的括号内容，例如 "TYPE()")
            if (type_name_upper == "VARCHAR" || type_name_upper == "CHAR" ||
                type_name_upper == "DECIMAL" || type_name_upper == "NUMERIC") {
                // 这些类型如果带有括号，则括号内应有有效参数
                out_error_message = QString("类型 '%1' 的参数 '%2' 格式无效或缺失.").arg(type_name_upper).arg(params_in_paren_str);
                // p_val 会是0 (或extractParenthesizedParams的初始值), 后续类型判断逻辑会处理
            }
        }
    }

    // 类型判断和约束生成
    if (type_name_upper == "INT" || type_name_upper == "INTEGER") return xhyfield::INT;
    if (type_name_upper == "TINYINT") return xhyfield::TINYINT;
    if (type_name_upper == "SMALLINT") return xhyfield::SMALLINT;
    if (type_name_upper == "BIGINT") return xhyfield::BIGINT;
    if (type_name_upper == "FLOAT") return xhyfield::FLOAT;
    if (type_name_upper == "DOUBLE" || type_name_upper == "REAL") return xhyfield::DOUBLE;

    if (type_name_upper == "DECIMAL" || type_name_upper == "NUMERIC") {
        // DECIMAL 必须有参数 P，S 可选（默认为0）
        if (params_in_paren_str.isEmpty() || p_val <= 0 || (s_is_specified && (s_val < 0 || s_val > p_val))) {
            out_error_message = QString("DECIMAL/NUMERIC 定义 '%1' 无效. 期望格式如 DECIMAL(P,S)，其中 P > 0 且 0 <= S <= P.")
                                    .arg(type_str_input);
            return xhyfield::VARCHAR; // 回退
        }
        auto_generated_constraints.append(QString("PRECISION(%1)").arg(p_val));
        auto_generated_constraints.append(QString("SCALE(%1)").arg(s_val)); // s_val 由 extract... 初始化为0（若未指定）
        return xhyfield::DECIMAL;
    }

    if (type_name_upper == "CHAR") {
        if (params_in_paren_str.isEmpty()) { // SQL 标准：CHAR 等同于 CHAR(1)
            p_val = 1;
        } else if (p_val <= 0) { // CHAR() 或 CHAR(0) 或 CHAR(负数) 是无效的
            out_error_message = QString("CHAR 类型在 '%1' 中的长度无效. 长度必须 > 0.").arg(type_str_input);
            return xhyfield::VARCHAR; // 回退
        }
        auto_generated_constraints.append(QString("SIZE(%1)").arg(p_val));
        return xhyfield::CHAR;
    }

    if (type_name_upper == "VARCHAR") {
        if (params_in_paren_str.isEmpty() || p_val <= 0) { // VARCHAR 必须指定长度 > 0
            out_error_message = QString("VARCHAR 类型在 '%1' 中的长度无效或未指定. 长度必须 > 0.").arg(type_str_input);
            return xhyfield::TEXT; // 或者您可以选择返回一个具有非常大默认长度的VARCHAR，或直接报错
        }
        auto_generated_constraints.append(QString("SIZE(%1)").arg(p_val));
        return xhyfield::VARCHAR;
    }

    if (type_name_upper == "TEXT") return xhyfield::TEXT;
    if (type_name_upper == "DATE") return xhyfield::DATE;
    if (type_name_upper == "DATETIME") return xhyfield::DATETIME;
    if (type_name_upper == "TIMESTAMP") return xhyfield::TIMESTAMP;
    if (type_name_upper == "BOOL" || type_name_upper == "BOOLEAN") return xhyfield::BOOL;

    if (type_name_upper == "ENUM") {
        if (params_in_paren_str.isEmpty() || !params_in_paren_str.startsWith('(') || !params_in_paren_str.endsWith(')')) {
            out_error_message = QString("ENUM 类型定义 '%1' 需要括号括起来的值列表，例如 ENUM('a','b').").arg(type_str_input);
            return xhyfield::VARCHAR; // 回退
        }
        // ENUM 值的实际解析（括号内的内容）由调用者 (handleCreateTable/handleAlterTable) 完成
        return xhyfield::ENUM;
    }

    out_error_message = QString("未知数据类型: '%1'.").arg(type_str_input);
    return xhyfield::VARCHAR; // 对未知类型的回退
}
void MainWindow::handleCreateTable(QString& command) { // 保持 QString& 以匹配您原有的函数签名
    QString processedCommand = command.replace(QRegularExpression(R"(\s+)"), " ").trimmed();
    QRegularExpression re(R"(CREATE\s+TABLE\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(processedCommand);

    if (!match.hasMatch()) {
        textBuffer.append("语法错误: CREATE TABLE <表名> (<列定义1> <类型1> [约束], ...)");
        return;
    }
    QString table_name = match.captured(1).trimmed();
    QString fields_and_constraints_str = match.captured(2).trimmed(); // 括号内的所有内容
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("错误: 未选择数据库。请先使用 'USE <数据库名>;' 命令。");
        return;
    }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (db_manager.beginTransaction()) {
            transactionStartedHere = true;
        } else {
            textBuffer.append("错误: 为 CREATE TABLE 操作启动事务失败。");
            return;
        }
    }

    xhytable new_table(table_name); // 为新表创建 xhytable 对象

    // 解析括号内的定义列表 (处理嵌套括号和字符串内的逗号)
    QStringList definitions;
    int parenLevel = 0;
    QString currentDef;
    bool inStringLiteral = false;
    QChar stringChar = ' ';

    // 字段和约束定义分割逻辑 (保持不变)
    for (QChar c : fields_and_constraints_str) {
        if (c == '\'' || c == '"') {
            if (inStringLiteral && c == stringChar) {
                // 检查是否是转义的引号
                if (!currentDef.isEmpty() && currentDef.endsWith('\\')) {
                    currentDef.append(c); // 保留转义的引号和前面的反斜杠
                } else {
                    inStringLiteral = false; // 字符串字面量结束
                    currentDef.append(c); // 包含结束引号
                }
            } else if (!inStringLiteral) {
                inStringLiteral = true; // 字符串字面量开始
                stringChar = c;
                currentDef.append(c); // 包含开始引号
            } else { // 在字符串字面量内部，但不是结束引号或转义引号
                currentDef.append(c);
            }
        } else if (c == '(' && !inStringLiteral) {
            parenLevel++;
            currentDef.append(c);
        } else if (c == ')' && !inStringLiteral) {
            parenLevel--;
            currentDef.append(c);
        } else if (c == ',' && parenLevel == 0 && !inStringLiteral) {
            // 顶层的逗号，表示一个定义项的结束
            definitions.append(currentDef.trimmed());
            currentDef.clear();
        } else {
            currentDef.append(c);
        }
    }
    if (!currentDef.isEmpty()) { // 添加最后一个定义项
        definitions.append(currentDef.trimmed());
    }

    if (definitions.isEmpty()) {
        textBuffer.append("错误: CREATE TABLE 语句中未找到列定义或约束。");
        if (transactionStartedHere) db_manager.rollbackTransaction();
        return;
    }


    for (const QString& def_str_const : definitions) {
        QString def_str = def_str_const.trimmed();
        if (def_str.isEmpty()) continue;


        QString def_str_upper = def_str.toUpper();
        bool isLikelyTableConstraint = false;

        // **【核心修改】** 判断此定义项是否为表级约束
        if (def_str_upper.startsWith("CONSTRAINT")) {
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("PRIMARY KEY")) {
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("UNIQUE") && def_str.contains('(')) {
            // 表级 UNIQUE 约束通常是 UNIQUE (col1, col2) 形式
            // 使用 contains('(') 初步判断，handleTableConstraint 会用正则精确匹配
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("FOREIGN KEY") && def_str.contains('(')) {
            isLikelyTableConstraint = true;
        } else if (def_str_upper.startsWith("CHECK") && def_str.contains('(')) {
            // 表级 CHECK 约束通常是 CHECK (expression) 形式
            isLikelyTableConstraint = true;
        }

        if (isLikelyTableConstraint) {
            qDebug() << "[handleCreateTable] 识别为潜在的表级约束，交由 handleTableConstraint 处理:" << def_str;
            handleTableConstraint(def_str, new_table); // handleTableConstraint 内部有更详细的正则匹配
        } else {
            // 如果不是明确的表级约束开头，则作为列定义处理
            qDebug() << "[handleCreateTable] 识别为列定义:" << def_str;

            // === 以下是您原有的、用于解析单个列定义的完整代码块 ===

            QString working_def_str = def_str.trimmed();
            QString field_name, type_str_full, col_constraints_str;

            int first_space_idx = working_def_str.indexOf(QRegularExpression(R"(\s)"));
            if (first_space_idx == -1) {
                textBuffer.append("错误: 列定义不完整 (缺少类型): '" + def_str + "'.");
                if (transactionStartedHere) db_manager.rollbackTransaction(); return;
            }
            field_name = working_def_str.left(first_space_idx);
            working_def_str = working_def_str.mid(first_space_idx).trimmed();

            // (以下是您原代码中解析 type_str_full 和 col_constraints_str 的复杂逻辑)
            int type_param_paren_open_idx = working_def_str.indexOf('(');
            int first_space_after_potential_type_name = working_def_str.indexOf(QRegularExpression(R"(\s)"));

            if (type_param_paren_open_idx != -1 &&
                (first_space_after_potential_type_name == -1 || type_param_paren_open_idx < first_space_after_potential_type_name)) {
                int balance = 0; int end_of_type_params_idx = -1; bool in_str_lit_param = false; QChar str_char_param = ' ';
                for (int i = type_param_paren_open_idx; i < working_def_str.length(); ++i) {
                    QChar c_loop = working_def_str[i]; // 避免与外层循环的 c 变量冲突
                    if (c_loop == '\'' || c_loop == '"') {
                        if (in_str_lit_param && c_loop == str_char_param) {
                            if (i > 0 && working_def_str[i-1] == '\\') { /* escaped quote */ }
                            else { in_str_lit_param = false; }
                        } else if (!in_str_lit_param) {
                            in_str_lit_param = true; str_char_param = c_loop;
                        }
                    }
                    else if (c_loop == '(' && !in_str_lit_param) balance++;
                    else if (c_loop == ')' && !in_str_lit_param) {
                        balance--;
                        if (balance == 0 && i >= type_param_paren_open_idx) {
                            end_of_type_params_idx = i;
                            break;
                        }
                    }
                }
                if (end_of_type_params_idx != -1) {
                    type_str_full = working_def_str.left(end_of_type_params_idx + 1);
                    col_constraints_str = working_def_str.mid(end_of_type_params_idx + 1).trimmed();
                } else {
                    textBuffer.append(QString("错误: 字段 '%1' 的类型定义中括号不匹配: '%2'").arg(field_name, working_def_str));
                    if (transactionStartedHere) db_manager.rollbackTransaction(); return;
                }
            } else {
                if (first_space_after_potential_type_name != -1) {
                    type_str_full = working_def_str.left(first_space_after_potential_type_name).trimmed();
                    col_constraints_str = working_def_str.mid(first_space_after_potential_type_name).trimmed();
                } else {
                    type_str_full = working_def_str.trimmed(); col_constraints_str = "";
                }
            }

            QStringList auto_generated_type_constraints; QString type_parse_error;
            xhyfield::datatype type = parseDataTypeAndParams(type_str_full, auto_generated_type_constraints, type_parse_error);

            if (!type_parse_error.isEmpty()) {
                textBuffer.append(QString("错误：字段 '%1'：类型 '%2' 解析失败 - %3").arg(field_name, type_str_full, type_parse_error));
                if (transactionStartedHere) db_manager.rollbackTransaction(); return;
            }

            QStringList user_defined_col_constraints = parseConstraints(col_constraints_str); // 解析列级约束字符串
            QStringList final_constraints = auto_generated_type_constraints;
            final_constraints.append(user_defined_col_constraints);
            final_constraints.removeDuplicates();

            qDebug() << "[handleCreateTable] 对于字段 '" << field_name << "', 解析的类型字符串: '" << type_str_full << "'";
            qDebug() << "  自动生成的类型约束:" << auto_generated_type_constraints;
            qDebug() << "  用户定义的列约束:" << user_defined_col_constraints;
            qDebug() << "  最终用于 xhyfield 的约束:" << final_constraints;

            if (type == xhyfield::ENUM) {
                QRegularExpression enum_values_re(R"(ENUM\s*\((.+)\)\s*$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                QRegularExpressionMatch enum_match = enum_values_re.match(type_str_full);
                if (enum_match.hasMatch()) {
                    QString enum_content = enum_match.captured(1).trimmed();
                    QStringList enum_vals_list = parseSqlValues(enum_content); // 解析 ENUM 值
                    if (enum_vals_list.isEmpty() && !enum_content.isEmpty() && enum_content != "''" && enum_content != "\"\"") {
                        textBuffer.append(QString("错误: ENUM 字段 '%1' 的值列表为空或格式错误: %2").arg(field_name, enum_content));
                        if(transactionStartedHere) db_manager.rollbackTransaction(); return;
                    }
                    xhyfield new_field_enum(field_name, type, final_constraints);
                    new_field_enum.set_enum_values(enum_vals_list);
                    new_table.addfield(new_field_enum);
                } else {
                    textBuffer.append(QString("错误: ENUM 字段 '%1' 定义无效。期望格式 ENUM('val1',...). 得到: '%2'").arg(field_name, type_str_full));
                    if (transactionStartedHere) db_manager.rollbackTransaction(); return;
                }
            } else {
                xhyfield new_field(field_name, type, final_constraints);
                new_table.addfield(new_field);
            }
            // === 列定义解析逻辑结束 ===
        }
    } // 结束 for 循环 (遍历 definitions)

    // 检查表是否至少有一个字段（除非它仅包含表级约束，但这种情况比较少见且可能需要进一步逻辑判断）
    if (new_table.fields().isEmpty()) {
        // 进一步检查是否真的没有任何有效的定义（例如，只有错误的表级约束定义）
        // 一个更严格的检查是：如果 definitions 列表不为空，但 new_table.fields() 为空，
        // 并且没有成功添加任何表级约束（这需要 handleTableConstraint 返回状态或 new_table 有方法查询），
        // 则可能意味着所有定义都有问题。
        // 为简单起见，如果字段列表为空，就报错（除非您的设计允许无字段的表）。
        bool hasOnlyTableConstraints = true;
        for(const QString& def : definitions) {
            if (!def.toUpper().startsWith("CONSTRAINT") && !def.toUpper().startsWith("PRIMARY KEY") &&
                !def.toUpper().startsWith("UNIQUE") && !def.toUpper().startsWith("FOREIGN KEY") &&
                !def.toUpper().startsWith("CHECK")) {
                hasOnlyTableConstraints = false;
                break;
            }
        }
        if (!hasOnlyTableConstraints) { // 如果定义中包含非表约束的内容但字段仍为空，说明列定义失败
            textBuffer.append("错误: 表必须至少包含一个有效的列定义。");
            if (transactionStartedHere) db_manager.rollbackTransaction(); return;
        } else if (definitions.isEmpty()){ // definitions 本身就是空的
            textBuffer.append("错误: 未提供任何列定义或表约束。");
            if (transactionStartedHere) db_manager.rollbackTransaction(); return;
        }
        // 如果 hasOnlyTableConstraints 为 true，意味着可能只定义了表级约束而没有列，
        // 这在标准SQL中通常是不允许的，但您的系统是否允许需要看设计。
        // 此处简化，若无字段则报错。
    }

    if (new_table.fields().isEmpty() && !fields_and_constraints_str.contains("CONSTRAINT", Qt::CaseInsensitive)) {
        // 这个检查可能需要调整，因为即使没有显式的列定义，也可能只有表级约束（例如，一个全是约束的表定义，虽然不常见）
        // 但如果`handleTableConstraint`成功添加了主键等，`new_table.primaryKeys()`等就不会为空。
        // 更安全的检查是：if (new_table.fields().isEmpty() && new_table.primaryKeys().isEmpty() && new_table.uniqueConstraints().isEmpty() && ...)
        // 暂时保留原逻辑，因为通常表至少会有一个字段。
        textBuffer.append("Error: Table must have at least one column or a table-level constraint.");
        if (transactionStartedHere) db_manager.rollbackTransaction(); return;
    }


    // 尝试创建表并提交/回滚事务
    if (db_manager.createtable(current_db_name, new_table)) {
        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                textBuffer.append(QString("表 '%1' 在数据库 '%2' 中创建成功。事务已提交。").arg(table_name, current_db_name));
            } else {
                // 理论上 createtable 成功后，commit 应该也成功，除非有外部因素或db_manager内部问题
                textBuffer.append(QString("表 '%1' 创建成功（内存中），但事务提交失败！数据库 '%2' 可能处于不一致状态。").arg(table_name, current_db_name));
                // 可能需要更复杂的错误恢复逻辑
            }
        } else { // 在一个已存在的事务中操作，或者系统不支持事务
            textBuffer.append(QString("表 '%1' 在数据库 '%2' 中创建成功（在现有事务中或无事务模式下）。").arg(table_name, current_db_name));
        }
    } else {
        textBuffer.append(QString("错误: 创建表 '%1' 失败 (可能已存在，或定义无效，或表级约束解析失败)。").arg(table_name));
        if (transactionStartedHere) db_manager.rollbackTransaction();
    }
}

void MainWindow::handleDropTable(const QString& command) {
    QRegularExpression re(R"(DROP\s+TABLE\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString table_name = match.captured(1);
        QString current_db_name = db_manager.get_current_database();
        if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "确认删除表",
                                                                 QString("确定要永久删除表 '%1' 吗? 此操作不可恢复!").arg(table_name),
                                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (db_manager.droptable(current_db_name, table_name)) {
                textBuffer.append(QString("表 '%1' 已从数据库 '%2' 删除。").arg(table_name, current_db_name));
            } else {
                textBuffer.append(QString("错误: 删除表 '%1' 失败 (可能不存在)。").arg(table_name));
            }
        } else {
            textBuffer.append("删除表操作已取消。");
        }
    } else {
        textBuffer.append("语法错误: DROP TABLE <表名>");
    }
}

void MainWindow::handleDescribe(const QString& command) {
    QRegularExpression re(R"((?:DESCRIBE|DESC)\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command);
    if (match.hasMatch()) {
        QString table_name = match.captured(1);
        QString current_db_name = db_manager.get_current_database();
        if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
        show_schema(current_db_name, table_name);
    } else {
        textBuffer.append("语法错误: DESCRIBE <表名> 或 DESC <表名>");
    }
}

// mainwindow.cpp

void MainWindow::handleInsert(const QString& command) {
    QRegularExpression re(
        R"(INSERT\s+INTO\s+([\w_]+)\s*(?:\(([^)]+)\))?\s*VALUES\s*((?:\([^)]*\)\s*,?\s*)+)\s*;?)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch main_match = re.match(command.trimmed());

    if (!main_match.hasMatch()) {
        textBuffer.append("Syntax Error: INSERT INTO TableName [(col1,...)] VALUES (val1,...)[, (valA,...)];");
        return;
    }

    QString table_name = main_match.captured(1).trimmed();
    QString fields_part = main_match.captured(2).trimmed();
    QString all_value_groups_string = main_match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("Error: No database selected. Use 'USE <database_name>;' first.");
        return;
    }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (!db_manager.beginTransaction()) {
            textBuffer.append("Warning: Could not start a new transaction. Operation will proceed without explicit transaction control from here.");
            // transactionStartedHere remains false
        } else {
            transactionStartedHere = true;
        }
    }

    try {
        QStringList specified_fields;
        if (!fields_part.isEmpty()) {
            specified_fields = fields_part.split(',', Qt::SkipEmptyParts);
            for (QString& field : specified_fields) field = field.trimmed();
        }

        QList<QStringList> rows_of_values;
        QRegularExpression value_group_re(R"(\(([^)]*)\))");
        QRegularExpressionMatchIterator it_val_groups = value_group_re.globalMatch(all_value_groups_string);
        while (it_val_groups.hasNext()) {
            QRegularExpressionMatch m = it_val_groups.next();
            rows_of_values.append(parseSqlValues(m.captured(1))); // parseSqlValues handles 'val1','val2',...
        }

        if (rows_of_values.isEmpty()) {
            throw std::runtime_error("No valid value groups found in VALUES clause.");
        }

        int total_inserted_successfully = 0;
        bool all_rows_processed_successfully = true;

        for (const QStringList& single_row_values : rows_of_values) {
            QMap<QString, QString> field_value_map;
            if (specified_fields.isEmpty()) { // INSERT INTO Table VALUES (val1, val2)
                xhydatabase* db = db_manager.find_database(current_db_name);
                if (!db) throw std::runtime_error(("Database '" + current_db_name + "' not found.").toStdString());
                const xhytable* table_ptr = db->find_table(table_name); // Use const version for introspection
                if (!table_ptr) throw std::runtime_error(("Table '" + table_name + "' not found in database '" + current_db_name + "'.").toStdString());

                const auto& table_fields_ordered = table_ptr->fields(); // Assuming fields() returns in defined order
                if (single_row_values.size() != table_fields_ordered.size()) {
                    throw std::runtime_error(QString("Value count (%1) does not match column count (%2) for table '%3'.")
                                                 .arg(single_row_values.size()).arg(table_name).arg(table_fields_ordered.size()).toStdString());
                }
                for (int i = 0; i < table_fields_ordered.size(); ++i) {
                    field_value_map[table_fields_ordered[i].name()] = single_row_values[i];
                }
            } else { // INSERT INTO Table (col1, col2) VALUES (val1, val2)
                if (single_row_values.size() != specified_fields.size()) {
                    throw std::runtime_error(QString("Value count (%1) does not match specified column count (%2).")
                                                 .arg(single_row_values.size()).arg(specified_fields.size()).toStdString());
                }
                for (int i = 0; i < specified_fields.size(); ++i) {
                    field_value_map[specified_fields[i]] = single_row_values[i];
                }
            }

            if (db_manager.insertData(current_db_name, table_name, field_value_map)) {
                total_inserted_successfully++;
            } else {
                all_rows_processed_successfully = false;
                // Error message already printed by db_manager.insertData or underlying calls
                if (rows_of_values.size() > 1) {
                    textBuffer.append(QString("Warning: One of the rows for table '%1' failed to insert. Continuing with other rows if any...").arg(table_name));
                    // To stop on first error: break; or throw std::runtime_error("Multi-row insert failed.");
                } else {
                    // Single row insert failed, error already printed.
                }
            }
        } // End for each row

        if (transactionStartedHere) {
            if (all_rows_processed_successfully) {
                if (db_manager.commitTransaction()) {
                    textBuffer.append(QString("%1 row(s) successfully inserted into '%2' and transaction committed.")
                                                       .arg(total_inserted_successfully).arg(table_name));
                } else {
                    // This case implies data was inserted into memory structures but commit to persistent storage failed.
                    textBuffer.append(QString("CRITICAL Error: %1 row(s) inserted into '%2' (in memory), but transaction COMMIT FAILED. Database may be inconsistent.")
                                                       .arg(total_inserted_successfully).arg(table_name));
                }
            } else { // Some rows failed
                db_manager.rollbackTransaction();
                textBuffer.append(QString("Insert operation for '%1' had errors (attempted %2 row(s), successfully inserted %3). Transaction rolled back.")
                                                   .arg(table_name).arg(rows_of_values.size()).arg(total_inserted_successfully));
            }
        } else { // Not in a transaction started by this function
            if (all_rows_processed_successfully) {
                textBuffer.append(QString("%1 row(s) successfully processed for '%2' (within existing transaction or no transaction).")
                                                   .arg(total_inserted_successfully).arg(table_name));
            } else {
                textBuffer.append(QString("Insert operation for '%1' had errors (attempted %2 row(s), successfully inserted %3) (within existing transaction or no transaction).")
                                                   .arg(table_name).arg(rows_of_values.size()).arg(total_inserted_successfully));
            }
        }

    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("Insert Data Runtime Error: " + QString::fromStdString(e.what()) + (transactionStartedHere ? " (Transaction rolled back)" : ""));
        throw;
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
       textBuffer.append(QString("Unknown critical error during Insert Data.") + (transactionStartedHere ? " (Transaction rolled back)" : ""));
       throw;
    }
}



// ============================================================================
// START: Implementation of parseLiteralValue, parseWhereClause and helpers
// ============================================================================

QVariant MainWindow::parseLiteralValue(const QString& valueStr) {
    QString trimmedVal = valueStr.trimmed();

    if (trimmedVal.compare("NULL", Qt::CaseInsensitive) == 0) {
        return QVariant(); // Represents SQL NULL
    }

    // String literal (handles single and double quotes)
    if ((trimmedVal.startsWith('\'') && trimmedVal.endsWith('\'')) ||
        (trimmedVal.startsWith('"') && trimmedVal.endsWith('"'))) {
        if (trimmedVal.length() >= 2) {
            QString inner = trimmedVal.mid(1, trimmedVal.length() - 2);
            if (trimmedVal.startsWith('\'')) {
                inner.replace("''", "'"); // Unescape '' to '
            } else { // Double quotes
                inner.replace("\"\"", "\""); // Unescape "" to "
            }
            return inner;
        }
        return QString(""); // Empty string if only quotes
    }

    // Boolean literals
    if (trimmedVal.compare("TRUE", Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (trimmedVal.compare("FALSE", Qt::CaseInsensitive) == 0) {
        return false;
    }

    // Numeric literals
    bool ok;
    // Try parsing as integer (long long for wider range)
    qlonglong longVal = trimmedVal.toLongLong(&ok);
    if (ok) {
        // If it doesn't contain a decimal point or exponent, it's likely an integer type
        if (!trimmedVal.contains('.') && !trimmedVal.contains('e', Qt::CaseInsensitive)) {
            // Check if it fits in a standard int
            if (longVal >= std::numeric_limits<int>::min() && longVal <= std::numeric_limits<int>::max()) {
                return static_cast<int>(longVal);
            }
            return longVal; // Otherwise, return as qlonglong
        }
    }

    // If integer parsing failed or it contains a decimal/exponent, try as double
    double doubleVal = trimmedVal.toDouble(&ok);
    if (ok) {
        return doubleVal;
    }

    // If not quoted, and not NULL, TRUE, FALSE, or a valid number,
    // it might be an unquoted string (e.g., enum values in some SQL dialects) or a column name.
    // At this stage of SQL parsing, a literal parser usually only handles true literals.
    // Column names should be handled by other parts of the parser.
    // For simplicity, if it's none of the above, we treat it as a (potentially erroneous) unquoted string.
    // Alternatively, you might throw an error here.
    // qWarning() << "Could not uniquely identify literal as number, string, or boolean:" << trimmedVal;
    return trimmedVal; // Fallback: treat as string
}

QPair<int, QString> MainWindow::findLowestPrecedenceOperator(const QString& expr, const QStringList& operatorsInPrecedenceOrder) {
    int currentParenLevel = 0;
    int bestOpPos = -1;
    QString bestOpFound;
    // Lower index in operatorsInPrecedenceOrder means lower precedence (parsed first when recursing)
    int lowestPrecedenceIndex = operatorsInPrecedenceOrder.size();

    for (int i = expr.length() - 1; i >= 0; --i) {
        QChar c = expr[i];
        if (c == ')') {
            currentParenLevel++;
        } else if (c == '(') {
            currentParenLevel--;
        } else if (currentParenLevel == 0) {
            for (int opIdx = 0; opIdx < operatorsInPrecedenceOrder.size(); ++opIdx) {
                const QString& opCandidate = operatorsInPrecedenceOrder[opIdx];
                if (i >= opCandidate.length() - 1) {
                    QString sub = expr.mid(i - opCandidate.length() + 1, opCandidate.length());
                    if (sub.compare(opCandidate, Qt::CaseInsensitive) == 0) {
                        bool leftOk = (i - opCandidate.length() < 0) || expr[i - opCandidate.length()].isSpace() || expr[i - opCandidate.length()] == ')' || expr[i - opCandidate.length()] == '(';
                        bool rightOk = (i + 1 >= expr.length()) || expr[i + 1].isSpace() || expr[i + 1] == '(' || expr[i + 1] == ')';

                        if (leftOk && rightOk) {
                            // Find the operator with the lowest precedence (smallest index in operatorsInPrecedenceOrder)
                            // If precedence is the same, the rightmost one (due to right-to-left scan) is chosen.
                            if (opIdx < lowestPrecedenceIndex) {
                                lowestPrecedenceIndex = opIdx;
                                bestOpPos = i - opCandidate.length() + 1;
                                bestOpFound = opCandidate.toUpper();
                            }
                        }
                    }
                }
            }
        }
    }
    return {bestOpPos, bestOpFound};
}

ConditionNode MainWindow::parseSubExpression(QStringView expressionView) {
    QString expression = expressionView.toString().trimmed();
    // Keep the initial qDebug for the overall expression
    // qDebug() << "[parseSubExpression] Overall Parsing sub-expression: '" << expression << "'";
    ConditionNode node;

    // 1. 移除最外层括号 ... (这部分代码与您之前提供的版本相同，保持不变)
    while (expression.startsWith('(') && expression.endsWith(')')) {
        int parenLevel = 0;
        bool allEnclosedProperly = true;
        if (expression.length() <= 2) {
            // qDebug() << "  Expression too short for meaningful parenthesized content, breaking paren stripping.";
            break;
        }
        for (int i = 0; i < expression.length() - 1; ++i) {
            if (expression[i] == '(') parenLevel++;
            else if (expression[i] == ')') parenLevel--;
            if (parenLevel == 0) {
                allEnclosedProperly = false;
                // qDebug() << "  Parentheses do not enclose the entire sub-expression (level became 0 at index " << i << ").";
                break;
            }
        }
        if (allEnclosedProperly && parenLevel == 1 && expression.at(expression.length()-1) == ')') {
            QString newExpression = expression.mid(1, expression.length() - 2).trimmed();
            // qDebug() << "  Stripped outer parentheses. Old: '" << expression << "', New: '" << newExpression << "'";
            expression = newExpression;
        } else {
            // qDebug() << "  Outer parentheses not stripped. AllEnclosed:" << allEnclosedProperly
            //          << "FinalParenLevel (before last char):" << parenLevel
            //          << "LastCharIsClosingParen:" << (expression.length() > 0 && expression.at(expression.length()-1) == ')');
            break;
        }
    }
    if (expression.isEmpty()) {
        // qDebug() << "  Expression became empty after stripping parentheses, treating as empty node.";
        node.type = ConditionNode::EMPTY;
        return node;
    }

    // 2. 处理 NOT 操作符 ... (这部分代码与您之前提供的版本相同，保持不变)
    if (expression.toUpper().startsWith("NOT ") &&
        !expression.toUpper().startsWith("NOT LIKE") &&
        !expression.toUpper().startsWith("NOT IN") &&
        !expression.toUpper().startsWith("NOT BETWEEN")) {
        QString restOfExpression = QStringView(expression).mid(4).toString().trimmed();
        // qDebug() << "  Found NOT prefix. Remainder for recursive call: '" << restOfExpression << "'";
        if (restOfExpression.isEmpty()) {
            throw std::runtime_error("NOT 操作符后缺少条件表达式。");
        }
        ConditionNode childNode = parseSubExpression(restOfExpression);
        node.type = ConditionNode::NEGATION_OP;
        node.children.append(childNode);
        // qDebug() << "  Created NEGATION_OP node with child type:" << childNode.type;
        return node;
    }

    // 特别添加：直接检查是否包含BETWEEN ... AND ...
        if (expression.contains("BETWEEN", Qt::CaseInsensitive)) {
            // 解析 BETWEEN ... AND ...
            QRegularExpression betweenRegex(R"((.+?)\s+BETWEEN\s+(.+?)\s+AND\s+(.+))", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = betweenRegex.match(expression);

            if (match.hasMatch()) {
                QString field = match.captured(1).trimmed();
                QString value1 = match.captured(2).trimmed();
                QString value2 = match.captured(3).trimmed();

                node.type = ConditionNode::COMPARISON_OP;
                node.comparison.fieldName = field;
                node.comparison.operation = "BETWEEN";
                node.comparison.value = parseLiteralValue(value1);
                node.comparison.value2 = parseLiteralValue(value2);

                qDebug() << "Parsed BETWEEN: Field=" << field << ", Value1=" << value1 << ", Value2=" << value2;
                return node;
            }
        }


    // 3. 处理逻辑运算符 OR, AND ... (这部分代码与您之前提供的版本相同，保持不变)
    QPair<int, QString> opDetails = findLowestPrecedenceOperator(expression, {"OR", "AND"});
    if (opDetails.first != -1) {
        // qDebug() << "  Found logical operator: '" << opDetails.second << "' at pos " << opDetails.first;
        node.type = ConditionNode::LOGIC_OP;
        node.logicOp = opDetails.second.toUpper();
        QString leftPart = QStringView(expression).left(opDetails.first).toString().trimmed();
        QString rightPart = QStringView(expression).mid(opDetails.first + opDetails.second.length()).toString().trimmed();
        if (leftPart.isEmpty() || rightPart.isEmpty()) {
            throw std::runtime_error("逻辑运算符 '" + opDetails.second.toStdString() + "' 缺少操作数。表达式: " + expression.toStdString());
        }
        // qDebug() << "    Left part: '" << leftPart << "', Right part: '" << rightPart << "'";
        node.children.append(parseSubExpression(leftPart));
        node.children.append(parseSubExpression(rightPart));
        return node;
    }

    // 4. 处理比较运算符
    const QList<QString> comparisonOps = {
        "IS NOT NULL", "IS NULL",    // 一元操作符，通常优先级较高或特殊处理
        "NOT BETWEEN", "BETWEEN",    // 三元操作符 (field BETWEEN val1 AND val2)
        "NOT LIKE", "LIKE",
        "NOT IN", "IN",
        ">=", "<=", "<>", "!=", "=", ">", "<" // 二元操作符
    };

    for (const QString& op_from_list : comparisonOps) {
        QRegularExpression compRe;
        QString patternStr;

        // ==================== MODIFICATION START ====================
        // 修改 fieldNamePattern 以支持 alias.column 和 quoted versions
        // 原来的: QString fieldNamePattern = R"(([\w_][\w\d_]*|`[^`]+`|\[[^\]]+\]))";
        QString fieldNamePattern = R"((`[^`]+`(?:\.`[^`]+`)?|\[[^\]]+\](?:\.\[[^\]]+\])?|[\w_]+(?:\.[\w_]+)?))";
        // 这个模式的含义:
        // 1. `[^`]+`(?:\.`[^`]+`)?     : `table`.`column` 或 `column` (反引号包裹)
        // 2. \[[^\]]+\](?:\.\[[^\]]+\])? : [table].[column] 或 [column] (方括号包裹)
        // 3. [\w_]+(?:\.[\w_]+)?         : table.column 或 column (无引号)
        // ===================== MODIFICATION END =====================

        QString escaped_op = QRegularExpression::escape(op_from_list);
        QString operator_pattern_segment;

        if (op_from_list == "IN" || op_from_list == "NOT IN" ||
            op_from_list == "LIKE" || op_from_list == "NOT LIKE") {
            operator_pattern_segment = QString(R"(\b(%1)\b)").arg(escaped_op);
        } else {
            operator_pattern_segment = QString("(%1)").arg(escaped_op);
        }

        if (op_from_list.compare("IS NULL", Qt::CaseInsensitive) == 0 || op_from_list.compare("IS NOT NULL", Qt::CaseInsensitive) == 0) {
            patternStr = QString(R"(^\s*%1\s+%2\s*$)").arg(fieldNamePattern).arg(operator_pattern_segment);
        } else if (op_from_list.compare("BETWEEN", Qt::CaseInsensitive) == 0 || op_from_list.compare("NOT BETWEEN", Qt::CaseInsensitive) == 0) {
            patternStr = QString(R"(^\s*%1\s+%2\s*(.+?)\s+AND\s+(.+?)\s*$)").arg(fieldNamePattern).arg(operator_pattern_segment); // Note: operator_pattern_segment already contains BETWEEN/NOT BETWEEN
        } else {
            patternStr = QString(R"(^\s*%1\s*%2\s*(.+)\s*$)").arg(fieldNamePattern).arg(operator_pattern_segment);
        }

        compRe.setPattern(patternStr);
        compRe.setPatternOptions(QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch match = compRe.match(expression);

        if (match.hasMatch()) {
            qDebug() << "[parseSubExpression][COMPARISON] Matched op_from_list: '" << op_from_list
                     << "' with pattern: '" << patternStr
                     << "' on expression: '" << expression << "'";

            node.type = ConditionNode::COMPARISON_OP;
            // match.captured(1) 现在应该能捕获如 "D.DepartmentName" 或 "DepartmentName"
            node.comparison.fieldName = match.captured(1).trimmed();
            node.comparison.operation = op_from_list.toUpper(); // 使用原始列表中的操作符，已是正确的大小写或形式

            qDebug() << "  [COMPARISON] Field from regex: '" << node.comparison.fieldName
                     << "', Operation set to: '" << node.comparison.operation << "'";
            // for(int capIdx = 0; capIdx <= match.lastCapturedIndex(); ++capIdx) {
            //     qDebug() << "    [COMPARISON] Regex Capture Group " << capIdx << ": '" << match.captured(capIdx) << "'";
            // }

            if (node.comparison.operation == "IS NULL" || node.comparison.operation == "IS NOT NULL") {
                qDebug() << "    [COMPARISON] Branch: IS NULL / IS NOT NULL";
                // No value part to parse for these
            } else if (node.comparison.operation == "BETWEEN" || node.comparison.operation == "NOT BETWEEN") {
                qDebug() << "    [COMPARISON] Branch: BETWEEN / NOT BETWEEN";
                // For "field BETWEEN val1 AND val2", regex captured groups are:
                // 1: fieldName (from fieldNamePattern)
                // 2: operator ("BETWEEN" or "NOT BETWEEN") (from operator_pattern_segment)
                // 3: val1
                // 4: val2
                QString val1Str = match.captured(3).trimmed(); // Value1
                QString val2Str = match.captured(4).trimmed(); // Value2
                node.comparison.value = parseLiteralValue(val1Str);
                node.comparison.value2 = parseLiteralValue(val2Str);
                qDebug() << "      Values: Value1=" << node.comparison.value
                         << " (Raw: '" << val1Str << "'), Value2=" << node.comparison.value2
                         << " (Raw: '" << val2Str << "')";
            } else if (node.comparison.operation == "IN" || node.comparison.operation == "NOT IN") {
                qDebug() << "    [COMPARISON] Branch: IN / NOT IN";
                // For "field IN (list)", regex captured groups are:
                // 1: fieldName
                // 2: operator ("IN" or "NOT IN")
                // 3: value list part including parentheses, e.g., "(val1, val2)" or "('a', 'b')"
                QString valueListPartWithParens = match.captured(3).trimmed();
                qDebug() << "      valueListPart (raw from capture group 3 for IN/NOT IN): '" << valueListPartWithParens << "'";
                if (!valueListPartWithParens.startsWith('(') || !valueListPartWithParens.endsWith(')')) {
                    throw std::runtime_error("IN 子句的值必须用括号括起来。 Got: " + valueListPartWithParens.toStdString());
                }
                QString innerValues = valueListPartWithParens.mid(1, valueListPartWithParens.length() - 2).trimmed();
                if (innerValues.isEmpty()) {
                    throw std::runtime_error("IN 子句的值列表不能为空 (例如 IN () )");
                }
                QStringList valuesStr = parseSqlValues(innerValues); // parseSqlValues should handle parsing 'val1','val2',...
                for(const QString& valStr : valuesStr) {
                    if (!valStr.trimmed().isEmpty()) { // Ensure non-empty strings from parseSqlValues
                        node.comparison.valueList.append(parseLiteralValue(valStr.trimmed()));
                    }
                }
                if (node.comparison.valueList.isEmpty() && !valuesStr.isEmpty()) { // If parseSqlValues returned items but they all became empty QVariants
                     throw std::runtime_error("IN 子句的值列表解析后为空或格式错误。 (Inner: " + innerValues.toStdString() + ")");
                } else if (node.comparison.valueList.isEmpty() && valuesStr.isEmpty() && !innerValues.isEmpty()){ // parseSqlValues returned nothing from non-empty input
                    throw std::runtime_error("IN 子句的值列表解析失败或格式错误。 (Inner: " + innerValues.toStdString() + ")");
                }

                qDebug() << "      IN/NOT IN values parsed:" << node.comparison.valueList;
            } else { // Standard binary operators like =, >, LIKE etc.
                qDebug() << "    [COMPARISON] Branch: Other binary operators (e.g., LIKE, =, >, <)";
                // For "field op value", regex captured groups are:
                // 1: fieldName
                // 2: operator
                // 3: value part
                QString valuePart = match.captured(3).trimmed();
                qDebug() << "      valuePart (raw from capture group 3 for other ops): '" << valuePart << "'";
                node.comparison.value = parseLiteralValue(valuePart);
                qDebug() << "      Parsed value: " << node.comparison.value;
            }
            return node;
        }
    }

    qDebug() << "[parseSubExpression] No operator pattern matched for expression: '" << expression << "'";
    throw std::runtime_error("无效的表达式或 WHERE 子句中不支持的语法: '" + expression.toStdString() + "'");
}

// MainWindow::parseWhereClause 的主实现
bool MainWindow::parseWhereClause(const QString& whereStr, ConditionNode& rootNode) {
    QString currentWhere = whereStr.trimmed();
    if (currentWhere.endsWith(';')) {
        currentWhere.chop(1); // 移除末尾的分号
        currentWhere = currentWhere.trimmed(); // 再次 trim
    }

    if (currentWhere.isEmpty()) {
        rootNode.type = ConditionNode::EMPTY;
        qDebug() << "[parseWhereClause] WHERE clause is empty.";
        return true;
    }

    qDebug() << "[parseWhereClause] Parsing cleaned WHERE clause: '" << currentWhere << "'";
    try {
        rootNode = parseSubExpression(QStringView(currentWhere));
        qDebug() << "[parseWhereClause] Parsed successfully. Root node type:" << rootNode.type;
        return true;
    } catch (const std::runtime_error& e) {
        QString errorMsg = "解析WHERE子句错误: " + QString::fromStdString(e.what());
        qDebug() << "[parseWhereClause] Error:" << errorMsg;
        textBuffer.append(errorMsg); // 显示错误到UI
        return false;
    }
}


// mainwindow.cpp
void MainWindow::handleUpdate(const QString& command) {
    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) {
        textBuffer.append("错误: 未选择数据库");
        return;
    }

    // 保持原来的顶级 UPDATE 语句正则表达式
    QRegularExpression re(
        R"(UPDATE\s+([\w\.]+)\s+SET\s+(.+?)(?:\s+WHERE\s+(.+))?\s*;?$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        textBuffer.append("语法错误: UPDATE 表名 SET 列1=值1,... [WHERE 条件]");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString set_part_str = match.captured(2).trimmed(); // 完整 SET 子句字符串, e.g., "col1 = val1, col2 = expr2"
    QString where_part = match.captured(3).trimmed();

    qDebug() << "[MainWindow::handleUpdate] == 进入 handleUpdate 函数 ==";
    qDebug() << "[MainWindow::handleUpdate] 原始命令:" << command;
    qDebug() << "[MainWindow::handleUpdate] 提取的表名:" << table_name;
    qDebug() << "[MainWindow::handleUpdate] 提取的完整SET部分:" << set_part_str;
    qDebug() << "[MainWindow::handleUpdate] 提取的WHERE部分:" << where_part;

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        qDebug() << "[MainWindow::handleUpdate] 当前无活动事务，尝试为数据库启动新事务:" << current_db_name;
        if (!db_manager.beginTransaction()) {
            textBuffer.append("警告: 无法为UPDATE操作开始新事务，操作将在无事务保护下进行。");
            qWarning() << "[MainWindow::handleUpdate] 为数据库启动新事务失败:" << current_db_name;
        } else {
            transactionStartedHere = true;
            qDebug() << "[MainWindow::handleUpdate] 已为数据库成功启动新事务:" << current_db_name;
        }
    } else {
        qDebug() << "[MainWindow::handleUpdate] 当前已在数据库的活动事务中:" << current_db_name;
    }

    try {
        QMap<QString, QString> updates; // 存储 列名 -> 完整值表达式字符串
        int current_pos = 0;
        QString remaining_set_part = set_part_str;

        qDebug() << "[MainWindow::handleUpdate] 开始解析SET子句: '" << remaining_set_part << "'";

        while(current_pos < remaining_set_part.length()) {
            // 1. 提取列名
            int equals_sign_pos = -1;
            QString current_col_name_candidate;
            int temp_search_pos = current_pos;

            // 查找列名直到 '='
            // 列名可以是被反引号` `或方括号[ ]包围的，或者就是普通标识符
            QRegularExpression columnNameRe(R"(([\w_]+|`[^`]+`|\[[^\]]+\])\s*=)");
            columnNameRe.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch colNameMatch = columnNameRe.match(remaining_set_part, current_pos);

            if (colNameMatch.hasMatch() && colNameMatch.capturedStart() == current_pos) {
                current_col_name_candidate = colNameMatch.captured(1).trimmed(); // 列名本身
                // 如果列名被包围，移除包围符号
                if ((current_col_name_candidate.startsWith('`') && current_col_name_candidate.endsWith('`')) ||
                    (current_col_name_candidate.startsWith('[') && current_col_name_candidate.endsWith(']'))) {
                    current_col_name_candidate = current_col_name_candidate.mid(1, current_col_name_candidate.length() - 2);
                }
                equals_sign_pos = colNameMatch.capturedEnd(1); // 等号开始的位置 (在匹配的列名之后，等号之前)
                    // 我们需要的是等号之后值开始的位置
                while(equals_sign_pos < remaining_set_part.length() && remaining_set_part.at(equals_sign_pos).isSpace()) {
                    equals_sign_pos++; // 跳过列名和等号间的空格
                }
                if (equals_sign_pos < remaining_set_part.length() && remaining_set_part.at(equals_sign_pos) == '=') {
                    // 正确找到了列名和等号
                } else {
                    throw std::runtime_error("SET子句解析错误：在列名 '" + current_col_name_candidate.toStdString() + "' 后缺少 '='。");
                }

            } else {
                throw std::runtime_error("SET子句解析错误：无法在位置 " + QString::number(current_pos).toStdString() + " 处找到有效的 '列名 =' 模式。剩余: '" + remaining_set_part.mid(current_pos).toStdString() + "'");
            }

            QString fieldName = current_col_name_candidate;
            qDebug() << "[MainWindow::handleUpdate]   解析到列名: '" << fieldName << "'";

            // 2. 提取值表达式 (直到下一个顶层逗号或字符串末尾)
            int value_expression_start_pos = equals_sign_pos + 1; // 等号之后是值表达式的开始
            while(value_expression_start_pos < remaining_set_part.length() && remaining_set_part.at(value_expression_start_pos).isSpace()){
                value_expression_start_pos++; // 跳过等号后的空格
            }

            int value_expression_end_pos = value_expression_start_pos;
            int parentheses_level = 0;
            bool in_string_literal = false;
            QChar string_literal_char = ' ';

            while(value_expression_end_pos < remaining_set_part.length()) {
                QChar ch = remaining_set_part.at(value_expression_end_pos);
                if (in_string_literal) {
                    if (ch == string_literal_char) {
                        // 检查是否是SQL中的两个连续引号表示一个引号的情况 'don''t' or "say ""hello"""
                        if (value_expression_end_pos + 1 < remaining_set_part.length() && remaining_set_part.at(value_expression_end_pos + 1) == string_literal_char) {
                            value_expression_end_pos++; // 跳过第二个引号
                        } else {
                            in_string_literal = false;
                        }
                    }
                } else {
                    if (ch == '\'' || ch == '"') {
                        in_string_literal = true;
                        string_literal_char = ch;
                    } else if (ch == '(') {
                        parentheses_level++;
                    } else if (ch == ')') {
                        parentheses_level--;
                    } else if (ch == ',' && parentheses_level == 0) {
                        break; // 遇到顶层逗号，当前值表达式结束
                    }
                }
                value_expression_end_pos++;
            }

            QString valueExpression = remaining_set_part.mid(value_expression_start_pos, value_expression_end_pos - value_expression_start_pos).trimmed();
            if (valueExpression.isEmpty()) {
                throw std::runtime_error("SET子句中列 '" + fieldName.toStdString() + "' 的值表达式不能为空。");
            }
            qDebug() << "[MainWindow::handleUpdate]   解析到值表达式: '" << valueExpression << "' (for column '" << fieldName << "')";

            // 存储原始的、完整的右侧表达式字符串
            // QVariant parsedVal = parseLiteralValue(valueExpression); // 不再在这里调用 parseLiteralValue
            // updates[fieldName] = parsedVal.isNull() ? "NULL" : parsedVal.toString();
            updates[fieldName] = valueExpression; // xhytable::updateData 将负责解释这个表达式

            current_pos = value_expression_end_pos;

            // 3. 处理逗号分隔符
            if (current_pos < remaining_set_part.length()) {
                if (remaining_set_part.at(current_pos) == ',') {
                    current_pos++; // 跳过逗号
                    // 跳过逗号后的空格
                    while(current_pos < remaining_set_part.length() && remaining_set_part.at(current_pos).isSpace()) {
                        current_pos++;
                    }
                    if (current_pos == remaining_set_part.length()) { // 末尾是逗号
                        throw std::runtime_error("SET子句在末尾有多余的逗号。");
                    }
                } else {
                    // 如果不是逗号也不是字符串末尾，说明格式有问题
                    throw std::runtime_error("SET子句中多个赋值表达式之间缺少逗号，或在 '" + valueExpression.toStdString() + "' 后存在无法识别的内容: '" + remaining_set_part.mid(current_pos).trimmed().toStdString() + "'");
                }
            }
        } // 结束 while 循环解析 SET 子句

        qDebug() << "[MainWindow::handleUpdate] 完成SET子句解析。Updates map (列名 -> 原始值表达式):" << updates;
        if (updates.isEmpty() && !set_part_str.trimmed().isEmpty()) {
            // 这通常意味着整个 set_part_str 无法被解析为任何有效的 "col = val" 对
            throw std::runtime_error(("无法解析整个SET子句: " + set_part_str).toStdString());
        }


        ConditionNode conditionRoot;
        if (!parseWhereClause(where_part, conditionRoot)) { // parseWhereClause 内部出错会填充 textBuffer
            if(transactionStartedHere) {
                qDebug() << "[MainWindow::handleUpdate] WHERE子句解析失败。回滚数据库事务:" << current_db_name;
                db_manager.rollbackTransaction(); // 确保回滚
                textBuffer.append("事务已回滚 (WHERE子句解析失败)。");
            }
            return; // 错误信息已由parseWhereClause记录
        }

        // 调用 db_manager.updateData，它会调用 xhytable::updateData
        // xhytable::updateData 现在会接收到包含完整表达式的 'updates' map
        int affected_rows = db_manager.updateData(current_db_name, table_name, updates, conditionRoot);

        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                textBuffer.append(QString("%1 行已更新。事务已提交。").arg(affected_rows));
                qDebug() << "[MainWindow::handleUpdate] 事务已提交。影响行数:" << affected_rows;
            } else {
                textBuffer.append(QString("错误：更新了 %1 行 (内存中)，但事务提交失败！数据可能未持久化。").arg(affected_rows));
                qWarning() << "[MainWindow::handleUpdate] 严重: 事务提交失败，但 updateData 报告成功影响 " << affected_rows << " 行。数据库 '" << current_db_name << "' 的表 '" << table_name << "' 数据可能不一致。";
                // 理论上，如果commit失败，db_manager.commitTransaction()内部应该已经尝试回滚内存中的更改
            }
        } else { // 不在此处启动事务 (可能在外部事务中，或不支持事务)
            textBuffer.append(QString("%1 行已更新 (在现有事务中或无事务模式下)。").arg(affected_rows));
        }

    } catch (const std::runtime_error& e) {
        QString error_message = QString::fromStdString(e.what());
        qWarning() << "[MainWindow::handleUpdate] 捕获到 runtime_error:" << error_message;
        if (transactionStartedHere) {
            db_manager.rollbackTransaction(); // 确保回滚
            qDebug() << "[MainWindow::handleUpdate] 数据库事务已回滚:" << current_db_name << "错误原因:" << error_message;
            textBuffer.append(QString("错误: 更新操作失败。原因: %1 (事务已回滚)").arg(error_message));
        } else {
            textBuffer.append(QString("错误: 更新操作失败。原因: %1").arg(error_message));
        }
    } catch (...) {
        qCritical() << "[MainWindow::handleUpdate] 捕获到未知异常!";
        if (transactionStartedHere) {
            db_manager.rollbackTransaction(); // 确保回滚
            qDebug() << "[MainWindow::handleUpdate] 数据库事务因未知错误已回滚:" << current_db_name;
            textBuffer.append("更新操作发生未知严重错误。(事务已回滚)");
        } else {
            textBuffer.append("更新操作发生未知严重错误。");
        }
    }
}
void MainWindow::handleDelete(const QString& command) {
    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    QRegularExpression re(R"(DELETE\s+FROM\s+([\w\.]+)(?:\s+WHERE\s+(.+))?\s*;?$)",
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) { textBuffer.append("语法错误: DELETE FROM <表名> [WHERE <条件>]"); return; }
    QString table_name = match.captured(1).trimmed();
    QString where_part = match.captured(2).trimmed();

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if(!db_manager.beginTransaction()) { /* ... */ } else transactionStartedHere = true;
    }

    try {
        if (where_part.isEmpty()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "确认删除",
                                                                      QString("确定要删除表 '%1' 中的所有数据吗? 此操作不可恢复!").arg(table_name),
                                                                      QMessageBox::Yes|QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::No) {
                if (transactionStartedHere) db_manager.rollbackTransaction();
                textBuffer.append("删除操作已取消。");
                return;
            }
        }
        ConditionNode conditionRoot;
        if (!parseWhereClause(where_part, conditionRoot)) { // **Aufruf**
            if(transactionStartedHere) db_manager.rollbackTransaction();
            return;
        }
        int affected_rows = db_manager.deleteData(current_db_name, table_name, conditionRoot);
        if (affected_rows >= 0) {
            if (transactionStartedHere) {
                if (db_manager.commitTransaction()) {
                    textBuffer.append(QString("%1 行已删除。").arg(affected_rows));
                } else { textBuffer.append("错误：事务提交失败。更改已回滚。");}
            } else {
                textBuffer.append(QString("%1 行已删除 (在现有事务中)。").arg(affected_rows));
            }
        } else { textBuffer.append("错误：删除数据失败。"); if(transactionStartedHere) db_manager.rollbackTransaction(); }
    } catch (const std::runtime_error& e) {
        QString error_message = QString::fromStdString(e.what());
        qWarning() << "[handleDelete] Caught runtime_error:" << error_message;
        if (transactionStartedHere) {
            db_manager.rollbackTransaction();
            qDebug() << "[handleDelete] Transaction rolled back for database:" << current_db_name << "due to error:" << error_message;
            textBuffer.append(QString("错误: 删除操作失败。原因: %1 (事务已回滚)").arg(error_message));
        } else {
            textBuffer.append(QString("错误: 删除操作失败。原因: %1").arg(error_message));
        }
    }}
QString MainWindow::cleanIdentifier(QString id) const { // 添加 const
    id = id.trimmed();
    if ((id.startsWith('`') && id.endsWith('`')) || (id.startsWith('[') && id.endsWith(']'))) {
        if (id.length() >= 2) {
            return id.mid(1, id.length() - 2);
        } else {
            return QString(); // 无效的如 "`"
        }
    }
    return id;
}
// by zyh
// mainwindow.cpp

// MainWindow::handleSelect 函数
void MainWindow::handleSelect(const QString& command) {
    QString trimmedCommand = command.trimmed();
    qDebug() << "[handleSelect] 接收到命令: " << trimmedCommand;
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("错误: 未选择数据库。");
        return;
    }
    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) {
        textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。");
        return;
    }

    // 这就是需要修改的正则表达式！确保 ORDER BY 捕获组是 ((?:...)+?)
    // 在 MainWindow::handleSelect 函数中
    QRegularExpression join_re(
        R"(^SELECT\s+(.+?)\s+FROM\s+([\w_`\[\]]+)(?:\s+(?:AS\s+)?([\w_`\[\]]+))?\s+(?:INNER\s+)?JOIN\s+([\w_`\[\]]+)(?:\s+(?:AS\s+)?([\w_`\[\]]+))?\s+ON\s+([\w\d_`\[\]\.]+)\s*=\s*([\w\d_`\[\]\.]+)(?:\s+WHERE\s+(.+?))?(?:\s+ORDER\s+BY\s+((?:(?!LIMIT\b|ORDER\b|GROUP\b|WHERE\b|FROM\b|SELECT\b|JOIN\b|ON\b|AS\b|DESC\b|ASC\b)[\w\d_`\[\]\.]+\s*(?:ASC|DESC)?\s*,?\s*)+?))?(?:\s+LIMIT\s+(\d+))?\s*;?$)",
        //                                                                                                                                                                                                                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        //                                                                                                                                                                                                                                在列名匹配前加入否定预查，排除常用SQL关键字
        QRegularExpression::CaseInsensitiveOption
    );
    QRegularExpressionMatch join_match = join_re.match(trimmedCommand);

    if (join_match.hasMatch()) {
        qDebug() << "JOIN 语法匹配成功! 开始处理JOIN查询...";
        try {
            QString select_cols_str_join = join_match.captured(1).trimmed();
            QString table1_name_raw_orig = join_match.captured(2).trimmed();
            QString table1_alias_opt_orig = join_match.captured(3).trimmed();
            QString table2_name_raw_orig = join_match.captured(4).trimmed();
            QString table2_alias_opt_orig = join_match.captured(5).trimmed();
            QString join_cond_part1_full_orig = join_match.captured(6).trimmed();
            QString join_cond_part2_full_orig = join_match.captured(7).trimmed();
            QString where_part_join = join_match.captured(8).trimmed();
            QString order_by_part_join = join_match.captured(9).trimmed();
            QString limit_part_join = join_match.captured(10).trimmed();

            // 验证捕获组的调试信息
            qDebug() << "[JOIN DEBUG] Captured SELECT Part:" << select_cols_str_join;
            qDebug() << "[JOIN DEBUG] Captured Table1 Raw:" << table1_name_raw_orig << ", Alias1 Opt:" << table1_alias_opt_orig;
            qDebug() << "[JOIN DEBUG] Captured Table2 Raw:" << table2_name_raw_orig << ", Alias2 Opt:" << table2_alias_opt_orig;
            qDebug() << "[JOIN DEBUG] Captured ON Cond1:" << join_cond_part1_full_orig << ", ON Cond2:" << join_cond_part2_full_orig;
            qDebug() << "[JOIN DEBUG] Captured WHERE Part:" << where_part_join;
            qDebug() << "[JOIN DEBUG] Captured ORDER BY Part (Group 9):" << order_by_part_join; // 关键检查点
            qDebug() << "[JOIN DEBUG] Captured LIMIT Part (Group 10):" << limit_part_join;     // 关键检查点

            QString table1_name_raw = cleanIdentifier(table1_name_raw_orig);
            QString table1_alias_opt = cleanIdentifier(table1_alias_opt_orig);
            QString table2_name_raw = cleanIdentifier(table2_name_raw_orig);
            QString table2_alias_opt = cleanIdentifier(table2_alias_opt_orig);

            QString table1_display_name = table1_alias_opt.isEmpty() ? table1_name_raw : table1_alias_opt;
            QString table2_display_name = table2_alias_opt.isEmpty() ? table2_name_raw : table2_alias_opt;

            xhytable* table1_ptr = db->find_table(table1_name_raw);
            xhytable* table2_ptr = db->find_table(table2_name_raw);
            if (!table1_ptr) throw std::runtime_error("错误: 表 '" + table1_name_raw.toStdString() + "' 不存在。");
            if (!table2_ptr) throw std::runtime_error("错误: 表 '" + table2_name_raw.toStdString() + "' 不存在。");

            QPair<QString, QString> cond1_parsed = parseQualifiedColumn(join_cond_part1_full_orig);
            QPair<QString, QString> cond2_parsed = parseQualifiedColumn(join_cond_part2_full_orig);

            QString join_col_t1_actual_name, join_col_t2_actual_name;
            QString cond1_qualifier = cond1_parsed.first;
            QString cond2_qualifier = cond2_parsed.first;

            bool c1_matches_t1_name_or_alias = cond1_qualifier.compare(table1_name_raw, Qt::CaseInsensitive) == 0 || (!table1_alias_opt.isEmpty() && cond1_qualifier.compare(table1_alias_opt, Qt::CaseInsensitive) == 0);
            bool c1_matches_t2_name_or_alias = cond1_qualifier.compare(table2_name_raw, Qt::CaseInsensitive) == 0 || (!table2_alias_opt.isEmpty() && cond1_qualifier.compare(table2_alias_opt, Qt::CaseInsensitive) == 0);
            bool c2_matches_t1_name_or_alias = cond2_qualifier.compare(table1_name_raw, Qt::CaseInsensitive) == 0 || (!table1_alias_opt.isEmpty() && cond2_qualifier.compare(table1_alias_opt, Qt::CaseInsensitive) == 0);
            bool c2_matches_t2_name_or_alias = cond2_qualifier.compare(table2_name_raw, Qt::CaseInsensitive) == 0 || (!table2_alias_opt.isEmpty() && cond2_qualifier.compare(table2_alias_opt, Qt::CaseInsensitive) == 0);

            if ((cond1_qualifier.isEmpty() || c1_matches_t1_name_or_alias) && (cond2_qualifier.isEmpty() || c2_matches_t2_name_or_alias)) {
                join_col_t1_actual_name = cond1_parsed.second;
                join_col_t2_actual_name = cond2_parsed.second;
            } else if ((cond1_qualifier.isEmpty() || c1_matches_t2_name_or_alias) && (cond2_qualifier.isEmpty() || c2_matches_t1_name_or_alias)) {
                join_col_t1_actual_name = cond2_parsed.second;
                join_col_t2_actual_name = cond1_parsed.second;
            } else {
                throw std::runtime_error("JOIN ON 条件中的表限定符无法明确匹配。");
            }

            if (!table1_ptr->has_field(join_col_t1_actual_name)) throw std::runtime_error("错误: JOIN ON 列 '" + join_col_t1_actual_name.toStdString() + "' 在表 '" + table1_display_name.toStdString() + "' 中不存在。");
            if (!table2_ptr->has_field(join_col_t2_actual_name)) throw std::runtime_error("错误: JOIN ON 列 '" + join_col_t2_actual_name.toStdString() + "' 在表 '" + table2_display_name.toStdString() + "' 中不存在。");

            QVector<xhyrecord> records1, records2;
            ConditionNode empty_condition;
            table1_ptr->selectData(empty_condition, records1);
            table2_ptr->selectData(empty_condition, records2);

            QVector<xhyrecord> joined_pre_where_results;
            for (const xhyrecord& r1 : records1) {
                for (const xhyrecord& r2 : records2) {
                    if (r1.value(join_col_t1_actual_name) == r2.value(join_col_t2_actual_name) && !r1.value(join_col_t1_actual_name).isNull()) {
                        xhyrecord combined_record;
                        for (const xhyfield& field : table1_ptr->fields()) {
                            combined_record.insert(table1_display_name + "." + field.name(), r1.value(field.name()));
                        }
                        for (const xhyfield& field : table2_ptr->fields()) {
                            combined_record.insert(table2_display_name + "." + field.name(), r2.value(field.name()));
                        }
                        joined_pre_where_results.append(combined_record);
                    }
                }
            }

            QVector<xhyrecord> results_after_where = joined_pre_where_results;
            if (!where_part_join.isEmpty()) {
                ConditionNode where_condition_root_join;
                if (!parseWhereClause(where_part_join, where_condition_root_join)) { return; }
                results_after_where.clear();
                for (const xhyrecord& joined_rec : joined_pre_where_results) {
                    if (this->matchJoinedRecordConditions(joined_rec, where_condition_root_join, table1_ptr, table1_display_name, table2_ptr, table2_display_name)) {
                        results_after_where.append(joined_rec);
                    }
                }
            }

            QStringList final_display_columns_join;
            QMap<QString, QString> join_select_col_aliases;
            QMap<QString, QPair<QString, QString>> join_aggregate_funcs;

            if (select_cols_str_join == "*") {
                if (!results_after_where.isEmpty()) {
                     for (const QString& key : results_after_where.first().allValues().keys()) {
                        if (!final_display_columns_join.contains(key)) {
                             final_display_columns_join.append(key);
                        }
                        join_select_col_aliases[key] = key;
                     }
                } else if (!joined_pre_where_results.isEmpty()) {
                    for (const QString& key : joined_pre_where_results.first().allValues().keys()) {
                        if (!final_display_columns_join.contains(key)) {
                             final_display_columns_join.append(key);
                        }
                        join_select_col_aliases[key] = key;
                     }
                } else {
                    for (const xhyfield& field : table1_ptr->fields()) {
                        QString colKey = table1_display_name + "." + field.name();
                        final_display_columns_join.append(colKey);
                        join_select_col_aliases[colKey] = colKey;
                    }
                    for (const xhyfield& field : table2_ptr->fields()) {
                        QString colKey = table2_display_name + "." + field.name();
                        final_display_columns_join.append(colKey);
                        join_select_col_aliases[colKey] = colKey;
                    }
                }
                if (!final_display_columns_join.isEmpty()) std::sort(final_display_columns_join.begin(), final_display_columns_join.end());
            } else {
                QStringList requested_cols_with_aliases = select_cols_str_join.split(',', Qt::SkipEmptyParts);
                for (const QString& req_col_raw : requested_cols_with_aliases) {
                    QString req_col_full = req_col_raw.trimmed();
                    QString display_name_for_col = req_col_full;
                    QString actual_col_or_expr = req_col_full;

                    QRegularExpression as_alias_re(R"((.+?)\s+AS\s+([\w_`\[\]]+)$)", QRegularExpression::CaseInsensitiveOption);
                    QRegularExpressionMatch as_match = as_alias_re.match(req_col_full);
                    if (as_match.hasMatch()) {
                        actual_col_or_expr = as_match.captured(1).trimmed();
                        display_name_for_col = cleanIdentifier(as_match.captured(2).trimmed());
                    }
                    actual_col_or_expr = cleanIdentifier(actual_col_or_expr);

                    QRegularExpression agg_func_re(R"(^(COUNT|SUM|AVG|MIN|MAX)\s*\((.+)\)$)", QRegularExpression::CaseInsensitiveOption);
                    QRegularExpressionMatch func_match = agg_func_re.match(actual_col_or_expr);
                    if (func_match.hasMatch()){
                        QString func_name = func_match.captured(1).toUpper();
                        QString func_arg_raw = func_match.captured(2).trimmed();
                        QString func_arg_cleaned;
                        if (func_arg_raw == "*") {
                            func_arg_cleaned = "*";
                        } else {
                            QPair<QString, QString> pqc_agg_arg = parseQualifiedColumn(func_arg_raw);
                            func_arg_cleaned = pqc_agg_arg.first.isEmpty() ? pqc_agg_arg.second : (pqc_agg_arg.first + "." + pqc_agg_arg.second);
                        }
                        join_aggregate_funcs[display_name_for_col] = qMakePair(func_name, func_arg_cleaned);
                        final_display_columns_join.append(display_name_for_col);
                        join_select_col_aliases[display_name_for_col] = display_name_for_col;
                    } else {
                        QPair<QString, QString> pqc_sel = parseQualifiedColumn(actual_col_or_expr);
                        QString final_key_in_joined_record;
                        if(!pqc_sel.first.isEmpty()){
                            final_key_in_joined_record = pqc_sel.first + "." + pqc_sel.second;
                        } else {
                            if (!results_after_where.isEmpty()){
                                if(results_after_where.first().allValues().contains(table1_display_name + "." + pqc_sel.second))
                                    final_key_in_joined_record = table1_display_name + "." + pqc_sel.second;
                                else if (results_after_where.first().allValues().contains(table2_display_name + "." + pqc_sel.second))
                                    final_key_in_joined_record = table2_display_name + "." + pqc_sel.second;
                            } else if (!joined_pre_where_results.isEmpty()){
                                if(joined_pre_where_results.first().allValues().contains(table1_display_name + "." + pqc_sel.second))
                                    final_key_in_joined_record = table1_display_name + "." + pqc_sel.second;
                                else if (joined_pre_where_results.first().allValues().contains(table2_display_name + "." + pqc_sel.second))
                                    final_key_in_joined_record = table2_display_name + "." + pqc_sel.second;
                            }
                            if(final_key_in_joined_record.isEmpty()){
                                bool inT1 = table1_ptr->has_field(pqc_sel.second);
                                bool inT2 = table2_ptr->has_field(pqc_sel.second);
                                if(inT1 && !inT2) final_key_in_joined_record = table1_display_name + "." + pqc_sel.second;
                                else if (!inT1 && inT2) final_key_in_joined_record = table2_display_name + "." + pqc_sel.second;
                                else if (inT1 && inT2) {textBuffer.append(QString("错误: SELECT 列 '%1' 存在歧义，请使用表名或别名限定。").arg(pqc_sel.second)); return;}
                                else {textBuffer.append(QString("错误: SELECT 列 '%1' 在任何表中均未找到。").arg(pqc_sel.second)); return;}
                            }
                        }
                        bool key_is_valid = false;
                        if (!final_key_in_joined_record.isEmpty()) {
                            if (!results_after_where.isEmpty()) {
                                key_is_valid = results_after_where.first().allValues().contains(final_key_in_joined_record);
                            } else if (!joined_pre_where_results.isEmpty()) {
                                key_is_valid = joined_pre_where_results.first().allValues().contains(final_key_in_joined_record);
                            } else {
                                QPair<QString, QString> temp_pqc = parseQualifiedColumn(final_key_in_joined_record);
                                if (temp_pqc.first == table1_display_name && table1_ptr->has_field(temp_pqc.second)) key_is_valid = true;
                                else if (temp_pqc.first == table2_display_name && table2_ptr->has_field(temp_pqc.second)) key_is_valid = true;
                            }
                        }
                        if (key_is_valid) {
                            final_display_columns_join.append(display_name_for_col);
                            join_select_col_aliases[display_name_for_col] = final_key_in_joined_record;
                        } else {
                            textBuffer.append(QString("警告: SELECT 子句中的列 '%1' (解析为 '%2') 在JOIN结果中无效或找不到，已忽略。").arg(req_col_full, final_key_in_joined_record));
                        }
                    }
                }
                if(final_display_columns_join.isEmpty() && !requested_cols_with_aliases.isEmpty()) {
                    throw std::runtime_error("错误: SELECT 子句中所有指定的列均无效。");
                }
            }

            QList<QPair<QString, bool>> order_columns_join_list;
            if (!order_by_part_join.isEmpty()) {
                QStringList order_segments = order_by_part_join.split(',', Qt::SkipEmptyParts);
                for (const QString& segment_raw : order_segments) {
                    QString segment = segment_raw.trimmed();
                    bool descending = false;
                    QString col_name_to_order = segment;
                    if (segment.toUpper().endsWith(" DESC")) {
                        descending = true;
                        col_name_to_order = segment.left(segment.length() - 5).trimmed();
                    } else if (segment.toUpper().endsWith(" ASC")) {
                        col_name_to_order = segment.left(segment.length() - 4).trimmed();
                    }
                    col_name_to_order = cleanIdentifier(col_name_to_order);
                    order_columns_join_list.append(qMakePair(col_name_to_order, descending));
                }

                if (!order_columns_join_list.isEmpty()) {
                    std::sort(results_after_where.begin(), results_after_where.end(),
                        [&, join_select_col_aliases, table1_ptr, table1_display_name, table2_ptr, table2_display_name, order_columns_join_list](const xhyrecord& a, const xhyrecord& b) {
                        for (const auto& current_order_pair : order_columns_join_list) {
                            QString display_col_name_to_sort = current_order_pair.first;
                            QString actual_key_in_record_a = "";
                            QString actual_key_in_record_b = "";

                            if (join_select_col_aliases.contains(display_col_name_to_sort)) {
                                actual_key_in_record_a = join_select_col_aliases.value(display_col_name_to_sort);
                                actual_key_in_record_b = join_select_col_aliases.value(display_col_name_to_sort);
                            } else {
                                QPair<QString, QString> pqc_order = this->parseQualifiedColumn(display_col_name_to_sort);
                                QString table_alias_in_order = pqc_order.first;
                                QString column_in_order = pqc_order.second;
                                if (!table_alias_in_order.isEmpty()) {
                                    if (table_alias_in_order.compare(table1_display_name, Qt::CaseInsensitive) == 0 || table_alias_in_order.compare(table1_name_raw, Qt::CaseInsensitive) == 0) {
                                        actual_key_in_record_a = table1_display_name + "." + column_in_order;
                                    } else if (table_alias_in_order.compare(table2_display_name, Qt::CaseInsensitive) == 0 || table_alias_in_order.compare(table2_name_raw, Qt::CaseInsensitive) == 0) {
                                        actual_key_in_record_a = table2_display_name + "." + column_in_order;
                                    } else { actual_key_in_record_a = display_col_name_to_sort; }
                                    actual_key_in_record_b = actual_key_in_record_a;
                                } else {
                                    QString key_from_table1 = table1_display_name + "." + column_in_order;
                                    QString key_from_table2 = table2_display_name + "." + column_in_order;
                                    bool in_t1_a = a.allValues().contains(key_from_table1);
                                    bool in_t2_a = a.allValues().contains(key_from_table2);
                                    if (in_t1_a && !in_t2_a) actual_key_in_record_a = key_from_table1;
                                    else if (!in_t1_a && in_t2_a) actual_key_in_record_a = key_from_table2;
                                    else if (in_t1_a && in_t2_a) actual_key_in_record_a = key_from_table1;
                                    else actual_key_in_record_a = column_in_order;
                                    actual_key_in_record_b = actual_key_in_record_a;
                                }
                                if (!a.allValues().contains(actual_key_in_record_a) || !b.allValues().contains(actual_key_in_record_b) || actual_key_in_record_a.isEmpty() ) {
                                    if (a.allValues().contains(display_col_name_to_sort) && b.allValues().contains(display_col_name_to_sort)) {
                                        actual_key_in_record_a = display_col_name_to_sort;
                                        actual_key_in_record_b = display_col_name_to_sort;
                                    } else {
                                        qDebug() << "ORDER BY: Cannot resolve or find key for display column:" << display_col_name_to_sort;
                                        continue;
                                    }
                                }
                            }
                            if (actual_key_in_record_a.isEmpty() || actual_key_in_record_b.isEmpty() ||
                                !a.allValues().contains(actual_key_in_record_a) || !b.allValues().contains(actual_key_in_record_b)) {
                                qDebug() << "ORDER BY: Critical - Sort key is empty or not in record. Key A:" << actual_key_in_record_a << "Key B:" << actual_key_in_record_b;
                                continue;
                            }

                            QString val_a_str = a.value(actual_key_in_record_a);
                            QString val_b_str = b.value(actual_key_in_record_b);
                            xhyfield::datatype type_for_comp = this->getFieldTypeFromJoinedRecord(
                                actual_key_in_record_a, a, table1_ptr, table1_display_name, table2_ptr, table2_display_name
                            );
                            QVariant val_a_typed = table1_ptr->convertToTypedValue(val_a_str, type_for_comp);
                            QVariant val_b_typed = table1_ptr->convertToTypedValue(val_b_str, type_for_comp);

                            if (table1_ptr->compareQVariants(val_a_typed, val_b_typed, "!=")) {
                                if (current_order_pair.second) {
                                    return table1_ptr->compareQVariants(val_a_typed, val_b_typed, ">");
                                } else {
                                    return table1_ptr->compareQVariants(val_a_typed, val_b_typed, "<");
                                }
                            }
                        }
                        return false;
                    });
                }
            }

            QList<QStringList> output_rows_for_join;
            bool perform_join_whole_set_aggregation = !join_aggregate_funcs.isEmpty();

            if (perform_join_whole_set_aggregation) {
                 if (!results_after_where.isEmpty() || select_cols_str_join.contains("COUNT", Qt::CaseInsensitive)) {
                    xhyrecord aggregate_row;
                    for (const QString& display_col_name : final_display_columns_join) {
                        if (join_aggregate_funcs.contains(display_col_name)) {
                            QPair<QString, QString> func_pair = join_aggregate_funcs[display_col_name];
                            QString agg_arg_for_calc = func_pair.second;
                            QVariant agg_result = calculateAggregate(func_pair.first, agg_arg_for_calc, results_after_where);
                            aggregate_row.insert(display_col_name, agg_result.isValid() ? agg_result.toString() : "NULL");
                        } else {
                            aggregate_row.insert(display_col_name, results_after_where.isEmpty() ? "NULL" : results_after_where.first().value(join_select_col_aliases.value(display_col_name, display_col_name)));
                        }
                    }
                    QStringList agg_row_values;
                    for(const QString& dc : final_display_columns_join) {
                        agg_row_values.append(aggregate_row.value(dc));
                    }
                    output_rows_for_join.append(agg_row_values);
                }
            } else {
                for (const xhyrecord& rec : results_after_where) {
                    QStringList row_values;
                    for (const QString& display_col_name : final_display_columns_join) {
                        row_values.append(rec.value(join_select_col_aliases.value(display_col_name, display_col_name)));
                    }
                    output_rows_for_join.append(row_values);
                }
            }

            qDebug() << "[handleSelect JOIN] Before LIMIT: output_rows_for_join.size() =" << output_rows_for_join.size() << "limit_part_join =" << limit_part_join;
            if (!limit_part_join.isEmpty()) {
                bool ok_limit;
                int limit_val = limit_part_join.toInt(&ok_limit);
                qDebug() << "[handleSelect JOIN] Parsed limit_val =" << limit_val << "ok_limit =" << ok_limit;
                if (ok_limit && limit_val >= 0) {
                    if (limit_val < output_rows_for_join.size()) {
                         output_rows_for_join = output_rows_for_join.mid(0, limit_val);
                         qDebug() << "[handleSelect JOIN] After LIMIT applied: output_rows_for_join.size() =" << output_rows_for_join.size();
                    } else {
                         qDebug() << "[handleSelect JOIN] LIMIT value" << limit_val << "is not less than current row count" << output_rows_for_join.size() << ". No change by LIMIT.";
                    }
                } else if (!ok_limit || limit_val < 0) {
                    textBuffer.append("警告: 无效的 LIMIT 值 '" + limit_part_join + "'，已忽略。");
                }
            }

            if (final_display_columns_join.isEmpty() && !output_rows_for_join.isEmpty()) {
                 textBuffer.append("警告: 无法确定显示的列名，但有数据行。");
            } else if (final_display_columns_join.isEmpty() && output_rows_for_join.isEmpty()) {
                 textBuffer.append("查询成功，但没有选择任何列或没有符合条件的记录。");
            } else {
                QString header_line_join = final_display_columns_join.join("\t");
                textBuffer.append(header_line_join);
                textBuffer.append(QString(visualLength(header_line_join) > 0 ? visualLength(header_line_join) : 20, '-'));
                for (const QStringList& row : output_rows_for_join) {
                    textBuffer.append(row.join("\t"));
                }
                textBuffer.append(QString("\n%1 行记录已返回。").arg(output_rows_for_join.size()));
            }

        } catch (const std::runtime_error& e) {
            textBuffer.append("错误 (JOIN 查询处理): " + QString::fromStdString(e.what()));
        }
    } else {
        // ... (您现有的单表 SELECT 逻辑，这部分代码应该不需要改变，因为单表 ORDER BY 和 LIMIT 工作正常) ...
        // [确保将您完整的单表 SELECT 逻辑放在这里]
        qDebug() << "JOIN 语法不匹配，尝试作为单表 SELECT 处理...";
        QRegularExpression re_single(
            R"(SELECT\s+(.+?)\s+FROM\s+(\S+?)(?:\s+(?:AS\s+)?(?!ORDER\s+BY|WHERE|GROUP\s+BY|HAVING|LIMIT)([\w_`\[\]]+))?\s*(?:WHERE\s+(.+?))?\s*(?:GROUP\s+BY\s+([\w\d_`\[\]\.,\s]+))?\s*(?:HAVING\s+(.+?))?(?:\s+ORDER\s+BY\s+((?:[\w\d_`\[\]\.]+(?:\s+(?:ASC|DESC))?(?:\s*,\s*|$))+?))?(?:\s+LIMIT\s+(\d+))?\s*;?$)",
            //                                                                                                                                                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ (组7, ORDER BY - 修改为此模式)
            QRegularExpression::CaseInsensitiveOption
        );
        QRegularExpressionMatch match_single = re_single.match(trimmedCommand);

        if (!match_single.hasMatch()) {
            textBuffer.append("语法错误: 无法识别的 SELECT 语句。");
            return;
        }
        qDebug() << "单表 SELECT 匹配成功。";

        QString select_cols_str_s = match_single.captured(1).trimmed();
        QString table_name_s_orig = match_single.captured(2).trimmed();
        QString table_alias_s_orig = match_single.captured(3).trimmed();
        QString where_part_s = match_single.captured(4).trimmed();
        QString group_by_part_s = match_single.captured(5).trimmed();
        QString having_part_s = match_single.captured(6).trimmed();
        QString order_by_part_s = match_single.captured(7).trimmed();
        QString limit_part_s = match_single.captured(8).trimmed();

        QString table_name_s = cleanIdentifier(table_name_s_orig);
        QString table_alias_s = cleanIdentifier(table_alias_s_orig);
        QString table_display_name_s = table_alias_s.isEmpty() ? table_name_s : table_alias_s;


        xhytable* table_s_ptr = db->find_table(table_name_s);
        if (!table_s_ptr) {
            textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name_s, current_db_name));
            return;
        }

        ConditionNode conditionRoot_s;
        if (!where_part_s.isEmpty() && !parseWhereClause(where_part_s, conditionRoot_s)) {
            return;
        }

        QVector<xhyrecord> results_s;
        if (!table_s_ptr->selectData(conditionRoot_s, results_s)) {
            textBuffer.append(QString("从表 '%1' 选择数据时发生错误。").arg(table_name_s));
            return;
        }

        bool s_has_aggregate = false;
        QStringList s_display_columns;
        QMap<QString, QString> s_column_real_names;
        QMap<QString, QPair<QString, QString>> s_aggregate_funcs;

        if (select_cols_str_s == "*") {
            for(const xhyfield& field : table_s_ptr->fields()) {
                s_display_columns.append(field.name());
                s_column_real_names[field.name()] = field.name();
            }
        } else {
            QStringList cols_to_parse = select_cols_str_s.split(',', Qt::SkipEmptyParts);
            for (const QString& col_raw : cols_to_parse) {
                QString col_full = col_raw.trimmed();
                QString s_display_name = col_full;
                QString s_actual_expr = col_full;

                QRegularExpression as_re_s(R"((.+?)\s+AS\s+([\w_`\[\]]+)$)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch as_match_s = as_re_s.match(col_full);
                if (as_match_s.hasMatch()) {
                    s_actual_expr = as_match_s.captured(1).trimmed();
                    s_display_name = cleanIdentifier(as_match_s.captured(2).trimmed());
                }
                s_actual_expr = cleanIdentifier(s_actual_expr);

                QRegularExpression func_re_s(R"(^(COUNT|SUM|AVG|MIN|MAX)\s*\((.+)\)$)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch func_match_s = func_re_s.match(s_actual_expr);
                if (func_match_s.hasMatch()) {
                    s_has_aggregate = true;
                    QString func_name_s = func_match_s.captured(1).toUpper();
                    QString func_arg_s_raw = func_match_s.captured(2).trimmed();
                    QString func_arg_s = (func_arg_s_raw == "*") ? "*" : removeTableAlias(cleanIdentifier(func_arg_s_raw), table_display_name_s, table_name_s);


                    if (func_arg_s != "*" && !table_s_ptr->has_field(func_arg_s)) {
                        textBuffer.append(QString("错误: 聚合函数中的列 '%1' 在表 '%2' 中不存在。").arg(func_arg_s, table_name_s)); return;
                    }
                    s_aggregate_funcs[s_display_name] = qMakePair(func_name_s, func_arg_s);
                    s_display_columns.append(s_display_name);
                    s_column_real_names[s_display_name] = s_display_name;
                } else {
                    QString real_col_name_s = removeTableAlias(s_actual_expr, table_display_name_s, table_name_s);
                    if (!table_s_ptr->has_field(real_col_name_s)) {
                        textBuffer.append(QString("错误: 选择的列 '%1' (解析为 '%2') 在表 '%3' 中不存在。").arg(s_actual_expr, real_col_name_s, table_name_s)); return;
                    }
                    s_display_columns.append(s_display_name);
                    s_column_real_names[s_display_name] = real_col_name_s;
                }
            }
        }

        QVector<xhyrecord> final_results_s = results_s;

        if (!group_by_part_s.isEmpty() || s_has_aggregate) {
             QStringList s_group_by_cols_list;
            if(!group_by_part_s.isEmpty()){
                s_group_by_cols_list = group_by_part_s.split(',', Qt::SkipEmptyParts);
                for(QString& gb_col : s_group_by_cols_list) {
                    gb_col = removeTableAlias(cleanIdentifier(gb_col.trimmed()), table_display_name_s, table_name_s);
                    if(!table_s_ptr->has_field(gb_col)){
                        textBuffer.append(QString("错误: GROUP BY 列 '%1' 不存在。").arg(gb_col)); return;
                    }
                }
            }

            if (s_has_aggregate && s_group_by_cols_list.isEmpty()) {
                if (!results_s.isEmpty() || select_cols_str_s.toUpper().contains("COUNT")) {
                    xhyrecord agg_row_s;
                    for(const QString& disp_col_s : s_display_columns){
                        if(s_aggregate_funcs.contains(disp_col_s)){
                            QPair<QString,QString> func_p = s_aggregate_funcs[disp_col_s];
                            QVariant agg_v = calculateAggregate(func_p.first, func_p.second, results_s);
                            agg_row_s.insert(disp_col_s, agg_v.isValid() ? agg_v.toString() : "NULL");
                        } else {
                            agg_row_s.insert(disp_col_s, results_s.isEmpty() ? "NULL" : results_s.first().value(s_column_real_names.value(disp_col_s)));
                        }
                    }
                    final_results_s.clear();
                    final_results_s.append(agg_row_s);
                } else {
                    final_results_s.clear();
                }
            } else if (!s_group_by_cols_list.isEmpty()){
                QMap<QList<QString>, QVector<xhyrecord>> grouped_map_s;
                for(const xhyrecord& rec_s : results_s){
                    QList<QString> key_s;
                    for(const QString& gb_col_s : s_group_by_cols_list) key_s.append(rec_s.value(gb_col_s));
                    grouped_map_s[key_s].append(rec_s);
                }

                QVector<xhyrecord> grouped_results_s_temp;
                for(auto it = grouped_map_s.begin(); it != grouped_map_s.end(); ++it){
                    xhyrecord grouped_row_s;
                    const QList<QString>& current_group_key = it.key();
                    const QVector<xhyrecord>& records_in_group = it.value();

                    for(const QString& disp_col_s : s_display_columns){
                        if(s_aggregate_funcs.contains(disp_col_s)){
                            QPair<QString,QString> func_p = s_aggregate_funcs[disp_col_s];
                            QVariant agg_v = calculateAggregate(func_p.first, func_p.second, records_in_group);
                            grouped_row_s.insert(disp_col_s, agg_v.isValid() ? agg_v.toString() : "NULL");
                        } else {
                            int gb_idx = s_group_by_cols_list.indexOf(s_column_real_names.value(disp_col_s));
                            if(gb_idx != -1){
                                grouped_row_s.insert(disp_col_s, current_group_key.at(gb_idx));
                            } else {
                                textBuffer.append(QString("错误: SELECT 列 '%1' 未包含在聚合函数或 GROUP BY 子句中。").arg(disp_col_s)); return;
                            }
                        }
                    }
                    grouped_results_s_temp.append(grouped_row_s);
                }
                final_results_s = grouped_results_s_temp;

                if(!having_part_s.isEmpty() && !final_results_s.isEmpty()){
                     textBuffer.append("提示: 单表查询的 HAVING 功能需要您根据原有逻辑填充。");
                }
            }
        }

        if (!order_by_part_s.isEmpty()) {
            QList<QPair<QString, bool>> order_cols_s;
            QStringList order_segs_s = order_by_part_s.split(',', Qt::SkipEmptyParts);
            for (const QString& seg_raw_s : order_segs_s){
                QString seg_s = seg_raw_s.trimmed();
                bool desc_s = false;
                QString col_to_order_s = seg_s;
                if(seg_s.toUpper().endsWith(" DESC")){ desc_s = true; col_to_order_s = seg_s.left(seg_s.length()-5).trimmed();}
                else if(seg_s.toUpper().endsWith(" ASC")){ col_to_order_s = seg_s.left(seg_s.length()-4).trimmed();}

                if (s_display_columns.contains(cleanIdentifier(col_to_order_s))) {
                     order_cols_s.append(qMakePair(cleanIdentifier(col_to_order_s), desc_s));
                } else {
                    QString real_col_for_order = removeTableAlias(cleanIdentifier(col_to_order_s), table_display_name_s, table_name_s);
                    if (table_s_ptr->has_field(real_col_for_order)) {
                        order_cols_s.append(qMakePair(real_col_for_order, desc_s));
                    } else {
                         textBuffer.append(QString("警告: ORDER BY 列 '%1' 无效。").arg(col_to_order_s));
                    }
                }
            }
            if(!order_cols_s.isEmpty()){
                std::sort(final_results_s.begin(), final_results_s.end(), [&](const xhyrecord& a, const xhyrecord& b){
                    for(const auto& order_p_s : order_cols_s){
                        QString sort_key_ref = order_p_s.first;
                        QString actual_sort_key = s_column_real_names.value(sort_key_ref, sort_key_ref);

                        QString val_a_s_str = a.value(actual_sort_key);
                        QString val_b_s_str = b.value(actual_sort_key);

                        xhyfield::datatype type_s;
                        if(s_aggregate_funcs.contains(sort_key_ref)){
                            bool okNumA, okNumB;
                            val_a_s_str.toDouble(&okNumA); val_b_s_str.toDouble(&okNumB);
                            type_s = (okNumA && okNumB) ? xhyfield::DOUBLE : xhyfield::VARCHAR;
                        } else {
                            type_s = table_s_ptr->getFieldType(actual_sort_key);
                        }

                        QVariant val_a_s_typed = table_s_ptr->convertToTypedValue(val_a_s_str, type_s);
                        QVariant val_b_s_typed = table_s_ptr->convertToTypedValue(val_b_s_str, type_s);

                        if (table_s_ptr->compareQVariants(val_a_s_typed,val_b_s_typed,"!=")) {
                             if (order_p_s.second) { // Descending
                                return table_s_ptr->compareQVariants(val_a_s_typed, val_b_s_typed, ">");
                            } else { // Ascending
                                return table_s_ptr->compareQVariants(val_a_s_typed, val_b_s_typed, "<");
                            }
                        }
                    }
                    return false;
                });
            }
        }

        QList<QStringList> output_rows_s_final;
        for(const auto& rec_s : final_results_s) {
            QStringList row_parts_s;
            for(const QString& disp_col_name_s : s_display_columns) {
                row_parts_s.append(rec_s.value(disp_col_name_s));
            }
            output_rows_s_final.append(row_parts_s);
        }

        if (!limit_part_s.isEmpty()) {
            bool ok_limit_s;
            int limit_val_s = limit_part_s.toInt(&ok_limit_s);
            if (ok_limit_s && limit_val_s >= 0 ) {
                 if (limit_val_s < output_rows_s_final.size()) {
                    output_rows_s_final = output_rows_s_final.mid(0, limit_val_s);
                 }
            } else if (!ok_limit_s || limit_val_s < 0) {
                textBuffer.append("警告: 无效的 LIMIT 值 '" + limit_part_s + "'，已忽略。");
            }
        }

        if (s_display_columns.isEmpty() && !output_rows_s_final.isEmpty()) textBuffer.append("警告: (单表) 无法确定显示的列名。");
        else if (s_display_columns.isEmpty() && output_rows_s_final.isEmpty()) textBuffer.append("(单表) 没有数据或列被选择。");
        else {
            QString header_s_final = s_display_columns.join("\t");
            textBuffer.append(header_s_final);
            textBuffer.append(QString(visualLength(header_s_final) > 0 ? visualLength(header_s_final) : 20, '-'));
            for(const QStringList& row_s : output_rows_s_final) {
                textBuffer.append(row_s.join("\t"));
            }
            textBuffer.append(QString("\n%1 行记录已返回。").arg(output_rows_s_final.size()));
        }
    }
}



void MainWindow::handleAlterTable(const QString& command) {
    QRegularExpression re(
        R"(ALTER\s+TABLE\s+([\w_]+)\s+(ADD|DROP|ALTER|MODIFY|RENAME)\s+(?:COLUMN\s+)?(.+?)\s*;?$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        textBuffer.append("Syntax Error: ALTER TABLE <TableName> ADD|DROP|ALTER|MODIFY|RENAME [COLUMN] <parameters>");
        return;
    }

    QString table_name = match.captured(1).trimmed();
    QString action = match.captured(2).trimmed().toUpper();
    QString parameters = match.captured(3).trimmed();
    QString current_db_name = db_manager.get_current_database();

    if (current_db_name.isEmpty()) {
        textBuffer.append("Error: No database selected. Use 'USE <database_name>;' first.");
        return;
    }

    bool transactionStartedHere = false;
    if (!db_manager.isInTransaction()) {
        if (db_manager.beginTransaction()) {
            transactionStartedHere = true;
        } else {
            textBuffer.append("Error: Failed to start transaction for ALTER TABLE.");
            return;
        }
    }

    bool success = false;
    QString result_msg;

    try {
        if (action == "ADD") {
            if (parameters.toUpper().startsWith("CONSTRAINT")) {
                xhydatabase* db = db_manager.find_database(current_db_name);
                if (!db) throw std::runtime_error("Current database not found during ALTER TABLE ADD CONSTRAINT.");
                xhytable* table_ptr = db->find_table(table_name);
                if (!table_ptr) throw std::runtime_error(("Table '" + table_name + "' not found for ADD CONSTRAINT.").toStdString());
                handleTableConstraint(parameters, *table_ptr);
                success = db_manager.update_table(current_db_name, *table_ptr);
                result_msg = success ? "Table-level constraint added." : "Failed to add or persist table-level constraint.";
                if (!success) throw std::runtime_error(result_msg.toStdString());
            } else { // ADD COLUMN
                QString working_param_str = parameters.trimmed();
                QString field_name, type_str_full, col_constraints_str;

                int first_space_idx_ac = working_param_str.indexOf(QRegularExpression(R"(\s)"));
                if (first_space_idx_ac == -1) {
                    throw std::runtime_error("ADD COLUMN syntax error: Missing type for column. Parameters: " + parameters.toStdString());
                }
                field_name = working_param_str.left(first_space_idx_ac);
                working_param_str = working_param_str.mid(first_space_idx_ac).trimmed();

                int type_param_paren_open_idx_ac = working_param_str.indexOf('(');
                int first_space_after_base_type_idx_ac = working_param_str.indexOf(QRegularExpression(R"(\s)"));
                if (type_param_paren_open_idx_ac != -1 &&
                    (first_space_after_base_type_idx_ac == -1 || type_param_paren_open_idx_ac < first_space_after_base_type_idx_ac)) {
                    int balance_ac = 0; int end_of_type_params_idx_ac = -1; bool in_str_lit_ac = false; QChar str_char_ac = ' ';
                    for (int i = type_param_paren_open_idx_ac; i < working_param_str.length(); ++i) {
                        QChar c = working_param_str[i];
                        if (c == '\'' || c == '"') { if (in_str_lit_ac && c == str_char_ac) {if (i>0 && working_param_str[i-1]=='\\'){} else in_str_lit_ac = false;} else if (!in_str_lit_ac) {in_str_lit_ac=true; str_char_ac=c;} }
                        else if (c == '(' && !in_str_lit_ac) balance_ac++;
                        else if (c == ')' && !in_str_lit_ac) { balance_ac--; if (balance_ac == 0 && i >= type_param_paren_open_idx_ac) { end_of_type_params_idx_ac = i; break; } }
                    }
                    if (end_of_type_params_idx_ac != -1) {
                        type_str_full = working_param_str.left(end_of_type_params_idx_ac + 1);
                        col_constraints_str = working_param_str.mid(end_of_type_params_idx_ac + 1).trimmed();
                    } else {
                        throw std::runtime_error(QString("Mismatched parentheses in ADD COLUMN type definition for '%1': '%2'").arg(field_name, working_param_str).toStdString());
                    }
                } else {
                    if (first_space_after_base_type_idx_ac != -1) {
                        type_str_full = working_param_str.left(first_space_after_base_type_idx_ac).trimmed();
                        col_constraints_str = working_param_str.mid(first_space_after_base_type_idx_ac).trimmed();
                    } else {
                        type_str_full = working_param_str.trimmed(); col_constraints_str = "";
                    }
                }

                QStringList auto_gen_constraints_alt; QString type_parse_error_alt;
                xhyfield::datatype type_alt = parseDataTypeAndParams(type_str_full, auto_gen_constraints_alt, type_parse_error_alt);
                if (!type_parse_error_alt.isEmpty()) {
                    throw std::runtime_error(QString("Error parsing type for new column '%1' ('%2'): %3").arg(field_name, type_str_full, type_parse_error_alt).toStdString());
                }
                QStringList user_defined_col_constraints_alt = parseConstraints(col_constraints_str);
                QStringList final_constraints_alt = auto_gen_constraints_alt + user_defined_col_constraints_alt;
                final_constraints_alt.removeDuplicates();

                qDebug() << "[handleAlterTable ADD] For field '" << field_name << "', type_str_full: '" << type_str_full << "'";
                qDebug() << "  Auto-generated constraints:" << auto_gen_constraints_alt;
                qDebug() << "  User-defined constraints:" << user_defined_col_constraints_alt;
                qDebug() << "  Final constraints for xhyfield:" << final_constraints_alt;

                xhyfield new_field_obj;
                if (type_alt == xhyfield::ENUM) {
                    QRegularExpression enum_values_re_alt(R"(ENUM\s*\((.+)\)\s*$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                    QRegularExpressionMatch enum_match_alt = enum_values_re_alt.match(type_str_full);
                    if (enum_match_alt.hasMatch()) {
                        QString enum_content_alt = enum_match_alt.captured(1).trimmed();
                        QStringList enum_vals_list_alt = parseSqlValues(enum_content_alt);
                        if (enum_vals_list_alt.isEmpty() && !enum_content_alt.isEmpty() && enum_content_alt != "''" && enum_content_alt != "\"\"") {
                            throw std::runtime_error(QString("Error: ENUM field '%1' has empty or malformed value list: %2").arg(field_name, enum_content_alt).toStdString());
                        }
                        xhyfield temp_enum_field(field_name, type_alt, final_constraints_alt);
                        temp_enum_field.set_enum_values(enum_vals_list_alt);
                        new_field_obj = temp_enum_field;
                    } else {
                        throw std::runtime_error(QString("Error: ENUM field '%1' definition invalid. Expected ENUM('val1',...). Got: '%2'").arg(field_name, type_str_full).toStdString());
                    }
                } else {
                    new_field_obj = xhyfield(field_name, type_alt, final_constraints_alt);
                }
                success = db_manager.add_column(current_db_name, table_name, new_field_obj);
                result_msg = success ? QString("Column '%1' added to table '%2'.").arg(field_name, table_name)
                                     : QString("Failed to add column '%1'. It might already exist or definition is invalid.").arg(field_name);
                if (!success) throw std::runtime_error(result_msg.toStdString());
            }
        } else if (action == "DROP") {
            // (您的 DROP COLUMN / DROP CONSTRAINT 逻辑，确保也使用 try-catch 和事务处理)
            if (parameters.toUpper().startsWith("CONSTRAINT")) {
                QRegularExpression dropConstrRe(R"(CONSTRAINT\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch dropMatch = dropConstrRe.match(parameters);
                if(!dropMatch.hasMatch()) throw std::runtime_error("Syntax Error: DROP CONSTRAINT <constraint_name>");
                QString constr_name = dropMatch.captured(1).trimmed();
                success = db_manager.drop_constraint(current_db_name, table_name, constr_name);
                result_msg = success ? QString("Constraint '%1' dropped.").arg(constr_name) : QString("Failed to drop constraint '%1'.").arg(constr_name);
            } else {
                QString field_name_to_drop = parameters.trimmed();
                if (field_name_to_drop.contains(QRegularExpression("\\s"))) throw std::runtime_error("Syntax Error: DROP COLUMN <column_name> (column name cannot contain spaces)");
                success = db_manager.drop_column(current_db_name, table_name, field_name_to_drop);
                result_msg = success ? QString("Column '%1' dropped from table '%2'.").arg(field_name_to_drop, table_name)
                                     : QString("Failed to drop column '%1'.").arg(field_name_to_drop);
            }
            if (!success) throw std::runtime_error(result_msg.toStdString());
        } else if (action == "ALTER" || action == "MODIFY") {
            // (您的 MODIFY COLUMN 逻辑，确保也使用新的类型解析和事务处理)
            // 这是一个复杂的操作，涉及到旧列查找、新类型解析、数据转换（如果类型改变）、约束更新等。
            // 下面是一个简化的骨架，您需要根据您的 db_manager.alter_column 实现来填充。
            QRegularExpression alterColRe(R"(([\w_]+)\s+(?:TYPE\s+)?([\w\s\(\),'"\-\\]+))", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch alterMatch = alterColRe.match(parameters);
            if(!alterMatch.hasMatch()) throw std::runtime_error("Syntax Error: ALTER/MODIFY COLUMN <col_name> <new_type_definition>");

            QString old_field_name = alterMatch.captured(1).trimmed();
            QString new_type_str_full = alterMatch.captured(2).trimmed();
            QStringList auto_gen_constraints_modify; QString type_parse_error_modify;
            xhyfield::datatype new_type = parseDataTypeAndParams(new_type_str_full, auto_gen_constraints_modify, type_parse_error_modify);
            if (!type_parse_error_modify.isEmpty()) throw std::runtime_error(QString("Error parsing new type for column '%1' ('%2'): %3").arg(old_field_name, new_type_str_full, type_parse_error_modify).toStdString());

            // 获取旧字段的非类型约束，与 auto_gen_constraints_modify 合并 -> final_constraints_modify
            // 这部分逻辑比较复杂，需要您根据实际情况实现
            // xhyfield modified_field_obj(old_field_name, new_type, final_constraints_modify);
            // ... (ENUM value setting if new_type is ENUM) ...
            // success = db_manager.alter_column(current_db_name, table_name, old_field_name, modified_field_obj);
            // result_msg = ...
            textBuffer.append("注意: ALTER/MODIFY COLUMN 的完整实现较为复杂，此处为简化结构。");
            success = false; // 暂时标记为不成功，直到您完成实现
            result_msg = "ALTER/MODIFY COLUMN not fully implemented yet.";
            if (!success) throw std::runtime_error(result_msg.toStdString());

        } else if (action == "RENAME") {
            // (您的 RENAME COLUMN / RENAME TABLE TO 逻辑)
            if (parameters.toUpper().startsWith("COLUMN")) {
                QRegularExpression renameColRe(R"(COLUMN\s+([\w_]+)\s+TO\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch renameMatch = renameColRe.match(parameters);
                if(!renameMatch.hasMatch()) throw std::runtime_error("Syntax Error: RENAME COLUMN <old_name> TO <new_name>");
                QString old_col_name = renameMatch.captured(1).trimmed();
                QString new_col_name = renameMatch.captured(2).trimmed();
                success = db_manager.rename_column(current_db_name, table_name, old_col_name, new_col_name);
                result_msg = success ? QString("Column '%1' renamed to '%2'.").arg(old_col_name, new_col_name)
                                     : QString("Failed to rename column '%1'.").arg(old_col_name);
            } else if (parameters.toUpper().startsWith("TO")) {
                QRegularExpression renameTableRe(R"(TO\s+([\w_]+))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch renameMatch = renameTableRe.match(parameters);
                if(!renameMatch.hasMatch()) throw std::runtime_error("Syntax Error: RENAME TO <new_table_name>");
                QString new_table_name = renameMatch.captured(1).trimmed();
                success = db_manager.rename_table(current_db_name, table_name, new_table_name);
                result_msg = success ? QString("Table '%1' renamed to '%2'.").arg(table_name, new_table_name)
                                     : QString("Failed to rename table '%1'.").arg(table_name);
            } else {
                throw std::runtime_error("Unsupported RENAME syntax.");
            }
            if (!success) throw std::runtime_error(result_msg.toStdString());
        } else {
            throw std::runtime_error(("Unsupported ALTER TABLE action: " + action).toStdString());
        }

        if (transactionStartedHere) {
            if (db_manager.commitTransaction()) {
                textBuffer.append(result_msg + " Transaction committed.");
            } else {
                textBuffer.append(result_msg + " BUT Transaction commit FAILED. Database might be inconsistent.");
            }
        } else {
            textBuffer.append(result_msg + " (executed within existing transaction or no transaction).");
        }

    } catch (const std::runtime_error& e) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("ALTER TABLE Error: " + QString::fromStdString(e.what()));
    } catch (...) {
        if (transactionStartedHere) db_manager.rollbackTransaction();
        textBuffer.append("An unknown error occurred during ALTER TABLE.");
    }
}


void MainWindow::handleExplainSelect(const QString& command) {
    QRegularExpression re(R"(EXPLAIN\s+SELECT\s+\*\s+FROM\s+([\w_]+)(?:\s+WHERE\s+(.+))?\s*;?)",
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch match = re.match(command.trimmed());

    if (!match.hasMatch()) {
        textBuffer.append("语法错误: EXPLAIN SELECT * FROM <表名> [WHERE <条件>]");
        return;
    }

    QString tableName = match.captured(1).trimmed();
    QString wherePart = match.captured(2).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    xhydatabase* db = db_manager.find_database(current_db_name);
    if (!db) { textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }
    xhytable* table = db->find_table(tableName);
    if (!table) { textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(tableName, current_db_name)); return; }

    ConditionNode conditionRootForExplain;
    if (!parseWhereClause(wherePart, conditionRootForExplain)) { // **Aufruf**
        return;
    }

    QString firstFieldNameInCondition;
    if (conditionRootForExplain.type == ConditionNode::COMPARISON_OP) {
        firstFieldNameInCondition = conditionRootForExplain.comparison.fieldName;
    } else if (conditionRootForExplain.type == ConditionNode::LOGIC_OP && !conditionRootForExplain.children.isEmpty()) {
        const ConditionNode* firstComparableChild = nullptr;
        QStack<const ConditionNode*> stack;
        stack.push(&conditionRootForExplain);
        while(!stack.isEmpty()){
            const ConditionNode* current = stack.pop();
            if(current->type == ConditionNode::COMPARISON_OP){
                firstComparableChild = current;
                break;
            }
            for(const auto& child : current->children) stack.push(&child);
        }
        if(firstComparableChild) firstFieldNameInCondition = firstComparableChild->comparison.fieldName;
    }

    if (!firstFieldNameInCondition.isEmpty()) {
        const xhyindex* index = db->findIndex(firstFieldNameInCondition);
        if (index && index->tableName().compare(tableName, Qt::CaseInsensitive) == 0) {
            textBuffer.append(QString("查询计划: 可能使用字段 '%1' 上的索引 '%2'。").arg(firstFieldNameInCondition, index->name()));
        } else {
            textBuffer.append(QString("查询计划: 字段 '%1' 上没有直接可用的索引，或索引不属于表 '%2'。可能进行全表扫描。").arg(firstFieldNameInCondition, tableName));
        }
    } else if (!wherePart.isEmpty()){
        textBuffer.append("查询计划: 无法从WHERE条件中简单提取用于索引检查的字段，可能进行全表扫描。");
    } else {
        textBuffer.append("查询计划: 无WHERE条件，将进行全表扫描。");
    }

    textBuffer.append("模拟执行查询以验证...");
    QVector<xhyrecord> results;
    if (db_manager.selectData(current_db_name, tableName, conditionRootForExplain, results)) {
        QString header_str;
        for (const auto& field : table->fields()) { header_str += field.name() + "\t"; }
        textBuffer.append(header_str.trimmed());
        textBuffer.append(QString(header_str.length()*2 < 80 ? header_str.length()*2 : 80, '-'));
        if (results.isEmpty()) {
            textBuffer.append("(无符合条件的数据)");
        } else {
            for (const auto& record : results) {
                QString row_str;
                for (const auto& field : table->fields()) { row_str += record.value(field.name()) + "\t"; }
                textBuffer.append(row_str.trimmed());
            }
            textBuffer.append(QString("共 %1 行结果。").arg(results.size()));
        }
    } else {
        textBuffer.append("执行 EXPLAIN 的模拟查询时出错。");
    }
}


void MainWindow::handleCreateIndex(const QString& command) {
    QRegularExpression re(R"(CREATE\s+(UNIQUE\s+)?INDEX\s+([\w_]+)\s+ON\s+([\w_]+)\s*\((.+)\)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if (!match.hasMatch()) {
        textBuffer.append("语法错误: CREATE [UNIQUE] INDEX <索引名> ON <表名> (<列1>[, <列2>...])");
        return;
    }
    bool unique = !match.captured(1).trimmed().isEmpty();
    QString idxname = match.captured(2).trimmed();
    QString tablename = match.captured(3).trimmed();
    QString colsPart = match.captured(4).trimmed();
    QStringList cols = colsPart.split(',', Qt::SkipEmptyParts);

    for(auto &c : cols) { c = c.trimmed().split(" ").first(); if (c.isEmpty()) {textBuffer.append("错误：索引列名不能为空。"); return;} }
    if (cols.isEmpty()){ textBuffer.append("错误：索引必须至少包含一列。"); return; }

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }

    auto* db = db_manager.find_database(current_db_name);
    if (!db) { textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return; }

    xhytable* table = db->find_table(tablename);
    if (!table) { textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(tablename, current_db_name)); return; }

    for(const auto &col : cols) {
        if (!table->has_field(col)) {
            textBuffer.append(QString("错误: 字段 '%1' 在表 '%2' 中不存在。").arg(col, tablename)); return;
        }
    }

    if(db->createIndex(xhyindex(idxname, tablename, cols, unique))) {
        textBuffer.append(QString("索引 '%1' 在表 '%2' 上创建成功。").arg(idxname, tablename));
    } else {
        textBuffer.append(QString("错误：创建索引 '%1' 失败 (可能已存在或名称/列定义无效)。").arg(idxname));
    }
}

void MainWindow::handleDropIndex(const QString& command) {
    QRegularExpression re(R"(DROP\s+INDEX\s+([\w_]+)(?:\s+ON\s+([\w_]+))?\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if(!match.hasMatch()){
        textBuffer.append("语法错误: DROP INDEX <索引名> [ON <表名>]");
        return;
    }
    QString idxname = match.captured(1).trimmed();
    QString onTableName = match.captured(2).trimmed();

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
    auto* db = db_manager.find_database(current_db_name);
    if(!db){ textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return;}

    if(!onTableName.isEmpty()){
        const xhyindex* idxToDrop = db->findIndexByName(idxname);
        if(idxToDrop && idxToDrop->tableName().compare(onTableName, Qt::CaseInsensitive) != 0){
            textBuffer.append(QString("错误: 索引 '%1' 不属于表 '%2'。").arg(idxname, onTableName));
            return;
        }
    }

    if(db->dropIndex(idxname)) {
        textBuffer.append(QString("索引 '%1' 已删除。").arg(idxname));
    } else {
        textBuffer.append(QString("错误：删除索引 '%1' 失败 (可能不存在)。").arg(idxname));
    }
}

void MainWindow::handleShowIndexes(const QString& command) {
    QString filterTableName;
    QRegularExpression re(R"(SHOW\s+INDEXES\s+(?:FROM|ON)\s+([\w_]+)\s*;?)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(command.trimmed());
    if(match.hasMatch()){
        filterTableName = match.captured(1).trimmed();
    }

    QString current_db_name = db_manager.get_current_database();
    if (current_db_name.isEmpty()) { textBuffer.append("错误: 未选择数据库。"); return; }
    auto* db = db_manager.find_database(current_db_name);
    if(!db){ textBuffer.append("错误: 数据库 '" + current_db_name + "' 未找到。"); return;}

    QList<xhyindex> indexes = db->allIndexes();
    if(indexes.isEmpty()){
        textBuffer.append("数据库 '" + current_db_name + "' 中没有索引。");
        return;
    }

    QString output = "索引列表 (数据库: " + current_db_name;
    if(!filterTableName.isEmpty()) output += ", 表: " + filterTableName;
    output += "):\n";
    output += QString("%1\t%2\t%3\t%4\n").arg("索引名", -20).arg("表名", -20).arg("列名", -30).arg("是否唯一");
    output += QString(80, '-') + "\n";

    bool foundAny = false;
    for(const auto& idx : indexes){
        if (filterTableName.isEmpty() || idx.tableName().compare(filterTableName, Qt::CaseInsensitive) == 0) {
            output += QString("%1\t%2\t%3\t%4\n")
            .arg(idx.name(), -20)
                .arg(idx.tableName(), -20)
                .arg(idx.columns().join(", "), -30)
                .arg(idx.isUnique() ? "是" : "否");
            foundAny = true;
        }
    }

    if (!foundAny) {
        if (!filterTableName.isEmpty()) {
            textBuffer.append(QString("表 '%1' 上没有找到索引。").arg(filterTableName));
        } else {
            textBuffer.append("当前数据库没有符合条件的索引。");
        }
    } else {
        textBuffer.append(output);
    }
}

QStringList MainWindow::parseSqlValues(const QString &input_raw) {
    QString input = input_raw.trimmed();
    QStringList values;
    QString current_value;
    bool in_string_literal = false;
    QChar quote_char = QChar::Null;

    for (int i = 0; i < input.length(); ++i) {
        QChar ch = input[i];

        if (in_string_literal) {
            if (ch == quote_char) {
                if (i + 1 < input.length() && input[i + 1] == quote_char) {
                    current_value += quote_char;
                    i++;
                } else {
                    in_string_literal = false;
                }
            } else {
                current_value += ch;
            }
        } else {
            if (ch == '\'' || ch == '"') {
                in_string_literal = true;
                quote_char = ch;
            } else if (ch == ',') {
                values.append(current_value.trimmed());
                current_value.clear();
            } else {
                current_value += ch;
            }
        }
    }
    values.append(current_value.trimmed());
    return values;
}

xhyfield::datatype MainWindow::parseDataType(const QString& type_str_input, int* size_ptr) {
    QString type_str = type_str_input.trimmed();
    QString upperType = type_str.toUpper();
    if (size_ptr) *size_ptr = 0;

    QRegularExpression typeWithSizeRe(R"((VARCHAR|CHAR|DECIMAL|NUMERIC)\s*\(\s*(\d+)(?:\s*,\s*(\d+))?\s*\))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression typeOnlyRe(R"([\w_]+)");

    QRegularExpressionMatch match = typeWithSizeRe.match(type_str);
    QString actualType;
    int size1 = 0;

    if (match.hasMatch()) {
        actualType = match.captured(1).toUpper();
        size1 = match.captured(2).toInt();
        if (size_ptr) *size_ptr = size1;
    } else {
        match = typeOnlyRe.match(upperType);
        if(match.hasMatch()){
            actualType = match.captured(0);
        } else {
            qWarning() << "无法解析数据类型：" << type_str;
            return xhyfield::VARCHAR;
        }
    }

    if (actualType == "INT" || actualType == "INTEGER") return xhyfield::INT;
    if (actualType == "TINYINT") return xhyfield::TINYINT;
    if (actualType == "SMALLINT") return xhyfield::SMALLINT;
    if (actualType == "BIGINT") return xhyfield::BIGINT;
    if (actualType == "FLOAT") return xhyfield::FLOAT;
    if (actualType == "DOUBLE" || actualType == "REAL") return xhyfield::DOUBLE;
    if (actualType == "DECIMAL" || actualType == "NUMERIC") {
        return xhyfield::DECIMAL;
    }
    if (actualType == "CHAR") { if(size_ptr && size1 == 0) *size_ptr = 1; return xhyfield::CHAR;}
    if (actualType == "VARCHAR") { if(size_ptr && size1 == 0) *size_ptr = 255; return xhyfield::VARCHAR;}
    if (actualType == "TEXT") return xhyfield::TEXT;
    if (actualType == "DATE") return xhyfield::DATE;
    if (actualType == "DATETIME") return xhyfield::DATETIME;
    if (actualType == "TIMESTAMP") return xhyfield::TIMESTAMP;
    if (actualType == "BOOL" || actualType == "BOOLEAN") return xhyfield::BOOL;
    if (actualType == "ENUM") return xhyfield::ENUM;

    qWarning() << "未知的数据类型：" << actualType << " (来自: " << type_str_input << ")";
    return xhyfield::VARCHAR;
}

// mainwindow.cpp 或一个包含此定义的合适位置
namespace { // 匿名命名空间
namespace DefaultValueKeywords {
const QString SQL_NULL = "##SQL_NULL##";
const QString CURRENT_TIMESTAMP_KW = "##CURRENT_TIMESTAMP##";
 const QString CURRENT_DATE_KW = "##CURRENT_DATE##";
}
} // 结束匿名命名空间

QStringList MainWindow::parseConstraints(const QString& constraints_str_input) {
    QStringList parsed_constraints_for_xhyfield;
    QString current_processing_str = constraints_str_input.trimmed();
    qDebug() << "[解析约束] 开始处理列约束字符串: '" << constraints_str_input << "'";

    while (!current_processing_str.isEmpty()) {
        QString upper_current_str = current_processing_str.toUpper();
        bool constraint_found_in_this_iteration = false;
        int consumed_length_in_iteration = 0;

        if (upper_current_str.startsWith("NOT NULL")) {
            parsed_constraints_for_xhyfield.append("NOT_NULL");
            consumed_length_in_iteration = QString("NOT NULL").length();
            constraint_found_in_this_iteration = true;
            qDebug() << "  识别约束: NOT NULL";
        } else if (upper_current_str.startsWith("PRIMARY KEY")) {
            parsed_constraints_for_xhyfield.append("PRIMARY_KEY");
            consumed_length_in_iteration = QString("PRIMARY KEY").length();
            constraint_found_in_this_iteration = true;
            qDebug() << "  识别约束: PRIMARY KEY";
        } else if (upper_current_str.startsWith("UNIQUE")) {
            parsed_constraints_for_xhyfield.append("UNIQUE");
            consumed_length_in_iteration = QString("UNIQUE").length();
            constraint_found_in_this_iteration = true;
            qDebug() << "  识别约束: UNIQUE";
        } else if (upper_current_str.startsWith("DEFAULT ")) {
            QString default_keyword_prefix = current_processing_str.left(QString("DEFAULT ").length());
            QString value_part_candidate = current_processing_str.mid(default_keyword_prefix.length()).trimmed();
            QString actual_default_value_to_store;

            consumed_length_in_iteration = default_keyword_prefix.length(); // 先消耗 "DEFAULT "

            if (value_part_candidate.isEmpty()) {
                qWarning() << "  警告: DEFAULT 关键字后缺少值定义。";
                // consumed_length_in_iteration 保持不变，仅消耗了 "DEFAULT "
            } else {
                QRegularExpression keyword_null_re(R"(^(NULL)(?:\s+|,|$))", QRegularExpression::CaseInsensitiveOption);
                 QRegularExpression keyword_date_re(R"(^(CURRENT_DATE)(?:\s+|,|$))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpression keyword_ts_re(R"(^(CURRENT_TIMESTAMP)(?:\s+|,|$))", QRegularExpression::CaseInsensitiveOption);
                QRegularExpression literal_re(R"(^('([^']*(?:''[^']*)*)'|"([^"]*(?:""[^"]*)*)|[\w.+-]+)(?:\s+|,|$))", QRegularExpression::CaseInsensitiveOption);
                // 注意：上面的 [\w.+-]+ 用于匹配数字和可能的负数、小数，以及 TRUE/FALSE 等无引号标识符。
                // 如果你的 DEFAULT 值可以是更复杂的表达式，这里的 literal_re 可能需要调整。

                QRegularExpressionMatch match_result;

                if ((match_result = keyword_null_re.match(value_part_candidate)).hasMatch() && match_result.capturedStart() == 0) {
                    actual_default_value_to_store = DefaultValueKeywords::SQL_NULL;
                    consumed_length_in_iteration += match_result.capturedLength(1); // 消耗 "NULL"
                     qDebug() << "  识别 DEFAULT NULL";
                } else if ((match_result = keyword_ts_re.match(value_part_candidate)).hasMatch() && match_result.capturedStart() == 0) {
                    actual_default_value_to_store = DefaultValueKeywords::CURRENT_TIMESTAMP_KW;
                    consumed_length_in_iteration += match_result.capturedLength(1); // 消耗 "CURRENT_TIMESTAMP"
                    qDebug() << "  识别 DEFAULT CURRENT_TIMESTAMP";
                } else if ((match_result = keyword_date_re.match(value_part_candidate)).hasMatch() && match_result.capturedStart() == 0) { // <--- 新增对CURRENT_DATE的处理
                    actual_default_value_to_store = DefaultValueKeywords::CURRENT_DATE_KW; // 使用xhytable.cpp中定义的关键字
                    consumed_length_in_iteration += match_result.capturedLength(1); // 消耗 "CURRENT_DATE"
                    qDebug() << "  识别 DEFAULT CURRENT_DATE";
                } else if ((match_result = literal_re.match(value_part_candidate)).hasMatch() && match_result.capturedStart() == 0) {
                    QString captured_literal = match_result.captured(1).trimmed();
                    if ((captured_literal.startsWith('\'') && captured_literal.endsWith('\'')) ||
                        (captured_literal.startsWith('"') && captured_literal.endsWith('"'))) {
                        if (captured_literal.length() >= 2) {
                            actual_default_value_to_store = captured_literal.mid(1, captured_literal.length() - 2);
                            if (captured_literal.startsWith('\'')) actual_default_value_to_store.replace("''", "'");
                            else actual_default_value_to_store.replace("\"\"", "\"");
                        } else {
                            actual_default_value_to_store = ""; // 例如 DEFAULT ''
                        }
                    } else { // 数字, TRUE, FALSE, 或其他无引号字面量
                        actual_default_value_to_store = captured_literal;
                    }
                    consumed_length_in_iteration += match_result.capturedLength(1); // 消耗匹配到的字面量
                    qDebug() << "  识别 DEFAULT 字面量: '" << actual_default_value_to_store << "'";
                } else {
                    qWarning() << "  警告: DEFAULT 关键字后未能正确解析默认值，剩余部分: '" << value_part_candidate << "'";
                    // 尝试安全地消耗掉无法解析的部分，避免死循环
                    int next_separator_pos = value_part_candidate.indexOf(QRegularExpression(R"(\s|,|$)"));
                    if (next_separator_pos != -1) consumed_length_in_iteration += next_separator_pos;
                    else consumed_length_in_iteration += value_part_candidate.length();
                    actual_default_value_to_store.clear(); // 解析失败
                }
            }

            if (!actual_default_value_to_store.isNull()) { // 用 isNull() 检查，因为 clear() 后的空字符串也应该能被存储（代表 DEFAULT ''）
                parsed_constraints_for_xhyfield.append("DEFAULT");
                parsed_constraints_for_xhyfield.append(actual_default_value_to_store);
            }
            constraint_found_in_this_iteration = true;
        }
        else if (upper_current_str.startsWith("CHECK ")) {
            QString check_keyword_part = current_processing_str.left(QString("CHECK ").length());
            QString expr_part_candidate = current_processing_str.mid(check_keyword_part.length()).trimmed();
            consumed_length_in_iteration = check_keyword_part.length();

            if (expr_part_candidate.startsWith('(')) {
                int paren_balance = 0;
                int expr_end_idx = -1;
                bool in_check_string_literal = false;
                QChar check_string_char = ' ';

                for (int i = 0; i < expr_part_candidate.length(); ++i) {
                    QChar c = expr_part_candidate.at(i);
                    if (c == '\'' || c == '"') {
                        if (in_check_string_literal && c == check_string_char) {
                            if (i > 0 && expr_part_candidate.at(i - 1) == '\\') { /* escaped quote */ }
                            else in_check_string_literal = false;
                        } else if (!in_check_string_literal) {
                            in_check_string_literal = true; check_string_char = c;
                        }
                    } else if (c == '(' && !in_check_string_literal) {
                        paren_balance++;
                    } else if (c == ')' && !in_check_string_literal) {
                        paren_balance--;
                        if (paren_balance == 0 && i > 0) {
                            expr_end_idx = i; break;
                        }
                    }
                }

                if (expr_end_idx != -1 && paren_balance == 0) {
                    QString raw_expression = expr_part_candidate.mid(1, expr_end_idx - 1).trimmed();
                    if (!raw_expression.isEmpty()) {
                        // 存储规范格式 "CHECK(EXPRESSION)"，表达式本身大小写保留，关键字转大写
                        QString normalized_check_constraint = QString("CHECK(%1)").arg(raw_expression);
                        parsed_constraints_for_xhyfield.append(normalized_check_constraint);
                        consumed_length_in_iteration += (expr_end_idx + 1); // CHECK (expression)
                        qDebug() << "  识别 CHECK 约束: " << normalized_check_constraint;
                    } else {
                        qWarning() << "  警告: CHECK 约束括号内表达式为空: " << expr_part_candidate;
                         consumed_length_in_iteration += (expr_end_idx + 1); // 至少消耗掉 CHECK()
                    }
                } else {
                    qWarning() << "  警告: CHECK 约束括号不匹配或表达式不完整: " << expr_part_candidate;
                    // 安全消耗，避免死循环
                    int next_sep = expr_part_candidate.indexOf(QRegularExpression(R"(\s|,|$)"));
                    if (next_sep != -1) consumed_length_in_iteration += next_sep;
                    else consumed_length_in_iteration += expr_part_candidate.length();
                }
            } else {
                qWarning() << "  警告: CHECK 关键字后缺少括号表达式: " << expr_part_candidate;
                 // 安全消耗
                int next_sep = expr_part_candidate.indexOf(QRegularExpression(R"(\s|,|$)"));
                if (next_sep != -1) consumed_length_in_iteration += next_sep;
                else consumed_length_in_iteration += expr_part_candidate.length();
            }
            constraint_found_in_this_iteration = true;
        }

        if (!constraint_found_in_this_iteration && !current_processing_str.isEmpty()) {
            int next_space_or_comma = current_processing_str.indexOf(QRegularExpression(R"(\s|,)"));
            QString unknown_token = (next_space_or_comma == -1) ? current_processing_str : current_processing_str.left(next_space_or_comma);
            qWarning() << "  警告: 在列约束字符串中遇到未知或格式错误的标记: '" << unknown_token << "'";
            consumed_length_in_iteration = unknown_token.length();
            if (consumed_length_in_iteration == 0) { // 如果未知标记是空的，则消耗整个剩余字符串以避免死循环
                 qWarning() << "    [解析约束] 未知标记为空，但字符串非空，强制消耗剩余部分以防死循环: " << current_processing_str;
                consumed_length_in_iteration = current_processing_str.length();
            }
        }

        if (consumed_length_in_iteration == 0 && !current_processing_str.isEmpty()) {
            // 如果在一次迭代中没有消耗任何字符，但字符串仍然不为空，
            // 这可能意味着解析逻辑卡住了，强制跳出以避免无限循环。
            qWarning() << "错误: parseConstraints 可能进入死循环，当前未处理的字符串部分: '" << current_processing_str << "'。提前终止此列的约束解析。";
            break;
        }
        current_processing_str = current_processing_str.mid(consumed_length_in_iteration).trimmed();
    }
    qDebug() << "[解析约束] 完成，解析得到的约束列表: " << parsed_constraints_for_xhyfield;
    return parsed_constraints_for_xhyfield;
}

bool MainWindow::validateCheckExpression(const QString& expression) {
    // 简确保括号匹配
    int balance = 0;
    for (QChar c : expression) {
        if (c == '(') balance++;
        else if (c == ')') balance--;
        if (balance < 0) return false; // 括号不匹配
    }
    return balance == 0;
}

void MainWindow::handleTableConstraint(const QString &constraint_str_input, xhytable &table) {
    QString constraint_str = constraint_str_input.trimmed();
    qDebug() << "[handleTableConstraint] 开始解析表级约束: " << constraint_str;

    QString constraintName;
    QString constraintTypeKeyword; // PRIMARY KEY, UNIQUE, CHECK, FOREIGN KEY
    QString mainClause;            // 列列表或 CHECK 表达式或 FOREIGN KEY 的列列表
    QString remainingClause;       // FOREIGN KEY 的 REFERENCES 和 ON UPDATE/DELETE

    // 步骤 1: 提取可选的约束名和主要的约束类型关键字
    QRegularExpression constraintStartRe(
        R"(^(?:CONSTRAINT\s+([\w_]+)\s+)?(PRIMARY\s+KEY|UNIQUE|CHECK|FOREIGN\s+KEY)\s*(.*)$)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
        );

    QRegularExpressionMatch startMatch = constraintStartRe.match(constraint_str);

    if (!startMatch.hasMatch()) {
        if (constraint_str.toUpper().startsWith("CHECK")) {
            QRegularExpression checkOnlyRe(R"(^CHECK\s*\((.*)\)$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            QRegularExpressionMatch checkOnlyMatch = checkOnlyRe.match(constraint_str);
            if (checkOnlyMatch.hasMatch()) {
                constraintTypeKeyword = "CHECK";
                mainClause = checkOnlyMatch.captured(1).trimmed();
                remainingClause = "";
                qDebug() << "  检测到直接的 CHECK 约束。类型: " << constraintTypeKeyword << ", 表达式: " << mainClause;
            } else {
                textBuffer.append("错误: 无效的表级约束定义 (CHECK 格式错误): " + constraint_str);
                qWarning() << "  [handleTableConstraint] 无法解析约束 (CHECK 格式错误): " << constraint_str;
                // ***** MODIFICATION START *****
                // 提前返回或抛出异常，确保不会继续处理无效约束
                throw std::runtime_error(("无效的表级约束定义 (CHECK 格式错误): " + constraint_str).toStdString());
                // ***** MODIFICATION END *****
                // return; // 原来的 return，现在改为抛出异常
            }
        } else {
            textBuffer.append("错误: 无效的表级约束定义 (无法识别约束类型): " + constraint_str);
            qWarning() << "  [handleTableConstraint] 无法解析约束 (起始部分格式错误): " << constraint_str;
            // ***** MODIFICATION START *****
            throw std::runtime_error(("无效的表级约束定义 (无法识别约束类型): " + constraint_str).toStdString());
            // ***** MODIFICATION END *****
            // return;
        }
    } else {
        constraintName = startMatch.captured(1).trimmed();
        constraintTypeKeyword = startMatch.captured(2).trimmed().toUpper().replace(" ", "_");
        QString bodyAndRemainder = startMatch.captured(3).trimmed();

        qDebug() << "  初步解析: Name='" << constraintName << "', TypeKeyword='" << constraintTypeKeyword << "', BodyAndRemainder='" << bodyAndRemainder << "'";

        if (constraintTypeKeyword == "PRIMARY_KEY" || constraintTypeKeyword == "UNIQUE" || constraintTypeKeyword == "FOREIGN_KEY") {
            QRegularExpression columnsRe(R"(^\((.+?)\)(.*)$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            QRegularExpressionMatch columnsMatch = columnsRe.match(bodyAndRemainder);
            if (!columnsMatch.hasMatch()) {
                textBuffer.append(QString("错误: %1 约束缺少括号括起来的列列表: %2").arg(constraintTypeKeyword, bodyAndRemainder));
                // ***** MODIFICATION START *****
                throw std::runtime_error(QString("%1 约束缺少括号括起来的列列表: %2").arg(constraintTypeKeyword, bodyAndRemainder).toStdString());
                // ***** MODIFICATION END *****
                // return;
            }
            mainClause = columnsMatch.captured(1).trimmed();
            remainingClause = columnsMatch.captured(2).trimmed();
            qDebug() << "  列约束类型: 列列表='" << mainClause << "', 剩余='" << remainingClause << "'";
        } else if (constraintTypeKeyword == "CHECK") {
            QRegularExpression checkExprRe(R"(^\((.+)\)$)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            QRegularExpressionMatch checkMatch = checkExprRe.match(bodyAndRemainder);
            if (!checkMatch.hasMatch()) {
                textBuffer.append(QString("错误: CHECK 约束缺少括号括起来的表达式: %1").arg(bodyAndRemainder));
                // ***** MODIFICATION START *****
                throw std::runtime_error(QString("CHECK 约束缺少括号括起来的表达式: %1").arg(bodyAndRemainder).toStdString());
                // ***** MODIFICATION END *****
                // return;
            }
            mainClause = checkMatch.captured(1).trimmed();
            remainingClause = "";
            qDebug() << "  CHECK 约束: 表达式='" << mainClause << "'";
        }
    }

    QStringList columns;
    if (constraintTypeKeyword == "PRIMARY_KEY" || constraintTypeKeyword == "UNIQUE" || constraintTypeKeyword == "FOREIGN_KEY") {
        if (mainClause.isEmpty()) {
            textBuffer.append(QString("错误: %1 约束必须指定列。").arg(constraintTypeKeyword));
            // ***** MODIFICATION START *****
            throw std::runtime_error(QString("%1 约束必须指定列。").arg(constraintTypeKeyword).toStdString());
            // ***** MODIFICATION END *****
            // return;
        }
        columns = mainClause.split(',', Qt::SkipEmptyParts);
        for (QString& col : columns) {
            col = col.trimmed();
            if (col.isEmpty()) {
                textBuffer.append(QString("错误: %1 约束中的列名不能为空。").arg(constraintTypeKeyword));
                // ***** MODIFICATION START *****
                throw std::runtime_error(QString("%1 约束中的列名不能为空。").arg(constraintTypeKeyword).toStdString());
                // ***** MODIFICATION END *****
                // return;
            }
            if (!table.has_field(col)) {
                textBuffer.append(QString("错误: %1 约束引用的列 '%2' 在表 '%3' 中不存在。").arg(constraintTypeKeyword, col, table.name()));
                // ***** MODIFICATION START *****
                throw std::runtime_error(QString("%1 约束引用的列 '%2' 在表 '%3' 中不存在。").arg(constraintTypeKeyword, col, table.name()).toStdString());
                // ***** MODIFICATION END *****
                // return;
            }
        }
        if (columns.isEmpty()) {
            textBuffer.append(QString("错误: %1 约束的列列表解析后为空。").arg(constraintTypeKeyword));
            // ***** MODIFICATION START *****
            throw std::runtime_error(QString("%1 约束的列列表解析后为空。").arg(constraintTypeKeyword).toStdString());
            // ***** MODIFICATION END *****
            // return;
        }
    }

    if (constraintName.isEmpty()) {
        QString baseName = constraintTypeKeyword.at(0) + constraintTypeKeyword.mid(1).toLower() + "_" + table.name();
        if (!columns.isEmpty()) {
            for(const QString& col : columns) { baseName += "_" + col.toLower(); }
        } else if (constraintTypeKeyword == "CHECK") {
            baseName += "_expr" + QString::number(table.checkConstraints().size() + 1);
        }
        constraintName = baseName;
        constraintName.truncate(64);
        qDebug() << "  自动生成的约束名: " << constraintName;
    }

    // 步骤 5: 应用约束到表 - try-catch 块已存在于原函数中，我们将把检查逻辑放入 FOREIGN_KEY 分支
    // try { // 这个 try 块是原函数就有的，用于捕获 xhytable 内部可能抛出的错误
    if (constraintTypeKeyword == "PRIMARY_KEY") {
        table.add_primary_key(columns);
        textBuffer.append(QString("表约束 '%1' (PRIMARY KEY on %2) 已添加。").arg(constraintName, columns.join(", ")));
    } else if (constraintTypeKeyword == "UNIQUE") {
        table.add_unique_constraint(columns, constraintName);
        textBuffer.append(QString("表约束 '%1' (UNIQUE on %2) 已添加。").arg(constraintName, columns.join(", ")));
    } else if (constraintTypeKeyword == "CHECK") {
        if (mainClause.isEmpty()) {
            textBuffer.append("错误: CHECK 约束的表达式不能为空。");
            // ***** MODIFICATION START *****
            throw std::runtime_error("CHECK 约束的表达式不能为空。");
            // ***** MODIFICATION END *****
            // return;
        }
        if (!validateCheckExpression(mainClause)) {
            textBuffer.append("错误: CHECK 约束表达式 '" + mainClause + "' 括号不匹配。");
            // ***** MODIFICATION START *****
            throw std::runtime_error(("CHECK 约束表达式 '" + mainClause + "' 括号不匹配。").toStdString());
            // ***** MODIFICATION END *****
            // return;
        }
        table.add_check_constraint(mainClause, constraintName);
        textBuffer.append(QString("表约束 '%1' (CHECK (%2)) 已添加。").arg(constraintName, mainClause));
    } else if (constraintTypeKeyword == "FOREIGN_KEY") {
        QRegularExpression fkRefRe(
            R"(REFERENCES\s+([\w_]+)\s*\(([\w_,\s]+)\))",
            QRegularExpression::CaseInsensitiveOption
            );
        QRegularExpressionMatch refMatch = fkRefRe.match(remainingClause);
        if (!refMatch.hasMatch()) {
            textBuffer.append("错误: FOREIGN KEY 约束 '" + constraint_str_input + "' 缺少有效的 REFERENCES 子句。检测部分: '" + remainingClause + "'");
            // ***** MODIFICATION START *****
            throw std::runtime_error(("FOREIGN KEY 约束 '" + constraint_str_input + "' 缺少有效的 REFERENCES 子句。").toStdString());
            // ***** MODIFICATION END *****
            // return;
        }

        QString referencedTable = refMatch.captured(1).trimmed();
        QStringList referencedColumnsList = refMatch.captured(2).trimmed().split(',', Qt::SkipEmptyParts);
        for(QString& rcol : referencedColumnsList) {
            rcol = rcol.trimmed();
            if (rcol.isEmpty()) {
                textBuffer.append("错误: FOREIGN KEY 约束的引用列名不能为空。");
                // ***** MODIFICATION START *****
                throw std::runtime_error("FOREIGN KEY 约束的引用列名不能为空。");
                // ***** MODIFICATION END *****
                // return;
            }
        }

        if (columns.size() != referencedColumnsList.size()) {
            textBuffer.append(QString("错误: FOREIGN KEY 列数量 (%1) 与引用的列数量 (%2) 不匹配。")
                                  .arg(columns.size()).arg(referencedColumnsList.size()));
            // ***** MODIFICATION START *****
            throw std::runtime_error(QString("FOREIGN KEY 列数量 (%1) 与引用的列数量 (%2) 不匹配。")
                                         .arg(columns.size()).arg(referencedColumnsList.size()).toStdString());
            // ***** MODIFICATION END *****
            // return;
        }

        // ***** NEW CHECK IMPLEMENTATION START *****
        QString current_db_name = db_manager.get_current_database(); // 'db_manager' 是 MainWindow 的成员变量
        if (current_db_name.isEmpty()) { // 应该在 handleCreateTable 中已检查，但再次确认
            textBuffer.append("错误: 内部错误，当前数据库未设定，无法验证引用的表。");
            throw std::runtime_error("内部错误: 当前数据库未设定。");
        }
        xhydatabase* db = db_manager.find_database(current_db_name);
        if (!db) {
            textBuffer.append("错误: 内部错误，无法找到当前数据库实例 '" + current_db_name + "' 以验证引用的表。");
            throw std::runtime_error("内部错误: 无法找到当前数据库实例。");
        }
        if (!db->has_table(referencedTable)) { // 使用 xhydatabase::has_table
            QString errorMsg = QString("错误: 外键约束 '%1' (在表 '%2' 上) 引用的父表 '%3' 不存在于数据库 '%4' 中。")
                                   .arg(constraintName.isEmpty() ? "未命名FK" : constraintName)
                                   .arg(table.name())
                                   .arg(referencedTable)
                                   .arg(current_db_name);
            textBuffer.append(errorMsg);
            qWarning() << "[handleTableConstraint] Validation Error: " << errorMsg;
            throw std::runtime_error(errorMsg.toStdString()); // 抛出异常，由 handleCreateTable 捕获
        }
        qDebug() << "  [handleTableConstraint] 引用的父表 '" << referencedTable << "' 存在。继续创建外键。";
        // ***** NEW CHECK IMPLEMENTATION END *****


        ForeignKeyDefinition::ReferentialAction onDeleteAction = ForeignKeyDefinition::NO_ACTION;
        ForeignKeyDefinition::ReferentialAction onUpdateAction = ForeignKeyDefinition::NO_ACTION;

        QString actionsPart = remainingClause.mid(refMatch.capturedLength()).trimmed();
        qDebug() << "  [FK PARSE] 解析 ON DELETE/UPDATE 的部分: '" << actionsPart << "'";

        QRegularExpression onActionRe(R"(ON\s+(DELETE|UPDATE)\s+(CASCADE|SET\s+NULL|NO\s+ACTION|RESTRICT|SET\s+DEFAULT))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator action_it = onActionRe.globalMatch(actionsPart);
        bool onDeleteSpecified = false;
        bool onUpdateSpecified = false;

        while(action_it.hasNext()){
            QRegularExpressionMatch actionMatch = action_it.next();
            QString event = actionMatch.captured(1).toUpper();
            QString actionStr = actionMatch.captured(2).toUpper().replace(" ", "_");
            qDebug() << "    [FK PARSE ACTION] 匹配到: Event='" << event << "', ActionStr='" << actionStr << "'";

            ForeignKeyDefinition::ReferentialAction currentActionParsed = ForeignKeyDefinition::NO_ACTION;
            if (actionStr == "CASCADE") currentActionParsed = ForeignKeyDefinition::CASCADE;
            else if (actionStr == "SET_NULL") currentActionParsed = ForeignKeyDefinition::SET_NULL;
            else if (actionStr == "NO_ACTION") currentActionParsed = ForeignKeyDefinition::NO_ACTION;
            else if (actionStr == "RESTRICT") currentActionParsed = ForeignKeyDefinition::NO_ACTION; // RESTRICT 在此实现中等同于 NO_ACTION
            else if (actionStr == "SET_DEFAULT") {
                currentActionParsed = ForeignKeyDefinition::SET_DEFAULT;
                textBuffer.append(QString("注意: ON %1 SET DEFAULT 将被记录，但其完整级联行为可能未完全实现。").arg(event));
            }

            if (event == "DELETE" && !onDeleteSpecified) {
                onDeleteAction = currentActionParsed;
                onDeleteSpecified = true;
            } else if (event == "UPDATE" && !onUpdateSpecified) {
                onUpdateAction = currentActionParsed;
                onUpdateSpecified = true;
            } else if ((event == "DELETE" && onDeleteSpecified) || (event == "UPDATE" && onUpdateSpecified)) {
                qWarning() << "  [FK PARSE WARNING] 重复指定 ON" << event << "动作，将使用第一个解析到的。";
            }
        }
        qDebug() << "  [FK PARSE] 最终解析的动作: ON DELETE=" << static_cast<int>(onDeleteAction)
                 << ", ON UPDATE=" << static_cast<int>(onUpdateAction);

        table.add_foreign_key(columns, referencedTable, referencedColumnsList, constraintName, onDeleteAction, onUpdateAction);
        textBuffer.append(QString("表约束 '%1' (FOREIGN KEY (%2) REFERENCES %3(%4) ON DELETE %5 ON UPDATE %6) 已添加。")
                              .arg(constraintName.isEmpty() ? "auto_fk" : constraintName)
                              .arg(columns.join(", "))
                              .arg(referencedTable)
                              .arg(referencedColumnsList.join(", "))
                              .arg(onDeleteAction == ForeignKeyDefinition::CASCADE ? "CASCADE" : (onDeleteAction == ForeignKeyDefinition::SET_NULL ? "SET NULL" : (onDeleteAction == ForeignKeyDefinition::SET_DEFAULT ? "SET DEFAULT" : "NO ACTION")))
                              .arg(onUpdateAction == ForeignKeyDefinition::CASCADE ? "CASCADE" : (onUpdateAction == ForeignKeyDefinition::SET_NULL ? "SET NULL" : (onUpdateAction == ForeignKeyDefinition::SET_DEFAULT ? "SET DEFAULT" : "NO ACTION"))));
    } else {
        textBuffer.append("错误: 不支持的表约束类型: '" + constraintTypeKeyword + "'.");
        // ***** MODIFICATION START *****
        throw std::runtime_error(("不支持的表约束类型: '" + constraintTypeKeyword + "'.").toStdString());
        // ***** MODIFICATION END *****
    }

}

void MainWindow::show_databases() {
    auto databases = db_manager.databases();
    if (databases.isEmpty()) {
        textBuffer.append("没有数据库。");
        return;
    }
    QSet<QString> uniqueNames;
    for (const auto& db : databases) {
        uniqueNames.insert(db.name());
    }
    QString output = "数据库列表:\n";
    for (const QString& name : uniqueNames) {
        output += "  " + name + "\n";
    }
    textBuffer.append(output.trimmed());
}

void MainWindow::show_tables(const QString& db_name_input) {
    QString db_name = db_name_input.trimmed();
    if (db_name.isEmpty()) {
        textBuffer.append("错误: 未指定数据库名，或当前未选择数据库。");
        return;
    }
    xhydatabase* db = db_manager.find_database(db_name);
    if (!db) {
        textBuffer.append(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        return;
    }
    auto tables = db->tables();
    if (tables.isEmpty()) {
        textBuffer.append(QString("数据库 '%1' 中没有表。").arg(db_name));
        return;
    }
    QString output = QString("数据库 '%1' 中的表:\n").arg(db_name);
    for (const auto& table : tables) {
        output += "  " + table.name() + "\n";
    }
    textBuffer.append(output.trimmed());
}




namespace { // 使用匿名命名空间，使这两个函数仅在本文件可见


int visualLength(const QString& str) {
    int length = 0;
    for (QChar qc : str) {
        ushort ucs = qc.unicode();
        // 扩展了CJK和其他宽字符的范围
        if ((ucs >= 0x1100 && ucs <= 0x11FF) || // Hangul Jamo
            (ucs >= 0x2E80 && ucs <= 0x2EFF) || // CJK Radicals Supplement
            (ucs >= 0x3000 && ucs <= 0x303F) || // CJK Symbols and Punctuation
            (ucs >= 0x3040 && ucs <= 0x309F) || // Hiragana
            (ucs >= 0x30A0 && ucs <= 0x30FF) || // Katakana
            (ucs >= 0x3130 && ucs <= 0x318F) || // Hangul Compatibility Jamo
            (ucs >= 0x31C0 && ucs <= 0x31EF) || // CJK Strokes
            (ucs >= 0x3200 && ucs <= 0x32FF) || // Enclosed CJK Letters and Months
            (ucs >= 0x3400 && ucs <= 0x4DBF) || // CJK Unified Ideographs Extension A
            (ucs >= 0x4DC0 && ucs <= 0x4DFF) || // Yijing Hexagram Symbols
            (ucs >= 0x4E00 && ucs <= 0x9FFF) || // CJK Unified Ideographs
            (ucs >= 0xA960 && ucs <= 0xA97F) || // Hangul Jamo Extended-A
            (ucs >= 0xAC00 && ucs <= 0xD7AF) || // Hangul Syllables
            (ucs >= 0xF900 && ucs <= 0xFAFF) || // CJK Compatibility Ideographs
            (ucs >= 0xFE30 && ucs <= 0xFE4F) || // CJK Compatibility Forms
            (ucs >= 0xFF00 && ucs <= 0xFFEF)    // Fullwidth Forms
            ) {
            length += 2;
        } else {
            length += 1;
        }
    }
    return length;
}
QString padToVisualWidth(const QString& str, int targetVisualWidth, bool allowTruncate = true) {
    int currentVisLen = visualLength(str);
    QString resultStr = str;

    if (currentVisLen == targetVisualWidth) {
        return resultStr;
    }

    if (currentVisLen < targetVisualWidth) {
        resultStr += QString(targetVisualWidth - currentVisLen, ' ');
        return resultStr;
    }

    // currentVisLen > targetVisualWidth
    if (!allowTruncate) {
        return resultStr + " "; // 如果不允许截断但超长，至少加一个空格保证分隔
    }

    // 需要截断
    if (targetVisualWidth <= 0) return QString(targetVisualWidth > 0 ? targetVisualWidth : 0, ' '); // 返回空或少量空格

    QString suffix = "...";
    int suffixLen = visualLength(suffix);

    // 如果目标宽度连 "..." 都放不下，或者只够放一部分 "..."
    if (targetVisualWidth < suffixLen) {
        QString temp;
        int len = 0;
        for (QChar qc : str) {
            int charLen = visualLength(QString(qc));
            if (len + charLen <= targetVisualWidth) {
                temp += qc;
                len += charLen;
            } else {
                break;
            }
        }
        resultStr = temp;
        // 确保用空格补齐到 targetVisualWidth
        int finalLen = visualLength(resultStr);
        if (finalLen < targetVisualWidth) {
            resultStr += QString(targetVisualWidth - finalLen, ' ');
        }
        return resultStr;
    }

    // 目标宽度可以容纳 "..."
    int availableWidthForText = targetVisualWidth - suffixLen;
    QString truncatedText;
    int currentTruncatedLen = 0;
    for (QChar qc : str) {
        int charVisLen = visualLength(QString(qc));
        if (currentTruncatedLen + charVisLen <= availableWidthForText) {
            truncatedText += qc;
            currentTruncatedLen += charVisLen;
        } else {
            // 如果下一个字符放不下，检查当前截断的文本加上 ... 是否已经超过目标宽度
            // 这种情况通常在 availableWidthForText 很小时发生
            if (currentTruncatedLen == 0 && suffixLen > targetVisualWidth) { // 连...都放不下
                // 已在上面处理 targetVisualWidth < suffixLen 的情况
            }
            break;
        }
    }
    resultStr = truncatedText + suffix;
    // 补齐空格到 targetVisualWidth
    int finalLen = visualLength(resultStr);
    if (finalLen < targetVisualWidth) {
        resultStr += QString(targetVisualWidth - finalLen, ' ');
    } else if (finalLen > targetVisualWidth) { // 如果加上 ... 后超了，则重新截断（不加...）
        QString temp;
        int len = 0;
        for (QChar qc : str) {
            int charLen = visualLength(QString(qc));
            if (len + charLen <= targetVisualWidth) {
                temp += qc;
                len += charLen;
            } else {
                break;
            }
        }
        resultStr = temp;
        if (visualLength(resultStr) < targetVisualWidth) {
            resultStr += QString(targetVisualWidth - visualLength(resultStr), ' ');
        }
    }
    return resultStr;
}


// 新增辅助函数：将 ReferentialAction 枚举转换为字符串
QString referentialActionToString(ForeignKeyDefinition::ReferentialAction action) {
    switch (action) {
    case ForeignKeyDefinition::CASCADE:     return "CASCADE";
    case ForeignKeyDefinition::SET_NULL:    return "SET NULL";
    case ForeignKeyDefinition::SET_DEFAULT: return "SET DEFAULT";
    case ForeignKeyDefinition::NO_ACTION:   return "NO ACTION"; // 或者 "RESTRICT"
    default:                                return "UNKNOWN";
    }
}

} // 结束匿名命名空间

void MainWindow::show_schema(const QString& db_name_input, const QString& table_name_input) {
    QString db_name = db_name_input.trimmed();
    QString table_name = table_name_input.trimmed();

    if (db_name.isEmpty()) { textBuffer.append("错误: 未指定数据库名。"); return; }
    if (table_name.isEmpty()) { textBuffer.append("错误: 未指定表名。"); return; }

    xhydatabase* db = db_manager.find_database(db_name);
    if (!db) {
        textBuffer.append(QString("错误: 数据库 '%1' 不存在。").arg(db_name));
        return;
    }
    xhytable* table = db->find_table(table_name); // 注意：这里获取的是非 const 指针

    if (!table) {
        textBuffer.append(QString("错误: 表 '%1' 在数据库 '%2' 中不存在。").arg(table_name, db_name));
        return;
    }
    QString output_str;
    output_str += QString("表 '%1.%2' 的结构:\n").arg(db_name, table_name);

    int colNameWidth = 20;
    int typeWidth = 28;
    int constraintWidth = 45;

    output_str += padToVisualWidth("列名", colNameWidth, false) + " " +
                  padToVisualWidth("类型", typeWidth, false) + " " +
                  padToVisualWidth("约束", constraintWidth, false) + "\n";
    output_str += QString(colNameWidth + typeWidth + constraintWidth + 2, '-') + "\n";

    const QMap<QString, QString>& default_values_map = table->defaultValues();
    const QStringList& primary_keys_list = table->primaryKeys();
    const QSet<QString>& not_null_fields_set = table->notNullFields();
    const QMap<QString, QList<QString>>& table_unique_constraints = table->uniqueConstraints();


    for (const xhyfield& field : table->fields()) { // 使用 const auto&
        QStringList final_display_constraints_parts;

        // 1. 主键
        if (primary_keys_list.contains(field.name(), Qt::CaseInsensitive)) {
            final_display_constraints_parts.append("PRIMARY KEY");
        }

        // 2. 非空
        if (not_null_fields_set.contains(field.name())) {
            if (!primary_keys_list.contains(field.name(), Qt::CaseInsensitive)) { // 主键已隐含非空
                 final_display_constraints_parts.append("NOT NULL");
            }
        }

        // 3. 字段级 UNIQUE (从 field.constraints()，通常代表由FieldBlock.integrities的UNIQUE位加载而来)
        //    目的是显示那些不是通过表级 `CONSTRAINT UQ_NAME UNIQUE (col)` 定义的，
        //    而是直接在列定义时 `col_type UNIQUE` 声明的。
        if (field.constraints().contains("UNIQUE", Qt::CaseInsensitive)) {
                   bool isCoveredByExplicitlyNamedTableLevelUnique = false;
                   QString autoGeneratedConstraintName = "UQ_" + table->name().toUpper() + "_" + field.name().toUpper();

                   for (auto it_tbl_uq = table_unique_constraints.constBegin();
                        it_tbl_uq != table_unique_constraints.constEnd(); ++it_tbl_uq) {
                       const QList<QString>& uq_cols = it_tbl_uq.value();
                       if (uq_cols.size() == 1 && uq_cols.first().compare(field.name(), Qt::CaseInsensitive) == 0) {
                           // 如果存在一个只包含此字段的表级UNIQUE，并且其名称 *不是* 自动生成的，
                           // 那么我们就认为它是一个用户显式定义的表级约束，字段行就不再重复显示UNIQUE。
                           if (it_tbl_uq.key().compare(autoGeneratedConstraintName, Qt::CaseInsensitive) != 0) {
                               isCoveredByExplicitlyNamedTableLevelUnique = true;
                               break;
                           }
                       }
                   }
                   if (!isCoveredByExplicitlyNamedTableLevelUnique) {
                       final_display_constraints_parts.append("UNIQUE");
                   }
               }

        // 4. 默认值
        if (default_values_map.contains(field.name())) {
            QString storedDefault = default_values_map.value(field.name());
            QString displayDefault;

            if (storedDefault == DefaultValueKeywords::SQL_NULL) {
                displayDefault = "DEFAULT NULL";
            } else if (storedDefault == DefaultValueKeywords::CURRENT_TIMESTAMP_KW) {
                displayDefault = "DEFAULT CURRENT_TIMESTAMP";
            } else if (storedDefault == DefaultValueKeywords::CURRENT_DATE_KW) {
                displayDefault = "DEFAULT CURRENT_DATE";
            } else {
                bool isNumericOrBool = false;
                bool okNum;
                storedDefault.toDouble(&okNum);
                if (okNum && !storedDefault.contains(QRegularExpression("[^0-9.eE-]"))) { // 确保是纯数字，不是 '123x'
                    isNumericOrBool = true;
                } else if (storedDefault.compare("true", Qt::CaseInsensitive) == 0 ||
                           storedDefault.compare("false", Qt::CaseInsensitive) == 0) {
                    isNumericOrBool = true;
                }

                // 使用 field.type() 来决定是否加引号，而不是 f_def->type()
                switch(field.type()) {
                case xhyfield::CHAR: case xhyfield::VARCHAR: case xhyfield::TEXT:
                case xhyfield::ENUM: case xhyfield::DATE: case xhyfield::DATETIME:
                case xhyfield::TIMESTAMP:
                    displayDefault = "DEFAULT '" + storedDefault.replace("'", "''") + "'";
                    break;
                default: // TINYINT, SMALLINT, INT, BIGINT, FLOAT, DOUBLE, DECIMAL, BOOL
                    displayDefault = "DEFAULT " + storedDefault;
                    break;
                }
            }
            final_display_constraints_parts.append(displayDefault);
        }

        // 5. 字段级 CHECK 约束 (从 field.constraints() 解析)
        for (const QString& fc_str : field.constraints()) {
            // 仅添加以 "CHECK(" 开头并以 ")" 结尾的约束
            // 并确保它不是已添加的其他约束类型 (PRIMARY KEY, NOT NULL, UNIQUE, SIZE, PRECISION, SCALE)
            // 这个筛选逻辑在您之前的代码中已通过跳过特定开头的约束实现，这里保留。
            // 进一步的，如果字段级CHECK也被存入table->m_checkConstraints，也需要考虑去重。
            // 假设这里的 field.constraints() 只包含原始的、未被提升到表级的字段CHECK。
            if (fc_str.startsWith("CHECK(", Qt::CaseInsensitive) && fc_str.endsWith(")")) {
                 bool alreadyCoveredByTableCheck = false;
                 // （可选）更复杂的去重：检查此字段级CHECK是否已由表级CHECK覆盖
                 // for (const auto& table_check_expr : table->checkConstraints().values()){ ... }
                 if(!alreadyCoveredByTableCheck){
                    final_display_constraints_parts.append(fc_str);
                 }
            }
        }

        final_display_constraints_parts.removeDuplicates();
        QString constraints_text = final_display_constraints_parts.join(" ");

        output_str += padToVisualWidth(field.name(), colNameWidth, true) + " " +
                      padToVisualWidth(field.typestring(), typeWidth, true) + " " +
                      padToVisualWidth(constraints_text, constraintWidth, true) + "\n";
    }

    // 表级约束部分

    // 主键 (如果为复合主键，在此处显示其组合)
    if (primary_keys_list.size() > 1) { // 通常单列主键已在字段行通过 "PRIMARY KEY" 标记
         output_str += "\nPRIMARY KEY (TABLE): (" + primary_keys_list.join(", ") + ")\n";
    }

    // 外键
    const QList<ForeignKeyDefinition>& foreign_keys_list = table->foreignKeys();
    if (!foreign_keys_list.isEmpty()) {
        output_str += "\nFOREIGN KEYS:\n";
        for (const auto& fkDef : foreign_keys_list) {
            QStringList childCols, parentCols;
            for (auto it = fkDef.columnMappings.constBegin(); it != fkDef.columnMappings.constEnd(); ++it) {
                childCols.append(it.key()); parentCols.append(it.value());
            }
            output_str += QString("  CONSTRAINT %1 FOREIGN KEY (%2) REFERENCES %3 (%4)")
                              .arg(fkDef.constraintName)
                              .arg(childCols.join(", "))
                              .arg(fkDef.referenceTable)
                              .arg(parentCols.join(", "));
            output_str += QString(" ON DELETE %1 ON UPDATE %2\n")
                              .arg(referentialActionToString(fkDef.onDeleteAction))
                              .arg(referentialActionToString(fkDef.onUpdateAction));
        }
    }

    // 表级 UNIQUE 约束 (从 table->uniqueConstraints())
    if (!table_unique_constraints.isEmpty()) {
           bool hasPrintedUQHeader = false;
           for (auto it_uq_map = table_unique_constraints.constBegin();
                it_uq_map != table_unique_constraints.constEnd(); ++it_uq_map) {
               const QString& constraintName = it_uq_map.key();
               const QList<QString>& uq_columns = it_uq_map.value();

               bool displayThisTableConstraint = true;
               if (uq_columns.size() == 1) {
                   // 如果是单列UNIQUE
                   const xhyfield* fld_ptr = table->get_field(uq_columns.first());
                   if (fld_ptr && fld_ptr->constraints().contains("UNIQUE", Qt::CaseInsensitive)) { // 确保是同一个 field 对象
                       // 如果它的名字是自动生成的 (意味着它源自字段级声明，且字段行已显示)
                       QString autoGeneratedName = "UQ_" + table->name().toUpper() + "_" + fld_ptr->name().toUpper();
                       if (constraintName.compare(autoGeneratedName, Qt::CaseInsensitive) == 0) {
                           displayThisTableConstraint = false; // 字段行会显示，这里就不重复了
                       }
                   }
               }
               // 多列 UNIQUE 或者用户显式命名的单列 UNIQUE 会在这里显示

               if (displayThisTableConstraint) {
                    if (!hasPrintedUQHeader) {
                       output_str += "\nUNIQUE CONSTRAINTS (TABLE):\n";
                       hasPrintedUQHeader = true;
                   }
                   output_str += QString("  CONSTRAINT %1 UNIQUE (%2)\n")
                                     .arg(constraintName)
                                     .arg(uq_columns.join(", "));
               }
           }
       }

    // 表级 CHECK 约束 (从 table->checkConstraints())
    const QMap<QString, QString>& check_constraints_map = table->checkConstraints();
    if (!check_constraints_map.isEmpty()) {
        bool hasPrintedCheckHeader = false;
        for (auto it_ck_map = check_constraints_map.constBegin(); it_ck_map != check_constraints_map.constEnd(); ++it_ck_map) {
            // 这里的逻辑假设所有存储在 table->m_checkConstraints 中的都是需要在此处显示的表级约束。
            // 如果字段级 CHECK 也被解析并存入 m_checkConstraints（带有唯一的约束名），
            // 并且你不想重复显示，那么需要更复杂的去重逻辑，
            // 例如，检查该 CHECK 表达式是否与某个字段的字段级 CHECK 表达式相同。
            // 目前，我们显示所有在 m_checkConstraints 中的约束。
            if (!hasPrintedCheckHeader) {
                 output_str += "\nCHECK CONSTRAINTS (TABLE):\n";
                 hasPrintedCheckHeader = true;
            }
            output_str += QString("  CONSTRAINT %1 CHECK (%2)\n")
                              .arg(it_ck_map.key())
                              .arg(it_ck_map.value());
        }
    }

    textBuffer.append(output_str.trimmed());
}





// ENDE von mainwindow.cpp
///////////////////////////////////////////////////
//GUI

void MainWindow::on_tableButton_released()
{
    ui->tableButton->setEnabled(false);
    ui->viewButton->setEnabled(true);
    ui->functionButton->setEnabled(true);
    ui->queryButton->setEnabled(true);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,tablelist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}

void MainWindow::on_viewButton_released()
{
    ui->tableButton->setEnabled(true);
    ui->viewButton->setEnabled(false);
    ui->functionButton->setEnabled(true);
    ui->queryButton->setEnabled(true);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,viewlist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}

void MainWindow::on_functionButton_released()
{
    ui->tableButton->setEnabled(true);
    ui->viewButton->setEnabled(true);
    ui->functionButton->setEnabled(false);
    ui->queryButton->setEnabled(true);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,functionlist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}

void MainWindow::on_queryButton_released()
{
    ui->tableButton->setEnabled(true);
    ui->viewButton->setEnabled(true);
    ui->functionButton->setEnabled(true);
    ui->queryButton->setEnabled(false);

    ui->tabWidget->removeTab(0);
    ui->tabWidget->insertTab(0,querylist,"对象");
    ui->tabWidget->setCurrentIndex(0);
    if (tabBar) {
        // 定位关闭按钮的位置（通常为右侧，具体取决于样式）
        QWidget *closeButton = tabBar->tabButton(0, QTabBar::RightSide);
        if (closeButton) {
            closeButton->setVisible(false);
        }
    }
}


void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    QWidget* widget = ui->tabWidget->widget(index);
    ui->tabWidget->removeTab(index);
    widget->deleteLater();
}

void MainWindow::openRegisterUserDialog()
{
    CreateUserDialog createUserDialog(&Account,&db_manager,this); // 创建注册用户对话框实例
    createUserDialog.exec();
}


void MainWindow::dataSearch(){
    GUI_dbms.clear();
    for(xhydatabase database: db_manager.databases()){
        Database gui_database = {database.name()};

        QStringList gui_tables;
        QStringList gui_views;
        QStringList gui_functions;
        QStringList gui_queries;

        for(xhytable table : database.tables()){
            gui_tables.append(table.name());
        }
        gui_database.tables=gui_tables;


        GUI_dbms.append(gui_database);
    }

    // GUI_dbms = {
    //     {
    //         "数据库1",
    //         {"student", "teacher"},  // 表
    //         {"view1", "view2"},      // 视图
    //         {"func1"},              // 函数
    //         {"query1", "query2"}    // 查询
    //     },
    //     {
    //         "数据库2",
    //         {"order", "product"},    // 表
    //         {"sales_view"},         // 视图
    //         {"calculate_total"},    // 函数
    //         {}      // 查询
    //     }
    // };
}

void MainWindow::buildTree(){
    ui->treeWidget->clear();

    for (const Database &db : GUI_dbms) {
        // 创建数据库节点（作为根的子节点）
        //只创建用户拥有的数据库节点
        bool exist=false;
        for(auto databaseinfo:userDatabaseInfo){
            if(db.database==databaseinfo.dbName) exist=true;
        }
        if(!exist&&Account.getUserRole(username)<2) continue;
        QTreeWidgetItem *dbItem = new QTreeWidgetItem(ui->treeWidget);
        dbItem->setText(0, db.database);

        // 创建固定子节点：表、视图、函数、查询
        QTreeWidgetItem *tablesItem = new QTreeWidgetItem(dbItem);
        tablesItem->setText(0, "表");

        QTreeWidgetItem *viewsItem = new QTreeWidgetItem(dbItem);
        viewsItem->setText(0, "视图");

        QTreeWidgetItem *functionsItem = new QTreeWidgetItem(dbItem);
        functionsItem->setText(0, "函数");

        QTreeWidgetItem *queriesItem = new QTreeWidgetItem(dbItem);
        queriesItem->setText(0, "查询");
        // 填充表
        for (const QString &table : db.tables) {
            QTreeWidgetItem *tableItem = new QTreeWidgetItem(tablesItem);
            tableItem->setText(0, table);
        }

        // 填充视图
        for (const QString &view : db.views) {
            QTreeWidgetItem *viewItem = new QTreeWidgetItem(viewsItem);
            viewItem->setText(0, view);
        }

        // 填充函数
        for (const QString &func : db.functions) {
            QTreeWidgetItem *funcItem = new QTreeWidgetItem(functionsItem);
            funcItem->setText(0, func);
        }

        // 填充查询
        for (const QString &query : db.queries) {
            QTreeWidgetItem *queryItem = new QTreeWidgetItem(queriesItem);
            queryItem->setText(0, query);
        }
    }

}

void MainWindow::updateList(QString currentDb){

    tablelist->clear();
    viewlist->clear();
    functionlist->clear();
    querylist->clear();

    for (const Database &db : GUI_dbms) {

        if(db.database == currentDb){
            // 填充表
            tablelist->addItems(db.tables);

            // 填充视图
            viewlist->addItems(db.views);

            // 填充函数
            functionlist->addItems(db.functions);

            // 填充查询
            querylist->addItems(db.queries);
        }
    }
}

void MainWindow::handleItemClicked(QTreeWidgetItem *item, int column)
{
    if (item) {
        QString text = item->text(column);
        qDebug() << "Clicked item: " << text;
    }
    if(item->parent() != nullptr){
        if(item->text(0) == "表" || item->parent()->text(0) == "表"){
            if(ui->tableButton->isEnabled()){
                MainWindow::on_tableButton_released();
            }

        }else if(item->text(0) == "视图" || item->parent()->text(0) == "视图"){
            if(ui->viewButton->isEnabled()){
                MainWindow::on_viewButton_released();
            }

        }else if(item->text(0) == "函数" || item->parent()->text(0) == "函数"){
            if(ui->functionButton->isEnabled()){
                MainWindow::on_functionButton_released();
            }

        }else if(item->text(0) == "查询" || item->parent()->text(0) == "查询"){
            if(ui->queryButton->isEnabled()){
                MainWindow::on_queryButton_released();
            }
        }
    }
    while(item->parent() != nullptr){
        item=item->parent();
    }
    if(current_GUI_Db == nullptr || item->text(0) != current_GUI_Db){
        current_GUI_Db = item->text(0);
        qDebug() << "数据库改变";

        updateList(current_GUI_Db);
    }
}

void MainWindow::handleItemDoubleClicked(QTreeWidgetItem *item, int column){
    if(item->parent() == nullptr){

        return;
    }
    QString parentText = item->parent()->text(0);
    if(parentText == "表"){

        QString tablename = item->text(0);
        openTable(tablename);
        //tableshow->

    }else if(parentText == "视图"){

    }else if(parentText == "函数"){

    }else if(parentText == "查询"){

    }
}

void MainWindow::openTable(QString tableName){
    if(current_GUI_Db.isEmpty()) return;
    for(int i=0; i < ui->tabWidget->count(); ++i){
        if(tableName+" @"+current_GUI_Db == ui->tabWidget->tabText(i)){
            return;
        }
    }


    xhydatabase* database = db_manager.find_database(current_GUI_Db);
    xhytable* table = database->find_table(tableName);
    if(table){
        tableShow *tableshow = new tableShow(&db_manager,current_GUI_Db,tableName,ui->tabWidget);
        ui->tabWidget->addTab(tableshow,tableName+" @"+current_GUI_Db);
        ui->tabWidget->setCurrentWidget(tableshow);
        // tableshow->setTable(table);
        tableshow->resetShow();

        connect(tableshow,&tableShow::dataChanged,[=](const QString& sql ){
        db_manager.use_database( current_GUI_Db );
        // qDebug()<<sql;
        handleString(sql+"\n");

        QString ass = textBuffer.join("");
        qDebug()<<ass;
        if(!ass.isEmpty()){

            if(ass.contains("0行") || ass.contains("errors") || ass.contains("错误") ||ass.contains("rolled back")) {
                QMessageBox msg;
                msg.setWindowTitle("错误");
                msg.setText(ass);
                msg.setStandardButtons(QMessageBox::Ok);
                msg.exec();
            }else
                tableshow->resetButton(true);
        }
        textBuffer.clear();
        if(sql.startsWith("DELETE")) tableshow->resetShow();
        });

    }
}

void MainWindow::handleString(const QString& text){
    qDebug()<<"handleString:"<<text;
    QString input = text;
    QStringList commands = SQLParser::parseMultiLineSQL(input); // SQLParser::静态调用

    for (const QString& command_const : commands) {
        QString trimmedCmd = command_const.trimmed();
        if (!trimmedCmd.isEmpty() && trimmedCmd.endsWith(';')) {
            textBuffer.append("> " + trimmedCmd);
            execute_command(trimmedCmd);
            textBuffer.append("");
        } else if (!trimmedCmd.isEmpty()){
            textBuffer.append("! 忽略未以分号结尾的语句: " + trimmedCmd);
        }
    }
    QString remaining = input.trimmed().section(';', -1).trimmed();
    if(!remaining.isEmpty()) {
        bool alreadyProcessedOrEmpty = commands.isEmpty();
        if (!commands.isEmpty()) {
            QString lastProcessedCmdFull = commands.last().trimmed();
            QString lastProcessedCmdNoSemi = lastProcessedCmdFull;
            if (lastProcessedCmdNoSemi.endsWith(';')) {
                lastProcessedCmdNoSemi.chop(1);
            }
            if (lastProcessedCmdNoSemi == remaining || lastProcessedCmdFull == remaining) {
                alreadyProcessedOrEmpty = true;
            }
        }
        if (!alreadyProcessedOrEmpty) {
            bool isPartOfProcessed = false;
            for(const QString& cmd : commands) {
                if (cmd.trimmed() == remaining + ";" || cmd.trimmed() == remaining) {
                    isPartOfProcessed = true;
                    break;
                }
            }
            if (!isPartOfProcessed) {
                textBuffer.append("错误: 检测到未完成的SQL语句（末尾缺少分号）: " + remaining);
            }
        }
    }
}

void MainWindow::handleString(const QString& text, queryWidget* query){
    current_query = query;
    current_query->clear();
    handleString(text);
    for(const QString& msg : textBuffer ){
        query->appendPlainText(msg);
    }
    textBuffer.clear();
    dataSearch();
    buildTree();
    updateList(current_GUI_Db);
}

void MainWindow::on_addQuery_released()
{
    queryWidget *query = new queryWidget;
    ui->tabWidget->addTab(query,"新建查询");
    connect(query,&queryWidget::sendString,[=](const QString& text){
        handleString(text,query);
    });
    ui->tabWidget->setCurrentWidget(query);
    query->putin_focus();
}

// 由zyh新增，用于handleSelect里的聚组函数
// 修改后 - MainWindow::calculateAggregate函数
QVariant MainWindow::calculateAggregate(const QString& function, const QString& column, const QVector<xhyrecord>& records) {
    // 特殊处理COUNT(*)
    if (function == "COUNT" && (column == "*" || column.isEmpty())) {
        return records.size();
    }

    // 列名为空但不是COUNT(*)的情况
    if (column.isEmpty() && function != "COUNT") {
        return QVariant();
    }

    if (function == "COUNT") {
        int count = 0;
        for (const auto& record : records) {
            if (!record.value(column).isEmpty()) {
                count++;
            }
        }
        return count;
    } else if (function == "SUM") {
        double sum = 0.0;
        bool valid = false;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                sum += val;
                valid = true;
            }
        }
        return valid ? QVariant(sum) : QVariant();
    } else if (function == "AVG") {
        double sum = 0.0;
        int count = 0;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                sum += val;
                count++;
            }
        }
        if (count > 0) {
            double avg = sum / count;
            // 使用std::round四舍五入到两位小数
            avg = std::round(avg * 100) / 100;
            return QVariant(avg);
        }
        return QVariant();
    } else if (function == "MIN") {
        double min = std::numeric_limits<double>::max();
        bool found = false;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                min = qMin(min, val);
                found = true;
            }
        }
        return found ? QVariant(min) : QVariant();
    } else if (function == "MAX") {
        double max = std::numeric_limits<double>::lowest();
        bool found = false;
        for (const auto& record : records) {
            bool ok;
            double val = record.value(column).toDouble(&ok);
            if (ok) {
                max = qMax(max, val);
                found = true;
            }
        }
        return found ? QVariant(max) : QVariant();
    }

    return QVariant(); // NULL for unsupported functions
}

// 同上
bool MainWindow::isNumeric(const QString& str) const {
    bool ok;
    str.toDouble(&ok);
    return ok;
}

// 同上
// 实现从.h移动到.cpp的removeTableAlias函数
QString MainWindow::removeTableAlias(const QString& col, const QString& table_alias, const QString& table_name) {
    QString result = col.trimmed();
    if (!table_alias.isEmpty() && result.startsWith(table_alias + ".")) {
        result = result.mid(table_alias.length() + 1);
    } else {
        int dotPos = result.indexOf('.');
        if (dotPos != -1) {
            QString prefix = result.left(dotPos);
            QString colNameOnly = result.mid(dotPos + 1);
            if (prefix.compare(table_name, Qt::CaseInsensitive) == 0) {
                result = colNameOnly;
            }
        }
    }
    return result;
}
