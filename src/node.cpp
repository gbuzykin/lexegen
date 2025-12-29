#include "node.h"

#include <stdexcept>

std::unique_ptr<Node> Node::cloneTree() const {
    auto new_node = clone();
    if (left_) { new_node->left_ = left_->cloneTree(); }
    if (right_) { new_node->right_ = right_->cloneTree(); }
    return new_node;
}

//---------------------------------------------------------------------------------------

void Node::calcFunctions(std::vector<PositionalNode*>& positions) {
    assert(left_);
    left_->calcFunctions(positions);
    if (right_) { right_->calcFunctions(positions); }
    switch (type_) {
        case NodeType::kOr: {
            assert(right_);
            nullable_ = left_->nullable_ || right_->nullable_;
            firstpos_ = left_->firstpos_ | right_->firstpos_;
            lastpos_ = left_->lastpos_ | right_->lastpos_;
        } break;
        case NodeType::kCat: {
            assert(right_);
            nullable_ = left_->nullable_ && right_->nullable_;
            firstpos_ = left_->firstpos_;
            lastpos_ = right_->lastpos_;
            if (left_->nullable_) { firstpos_ |= right_->firstpos_; }
            if (right_->nullable_) { lastpos_ |= left_->lastpos_; }
        } break;
        case NodeType::kStar:
        case NodeType::kPlus:
        case NodeType::kQuestion:
        case NodeType::kLeftNlAnchoring:
        case NodeType::kLeftNotNlAnchoring: {
            nullable_ = type_ == NodeType::kStar || type_ == NodeType::kQuestion || left_->nullable_;
            firstpos_ = left_->firstpos_;
            lastpos_ = left_->lastpos_;
        } break;
        default: assert(false); break;
    }

    switch (type_) {
        case NodeType::kCat: {
            for (unsigned pos : left_->lastpos_) { positions[pos]->addFollowpos(right_->firstpos_); }
        } break;
        case NodeType::kStar:
        case NodeType::kPlus: {
            for (unsigned pos : left_->lastpos_) { positions[pos]->addFollowpos(left_->firstpos_); }
        } break;
        default: break;
    }
}

void EmptySymbNode::calcFunctions(std::vector<PositionalNode*>& /*positions*/) { nullable_ = true; }

void PositionalNode::calcFunctions(std::vector<PositionalNode*>& positions) {
    position_ = static_cast<unsigned>(positions.size());
    if (position_ > ValueSet::kMaxValue) { throw std::runtime_error("too many positions"); }
    positions.push_back(this);

    nullable_ = false;
    firstpos_.addValue(position_);
    lastpos_.addValue(position_);
}

void TrailingContextNode::calcFunctions(std::vector<PositionalNode*>& positions) {
    assert(left_);
    assert(right_);
    left_->calcFunctions(positions);
    right_->calcFunctions(positions);

    position_ = static_cast<unsigned>(positions.size());
    if (position_ > ValueSet::kMaxValue) { throw std::runtime_error("too many positions"); }
    positions.push_back(this);

    nullable_ = false;
    firstpos_ = left_->getFirstpos();
    if (left_->isNullable()) { firstpos_.addValue(position_); }
    lastpos_ = right_->getLastpos();
    if (right_->isNullable()) { lastpos_.addValue(position_); }

    for (unsigned pos : left_->getLastpos()) { positions[pos]->addFollowpos(position_); }
    positions[position_]->addFollowpos(right_->getFirstpos());
}
