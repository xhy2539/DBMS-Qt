// sqlparser.cpp
#include "sqlparser.h"

QStringList SQLParser::parseMultiLineSQL(const QString& sqlText) {
    QStringList commands;
    QString currentCommand;
    bool inQuotes = false;
    bool inSingleLineComment = false;
    bool inMultiLineComment = false;
    QChar quoteChar;

    for(int i = 0; i < sqlText.length(); i++) {
        QChar ch = sqlText[i];

        // 处理单行注释
        if(!inQuotes && !inMultiLineComment && ch == '-' && i+1 < sqlText.length() && sqlText[i+1] == '-') {
            inSingleLineComment = true;
            i++; // 跳过第二个'-'
            continue;
        }

        // 处理多行注释开始
        if(!inQuotes && !inSingleLineComment && ch == '/' && i+1 < sqlText.length() && sqlText[i+1] == '*') {
            inMultiLineComment = true;
            i++; // 跳过'*'
            continue;
        }

        // 处理多行注释结束
        if(inMultiLineComment && ch == '*' && i+1 < sqlText.length() && sqlText[i+1] == '/') {
            inMultiLineComment = false;
            i++; // 跳过'/'
            continue;
        }

        // 如果在注释中，跳过当前字符
        if(inSingleLineComment || inMultiLineComment) {
            if(inSingleLineComment && ch == '\n') {
                inSingleLineComment = false;
            }
            continue;
        }

        // 处理字符串字面量
        if(ch == '\'' || ch == '"') {
            if(inQuotes) {
                if(quoteChar == ch) {
                    // 处理转义引号
                    if(i > 0 && sqlText[i-1] == '\\') {
                        currentCommand.chop(1); // 移除转义符
                        currentCommand += ch;
                        continue;
                    }
                    inQuotes = false;
                }
            } else {
                inQuotes = true;
                quoteChar = ch;
            }
        }

        currentCommand += ch;

        // 只在非字符串状态下检查分号
        if(ch == ';' && !inQuotes) {
            QString trimmedCmd = currentCommand.trimmed();
            // 严格检查：分号必须是最后一个非空白字符
            if(!trimmedCmd.isEmpty() && trimmedCmd.endsWith(';')) {
                commands.append(trimmedCmd);
                currentCommand.clear();
            }
        }
    }

    return commands;
}
