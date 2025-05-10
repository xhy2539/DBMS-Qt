#ifndef CONDITIONNODE_H
#define CONDITIONNODE_H

#include <QList>
#include <QMap>
#include <QString>
    class ConditionNode {
    public:
        enum NodeType { LOGIC_OP, COMPARISON};
        NodeType type;
        QString logicOp;  // "AND" 或 "OR"
        QMap<QString, QString> comparison; // 格式: "字段名" -> "操作符 值"
        QList<ConditionNode> children;
        ConditionNode(){

        }
    };
#endif // CONDITIONNODE_H
