// sqlparser.cpp
#include "sqlparser.h"

QStringList SQLParser::parseMultiLineSQL(const QString& input) {
    QStringList commands;
    QString buffer;
    bool inString = false;
    QChar stringDelimiter;
    bool inComment = false;
    bool inLineComment = false;

    for (int i = 0; i < input.length(); ++i) {
        QChar ch = input[i];
        QChar nextCh = (i < input.length() - 1) ? input[i+1] : QChar();

        // 处理行注释
        if (!inString && !inComment && ch == '-' && nextCh == '-') {
            inLineComment = true;
            i++; // 跳过下一个'-'
            continue;
        }

        // 处理块注释
        if (!inString && !inLineComment && ch == '/' && nextCh == '*') {
            inComment = true;
            i++; // 跳过'*'
            continue;
        }

        // 结束块注释
        if (inComment && ch == '*' && nextCh == '/') {
            inComment = false;
            i++; // 跳过'/'
            continue;
        }

        // 跳过注释内容
        if (inLineComment || inComment) {
            if (inLineComment && ch == '\n') {
                inLineComment = false;
            }
            continue;
        }

        // 处理字符串字面量
        if (inString) {
            buffer.append(ch);
            if (ch == stringDelimiter && (i == 0 || input[i-1] != '\\')) {
                inString = false;
            }
            continue;
        }

        // 检测字符串开始
        if (ch == '\'' || ch == '"') {
            inString = true;
            stringDelimiter = ch;
            buffer.append(ch);
            continue;
        }

        // 分号作为语句结束
        if (ch == ';') {
            QString cmd = buffer.trimmed();
            if (!cmd.isEmpty()) {
                commands.append(cmd);
            }
            buffer.clear();
        } else {
            buffer.append(ch);
        }
    }

    // 添加最后一个语句（如果没有分号结尾）
    QString lastCmd = buffer.trimmed();
    if (!lastCmd.isEmpty()) {
        commands.append(lastCmd);
    }

    return commands;
}
