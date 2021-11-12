#pragma once

#include <iostream>
#include <variant>
#include <vector>

enum {
    tt_int = 256,
    tt_string,
    tt_id,
    tt_start,
    tt_option,
    tt_sep,
    tt_sc_list_begin,
    tt_eof_expr,
    tt_reg_expr,
};

namespace lex_detail {
#include "lex_defs.h"
}

// Lexer class
class Lexer {
 public:
    explicit Lexer(std::istream& input);
    int lex();
    void enterRegExprMode();
    void enterScListMode();
    void popMode() { sc_stack_.pop_back(); }
    int getLineNo() const { return line_no_; }
    template<typename Ty>
    const Ty& getVal() const {
        return std::get<Ty>(val_);
    }

 private:
    struct LexData : public lex_detail::StateData {
        explicit LexData(std::istream& in_input) : input(in_input) {}
        std::istream& input;
    } lex_data_;
    int line_no_ = 0;
    std::variant<int, std::string> val_;
    std::vector<int> sc_stack_;

    static void getMoreChars(lex_detail::StateData& data);
    static int str2int(const char* str, size_t length);
    static int dig(char ch) { return static_cast<int>(ch - '0'); }
    static int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) { return 10 + static_cast<int>(ch - 'a'); }
        if ((ch >= 'A') && (ch <= 'F')) { return 10 + static_cast<int>(ch - 'A'); }
        return dig(ch);
    }
};
