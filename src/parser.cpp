#include "parser.h"

#include "node.h"

#include <algorithm>
#include <optional>

namespace lex_detail {
#include "lex_analyzer.inl"
}

Parser::Parser(std::istream& input, std::string file_name)
    : input_(input), file_name_(std::move(file_name)), sc_stack_({lex_detail::sc_initial}) {}

int Parser::parse() {
    size_t file_sz = static_cast<size_t>(input_.seekg(0, std::ios_base::end).tellg());
    lex_state_.text.resize(file_sz + 1);

    // Read the whole file
    input_.seekg(0);
    input_.read(lex_state_.text.data(), file_sz);
    lex_state_.text.back() = '\0';

    int tt = 0;
    lex_state_.unread_text = lex_state_.text.data();
    current_line_ = lex_state_.unread_text;

    // Load definitions
    start_conditions_.emplace_back("initial");  // Add initial start condition
    do {
        switch (tt = lex()) {
            case tt_start: {  // Start condition definition
                if ((tt = lex()) != tt_id) { return logSyntaxError(tt); }
                if (std::find(start_conditions_.begin(), start_conditions_.end(), std::get<std::string>(tkn_.val)) !=
                    start_conditions_.end()) {
                    return logError() << "start condition is already defined.";
                }
                start_conditions_.emplace_back(std::move(std::get<std::string>(tkn_.val)));
            } break;
            case tt_id: {  // Regular definition
                std::string name(std::move(std::get<std::string>(tkn_.val)));
                if (definitions_.find(name) != definitions_.end()) {
                    return logError() << "regular expression is already defined.";
                }

                sc_stack_.push_back(lex_detail::sc_regex);
                std::unique_ptr<Node> syn_tree;
                std::tie(syn_tree, tt) = parseRegex(lex());
                sc_stack_.pop_back();

                if (tt < 0) { return tt; }
                definitions_.emplace(std::move(name), std::move(syn_tree));
            } break;
            case tt_option: {  // Option
                if ((tt = lex()) != tt_id) { return logSyntaxError(tt); }
                std::string name(std::move(std::get<std::string>(tkn_.val)));
                if ((tt = lex()) != tt_string) { return logSyntaxError(tt); }
                options_.emplace(std::move(name), std::move(std::get<std::string>(tkn_.val)));
            } break;
            case tt_sep: break;
            default: return logSyntaxError(tt);
        }
    } while (tt != tt_sep);

    // Load patterns
    do {
        if ((tt = lex()) == tt_id) {
            std::string name(std::move(std::get<std::string>(tkn_.val)));
            if (std::find_if(patterns_.begin(), patterns_.end(), [&](const auto& pat) { return pat.id == name; }) !=
                patterns_.end()) {
                return logError() << "pattern is already defined.";
            }

            ValueSet sc;
            sc_stack_.push_back(lex_detail::sc_regex);
            sc_stack_.push_back(lex_detail::sc_sc_list);
            if ((tt = lex()) == tt_sc_list_begin) {
                sc_stack_.push_back(lex_detail::sc_initial);
                // Parse start conditions
                while (true) {
                    if ((tt = lex()) == tt_id) {
                        auto sc_it = std::find(start_conditions_.begin(), start_conditions_.end(),
                                               std::get<std::string>(tkn_.val));
                        if (sc_it == start_conditions_.end()) { return logError() << "undefined start condition."; }
                        sc.addValue(static_cast<unsigned>(sc_it - start_conditions_.begin()));
                    } else if (tt == '>') {
                        break;
                    } else {
                        return logSyntaxError(tt);
                    }
                }
                sc_stack_.pop_back();
                sc_stack_.pop_back();
                tt = lex();
            } else {
                // Add all start conditions
                sc.addValues(0, static_cast<unsigned>(start_conditions_.size()) - 1);
                sc_stack_.pop_back();
            }

            std::unique_ptr<Node> syn_tree;
            std::tie(syn_tree, tt) = parseRegex(tt);
            sc_stack_.pop_back();

            if (tt < 0) { return tt; }
            patterns_.emplace_back(Pattern{std::move(name), sc, std::move(syn_tree)});
        } else if (tt != tt_sep) {
            return logSyntaxError(tt);
        }
    } while (tt != tt_sep);

    if (patterns_.empty()) {
        std::cerr << file_name_ << ": error: no patterns defined." << std::endl;
        return -1;
    }
    return 0;
}

namespace parser_detail {
enum { token_count = 16 };
enum { nonterm_count = 9 };
enum { tt_action = 0x2000 };
static int grammar[] = {4096, 4097, 4098, 4098, 47,   4097, 8192, 4098, 4097, 4099, 4100, 4101, 4101, 124,
                        4099, 4100, 8193, 4101, 4101, 4100, 4099, 8194, 4100, 4100, 4099, 256,  4102, 4099,
                        257,  4102, 4099, 258,  4102, 4099, 259,  4102, 4099, 40,   4096, 41,   4102, 4102,
                        42,   8195, 4102, 4102, 43,   8196, 4102, 4102, 63,   8197, 4102, 4102, 123,  8198,
                        260,  4103, 125,  8200, 4102, 4102, 4103, 44,   4104, 4103, 8199, 4104, 260,  4104};
static int grammar_idx[] = {0, 3, 7, 8, 12, 18, 19, 23, 24, 27, 30, 33, 36, 41, 45, 49, 53, 61, 62, 65, 67, 69, 70};
static int tt2idx[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 8,  9,  10, 11, 15, -1, -1, 6,  -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, 7,  14, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  2,  3,  4,  5,  0,  -1, -1, -1, -1, -1};
static int ll1_table[] = {
    -1, 0,  0,  0,  0,  -1, -1, -1, 0,  -1, -1, -1, -1, -1, -1, -1, -1, 3,  3,  3,  3,  -1, -1, -1, 3,  -1, -1, -1, -1,
    -1, -1, -1, 2,  -1, -1, -1, -1, -1, 1,  -1, -1, 2,  -1, -1, -1, -1, -1, -1, -1, 8,  9,  10, 11, -1, -1, -1, 12, -1,
    -1, -1, -1, -1, -1, -1, 7,  6,  6,  6,  6,  -1, 7,  7,  6,  7,  -1, -1, -1, -1, -1, -1, 5,  -1, -1, -1, -1, -1, 5,
    4,  -1, 5,  -1, -1, -1, -1, -1, -1, 17, 17, 17, 17, 17, -1, 17, 17, 17, 17, 13, 14, 15, 16, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 19, 18, -1, -1, -1, -1, -1, 20, -1, -1, -1, -1, -1, -1, -1, -1, 21, -1};
int parse(int tt, std::vector<int>& ll1_stack) {
    int symb_id = 0;
    do {
        symb_id = ll1_stack.back();  // Get symbol from LL(1) stack
        ll1_stack.pop_back();
        if (!(symb_id & 0x3000)) {                  // Token
            return symb_id == tt ? symb_id : -1;    // Valid token or syntax error
        } else if ((symb_id & 0x3000) == 0x1000) {  // Nonterminal
            int idx = parser_detail::tt2idx[tt];
            if (idx < 0) { return -1; }  // Syntax error
            int n_prod = parser_detail::ll1_table[parser_detail::token_count * (symb_id & 0xFFF) + idx];
            if (n_prod < 0) { return -1; }  // Syntax error
            // Append LL(1) stack
            for (int i = parser_detail::grammar_idx[n_prod + 1] - 1; i > parser_detail::grammar_idx[n_prod]; --i) {
                ll1_stack.push_back(parser_detail::grammar[i]);
            }
        } else {  // Action
            return symb_id;
        }
    } while (!ll1_stack.empty());
    return symb_id;
}
}  // namespace parser_detail

std::pair<std::unique_ptr<Node>, int> Parser::parseRegex(int tt) {
    // LL(1) stack initialization
    std::vector<int> ll1_stack;
    std::vector<std::unique_ptr<Node>> node_stack;
    ll1_stack.push_back(parser_detail::grammar[0]);

    // Parse regular expression
    unsigned num[2] = {0, 0}, num_given = 0;
    do {
        int symb_id = parser_detail::parse(tt, ll1_stack);
        if (symb_id < 0) { return {nullptr, logSyntaxError(tt)}; }

        switch (symb_id) {
            case tt_symb: {  // Create `symbol` subtree
                node_stack.emplace_back(std::make_unique<SymbNode>(std::get<unsigned>(tkn_.val)));
            } break;
            case tt_sset: {  // Create `symbol set` subtree
                node_stack.emplace_back(std::make_unique<SymbSetNode>(std::get<ValueSet>(tkn_.val)));
            } break;
            case tt_id: {  // Insert subtree
                auto pat_it = definitions_.find(std::get<std::string>(tkn_.val));
                if (pat_it == definitions_.end()) { return {nullptr, logError() << "undefined regular expression."}; }
                node_stack.emplace_back(pat_it->second->cloneTree());
            } break;
            case tt_string: {  // Create `string` subtree
                const auto& str = std::get<std::string>(tkn_.val);
                if (!str.empty()) {
                    std::unique_ptr<Node> str_node = std::make_unique<SymbNode>(static_cast<unsigned char>(str[0]));
                    for (size_t i = 1; i < str.size(); ++i) {
                        auto cat_node = std::make_unique<Node>(NodeType::kCat);
                        cat_node->setLeft(std::move(str_node));
                        cat_node->setRight(std::make_unique<SymbNode>(static_cast<unsigned char>(str[i])));
                        str_node = std::move(cat_node);
                    }
                    node_stack.emplace_back(std::move(str_node));
                } else {
                    node_stack.emplace_back(std::make_unique<EmptySymbNode>());
                }
            } break;
            case tt_num: {  // Save number
                assert(num_given < 2);
                num[num_given++] = std::get<unsigned>(tkn_.val);
            } break;
            case parser_detail::tt_action + 0: {  // Trailing context
                auto trail_cont_node = std::make_unique<TrailContNode>();
                trail_cont_node->setRight(std::move(node_stack.back()));
                node_stack.pop_back();
                trail_cont_node->setLeft(std::move(node_stack.back()));
                node_stack.back() = std::move(trail_cont_node);
            } break;
            case parser_detail::tt_action + 1: {  // Or
                auto or_node = std::make_unique<Node>(NodeType::kOr);
                or_node->setRight(std::move(node_stack.back()));
                node_stack.pop_back();
                or_node->setLeft(std::move(node_stack.back()));
                node_stack.back() = std::move(or_node);
            } break;
            case parser_detail::tt_action + 2: {  // Cat
                auto cat_node = std::make_unique<Node>(NodeType::kCat);
                cat_node->setRight(std::move(node_stack.back()));
                node_stack.pop_back();
                cat_node->setLeft(std::move(node_stack.back()));
                node_stack.back() = std::move(cat_node);
            } break;
            case parser_detail::tt_action + 3: {  // Star
                auto star_node = std::make_unique<Node>(NodeType::kStar);
                star_node->setLeft(std::move(node_stack.back()));
                assert(star_node->getLeft());
                node_stack.back() = std::move(star_node);
            } break;
            case parser_detail::tt_action + 4: {  // Plus
                auto plus_node = std::make_unique<Node>(NodeType::kPlus);
                plus_node->setLeft(std::move(node_stack.back()));
                node_stack.back() = std::move(plus_node);
            } break;
            case parser_detail::tt_action + 5: {  // Question
                auto question_node = std::make_unique<Node>(NodeType::kQuestion);
                question_node->setLeft(std::move(node_stack.back()));
                node_stack.back() = std::move(question_node);
            } break;
            case parser_detail::tt_action + 6: {  // Reset multiplication parameters
                num_given = 0;
            } break;
            case parser_detail::tt_action + 7: {
                assert(num_given == 1);
                num[num_given++] = num[0];  // Set exact multiplication
            } break;
            case parser_detail::tt_action + 8: {  // Multiplicate node
                assert(num_given > 0);
                const auto* child = node_stack.back().get();
                // Fixed part
                std::unique_ptr<Node> left_subtree;
                if (num[0] > 0) {
                    left_subtree = std::move(node_stack.back());
                    for (unsigned i = 1; i < num[0]; ++i) {
                        auto cat_node = std::make_unique<Node>(NodeType::kCat);
                        cat_node->setLeft(std::move(left_subtree));
                        cat_node->setRight(child->clone());
                        left_subtree = std::move(cat_node);
                    }
                }
                // Optional part
                std::unique_ptr<Node> right_subtree;
                if (num_given == 1) {  // Infinite multiplication
                    right_subtree = std::make_unique<Node>(NodeType::kStar);
                    right_subtree->setLeft(num[0] > 0 ? child->clone() : std::move(node_stack.back()));
                    assert(right_subtree->getLeft());
                } else if (num[1] > num[0]) {  // Finite multiplication
                    right_subtree = std::make_unique<Node>(NodeType::kQuestion);
                    right_subtree->setLeft(num[0] > 0 ? child->clone() : std::move(node_stack.back()));
                    for (unsigned i = num[0] + 1; i < num[1]; i++) {
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
        if (!(symb_id & 0x3000)) { tt = lex(); }
    } while (!ll1_stack.empty());
    assert(node_stack.size() == 1);
    return {std::move(node_stack.back()), tt};
}

int Parser::lex() {
    bool sset_is_inverted = false, sset_range_flag = false;
    unsigned sset_last = 0;
    tkn_.n_col = n_col_;

    while (true) {
        if (lex_state_.pat_length > 0 && lex_state_.text[0] == '\n') {
            current_line_ = lex_state_.unread_text;
            tkn_.n_col = n_col_ = 1;
            ++n_line_;
        }
        int pat = lex_detail::lex(lex_state_, sc_stack_.back());
        n_col_ += static_cast<unsigned>(lex_state_.pat_length);

        std::optional<char> escape;
        switch (pat) {
            // ------ escape sequences
            case lex_detail::pat_escape_a: escape = '\a'; break;
            case lex_detail::pat_escape_b: escape = '\b'; break;
            case lex_detail::pat_escape_f: escape = '\f'; break;
            case lex_detail::pat_escape_n: escape = '\n'; break;
            case lex_detail::pat_escape_r: escape = '\r'; break;
            case lex_detail::pat_escape_t: escape = '\t'; break;
            case lex_detail::pat_escape_v: escape = '\v'; break;
            case lex_detail::pat_escape_other: escape = lex_state_.text[1]; break;
            case lex_detail::pat_escape_hex: {
                escape = hdig(lex_state_.text[2]);
                if (lex_state_.pat_length > 3) { *escape = (*escape << 4) + hdig(lex_state_.text[3]); }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = dig(lex_state_.text[1]);
                if (lex_state_.pat_length > 2) { *escape = (*escape << 3) + dig(lex_state_.text[2]); }
                if (lex_state_.pat_length > 3) { *escape = (*escape << 3) + dig(lex_state_.text[3]); }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                tkn_.val = std::string();
                sc_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: {
                std::get<std::string>(tkn_.val).append(lex_state_.text.data(), lex_state_.pat_length);
            } break;
            case lex_detail::pat_string_close: {
                sc_stack_.pop_back();
                return tt_string;
            } break;

            // ------ regex symbol sets
            case lex_detail::pat_regex_sset:
            case lex_detail::pat_regex_sset_inv: {
                sset_is_inverted = pat == lex_detail::pat_regex_sset_inv;
                sset_range_flag = false;
                sset_last = 0;
                tkn_.val = ValueSet();
                sc_stack_.push_back(lex_detail::sc_sset);
            } break;
            case lex_detail::pat_regex_sset_seq: {
                if (sset_range_flag) {
                    std::get<ValueSet>(tkn_.val).addValues(sset_last, static_cast<unsigned char>(lex_state_.text[0]));
                    sset_range_flag = false;
                }
                sset_last = static_cast<unsigned char>(lex_state_.text[lex_state_.pat_length - 1]);
                for (unsigned i = 0; i < lex_state_.pat_length; ++i) {
                    std::get<ValueSet>(tkn_.val).addValue(static_cast<unsigned char>(lex_state_.text[i]));
                }
            } break;
            case lex_detail::pat_regex_sset_range: {
                if (!sset_range_flag && sset_last != '\0') {
                    sset_range_flag = true;
                } else {
                    escape = '-';  // Treat `-` as a character
                }
            } break;
            case lex_detail::pat_regex_sset_close: {
                if (sset_range_flag) { std::get<ValueSet>(tkn_.val).addValue('-'); }  // Treat `-` as a character
                if (sset_is_inverted) { std::get<ValueSet>(tkn_.val) ^= ValueSet(1, 255); }
                sc_stack_.pop_back();
                return tt_sset;
            } break;
            case lex_detail::pat_regex_dot: {
                tkn_.val = ValueSet(1, 255);
                std::get<ValueSet>(tkn_.val).removeValue('\n');
                return tt_sset;
            } break;
            case lex_detail::pat_regex_symb: {
                tkn_.val = static_cast<unsigned char>(lex_state_.text[0]);
                return tt_symb;
            } break;
            case lex_detail::pat_regex_eof_symb: {
                tkn_.val = 0;
                return tt_symb;
            } break;
            case lex_detail::pat_regex_id: {  // {id}
                tkn_.val = std::string(lex_state_.text.data() + 1, lex_state_.pat_length - 2);
                return tt_id;
            } break;
            case lex_detail::pat_regex_br: {
                sc_stack_.push_back(lex_detail::sc_regex_br);
                return '{';
            } break;
            case lex_detail::pat_regex_br_close: {
                sc_stack_.pop_back();
                return '}';
            } break;

            // ------ identifier
            case lex_detail::pat_id: {
                tkn_.val = std::string(lex_state_.text.data(), lex_state_.pat_length);
                return tt_id;
            } break;

            // ------ integer number
            case lex_detail::pat_num: {
                unsigned num = 0;
                for (unsigned i = 0; i < lex_state_.pat_length; ++i) { num = 10 * num + dig(lex_state_.text[i]); }
                tkn_.val = num;
                return tt_num;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                auto* p = lex_state_.unread_text;
                while (*p != '\n' && *p != '\0') { ++p; }
                lex_state_.unread_text = p;
            } break;

            // ------ other
            case lex_detail::pat_sc_list_begin: return tt_sc_list_begin;
            case lex_detail::pat_regex_nl: return tt_nl;
            case lex_detail::pat_start: return tt_start;
            case lex_detail::pat_option: return tt_option;
            case lex_detail::pat_sep: return tt_sep;
            case lex_detail::pat_other: return static_cast<unsigned char>(lex_state_.text[0]);
            case lex_detail::pat_eof: return tt_eof;
            case lex_detail::pat_whitespace: tkn_.n_col = n_col_; break;
            case lex_detail::pat_nl: break;
            case lex_detail::pat_unterminated_token: return tt_unterminated_token;
            default: return -1;
        }

        if (escape) {  // Process escape character
            switch (sc_stack_.back()) {
                case lex_detail::sc_string: std::get<std::string>(tkn_.val).push_back(*escape); break;
                case lex_detail::sc_sset: {
                    if (sset_range_flag) {
                        std::get<ValueSet>(tkn_.val).addValues(sset_last, static_cast<unsigned char>(*escape));
                        sset_range_flag = false;
                    } else {
                        std::get<ValueSet>(tkn_.val).addValue(static_cast<unsigned char>(*escape));
                    }
                    sset_last = static_cast<unsigned char>(*escape);
                } break;
                case lex_detail::sc_regex:
                case lex_detail::sc_sc_list: {
                    tkn_.val = static_cast<unsigned char>(*escape);
                    return tt_symb;
                } break;
            }
        }
    }
    return tt_eof;
}

int Parser::logSyntaxError(int tt) const {
    switch (tt) {
        case tt_eof: return logError() << "unexpected end of file";
        case tt_nl: return logError() << "unexpected new line here";
        case tt_unterminated_token: return logError() << "unterminated token here";
    }
    return logError() << "unexpected token here";
}

void Parser::printError(const std::string& msg) const {
    std::cerr << file_name_ << ":" << n_line_ << ":" << tkn_.n_col << ": error: " << msg << std::endl;

    const auto* line_end = current_line_;
    while (*line_end != '\n' && *line_end != '\0') { ++line_end; }
    if (line_end == current_line_) { return; }

    uint32_t code = 0;
    const unsigned tab_size = 4;
    unsigned col = 0, current_col = 0;
    std::string tab2space_line, n_line = std::to_string(n_line_);
    tab2space_line.reserve(line_end - current_line_);
    for (auto p = current_line_, p1 = p; (p1 = detail::from_utf8(p, line_end, &code)) > p; p = p1) {
        if (code == '\t') {
            auto align_up = [](unsigned v, unsigned base) { return (v + base - 1) & ~(base - 1); };
            unsigned tab_pos = align_up(col + 1, tab_size);
            while (col < tab_pos) { tab2space_line.push_back(' '), ++col; }
        } else {
            while (p < p1) { tab2space_line.push_back(*p++); }
            ++col;
        }
        if (p - current_line_ < tkn_.n_col) { current_col = col; }
    }

    std::cerr << " " << n_line << " | " << tab2space_line << std::endl;
    std::cerr << std::string(n_line.size() + 1, ' ') << " | " << std::string(current_col, ' ') << "^" << std::endl;
}
