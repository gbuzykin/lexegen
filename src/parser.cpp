#include "parser.h"

#include "node.h"

#include <algorithm>
#include <cctype>

namespace lex_detail {
#include "lex_analyzer.inl"
}  // namespace lex_detail

Parser::Parser(std::istream& input) : lex_data_(input), sc_stack_{lex_detail::sc_initial} {
    lex_data_.get_more = getMoreChars;
}

namespace {
int errorSyntax(int line_no) {
    std::cerr << std::endl << "****Error(" << line_no << "): syntax." << std::endl;
    return -1;
}

int errorStartConditionAlreadyDef(int line_no, const std::string& sc_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): start condition '" << sc_id << "' is already defined." << std::endl;
    return -1;
}

int errorRegexAlreadyDef(int line_no, const std::string& reg_def_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): regular expresion '" << reg_def_id << "' is already defined."
              << std::endl;
    return -1;
}

int errorPatternAlreadyDef(int line_no, const std::string& pattern_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): pattern '" << pattern_id << "' is already used." << std::endl;
    return -1;
}

int errorUndefStartCondition(int line_no, const std::string& sc_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): undefined start condition identifier '" << sc_id << "'." << std::endl;
    return -1;
}

int errorRegex(int line_no, const std::string& reg_expr, size_t pos) {
    std::cerr << std::endl << "****Error(" << line_no << "): regular expression:" << std::endl;
    std::cerr << "    " << reg_expr.substr(0, pos) << "<=ERROR";
    if (pos < reg_expr.size()) { std::cerr << "  " << reg_expr.substr(pos); }
    std::cerr << "." << std::endl;
    return -1;
}
}  // namespace

int Parser::parse() {
    int tt = 0;
    TokenValue val;

    // Load definitions
    start_conditions_.emplace_back("initial");  // Add initial start condition
    while (1) {
        if ((tt = lex(val)) == tt_start) {  // Start condition definition
            if ((tt = lex(val)) != tt_id) { return errorSyntax(line_no_); }
            if (std::find(start_conditions_.begin(), start_conditions_.end(), val.str) != start_conditions_.end()) {
                return errorStartConditionAlreadyDef(line_no_, val.str);
            }
            start_conditions_.emplace_back(std::move(val.str));
        } else if (tt == tt_option) {  // Option
            if ((tt = lex(val)) != tt_id) { return errorSyntax(line_no_); }
            std::string opt_id(std::move(val.str));
            if ((tt = lex(val)) != '=') { return errorSyntax(line_no_); }
            if ((tt = lex(val)) != tt_string) { return errorSyntax(line_no_); }
            options_.emplace(std::move(opt_id), std::move(val.str));
        } else if (tt == tt_id) {  // Regular definition
            std::string definition_id(std::move(val.str));

            RegexParserState state;
            sc_stack_.push_back(lex_detail::sc_reg_expr);
            if ((tt = lex(val)) != tt_reg_expr) { return errorSyntax(line_no_); }
            sc_stack_.pop_back();

            auto syn_tree = parseRegex(val.str, state);
            if (!syn_tree) { return errorRegex(line_no_, val.str, state.pos); }
            auto [it, success] = definitions_.emplace(std::move(definition_id), std::move(syn_tree));
            if (!success) { return errorRegexAlreadyDef(line_no_, it->first); }
        } else if (tt == tt_sep) {
            break;
        } else {
            return errorSyntax(line_no_);
        }
    }

    // Load patterns
    while (1) {
        if ((tt = lex(val)) == tt_id) {
            std::string pattern_id(std::move(val.str));
            if (std::find_if(patterns_.begin(), patterns_.end(),
                             [&](const auto& pat) { return pat.id == pattern_id; }) != patterns_.end()) {
                return errorPatternAlreadyDef(line_no_, pattern_id);
            }

            // Load pattern
            ValueSet sc;
            sc_stack_.push_back(lex_detail::sc_sc_list);
            if ((tt = lex(val)) == tt_sc_list_begin) {
                sc_stack_.pop_back();
                // Parse start conditions
                while (1) {
                    if ((tt = lex(val)) == tt_id) {
                        // Find start condition
                        auto sc_it = std::find(start_conditions_.begin(), start_conditions_.end(), val.str);
                        if (sc_it == start_conditions_.end()) { return errorUndefStartCondition(line_no_, val.str); }
                        // Add start condition
                        sc.addValue(static_cast<unsigned>(sc_it - start_conditions_.begin()));
                    } else if (tt == '>') {
                        break;
                    } else {
                        return errorSyntax(line_no_);
                    }
                }
                sc_stack_.push_back(lex_detail::sc_reg_expr);
                tt = lex(val);
            } else {
                sc.addValues(0, static_cast<unsigned>(start_conditions_.size()) - 1);
            }
            sc_stack_.pop_back();

            std::unique_ptr<Node> syn_tree;
            if (tt == tt_reg_expr) {
                RegexParserState state;
                syn_tree = parseRegex(val.str, state);
                if (!syn_tree) { return errorRegex(line_no_, val.str, state.pos); }
            } else if (tt == tt_eof_expr) {
                syn_tree = std::make_unique<SymbNode>(0);  // Add <<EOF>> pattern
            } else {
                return errorSyntax(line_no_);
            }
            patterns_.emplace_back(Pattern{std::move(pattern_id), sc, std::move(syn_tree)});
        } else if ((tt == 0) || (tt == tt_sep)) {
            break;
        } else {
            return errorSyntax(line_no_);
        }
    }

    return 0;
}

std::unique_ptr<Node> Parser::parseRegex(const std::string& reg_expr, RegexParserState& state) {
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
    std::vector<std::unique_ptr<Node>> node_stack;
    ll1_stack.push_back(0);

    // Parse regular expression
    TokenValue val;
    int num1 = -1, num2 = -1;
    int cur_symb = grammar[0];
    int last_token = lexRegex(reg_expr, state, val);
    do {
        if ((cur_symb & 0x3000) == 0) {                // Token
            if (cur_symb != last_token) { return 0; }  // Syntax error
            if (cur_symb == tt_symb) {                 // Create 'symbol' subtree
                node_stack.emplace_back(std::make_unique<SymbNode>(val.i));
            } else if (cur_symb == tt_sset) {  // Create 'symbol set' subtree
                node_stack.emplace_back(std::make_unique<SymbSetNode>(val.sset));
            } else if (cur_symb == tt_id) {  // Insert subtree
                auto pat_it = definitions_.find(val.str);
                if (pat_it == definitions_.end()) { return 0; }  // Syntax error
                node_stack.emplace_back(pat_it->second->cloneTree());
            } else if (cur_symb == tt_string) {  // Create 'string' subtree
                if (!val.str.empty()) {
                    std::unique_ptr<Node> str_node = std::make_unique<SymbNode>(static_cast<unsigned char>(val.str[0]));
                    for (size_t i = 1; i < val.str.size(); ++i) {
                        auto cat_node = std::make_unique<Node>(NodeType::kCat);
                        cat_node->setLeft(std::move(str_node));
                        cat_node->setRight(std::make_unique<SymbNode>(static_cast<unsigned char>(val.str[i])));
                        str_node = std::move(cat_node);
                    }
                    node_stack.emplace_back(std::move(str_node));
                } else {
                    node_stack.emplace_back(std::make_unique<EmptySymbNode>());
                }
            } else if (cur_symb == tt_int) {  // Save number
                if (num1 == -1) {
                    num1 = val.i;
                } else {
                    num2 = val.i;
                }
            }
            last_token = lexRegex(reg_expr, state, val);
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
                    auto trail_cont_node = std::make_unique<TrailContNode>();
                    trail_cont_node->setRight(std::move(node_stack.back()));
                    node_stack.pop_back();
                    trail_cont_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(trail_cont_node);
                } break;
                case 1: {  // Or
                    auto or_node = std::make_unique<Node>(NodeType::kOr);
                    or_node->setRight(std::move(node_stack.back()));
                    node_stack.pop_back();
                    or_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(or_node);
                } break;
                case 2: {  // Cat
                    auto cat_node = std::make_unique<Node>(NodeType::kCat);
                    cat_node->setRight(std::move(node_stack.back()));
                    node_stack.pop_back();
                    cat_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(cat_node);
                } break;
                case 3: {  // Star
                    auto star_node = std::make_unique<Node>(NodeType::kStar);
                    star_node->setLeft(std::move(node_stack.back()));
                    assert(star_node->getLeft());
                    node_stack.back() = std::move(star_node);
                } break;
                case 4: {  // Plus
                    auto plus_node = std::make_unique<Node>(NodeType::kPlus);
                    plus_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(plus_node);
                } break;
                case 5: {  // Question
                    auto question_node = std::make_unique<Node>(NodeType::kQuestion);
                    question_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(question_node);
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
                    const auto* child = node_stack.back().get();
                    // Fixed part
                    std::unique_ptr<Node> left_subtree;
                    if (num1 > 0) {
                        left_subtree = std::move(node_stack.back());
                        for (int i = 1; i < num1; ++i) {
                            auto cat_node = std::make_unique<Node>(NodeType::kCat);
                            cat_node->setLeft(std::move(left_subtree));
                            cat_node->setRight(child->clone());
                            left_subtree = std::move(cat_node);
                        }
                    }
                    // Optional part
                    std::unique_ptr<Node> right_subtree;
                    if (num2 == -1) {  // Infinite multiplication
                        right_subtree = std::make_unique<Node>(NodeType::kStar);
                        right_subtree->setLeft(num1 > 0 ? child->clone() : std::move(node_stack.back()));
                        assert(right_subtree->getLeft());
                    } else if (num2 > num1) {  // Finite multiplication
                        right_subtree = std::make_unique<Node>(NodeType::kQuestion);
                        right_subtree->setLeft(num1 > 0 ? child->clone() : std::move(node_stack.back()));
                        for (int i = num1 + 1; i < num2; i++) {
                            auto cat_node = std::make_unique<Node>(NodeType::kCat);
                            cat_node->setLeft(std::move(right_subtree));
                            cat_node->setRight(std::make_unique<Node>(NodeType::kQuestion));
                            cat_node->getRight()->setLeft(child->clone());
                            right_subtree = std::move(cat_node);
                        }
                    }
                    // Concatenate fixed and optional parts
                    if (left_subtree && right_subtree) {
                        auto cat_node = std::make_unique<Node>(NodeType::kCat);
                        cat_node->setLeft(std::move(left_subtree));
                        cat_node->setRight(std::move(right_subtree));
                        node_stack.back() = std::move(cat_node);
                    } else if (left_subtree) {
                        node_stack.back() = std::move(left_subtree);
                    } else if (right_subtree) {
                        node_stack.back() = std::move(right_subtree);
                    } else {
                        node_stack.back() = std::make_unique<EmptySymbNode>();
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
    return std::move(node_stack.back());
}

/*static*/ void Parser::getMoreChars(lex_detail::StateData& data) {
    const size_t kChunkSize = 32;
    data.text.resize(data.pat_length + kChunkSize);
    data.unread_text = data.text.data() + data.pat_length;
    static_cast<LexerData&>(data).input.read(data.unread_text, kChunkSize);
    size_t count = static_cast<LexerData&>(data).input.gcount();
    if (count < kChunkSize) {
        data.text.resize(data.pat_length + count + 1);
        data.text.back() = '\0';  // EOF
    }
}

/*static*/ int Parser::str2int(const char* str, size_t length) {
    int ret = 0;
    bool neg = false;
    if (*str == '-') {
        ++str;
        --length;
        neg = true;
    } else if (*str == '+') {
        --length;
        ++str;
    }
    while (length) {
        ret = 10 * ret + dig(*str);
        --length;
        ++str;
    }
    return neg ? -ret : ret;
}

/*static*/ char Parser::readEscape(const std::string& reg_expr, RegexParserState& state) {
    if (state.pos >= reg_expr.size()) { return 0; }
    char ch = reg_expr[state.pos++];
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
                if (state.pos >= reg_expr.size()) { return 0; }
                char ch2 = reg_expr[state.pos++];
                if (!std::isxdigit(ch2)) { return 0; }
                ch = (ch << 4) | hdig(ch2);
            }
            return ch;
        }
        default: {
            if (isodigit(ch)) {
                ch = dig(ch);
                for (int i = 0; i < 2; i++) {
                    if (state.pos >= reg_expr.size()) { return 0; }
                    char ch2 = reg_expr[state.pos++];
                    if (!isodigit(ch2)) { return 0; }
                    ch = (ch << 3) | dig(ch2);
                }
            }
            return ch;
        }
    }
    return 0;
}

/*static*/ int Parser::lexRegex(const std::string& reg_expr, RegexParserState& state, TokenValue& val) {
    while (state.pos < reg_expr.size()) {
        char ch = reg_expr[state.pos++];  // Get the next char
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
                if (state.mult_op) {
                    state.mult_op = false;
                    return static_cast<unsigned char>(ch);  // Exit multiplication operator
                }
                val.i = static_cast<unsigned char>(ch);
                return tt_symb;  // Just '}' symbol
            }
            case '[': {  // Symbol set
                bool finished = false, invert = false, range = false;
                unsigned from = 0;
                ValueSet symb_set;
                while (state.pos < reg_expr.size()) {
                    unsigned to = 0;
                    ch = reg_expr[state.pos++];             // Get the next char
                    if (!from && !invert && (ch == '^')) {  // Is inverted set
                        invert = true;
                        continue;
                    } else if (ch == ']') {
                        if (range) { symb_set.addValue('-'); }
                        finished = true;
                        break;
                    } else if (ch == '\\') {  // Escape sequence
                        to = static_cast<unsigned char>(readEscape(reg_expr, state));
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
                        val.sset.clear();
                        val.sset.addValues(1, 255);
                        val.sset -= symb_set;
                    } else {
                        val.sset = symb_set;
                    }
                    return tt_sset;
                }
                return -1;
            }
            case '{': {  // Identifier or a single '{'
                if (state.mult_op) { return static_cast<unsigned char>(ch); }
                val.str.clear();
                if (state.pos >= reg_expr.size()) { return -1; }
                auto old_pos = state.pos;  // Position after '{'
                ch = reg_expr[state.pos++];
                if (std::isalpha(ch) || (ch == '_')) {  // The first character of the identifier must be a letter or '_'
                    val.str += ch;
                    while (state.pos < reg_expr.size()) {
                        ch = reg_expr[state.pos++];
                        if (ch == '}') {
                            return tt_id;
                        } else if (!std::isalpha(ch) && !std::isdigit(ch) && (ch != '_')) {
                            break;
                        }
                        val.str += ch;
                    }
                }
                state.mult_op = true;
                state.pos = old_pos;
                return '{';  // Enter multiplication operator
            }
            case '\"': {  // String
                val.str.clear();
                while (state.pos < reg_expr.size()) {
                    ch = reg_expr[state.pos++];
                    if (ch == '\"') {
                        return tt_string;
                    } else if (ch == '\\') {  // Escape sequence
                        ch = readEscape(reg_expr, state);
                        if (ch == 0) { return -1; }
                    }
                    val.str += ch;
                }
                return -1;
            }
            case '\\': {  // Escape sequence
                val.i = static_cast<unsigned char>(readEscape(reg_expr, state));
                if (val.i == 0) { return -1; }
                return tt_symb;
            }
            case '.': {
                // All symbols excepts '\n'
                val.sset.addValues(1, 255);
                val.sset.removeValue('\n');
                return tt_sset;
            }
            default: {
                if ((ch != ' ') && (ch != '\t') && (ch != '\n')) {
                    if (state.mult_op) {
                        if (std::isdigit(ch)) {
                            val.i = dig(ch);
                            while (state.pos < reg_expr.size()) {
                                ch = reg_expr[state.pos++];
                                if (!std::isdigit(ch)) {
                                    state.pos--;
                                    return tt_int;
                                } else {
                                    val.i = 10 * val.i + dig(ch);
                                }
                            }
                            return tt_int;
                        } else {
                            return static_cast<unsigned char>(ch);
                        }
                    }
                    val.i = static_cast<unsigned char>(ch);
                    return tt_symb;
                }
            }
        }
    }
    return 0;  // End of regular expression
}

int Parser::lex(TokenValue& val) {
    while (1) {
        int pat = lex_detail::lex(lex_data_, sc_stack_.back());
        switch (pat) {
            case lex_detail::pat_int: {
                val.i = str2int(lex_data_.text.data(), lex_data_.pat_length);
                return tt_int;  // Return integer
            }
            case lex_detail::pat_string_begin: {
                val.str.clear();
                sc_stack_.push_back(lex_detail::sc_string);
                break;
            }
            case lex_detail::pat_string_cont: {
                val.str.append(lex_data_.text.data(), lex_data_.pat_length);
                break;
            }
            case lex_detail::pat_string_es_a: val.str.push_back('\a'); break;
            case lex_detail::pat_string_es_b: val.str.push_back('\b'); break;
            case lex_detail::pat_string_es_f: val.str.push_back('\f'); break;
            case lex_detail::pat_string_es_n: val.str.push_back('\n'); break;
            case lex_detail::pat_string_es_r: val.str.push_back('\r'); break;
            case lex_detail::pat_string_es_t: val.str.push_back('\t'); break;
            case lex_detail::pat_string_es_v: val.str.push_back('\v'); break;
            case lex_detail::pat_string_es_bslash: val.str.push_back('\\'); break;
            case lex_detail::pat_string_es_dquot: val.str.push_back('\"'); break;
            case lex_detail::pat_string_es_hex:
                val.str.push_back((hdig(lex_data_.text[2]) << 4) | hdig(lex_data_.text[3]));
                break;
            case lex_detail::pat_string_es_oct:
                val.str.push_back((dig(lex_data_.text[1]) << 6) | (dig(lex_data_.text[2]) << 3) |
                                  dig(lex_data_.text[3]));
                break;
            case lex_detail::pat_string_nl: return -1;   // Error
            case lex_detail::pat_string_eof: return -1;  // Error
            case lex_detail::pat_string_end: {
                sc_stack_.pop_back();
                return tt_string;  // Return string
            }
            case lex_detail::pat_id: {
                val.str = std::string(lex_data_.text.data(), lex_data_.pat_length);
                return tt_id;  // Return identifier
            }
            case lex_detail::pat_start: return tt_start;                  // Return "%start" keyword
            case lex_detail::pat_option: return tt_option;                // Return "%option" keyword
            case lex_detail::pat_sep: return tt_sep;                      // Return separator "%%"
            case lex_detail::pat_sc_list_begin: return tt_sc_list_begin;  // Return start condition list begin
            case lex_detail::pat_reg_expr_begin: {
                val.str.clear();
                do {
                    if (lex_data_.unread_text == lex_data_.text.data() + lex_data_.text.size()) {
                        getMoreChars(lex_data_);
                    }
                    val.str.push_back(*lex_data_.unread_text++);
                } while (val.str.back() != '\0' && val.str.back() != '\n');
                ++line_no_;
                return tt_reg_expr;  // Return regular expression
            }
            case lex_detail::pat_eof_expr: return tt_eof_expr;  // Return "<<EOF>>" expression
            case lex_detail::pat_comment: {
                char symb = '\0';
                do {
                    if (lex_data_.unread_text == lex_data_.text.data() + lex_data_.text.size()) {
                        getMoreChars(lex_data_);
                    }
                    symb = *lex_data_.unread_text++;  // Eat up comment
                } while (symb != '\0' && symb != '\n');
                ++line_no_;
            } break;
            case lex_detail::pat_whitespace: break;
            case lex_detail::pat_nl: ++line_no_; break;
            case lex_detail::pat_other_char: return static_cast<unsigned char>(lex_data_.text[0]);  // Return character
            case lex_detail::pat_eof: return 0;                             // Return end of file
            default: return static_cast<unsigned char>(lex_data_.text[0]);  // Return character
        }
    }
    return 0;  // Return end of file
}
