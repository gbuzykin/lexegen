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

Parser::Parser(std::istream& input, std::string file_name)
    : input_(input), file_name_(std::move(file_name)), sc_stack_({lex_detail::sc_initial}) {}

int Parser::parse() {
    size_t file_sz = static_cast<size_t>(input_.seekg(0, std::ios_base::end).tellg());
    text_ = std::make_unique<char[]>(file_sz + 1);

    // Read the whole file
    input_.seekg(0);
    input_.read(text_.get(), file_sz);
    text_[input_.gcount()] = '\0';
    lex_ctx_.text_last = lex_ctx_.text_unread = text_.get();
    lex_ctx_.text_boundary = text_.get() + input_.gcount() + 1;
    current_line_.assign(lex_ctx_.text_unread, std::find_if(lex_ctx_.text_unread, lex_ctx_.text_boundary,
                                                            [](char ch) { return ch == '\n' || ch == '\0'; }));

    int tt = 0;

    // Load definitions
    start_conditions_.emplace_back("initial");  // Add initial start condition
    do {
        switch (tt = lex()) {
            case parser_detail::tt_start: {  // Start condition definition
                if ((tt = lex()) != parser_detail::tt_id) { return logSyntaxError(tt); }
                if (std::find(start_conditions_.begin(), start_conditions_.end(),
                              std::get<std::string_view>(tkn_.val)) != start_conditions_.end()) {
                    return logError() << "start condition is already defined";
                }
                start_conditions_.emplace_back(std::get<std::string_view>(tkn_.val));
            } break;
            case parser_detail::tt_id: {  // Regular definition
                std::string_view name = std::get<std::string_view>(tkn_.val);
                if (definitions_.find(name) != definitions_.end()) {
                    return logError() << "regular expression is already defined";
                }

                sc_stack_.push_back(lex_detail::sc_regex);
                std::unique_ptr<Node> syn_tree;
                std::tie(syn_tree, tt) = parseRegex(lex());
                sc_stack_.pop_back();

                if (tt < 0) { return tt; }
                definitions_.emplace(name, std::move(syn_tree));
            } break;
            case parser_detail::tt_option: {  // Option
                if ((tt = lex()) != parser_detail::tt_id) { return logSyntaxError(tt); }
                std::string_view name = std::get<std::string_view>(tkn_.val);
                if ((tt = lex()) != parser_detail::tt_string) { return logSyntaxError(tt); }
                options_.emplace(name, std::get<std::string_view>(tkn_.val));
            } break;
            case parser_detail::tt_sep: break;
            default: return logSyntaxError(tt);
        }
    } while (tt != parser_detail::tt_sep);

    // Load patterns
    do {
        if ((tt = lex()) == parser_detail::tt_id) {
            std::string_view name = std::get<std::string_view>(tkn_.val);
            if (std::find_if(patterns_.begin(), patterns_.end(), [&](const auto& pat) { return pat.id == name; }) !=
                patterns_.end()) {
                return logError() << "pattern is already defined";
            }

            ValueSet sc;
            sc_stack_.push_back(lex_detail::sc_regex);
            sc_stack_.push_back(lex_detail::sc_sc_list);
            if ((tt = lex()) == parser_detail::tt_sc_list_begin) {
                sc_stack_.push_back(lex_detail::sc_initial);
                // Parse start conditions
                while (true) {
                    if ((tt = lex()) == parser_detail::tt_id) {
                        auto sc_it = std::find(start_conditions_.begin(), start_conditions_.end(),
                                               std::get<std::string_view>(tkn_.val));
                        if (sc_it == start_conditions_.end()) { return logError() << "undefined start condition"; }
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
            patterns_.emplace_back(Pattern{name, sc, std::move(syn_tree)});
        } else if (tt != parser_detail::tt_sep) {
            return logSyntaxError(tt);
        }
    } while (tt != parser_detail::tt_sep);

    if (patterns_.empty()) {
        Log(Log::MsgType::kError, this) << "no patterns defined";
        return -1;
    }
    return 0;
}

std::pair<std::unique_ptr<Node>, int> Parser::parseRegex(int tt) {
    unsigned num[2] = {0, 0}, num_given = 0;
    parser_detail::CtxData parser_ctx;
    std::vector<std::unique_ptr<Node>> node_stack;
    std::vector<int> parser_state_stack;

    parser_state_stack.push_back(parser_detail::sc_initial);  // Push initial state
    while (true) {
        auto [reduce, code] = parser_detail::parse(parser_ctx, parser_state_stack, tt);
        if (reduce) {
            if (code < 0) { return {nullptr, logSyntaxError(tt)}; }
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
                        return {nullptr, logError() << "undefined regular expression"};
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
    tkn_.loc = loc_;

    while (true) {
        if (lex_ctx_.text_last > text_.get() && *(lex_ctx_.text_last - 1) == '\n') {
            current_line_.assign(lex_ctx_.text_unread, std::find_if(lex_ctx_.text_unread, lex_ctx_.text_boundary,
                                                                    [](char ch) { return ch == '\n' || ch == '\0'; }));
            ++loc_.n_line, loc_.n_col = 1;
            tkn_.loc = loc_;
        }
        char* lexeme = lex_ctx_.text_last;
        int pat = lex_detail::lex(lex_ctx_, lex_state_stack_, sc_stack_.back());
        unsigned lexeme_len = static_cast<unsigned>(lex_ctx_.text_last - lexeme);
        loc_.n_col += lexeme_len;

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
            case lex_detail::pat_escape_other: escape = lexeme[1]; break;
            case lex_detail::pat_escape_hex: {
                escape = hdig(lexeme[2]);
                if (lexeme_len > 3) { *escape = (*escape << 4) + hdig(lexeme[3]); }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = dig(lexeme[1]);
                if (lexeme_len > 2) { *escape = (*escape << 3) + dig(lexeme[2]); }
                if (lexeme_len > 3) { *escape = (*escape << 3) + dig(lexeme[3]); }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                str_start = lex_ctx_.text_last;
                sc_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: break;
            case lex_detail::pat_string_close: {
                tkn_.val = std::string_view(str_start, lexeme - str_start);
                sc_stack_.pop_back();
                return parser_detail::tt_string;
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
                    std::get<ValueSet>(tkn_.val).addValues(sset_last, static_cast<unsigned char>(*lexeme));
                    sset_range_flag = false;
                }
                sset_last = static_cast<unsigned char>(*(lex_ctx_.text_last - 1));
                for (const char* l = lexeme; l < lex_ctx_.text_last; ++l) {
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
                sc_stack_.pop_back();
                return parser_detail::tt_sset;
            } break;
            case lex_detail::pat_regex_dot: {
                tkn_.val = ValueSet(1, 255);
                std::get<ValueSet>(tkn_.val).removeValue('\n');
                return parser_detail::tt_sset;
            } break;
            case lex_detail::pat_regex_symb: {
                tkn_.val = static_cast<unsigned char>(*lexeme);
                return parser_detail::tt_symb;
            } break;
            case lex_detail::pat_regex_eof_symb: {
                tkn_.val = 0;
                return parser_detail::tt_symb;
            } break;
            case lex_detail::pat_regex_id: {  // {id}
                tkn_.val = std::string_view(lexeme + 1, lex_ctx_.text_last - lexeme - 2);
                return parser_detail::tt_id;
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
                tkn_.val = std::string_view(lexeme, lex_ctx_.text_last - lexeme);
                return parser_detail::tt_id;
            } break;

            // ------ integer number
            case lex_detail::pat_num: {
                unsigned num = 0;
                for (const char* l = lexeme; l < lex_ctx_.text_last; ++l) { num = 10 * num + dig(*l); }
                tkn_.val = num;
                return parser_detail::tt_num;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                lex_ctx_.text_unread = std::find_if(lex_ctx_.text_unread, lex_ctx_.text_boundary,
                                                    [](char ch) { return ch == '\n' || ch == '\0'; });
            } break;

            // ------ other
            case lex_detail::pat_sc_list_begin: return parser_detail::tt_sc_list_begin;
            case lex_detail::pat_regex_nl: return parser_detail::tt_nl;
            case lex_detail::pat_start: return parser_detail::tt_start;
            case lex_detail::pat_option: return parser_detail::tt_option;
            case lex_detail::pat_sep: return parser_detail::tt_sep;
            case lex_detail::pat_other: return static_cast<unsigned char>(*lexeme);
            case lex_detail::pat_eof: return parser_detail::tt_eof;
            case lex_detail::pat_whitespace: tkn_.loc.n_col = loc_.n_col; break;
            case lex_detail::pat_nl: break;
            case lex_detail::pat_unterminated_token: return parser_detail::tt_unterm_token;
            default: return -1;
        }

        if (escape) {  // Process escape character
            switch (sc_stack_.back()) {
                case lex_detail::sc_string: {
                    *lexeme = *escape;
                    lex_ctx_.text_last = lexeme + 1;
                } break;
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

int Parser::logSyntaxError(int tt) const {
    std::string_view msg;
    switch (tt) {
        case parser_detail::tt_eof: msg = "unexpected end of file"; break;
        case parser_detail::tt_nl: msg = "unexpected end of line"; break;
        case parser_detail::tt_unterm_token: msg = "unterminated token"; break;
        default: msg = "unexpected token"; break;
    }
    return logError() << msg;
}

void Log::printMessage(MsgType type, const TokenLoc& l, const std::string& msg) {
    std::string msg_hdr(parser_ ? parser_->getFileName() : "lexegen");
    std::string n_line = std::to_string(l.n_line);
    if (l.n_line > 0 && l.n_col > 0) { msg_hdr += ':' + n_line + ':' + std::to_string(l.n_col); }

    switch (type) {
        case Log::MsgType::kDebug: msg_hdr += ": debug: "; break;
        case Log::MsgType::kInfo: msg_hdr += ": info: "; break;
        case Log::MsgType::kWarning: msg_hdr += ": warning: "; break;
        case Log::MsgType::kError: msg_hdr += ": error: "; break;
        case Log::MsgType::kFatal: msg_hdr += ": fatal error: "; break;
    }

    std::cerr << msg_hdr << msg << std::endl;
    if (!parser_ || l.n_line == 0 || l.n_col == 0) { return; }

    const auto& line = parser_->getCurrentLine();
    if (line.empty()) { return; }

    uint32_t code = 0;
    const unsigned tab_size = 4;
    unsigned col = 0, pos_col = 0;
    std::string tab2space_line;
    tab2space_line.reserve(line.size());
    for (auto p = line.begin(), p1 = p; (p1 = detail::from_utf8(p, line.end(), &code)) > p; p = p1) {
        if (code == '\t') {
            auto align_up = [](unsigned v, unsigned base) { return (v + base - 1) & ~(base - 1); };
            unsigned tab_pos = align_up(col + 1, tab_size);
            while (col < tab_pos) { tab2space_line.push_back(' '), ++col; }
        } else {
            while (p < p1) { tab2space_line.push_back(*p++); }
            ++col;
        }
        if (p - line.begin() < l.n_col) { pos_col = col; }
    }

    std::cerr << " " << n_line << " | " << tab2space_line << std::endl;
    std::cerr << std::string(n_line.size() + 1, ' ') << " | " << std::string(pos_col, ' ') << "^" << std::endl;
}
