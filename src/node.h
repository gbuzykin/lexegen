#pragma once

#include "valset.h"

// Node types
enum NodeTypesEnum {
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

// Operator or empty symbol node class
class Node {
 public:
    explicit Node(NodeTypesEnum type) : type_(type) { assert((type >= kOr) && (type <= kTerm)); }
    virtual ~Node() = default;

    NodeTypesEnum getType() const { return type_; };
    int getPosition() const { return position_; };
    void setPosition(int pos) { position_ = pos; };
    bool getMark() const { return mark_; };
    void setMark(bool mark) { mark_ = mark; };
    Node* getLeft() const { return left_; };
    void setLeft(Node* left) { left_ = left; };
    Node* getRight() const { return right_; };
    void setRight(Node* right) { right_ = right; };
    bool isNullable() const { return nullable_; };
    const ValueSet& getFirstpos() const { return firstpos_; };
    const ValueSet& getLastpos() const { return lastpos_; };

    virtual Node* clone() const { return new Node(type_); };
    virtual void calcFunctions();

    void deleteTree();
    Node* cloneTree() const;

 protected:
    NodeTypesEnum type_;
    bool mark_ = false;                        // Node mark
    int position_ = -1;                        // Leaf position
    Node *left_ = nullptr, *right_ = nullptr;  // Binary tree leaves
    // Node functions:
    bool nullable_ = false;  // nullable(node) function
    ValueSet firstpos_;      // firstpos(node) function
    ValueSet lastpos_;       // lastpos(node) function
};

// Symbol node class
class SymbNode : public Node {
 public:
    explicit SymbNode(char symb) : Node(kSymbol), symb_(symb) {}
    char getSymbol() const { return symb_; };
    Node* clone() const override { return new SymbNode(symb_); };
    void calcFunctions() override;

 protected:
    char symb_;  // Node symbol
};

// Symbol set node class
class SymbSetNode : public Node {
 public:
    explicit SymbSetNode(const ValueSet& sset) : Node(kSymbSet), sset_(sset) {}
    const ValueSet& getSymbSet() const { return sset_; };
    Node* clone() const override { return new SymbSetNode(sset_); };
    void calcFunctions() override;

 protected:
    ValueSet sset_;
};

// Empty symbol node class
class EmptySymbNode : public Node {
 public:
    EmptySymbNode() : Node(kEmptySymb) {}
    Node* clone() const override { return new EmptySymbNode(); };
    void calcFunctions() override;
};

// Trailing context node
class TrailContNode : public Node {
 public:
    explicit TrailContNode(int pat_no) : Node(kTrailCont), pattern_no_(pat_no) {}
    int getPatternNo() const { return pattern_no_; };
    void setPatternNo(int pat_no) { pattern_no_ = pat_no; };
    Node* clone() const override { return new TrailContNode(pattern_no_); };
    void calcFunctions() override;

 protected:
    int pattern_no_;
};

// Termination node class
class TermNode : public Node {
 public:
    explicit TermNode(int pat_no) : Node(kTerm), pattern_no_(pat_no) {}
    int getPatternNo() const { return pattern_no_; };
    Node* clone() const override { return new TermNode(pattern_no_); };
    void calcFunctions() override;

 protected:
    int pattern_no_;
};
