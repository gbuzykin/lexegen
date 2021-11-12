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
        case NodeType::kQuestion: {
            nullable_ = type_ != NodeType::kPlus || left_->nullable_;
            firstpos_ = left_->firstpos_;
            lastpos_ = left_->lastpos_;
        } break;
        default: assert(false); break;
    }

    switch (type_) {
        case NodeType::kCat: {
            int p = left_->lastpos_.getFirstValue();
            while (p != -1) {
                positions[p]->addFollowpos(right_->firstpos_);
                p = left_->lastpos_.getNextValue(p);
            }
        } break;
        case NodeType::kStar:
        case NodeType::kPlus: {
            int p = left_->lastpos_.getFirstValue();
            while (p != -1) {
                positions[p]->addFollowpos(left_->firstpos_);
                p = left_->lastpos_.getNextValue(p);
            }
        } break;
        default: break;
    }
}

void EmptySymbNode::calcFunctions(std::vector<PositionalNode*>& positions) { nullable_ = true; }

void PositionalNode::calcFunctions(std::vector<PositionalNode*>& positions) {
    position_ = static_cast<unsigned>(positions.size());
    if (position_ > ValueSet::kMaxValue) { throw std::runtime_error("too many positions"); }
    positions.push_back(this);

    nullable_ = false;
    firstpos_.addValue(position_);
    lastpos_.addValue(position_);
}

void TrailContNode::calcFunctions(std::vector<PositionalNode*>& positions) {
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

    int p = left_->getLastpos().getFirstValue();
    while (p != -1) {
        positions[p]->addFollowpos(position_);
        p = left_->getLastpos().getNextValue(p);
    }
    positions[position_]->addFollowpos(right_->getFirstpos());
}
