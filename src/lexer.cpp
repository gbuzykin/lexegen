#include "lexer.h"

namespace lex_detail {
#include "lex_analyzer.inl"
}  // namespace lex_detail

///////////////////////////////////////////////////////////////////////////////
// Lexer construction

Lexer::Lexer(std::istream& input) : lex_data_(input), sc_stack_{lex_detail::sc_initial} {
    lex_data_.get_more = getMoreChars;
}

int Lexer::lex() {
    while (1) {
        int pat_id = lex_detail::lex(lex_data_, sc_stack_.back());
        switch (pat_id) {
            case lex_detail::pat_int: {
                val_ = str2int(lex_data_.text.data(), lex_data_.pat_length);
                return tt_int;  // Return integer
            }
            case lex_detail::pat_string_begin: {
                val_ = std::string();
                sc_stack_.push_back(lex_detail::sc_string);
                break;
            }
            case lex_detail::pat_string_cont: {
                std::get<std::string>(val_).append(lex_data_.text.data(), lex_data_.pat_length);
                break;
            }
            case lex_detail::pat_string_es_a: std::get<std::string>(val_).push_back('\a'); break;
            case lex_detail::pat_string_es_b: std::get<std::string>(val_).push_back('\b'); break;
            case lex_detail::pat_string_es_f: std::get<std::string>(val_).push_back('\f'); break;
            case lex_detail::pat_string_es_n: std::get<std::string>(val_).push_back('\n'); break;
            case lex_detail::pat_string_es_r: std::get<std::string>(val_).push_back('\r'); break;
            case lex_detail::pat_string_es_t: std::get<std::string>(val_).push_back('\t'); break;
            case lex_detail::pat_string_es_v: std::get<std::string>(val_).push_back('\v'); break;
            case lex_detail::pat_string_es_bslash: std::get<std::string>(val_).push_back('\\'); break;
            case lex_detail::pat_string_es_dquot: std::get<std::string>(val_).push_back('\"'); break;
            case lex_detail::pat_string_es_hex:
                std::get<std::string>(val_).push_back((hdig(lex_data_.text[2]) << 4) | hdig(lex_data_.text[3]));
                break;
            case lex_detail::pat_string_es_oct:
                std::get<std::string>(val_).push_back((dig(lex_data_.text[1]) << 6) | (dig(lex_data_.text[2]) << 3) |
                                                      dig(lex_data_.text[3]));
                break;
            case lex_detail::pat_string_nl: return -1;   // Error
            case lex_detail::pat_string_eof: return -1;  // Error
            case lex_detail::pat_string_end: {
                sc_stack_.pop_back();
                return tt_string;  // Return string
            }
            case lex_detail::pat_id: {
                val_ = std::string(lex_data_.text.data(), lex_data_.pat_length);
                return tt_id;  // Return identifier
            }
            case lex_detail::pat_start: return tt_start;                  // Return "%start" keyword
            case lex_detail::pat_option: return tt_option;                // Return "%option" keyword
            case lex_detail::pat_sep: return tt_sep;                      // Return separator "%%"
            case lex_detail::pat_sc_list_begin: return tt_sc_list_begin;  // Return start condition list begin
            case lex_detail::pat_reg_expr_begin: {
                std::string reg_expr;
                do {
                    if (lex_data_.unread_pos == static_cast<unsigned>(lex_data_.text.size())) {
                        getMoreChars(lex_data_);
                    }
                    reg_expr.push_back(lex_data_.text[lex_data_.unread_pos++]);
                } while (reg_expr.back() != '\0' && reg_expr.back() != '\n');
                val_ = std::move(reg_expr);
                ++line_no_;
                return tt_reg_expr;  // Return regular expression
            }
            case lex_detail::pat_eof_expr: return tt_eof_expr;  // Return "<<EOF>>" expression
            case lex_detail::pat_comment: {
                char symb = '\0';
                do {
                    if (lex_data_.unread_pos == static_cast<unsigned>(lex_data_.text.size())) {
                        getMoreChars(lex_data_);
                    }
                    symb = lex_data_.text[lex_data_.unread_pos++];  // Eat up comment
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

void Lexer::enterRegExprMode() { sc_stack_.push_back(lex_detail::sc_reg_expr); }
void Lexer::enterScListMode() { sc_stack_.push_back(lex_detail::sc_sc_list); }

/*static*/ void Lexer::getMoreChars(lex_detail::StateData& data) {
    const unsigned kChunkSize = 32;
    data.unread_pos = data.pat_length;
    data.text.resize(data.unread_pos + kChunkSize);
    static_cast<LexData&>(data).input.read(data.text.data() + data.unread_pos, kChunkSize);
    size_t count = static_cast<LexData&>(data).input.gcount();
    if (count < kChunkSize) {
        data.text.resize(data.unread_pos + count);
        data.text.push_back('\0');  // EOF
    }
}

int Lexer::str2int(const char* str, size_t length) {
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
