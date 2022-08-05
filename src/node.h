#pragma once

#include "valset.h"

#include <memory>
#include <vector>

// Node types
enum class NodeType {
    kOr,               // Variant
    kCat,              // Concatenation
    kStar,             // Series
    kPlus,             // At least one series
    kQuestion,         // One or nothing
    kSymbol,           // Symbol
    kSymbSet,          // Symbol set
    kEmptySymb,        // Empty symbol
    kTrailingContext,  // Trailing context
    kTerm              // Termination symbol
};

class PositionalNode;

// Common node class
class Node {
 public:
    explicit Node(NodeType type) : type_(type) {}
    virtual ~Node() = default;

    NodeType getType() const { return type_; };
    Node* getLeft() const { return left_.get(); };
    void setLeft(std::unique_ptr<Node> left) { left_ = std::move(left); };
    Node* getRight() const { return right_.get(); };
    void setRight(std::unique_ptr<Node> right) { right_ = std::move(right); };
    bool isNullable() const { return nullable_; };
    const ValueSet& getFirstpos() const { return firstpos_; };
    const ValueSet& getLastpos() const { return lastpos_; };
    std::unique_ptr<Node> cloneTree() const;

    virtual std::unique_ptr<Node> clone() const { return std::make_unique<Node>(type_); };
    virtual void calcFunctions(std::vector<PositionalNode*>& positions);

 protected:
    NodeType type_;
    std::unique_ptr<Node> left_, right_;  // Binary tree leaves
    // Node functions:
    bool nullable_ = false;  // nullable(node) function
    ValueSet firstpos_;      // firstpos(node) function
    ValueSet lastpos_;       // lastpos(node) function
};

// Empty symbol node class
class EmptySymbNode : public Node {
 public:
    EmptySymbNode() : Node(NodeType::kEmptySymb) {}
    std::unique_ptr<Node> clone() const override { return std::make_unique<EmptySymbNode>(); };
    void calcFunctions(std::vector<PositionalNode*>& positions) override;
};

// Positional node class
class PositionalNode : public Node {
 public:
    explicit PositionalNode(NodeType type) : Node(type) {}
    void calcFunctions(std::vector<PositionalNode*>& positions) override;
    const ValueSet& getFollowpos() const { return followpos_; };
    void addFollowpos(unsigned pos) { followpos_.addValue(pos); };
    void addFollowpos(const ValueSet& pos_set) { followpos_ |= pos_set; };

 protected:
    unsigned position_ = 0;  // Node position
    ValueSet followpos_;     // followpos(node) function
};

// Symbol node class
class SymbNode : public PositionalNode {
 public:
    explicit SymbNode(unsigned symb) : PositionalNode(NodeType::kSymbol), symb_(symb) {}
    unsigned getSymbol() const { return symb_; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<SymbNode>(symb_); };

 protected:
    unsigned symb_;  // Node symbol
};

// Symbol set node class
class SymbSetNode : public PositionalNode {
 public:
    explicit SymbSetNode(const ValueSet& sset) : PositionalNode(NodeType::kSymbSet), sset_(sset) {}
    const ValueSet& getSymbSet() const { return sset_; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<SymbSetNode>(sset_); };

 protected:
    ValueSet sset_;
};

// Trailing context node
class TrailingContextNode : public PositionalNode {
 public:
    TrailingContextNode() : PositionalNode(NodeType::kTrailingContext) {}
    std::unique_ptr<Node> clone() const override { return std::make_unique<TrailingContextNode>(); };
    void calcFunctions(std::vector<PositionalNode*>& positions) override;
};

// Termination node class
class TermNode : public PositionalNode {
 public:
    explicit TermNode(unsigned pat_no) : PositionalNode(NodeType::kTerm), pattern_no_(pat_no) {}
    unsigned getPatternNo() const { return pattern_no_; };
    std::unique_ptr<Node> clone() const override { return std::make_unique<TermNode>(pattern_no_); };

 protected:
    unsigned pattern_no_;
};
