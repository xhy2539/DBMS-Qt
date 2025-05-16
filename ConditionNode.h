#ifndef CONDITIONNODE_H
#define CONDITIONNODE_H

#include <QList>
#include <QString>
#include <QVariant> // 用于存储类型化的值

// 用于存储比较操作的详细信息
struct ComparisonDetails {
    QString fieldName;
    QString operation; // 例如: "=", ">", "LIKE", "IS NULL", "IN", "BETWEEN"
    QVariant value;    // 用于单操作数运算或BETWEEN的第一个操作数
    QVariant value2;   // 用于BETWEEN的第二个操作数 (如果适用)
    QList<QVariant> valueList; // 用于IN操作符的值列表

    ComparisonDetails() = default;
};

class ConditionNode { // <<<<<<<<<<<<<<<<<<<<<<< 注意这里是 class
public:
    // NodeType 枚举，增加了 EMPTY（空节点）和 NEGATION_OP（NOT操作）
    enum NodeType { EMPTY, LOGIC_OP, COMPARISON_OP, NEGATION_OP };

    NodeType type = EMPTY;
    QString logicOp;      // "AND" 或 "OR"，用于 LOGIC_OP 类型节点
    ComparisonDetails comparison; // 存储比较操作的详细信息，用于 COMPARISON_OP 类型节点


    // 子节点列表，用于构建条件树
    // 对于 NEGATION_OP，通常只有一个子节点
    QList<ConditionNode> children;

    ConditionNode() = default; // 默认构造函数

    // 辅助构造函数，方便创建不同类型的节点
    ConditionNode(NodeType t) : type(t) {}
    ConditionNode(NodeType t, const QString& op) : type(t), logicOp(op.toUpper()) {} // 逻辑操作符统一大写
    ConditionNode(NodeType t, const ComparisonDetails& comp) : type(t), comparison(comp) {}
};

#endif // CONDITIONNODE_H
