#include "reparser.h"

#include "node.h"

#include <vector>

///////////////////////////////////////////////////////////////////////////////
// REParser public methods

Node* REParser::parse(const std::map<std::string, Node*>& definitions, const std::string& reg_expr) {
    // Parser tables:
    static int token_count = 16;
    static int nonterm_count = 9;
    static int grammar[] = {4096, 4097, 4098, 4098, 47,   4097, 8192, 4098, 4097, 4099, 4100, 4101, 4101, 124,
                            4099, 4100, 8193, 4101, 4101, 4100, 4099, 8194, 4100, 4100, 4099, 256,  4102, 4099,
                            257,  4102, 4099, 258,  4102, 4099, 259,  4102, 4099, 40,   4096, 41,   4102, 4102,
                            42,   8195, 4102, 4102, 43,   8196, 4102, 4102, 63,   8197, 4102, 4102, 123,  8198,
                            260,  4103, 125,  8200, 4102, 4102, 4103, 44,   4104, 4103, 8199, 4104, 260,  4104};
    static int grammar_idx[] = {0, 3, 7, 8, 12, 18, 19, 23, 24, 27, 30, 33, 36, 41, 45, 49, 53, 61, 62, 65, 67, 69, 70};
    static int id_to_idx_tbl[] = {
        0,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 8,  9,  10, 11, 15, -1, -1, 6,  -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, 7,  14, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  2,  3,  4,  5};
    static int ll1_table[] = {-1, 0,  0,  0,  0,  -1, -1, -1, 0,  -1, -1, -1, -1, -1, -1, -1, -1, 3,  3,  3,  3,
                              -1, -1, -1, 3,  -1, -1, -1, -1, -1, -1, -1, 2,  -1, -1, -1, -1, -1, 1,  -1, -1, 2,
                              -1, -1, -1, -1, -1, -1, -1, 8,  9,  10, 11, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1,
                              -1, 7,  6,  6,  6,  6,  -1, 7,  7,  6,  7,  -1, -1, -1, -1, -1, -1, 5,  -1, -1, -1,
                              -1, -1, 5,  4,  -1, 5,  -1, -1, -1, -1, -1, -1, 17, 17, 17, 17, 17, -1, 17, 17, 17,
                              17, 13, 14, 15, 16, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                              19, 18, -1, -1, -1, -1, -1, 20, -1, -1, -1, -1, -1, -1, -1, -1, 21, -1};

    // LL(1) stack initialization
    std::vector<int> ll1_stack;
    std::vector<Node*> node_stack;
    ll1_stack.push_back(0);

    // Parse regular expression
    pos_ = 0;  // Reset position
    mult_op_ = false;
    int num1 = -1, num2 = -1;
    int cur_symb = grammar[0];
    int last_token = getNextToken(reg_expr);
    do {
        if ((cur_symb & 0x3000) == 0) {                // Token
            if (cur_symb != last_token) { return 0; }  // Syntax error
            if (cur_symb == ttSymb) {                  // Create 'symbol' subtree
                node_stack.push_back(new SymbNode(symb_));
            } else if (cur_symb == ttSSet) {  // Create 'symbol set' subtree
                node_stack.push_back(new SymbSetNode(sset_));
            } else if (cur_symb == ttID) {  // Insert subtree
                auto pat_it = definitions.find(id_);
                if (pat_it == definitions.end()) { return 0; }  // Syntax error
                node_stack.push_back(pat_it->second->cloneTree());
            } else if (cur_symb == ttStr) {  // Create 'string' subtree
                if (!str_.empty()) {
                    Node* str_node = new SymbNode(static_cast<unsigned char>(str_[0]));
                    for (std::string::size_type i = 1; i < str_.size(); i++) {
                        auto* cat_node = new Node(kCat);
                        cat_node->setLeft(str_node);
                        cat_node->setRight(new SymbNode(static_cast<unsigned char>(str_[i])));
                        str_node = cat_node;
                    }
                    node_stack.push_back(str_node);
                } else {
                    node_stack.push_back(new EmptySymbNode());
                }
            } else if (cur_symb == ttNum) {  // Save number
                if (num1 == -1) {
                    num1 = num_;
                } else {
                    num2 = num_;
                }
            }
            last_token = getNextToken(reg_expr);
        } else if ((cur_symb & 0x3000) == 0x1000) {  // Nonterminal
            int idx = id_to_idx_tbl[last_token];
            if (idx == -1) { return 0; }  // Syntax error
            int prod_no = ll1_table[token_count * (cur_symb & 0xFFF) + idx];
            if (prod_no != -1) {
                // Append LL(1) stack
                for (int i = grammar_idx[prod_no + 1] - 1; i > grammar_idx[prod_no]; --i) {
                    ll1_stack.push_back(grammar[i]);
                }
            } else {
                return 0;  // Syntax error
            }
        } else if ((cur_symb & 0x3000) == 0x2000) {  // Action
            switch (cur_symb & 0xFFF) {
                case 0: {  // Trailing context
                    auto* trail_cont_node = new TrailContNode(-1);
                    trail_cont_node->setRight(node_stack.back());
                    node_stack.pop_back();
                    trail_cont_node->setLeft(node_stack.back());
                    node_stack.back() = trail_cont_node;
                } break;
                case 1: {  // Or
                    auto* or_node = new Node(kOr);
                    or_node->setRight(node_stack.back());
                    node_stack.pop_back();
                    or_node->setLeft(node_stack.back());
                    node_stack.back() = or_node;
                } break;
                case 2: {  // Cat
                    auto* cat_node = new Node(kCat);
                    cat_node->setRight(node_stack.back());
                    node_stack.pop_back();
                    cat_node->setLeft(node_stack.back());
                    node_stack.back() = cat_node;
                } break;
                case 3: {  // Star
                    auto* star_node = new Node(kStar);
                    star_node->setLeft(node_stack.back());
                    node_stack.back() = star_node;
                } break;
                case 4: {  // Plus
                    auto* plus_node = new Node(kPlus);
                    plus_node->setLeft(node_stack.back());
                    node_stack.back() = plus_node;
                } break;
                case 5: {  // Question
                    auto* question_node = new Node(kQuestion);
                    question_node->setLeft(node_stack.back());
                    node_stack.back() = question_node;
                } break;
                case 6: {  // Reset multiplication parameters
                    num1 = -1;
                    num2 = -1;
                } break;
                case 7: {
                    num2 = num1;  // Set exact multiplication
                } break;
                case 8: {  // Multiplicate node
                    assert(num1 >= 0);
                    auto* child = node_stack.back();
                    // Fixed part
                    Node* left_subtree = nullptr;
                    if (num1 > 0) {
                        left_subtree = child;
                        for (int i = 1; i < num1; i++) {
                            auto* cat_node = new Node(kCat);
                            cat_node->setLeft(left_subtree);
                            cat_node->setRight(child->cloneTree());
                            left_subtree = cat_node;
                        }
                    }
                    // Optional part
                    Node* right_subtree = nullptr;
                    if (num2 == -1) {  // Infinite multiplication
                        right_subtree = new Node(kStar);
                        right_subtree->setLeft(num1 == 0 ? child : child->cloneTree());
                    } else if (num2 > num1) {  // Finite multiplication
                        right_subtree = new Node(kQuestion);
                        right_subtree->setLeft(num1 == 0 ? child : child->cloneTree());
                        for (int i = num1 + 1; i < num2; i++) {
                            auto* cat_node = new Node(kCat);
                            cat_node->setLeft(right_subtree);
                            cat_node->setRight(new Node(kQuestion));
                            cat_node->getRight()->setLeft(child->cloneTree());
                            right_subtree = cat_node;
                        }
                    }
                    // Concatenate fixed and optional parts
                    if (left_subtree && right_subtree) {
                        auto* cat_node = new Node(kCat);
                        cat_node->setLeft(left_subtree);
                        cat_node->setRight(right_subtree);
                        node_stack.back() = cat_node;
                    } else if (left_subtree) {
                        node_stack.back() = left_subtree;
                    } else if (right_subtree) {
                        node_stack.back() = right_subtree;
                    } else {
                        node_stack.back() = new EmptySymbNode();
                        child->deleteTree();
                    }
                } break;
            }
        } else {  // Syntax error
            return 0;
        }
        cur_symb = ll1_stack.back();  // Get symbol from LL(1) stack
        ll1_stack.pop_back();
    } while (cur_symb != 0);  // End of expression
    if (last_token != 0) { return 0; }
    assert(node_stack.size() == 1);
    return node_stack.back();
}

///////////////////////////////////////////////////////////////////////////////
// REParser private/protected methods

/*static*/ int REParser::hexDigit(char ch) {
    if ((ch >= '0') && (ch <= '9')) { return static_cast<int>(ch - '0'); }
    if ((ch >= 'a') && (ch <= 'f')) { return static_cast<int>(ch - 'a') + 10; }
    if ((ch >= 'A') && (ch <= 'F')) { return static_cast<int>(ch - 'A') + 10; }
    return -1;
}

/*static*/ int REParser::octDigit(char ch) {
    if ((ch >= '0') && (ch <= '7')) { return static_cast<int>(ch - '0'); }
    return -1;
}

char REParser::readEscape(const std::string& reg_expr) {
    if (pos_ >= reg_expr.size()) { return 0; }
    char ch = reg_expr[pos_++];
    switch (ch) {
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'v': return '\v';
        case 'x':
        case 'X': {
            ch = 0;
            for (int i = 0; i < 2; i++) {
                if (pos_ >= reg_expr.size()) { return 0; }
                int dig = hexDigit(reg_expr[pos_++]);
                if (dig == -1) { return 0; }
                ch = (ch << 4) | dig;
            }
            return ch;
        }
        default: {
            int dig = octDigit(ch);
            if (dig != -1) {
                ch = dig;
                for (int i = 0; i < 2; i++) {
                    if (pos_ >= reg_expr.size()) { return 0; }
                    dig = octDigit(reg_expr[pos_++]);
                    if (dig == -1) { return 0; }
                    ch = (ch << 3) | dig;
                }
            }
            return ch;
        }
    }
    return 0;
}

int REParser::getNextToken(const std::string& reg_expr) {
    while (pos_ < reg_expr.size()) {
        char ch = reg_expr[pos_++];  // Get the next char
        // Analyze the first character
        switch (ch) {
            case '\0':
            case '|':
            case '/':
            case '*':
            case '+':
            case '?':
            case '(':
            case ')': return static_cast<unsigned char>(ch);
            case '}': {
                if (mult_op_) {
                    mult_op_ = false;
                    return static_cast<unsigned char>(ch);  // Exit multiplication operator
                }
                symb_ = static_cast<unsigned char>(ch);
                return ttSymb;  // Just '}' symbol
            }
            case '[': {  // Symbol set
                bool finished = false, invert = false, range = false;
                int from = 0;
                ValueSet symb_set;
                while (pos_ < reg_expr.size()) {
                    int to = 0;
                    ch = reg_expr[pos_++];                  // Get the next char
                    if (!from && !invert && (ch == '^')) {  // Is inverted set
                        invert = true;
                        continue;
                    } else if (ch == ']') {
                        if (range) { symb_set.addValue('-'); }
                        finished = true;
                        break;
                    } else if (ch == '\\') {  // Escape sequence
                        to = static_cast<unsigned char>(readEscape(reg_expr));
                        if (to == 0) { return -1; }
                    } else if (from && (ch == '-')) {  // Range
                        range = true;
                        continue;
                    } else {
                        to = static_cast<unsigned char>(ch);
                    }

                    if (range) {
                        symb_set.addValues(from, to);
                        range = false;
                    } else {
                        symb_set.addValue(to);
                    }
                    from = to;
                }

                if (from && finished) {  // Is valid set
                    if (invert) {
                        sset_.addValues(1, 255);
                        sset_ -= symb_set;
                    } else {
                        sset_ = symb_set;
                    }
                    return ttSSet;
                }
                return -1;
            }
            case '{': {  // Identifier or a single '{'
                if (mult_op_) { return static_cast<unsigned char>(ch); }
                id_.clear();
                if (pos_ >= reg_expr.size()) { return -1; }
                auto old_pos = pos_;  // Position after '{'
                ch = reg_expr[pos_++];
                if (std::isalpha(ch) || (ch == '_')) {  // The first character of the identifier must be a letter or '_'
                    id_ += ch;
                    while (pos_ < reg_expr.size()) {
                        ch = reg_expr[pos_++];
                        if (ch == '}') {
                            return ttID;
                        } else if (!std::isalpha(ch) && !std::isdigit(ch) && (ch != '_')) {
                            break;
                        }
                        id_ += ch;
                    }
                }
                mult_op_ = true;
                pos_ = old_pos;
                return '{';  // Enter multiplication operator
            }
            case '\"': {  // String
                str_.clear();
                while (pos_ < reg_expr.size()) {
                    ch = reg_expr[pos_++];
                    if (ch == '\"') {
                        return ttStr;
                    } else if (ch == '\\') {  // Escape sequence
                        ch = readEscape(reg_expr);
                        if (ch == 0) { return -1; }
                    }
                    str_ += ch;
                }
                return -1;
            }
            case '\\': {  // Escape sequence
                symb_ = static_cast<unsigned char>(readEscape(reg_expr));
                if (symb_ == 0) { return -1; }
                return ttSymb;
            }
            case '.': {
                // All symbols excepts '\n'
                sset_.addValues(1, 255);
                sset_.removeValue('\n');
                return ttSSet;
            }
            default: {
                if ((ch != ' ') && (ch != '\t') && (ch != '\n')) {
                    if (mult_op_) {
                        if (std::isdigit(ch)) {
                            num_ = static_cast<int>(ch - '0');
                            while (pos_ < reg_expr.size()) {
                                ch = reg_expr[pos_++];
                                if (!std::isdigit(ch)) {
                                    pos_--;
                                    return ttNum;
                                } else {
                                    num_ = 10 * num_ + static_cast<int>(ch - '0');
                                }
                            }
                            return ttNum;
                        } else {
                            return static_cast<unsigned char>(ch);
                        }
                    }
                    symb_ = static_cast<unsigned char>(ch);
                    return ttSymb;
                }
            }
        }
    }
    return 0;  // End of regular expression
}
