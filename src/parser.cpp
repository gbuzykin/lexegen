#include "parser.h"

#include "node.h"

#include <algorithm>
#include <optional>

namespace lex_detail {
#include "lex_analyzer.inl"
}

namespace parser_detail {
#include "parser_analyzer.inl"
}

namespace {
const char* findEol(const char* text, const char* boundary) {
    return std::find_if(text, boundary, [](char ch) { return ch == '\n' || ch == '\0'; });
}
std::string_view getNextLine(const char* text, const char* boundary) {
    return std::string_view(text, findEol(text, boundary) - text);
}
}  // namespace

Parser::Parser(std::istream& input, std::string file_name) : input_(input), file_name_(std::move(file_name)) {}

bool Parser::parse() {
    size_t file_sz = static_cast<size_t>(input_.seekg(0, std::ios_base::end).tellg());
    text_ = std::make_unique<char[]>(file_sz);

    // Read the whole file
    input_.seekg(0);
    input_.read(text_.get(), file_sz);
    lex_ctx_.first = lex_ctx_.next = text_top_ = text_.get();
    lex_ctx_.last = text_.get() + input_.gcount();
    current_line_ = getNextLine(lex_ctx_.next, lex_ctx_.last);

    lex_state_stack_.reserve(256);

    int tt = 0;
    lex_state_stack_.push_back(lex_detail::sc_initial);

    // Load definitions
    start_conditions_.emplace_back("initial");  // Add initial start condition
    do {
        switch (tt = lex()) {
            case parser_detail::tt_start: {  // Start condition definition
                if ((tt = lex()) != parser_detail::tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                if (std::find(start_conditions_.begin(), start_conditions_.end(),
                              std::get<std::string_view>(tkn_.val)) != start_conditions_.end()) {
                    logger::error(*this, tkn_.loc) << "start condition is already defined";
                    return false;
                }
                start_conditions_.emplace_back(std::get<std::string_view>(tkn_.val));
            } break;
            case parser_detail::tt_id: {  // Regular definition
                std::string_view name = std::get<std::string_view>(tkn_.val);
                if (definitions_.find(name) != definitions_.end()) {
                    logger::error(*this, tkn_.loc) << "regular expression is already defined";
                    return false;
                }

                lex_state_stack_.push_back(lex_detail::sc_regex);
                std::unique_ptr<Node> syn_tree;
                std::tie(syn_tree, tt) = parseRegex(lex());
                lex_state_stack_.pop_back();

                if (!syn_tree) { return false; }
                definitions_.emplace(name, std::move(syn_tree));
            } break;
            case parser_detail::tt_option: {  // Option
                if ((tt = lex()) != parser_detail::tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                std::string_view name = std::get<std::string_view>(tkn_.val);
                if ((tt = lex()) != parser_detail::tt_string) {
                    logSyntaxError(tt);
                    return false;
                }
                options_.emplace(name, std::get<std::string_view>(tkn_.val));
            } break;
            case parser_detail::tt_sep: break;
            default: logSyntaxError(tt); return false;
        }
    } while (tt != parser_detail::tt_sep);

    // Load patterns
    do {
        if ((tt = lex()) == parser_detail::tt_id) {
            std::string_view name = std::get<std::string_view>(tkn_.val);
            if (std::find_if(patterns_.begin(), patterns_.end(), [&](const auto& pat) { return pat.id == name; }) !=
                patterns_.end()) {
                logger::error(*this, tkn_.loc) << "pattern is already defined";
                return false;
            }

            ValueSet sc;
            lex_state_stack_.push_back(lex_detail::sc_regex);
            lex_state_stack_.push_back(lex_detail::sc_sc_list);
            if ((tt = lex()) == parser_detail::tt_sc_list_begin) {
                lex_state_stack_.push_back(lex_detail::sc_initial);
                // Parse start conditions
                while (true) {
                    if ((tt = lex()) == parser_detail::tt_id) {
                        auto sc_it = std::find(start_conditions_.begin(), start_conditions_.end(),
                                               std::get<std::string_view>(tkn_.val));
                        if (sc_it == start_conditions_.end()) {
                            logger::error(*this, tkn_.loc) << "undefined start condition";
                            return false;
                        }
                        sc.addValue(static_cast<unsigned>(sc_it - start_conditions_.begin()));
                    } else if (tt == '>') {
                        break;
                    } else {
                        logSyntaxError(tt);
                        return false;
                    }
                }
                lex_state_stack_.pop_back();
                lex_state_stack_.pop_back();
                tt = lex();
            } else {
                // Add all start conditions
                sc.addValues(0, static_cast<unsigned>(start_conditions_.size()) - 1);
                lex_state_stack_.pop_back();
            }

            std::unique_ptr<Node> syn_tree;
            std::tie(syn_tree, tt) = parseRegex(tt);
            lex_state_stack_.pop_back();

            if (!syn_tree) { return false; }
            patterns_.emplace_back(name, sc, std::move(syn_tree));
        } else if (tt != parser_detail::tt_sep) {
            logSyntaxError(tt);
            return false;
        }
    } while (tt != parser_detail::tt_sep);

    if (patterns_.empty()) {
        logger::error(file_name_) << "no patterns defined";
        return false;
    }
    return true;
}

std::pair<std::unique_ptr<Node>, int> Parser::parseRegex(int tt) {
    unsigned num[2] = {0, 0}, num_given = 0;
    parser_detail::CtxData parser_ctx;
    std::vector<std::unique_ptr<Node>> node_stack;
    std::vector<int> parser_state_stack;

    node_stack.reserve(256);
    parser_state_stack.reserve(256);

    parser_state_stack.push_back(parser_detail::sc_initial);  // Push initial state
    while (true) {
        auto [reduce, code] = parser_detail::parse(parser_ctx, parser_state_stack, tt);
        if (reduce) {
            if (code < 0) {
                logSyntaxError(tt);
                return {nullptr, tt};
            }
            switch (code) {
                case parser_detail::act_trail_cont: {  // Trailing context
                    auto trail_cont_node = std::make_unique<TrailContNode>();
                    trail_cont_node->setRight(std::move(node_stack.back()));
                    node_stack.pop_back();
                    trail_cont_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(trail_cont_node);
                } break;
                case parser_detail::act_or: {  // Or
                    auto or_node = std::make_unique<Node>(NodeType::kOr);
                    or_node->setRight(std::move(node_stack.back()));
                    node_stack.pop_back();
                    or_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(or_node);
                } break;
                case parser_detail::act_cat: {  // Cat
                    auto cat_node = std::make_unique<Node>(NodeType::kCat);
                    cat_node->setRight(std::move(node_stack.back()));
                    node_stack.pop_back();
                    cat_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(cat_node);
                } break;
                case parser_detail::act_star: {  // Star
                    auto star_node = std::make_unique<Node>(NodeType::kStar);
                    star_node->setLeft(std::move(node_stack.back()));
                    assert(star_node->getLeft());
                    node_stack.back() = std::move(star_node);
                } break;
                case parser_detail::act_plus: {  // Plus
                    auto plus_node = std::make_unique<Node>(NodeType::kPlus);
                    plus_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(plus_node);
                } break;
                case parser_detail::act_question: {  // Question
                    auto question_node = std::make_unique<Node>(NodeType::kQuestion);
                    question_node->setLeft(std::move(node_stack.back()));
                    node_stack.back() = std::move(question_node);
                } break;
                case parser_detail::act_zero_num: {  // Zero number
                    assert(num_given < 2);
                    num[num_given++] = 0;
                } break;

                case parser_detail::act_same_num: {  // Duplicate number
                    assert(num_given < 2);
                    num[num_given++] = num[0];
                }; break;
                case parser_detail::act_mult_finite:
                case parser_detail::act_mult_infinite: {  // Multiplicate node
                    assert(num_given > 0);
                    const auto* child = node_stack.back().get();
                    // Mandatory part
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
                    if (code == parser_detail::act_mult_infinite) {  // Infinite multiplication
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
                    num_given = 0;
                } break;
            }
        } else if (tt != parser_detail::tt_nl) {
            switch (tt) {
                case parser_detail::tt_symb: {  // Create `symbol` subtree
                    node_stack.emplace_back(std::make_unique<SymbNode>(std::get<unsigned>(tkn_.val)));
                } break;
                case parser_detail::tt_sset: {  // Create `symbol set` subtree
                    node_stack.emplace_back(std::make_unique<SymbSetNode>(std::get<ValueSet>(tkn_.val)));
                } break;
                case parser_detail::tt_id: {  // Insert subtree
                    auto pat_it = definitions_.find(std::get<std::string_view>(tkn_.val));
                    if (pat_it == definitions_.end()) {
                        logger::error(*this, tkn_.loc) << "undefined regular expression";
                        return {nullptr, tt};
                    }
                    node_stack.emplace_back(pat_it->second->cloneTree());
                } break;
                case parser_detail::tt_string: {  // Create `string` subtree
                    const auto& str = std::get<std::string_view>(tkn_.val);
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
                case parser_detail::tt_num: {  // Save number
                    assert(num_given < 2);
                    num[num_given++] = std::get<unsigned>(tkn_.val);
                } break;
            }
            tt = lex();
        } else {
            break;
        }
    }
    assert(node_stack.size() == 1);
    return {std::move(node_stack.back()), tt};
}

int Parser::lex() {
    bool sset_is_inverted = false, sset_range_flag = false;
    unsigned sset_last = 0;
    const char* str_start = nullptr;
    tkn_.loc = {n_line_, n_col_, n_col_};

    while (true) {
        if (lex_ctx_.next > text_.get() && *(lex_ctx_.next - 1) == '\n') {
            current_line_ = getNextLine(lex_ctx_.next, lex_ctx_.last);
            ++n_line_, n_col_ = 1;
            tkn_.loc = {n_line_, n_col_, n_col_};
        }
        lex_ctx_.first = lex_ctx_.next;
        int pat = lex_detail::lex(lex_ctx_, lex_state_stack_);
        if (pat == lex_detail::err_end_of_input) {
            int sc = lex_state_stack_.back();
            tkn_.loc.col_last = tkn_.loc.col_first;
            if (sc == lex_detail::sc_string || sc == lex_detail::sc_sset) { return parser_detail::tt_unterm_token; }
            return parser_detail::tt_eof;
        }
        unsigned lexeme_len = static_cast<unsigned>(lex_ctx_.next - lex_ctx_.first);
        n_col_ += lexeme_len;
        tkn_.loc.col_last = n_col_ - 1;

        auto store_id_text = [&text = text_top_](const char* first, unsigned len) {
            text = std::copy(first, first + len, text);
            return std::string_view(text - len, len);
        };

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
            case lex_detail::pat_escape_other: escape = lex_ctx_.first[1]; break;
            case lex_detail::pat_escape_hex: {
                escape = hdig(lex_ctx_.first[2]);
                if (lexeme_len > 3) { *escape = (*escape << 4) + hdig(lex_ctx_.first[3]); }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = dig(lex_ctx_.first[1]);
                if (lexeme_len > 2) { *escape = (*escape << 3) + dig(lex_ctx_.first[2]); }
                if (lexeme_len > 3) { *escape = (*escape << 3) + dig(lex_ctx_.first[3]); }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                str_start = text_top_;
                lex_state_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: {
                text_top_ = std::copy(lex_ctx_.first, lex_ctx_.next, text_top_);
            } break;
            case lex_detail::pat_string_close: {
                tkn_.val = std::string_view(str_start, text_top_ - str_start);
                lex_state_stack_.pop_back();
                return parser_detail::tt_string;
            } break;

            // ------ regex symbol sets
            case lex_detail::pat_regex_sset:
            case lex_detail::pat_regex_sset_inv: {
                sset_is_inverted = pat == lex_detail::pat_regex_sset_inv;
                sset_range_flag = false;
                sset_last = 0;
                tkn_.val = ValueSet();
                lex_state_stack_.push_back(lex_detail::sc_sset);
            } break;
            case lex_detail::pat_regex_sset_seq: {
                if (sset_range_flag) {
                    std::get<ValueSet>(tkn_.val).addValues(sset_last, static_cast<unsigned char>(*lex_ctx_.first));
                    sset_range_flag = false;
                }
                sset_last = static_cast<unsigned char>(*(lex_ctx_.next - 1));
                for (const char* l = lex_ctx_.first; l < lex_ctx_.next; ++l) {
                    std::get<ValueSet>(tkn_.val).addValue(static_cast<unsigned char>(*l));
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
                lex_state_stack_.pop_back();
                return parser_detail::tt_sset;
            } break;
            case lex_detail::pat_regex_dot: {
                tkn_.val = ValueSet(1, 255);
                std::get<ValueSet>(tkn_.val).removeValue('\n');
                return parser_detail::tt_sset;
            } break;
            case lex_detail::pat_regex_symb: {
                tkn_.val = static_cast<unsigned char>(*lex_ctx_.first);
                return parser_detail::tt_symb;
            } break;
            case lex_detail::pat_regex_id: {  // {id}
                tkn_.val = store_id_text(lex_ctx_.first + 1, lexeme_len - 2);
                return parser_detail::tt_id;
            } break;
            case lex_detail::pat_regex_br: {
                lex_state_stack_.push_back(lex_detail::sc_regex_br);
                return '{';
            } break;
            case lex_detail::pat_regex_br_close: {
                lex_state_stack_.pop_back();
                return '}';
            } break;

            // ------ identifier
            case lex_detail::pat_id: {
                tkn_.val = store_id_text(lex_ctx_.first, lexeme_len);
                return parser_detail::tt_id;
            } break;

            // ------ integer number
            case lex_detail::pat_num: {
                unsigned num = 0;
                for (const char* l = lex_ctx_.first; l < lex_ctx_.next; ++l) { num = 10 * num + dig(*l); }
                tkn_.val = num;
                return parser_detail::tt_num;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                lex_ctx_.next = findEol(lex_ctx_.next, lex_ctx_.last);
            } break;

            // ------ other
            case lex_detail::pat_sc_list_begin: return parser_detail::tt_sc_list_begin;
            case lex_detail::pat_regex_nl: return parser_detail::tt_nl;
            case lex_detail::pat_start: return parser_detail::tt_start;
            case lex_detail::pat_option: return parser_detail::tt_option;
            case lex_detail::pat_sep: return parser_detail::tt_sep;
            case lex_detail::pat_other: return static_cast<unsigned char>(*lex_ctx_.first);
            case lex_detail::pat_whitespace: tkn_.loc.col_first = n_col_; break;
            case lex_detail::pat_unterm_token: return parser_detail::tt_unterm_token;
            case lex_detail::pat_nl: break;
            default: return -1;
        }

        if (escape) {  // Process escape character
            switch (lex_state_stack_.back()) {
                case lex_detail::sc_string: *text_top_++ = *escape; break;
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
                    return parser_detail::tt_symb;
                } break;
            }
        }
    }
    return parser_detail::tt_eof;
}

void Parser::logSyntaxError(int tt) const {
    std::string_view msg;
    switch (tt) {
        case parser_detail::tt_eof: msg = "unexpected end of file"; break;
        case parser_detail::tt_nl: msg = "unexpected end of line"; break;
        case parser_detail::tt_unterm_token: msg = "unterminated token"; break;
        default: msg = "unexpected token"; break;
    }
    logger::error(*this, tkn_.loc) << msg;
}
