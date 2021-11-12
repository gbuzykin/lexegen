#pragma once

#include "valset.h"

#include <memory>

// Node types
enum class NodeType {
    kOr,         // Variant
    kCat,        // Concatenation
    kStar,       // Series
    kPlus,       // At least one series
    kQuestion,   // One or nothing
    kSymbol,     // Symbol
    kSymbSet,    // Symbol set
    kEmptySymb,  // Empty symbol
    kTrailCont,  // Trailling context
    kTerm        // Termination symbol
};

// Common node class
class Node {
 public:
    explicit Node(NodeType type) : type_(type) {}
    virtual ~Node() = default;

    NodeType getType() const { return type_; };
    int getPosition() const { return position_; };
    void setPosition(int pos) { position_ = pos; };
    bool getMark() const { return mark_; };
    void setMark(bool mark) { mark_ = mark; };
    Node* getLeft() const { return left_.get(); };
    void setLeft(std::unique_ptr<Node> left) { left_ = std::move(left); };
    Node* getRight() const { return right_.get(); };
    void setRight(std::unique_ptr<Node> right) { right_ = std::move(right); };
    bool isNullable() const { return nullable_; };
    const ValueSet& getFirstpos() const { return firstpos_; };
    const ValueSet& getLastpos() const { return lastpos_; };
    std::unique_ptr<Node> cloneTree() const;

    virtual std::unique_ptr<Node> clone() const { return std::make_unique<Node>(type_); };
    virtual void calcFunctions();

 protected:
    NodeType type_;
    bool mark_ = false;                   // Node mark
    int position_ = -1;                   // Leaf position
    std::unique_ptr<Node> left_, right_;  // Binary tree leaves
    // Node functions:
    bool nullable_ = false;  // nullable(node) function
    ValueSet firstpos_;      // firstpos(node) function
    ValueSet lastpos_;       // lastpos(node) function
};

// Symbol node class
class SymbNode : public Node {
 public:
    explicit SymbNode(char symb) : Node(NodeType::kSymbol), symb_(symb) {}
    char getSymbol() const { return symb_; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<SymbNode>(symb_); };
    void calcFunctions() override;

 protected:
    char symb_;  // Node symbol
};

// Symbol set node class
class SymbSetNode : public Node {
 public:
    explicit SymbSetNode(const ValueSet& sset) : Node(NodeType::kSymbSet), sset_(sset) {}
    const ValueSet& getSymbSet() const { return sset_; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<SymbSetNode>(sset_); };
    void calcFunctions() override;

 protected:
    ValueSet sset_;
};

// Empty symbol node class
class EmptySymbNode : public Node {
 public:
    EmptySymbNode() : Node(NodeType::kEmptySymb) {}
    std::unique_ptr<Node> clone() const override { return std::make_unique<EmptySymbNode>(); };
    void calcFunctions() override;
};

// Trailing context node
class TrailContNode : public Node {
 public:
    explicit TrailContNode(int pat_no) : Node(NodeType::kTrailCont), pattern_no_(pat_no) {}
    int getPatternNo() const { return pattern_no_; };
    void setPatternNo(int pat_no) { pattern_no_ = pat_no; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<TrailContNode>(pattern_no_); };
    void calcFunctions() override;

 protected:
    int pattern_no_;
};

// Termination node class
class TermNode : public Node {
 public:
    explicit TermNode(int pat_no) : Node(NodeType::kTerm), pattern_no_(pat_no) {}
    int getPatternNo() const { return pattern_no_; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<TermNode>(pattern_no_); };
    void calcFunctions() override;

 protected:
    int pattern_no_;
};
