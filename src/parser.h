#pragma once

#include "valset.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace detail {
template<typename InputIt>
InputIt from_utf8(InputIt in, InputIt in_end, uint32_t* pcode) {
    if (in >= in_end) { return in; }
    uint32_t code = static_cast<uint8_t>(*in);
    if ((code & 0xC0) == 0xC0) {
        static const uint32_t mask_tbl[] = {0xFF, 0x1F, 0xF, 0x7};
        static const uint32_t count_tbl[] = {1, 1, 1, 1, 2, 2, 3, 0};
        uint32_t count = count_tbl[(code >> 3) & 7];  // continuation byte count
        if (in_end - in <= count) { return in; }
        code &= mask_tbl[count];
        while (count > 0) {
            code = (code << 6) | ((*++in) & 0x3F);
            --count;
        }
    }
    *pcode = code;
    return ++in;
}
}  // namespace detail

namespace lex_detail {
#include "lex_defs.h"
}

namespace parser_detail {
#include "parser_defs.h"
}

class Node;

// Input file parser class
class Parser {
 public:
    struct Pattern {
        std::string_view id;
        ValueSet sc;
        std::unique_ptr<Node> syn_tree;
    };

    Parser(std::istream& input, std::string file_name);
    int parse();
    const std::vector<Pattern>& getPatterns() const { return patterns_; }
    const std::vector<std::string>& getStartConditions() const { return start_conditions_; }
    std::unique_ptr<Node> extractPatternTree(size_t n) { return std::move(patterns_[n].syn_tree); }

 private:
    struct TokenInfo {
        unsigned n_col = 0;
        std::variant<unsigned, std::string_view, ValueSet> val;
    };

    struct ErrorLogger {
        const Parser* parser;
        std::stringstream ss;
        explicit ErrorLogger(const Parser* in_parser) : parser(in_parser) {}
        ErrorLogger(ErrorLogger&& el) noexcept : parser(el.parser) { el.parser = nullptr; }
        ~ErrorLogger() {
            if (parser) { parser->printError(ss.str()); }
        }
        ErrorLogger(const ErrorLogger&) = delete;
        ErrorLogger& operator=(const ErrorLogger&) = delete;
        ErrorLogger& operator=(ErrorLogger&&) = delete;
        template<typename Ty>
        ErrorLogger& operator<<(const Ty& v) {
            ss << v;
            return *this;
        }
        operator int() const { return -1; }
    };

    std::istream& input_;
    std::string file_name_;
    std::unique_ptr<char[]> text_;
    std::string current_line_;
    unsigned n_line_ = 1, n_col_ = 1;
    std::vector<int> sc_stack_;
    lex_detail::CtxData lex_ctx_;
    std::vector<int> lex_state_stack_;
    TokenInfo tkn_;
    std::unordered_map<std::string_view, std::string_view> options_;
    std::unordered_map<std::string_view, std::unique_ptr<Node>> definitions_;
    std::vector<std::string> start_conditions_;
    std::vector<Pattern> patterns_;

    std::pair<std::unique_ptr<Node>, int> parseRegex(int tt);

    static int dig(char ch) { return static_cast<int>(ch - '0'); }
    static int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) { return static_cast<int>(ch - 'a') + 10; }
        if ((ch >= 'A') && (ch <= 'F')) { return static_cast<int>(ch - 'A') + 10; }
        return static_cast<int>(ch - '0');
    }

    int lex();
    int logSyntaxError(int tt) const;
    ErrorLogger logError() const { return ErrorLogger(this); }
    void printError(const std::string& msg) const;
};
