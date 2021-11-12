#include "node.h"

std::unique_ptr<Node> Node::cloneTree() const {
    auto new_node = clone();
    if (left_) { new_node->left_ = left_->cloneTree(); }
    if (right_) { new_node->right_ = right_->cloneTree(); }
    return new_node;
}

//---------------------------------------------------------------------------------------

void Node::calcFunctions() {
    assert(left_);
    switch (type_) {
        case NodeType::kOr:
            assert(right_);
            nullable_ = left_->nullable_ || right_->nullable_;
            firstpos_ = left_->firstpos_ | right_->firstpos_;
            lastpos_ = left_->lastpos_ | right_->lastpos_;
            break;
        case NodeType::kCat:
            assert(right_);
            nullable_ = left_->nullable_ && right_->nullable_;
            firstpos_ = left_->firstpos_;
            lastpos_ = right_->lastpos_;
            if (left_->nullable_) { firstpos_ |= right_->firstpos_; }
            if (right_->nullable_) { lastpos_ |= left_->lastpos_; }
            break;
        case NodeType::kStar:
        case NodeType::kQuestion:
            nullable_ = true;
            firstpos_ = left_->firstpos_;
            lastpos_ = left_->lastpos_;
            break;
        case NodeType::kPlus:
            nullable_ = left_->nullable_;
            firstpos_ = left_->firstpos_;
            lastpos_ = left_->lastpos_;
            break;
    }
}

void SymbNode::calcFunctions() {
    nullable_ = false;
    firstpos_.addValue(position_);
    lastpos_.addValue(position_);
}

void SymbSetNode::calcFunctions() {
    nullable_ = false;
    firstpos_.addValue(position_);
    lastpos_.addValue(position_);
}

void EmptySymbNode::calcFunctions() { nullable_ = true; }

void TrailContNode::calcFunctions() {
    assert(left_);
    assert(right_);
    nullable_ = false;
    firstpos_ = left_->getFirstpos();
    if (left_->isNullable()) { firstpos_.addValue(position_); }
    lastpos_ = right_->getLastpos();
    if (right_->isNullable()) { lastpos_.addValue(position_); }
}

void TermNode::calcFunctions() {
    nullable_ = false;
    firstpos_.addValue(position_);
    lastpos_.addValue(position_);
}
