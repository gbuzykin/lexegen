#pragma once

#include "valset.h"

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum {
    tt_symb = 256,
    tt_sset,
    tt_id,
    tt_string,
    tt_int,
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

class Node;

// Input file parser class
class Parser {
 public:
    struct Pattern {
        std::string id;
        ValueSet sc;
        std::unique_ptr<Node> syn_tree;
    };

    explicit Parser(std::istream& input);
    int parse();
    const std::vector<Pattern>& getPatterns() const { return patterns_; }
    const std::vector<std::string>& getStartConditions() const { return start_conditions_; }
    std::unique_ptr<Node> extractPatternTree(size_t n) { return std::move(patterns_[n].syn_tree); }

 private:
    struct LexerData : public lex_detail::StateData {
        explicit LexerData(std::istream& in_input) : input(in_input) {}
        std::istream& input;
    } lex_data_;

    struct TokenValue {
        int i = -1;
        ValueSet sset;
        std::string str;
    };

    struct RegexParserState {
        int pos = 0;
        bool mult_op = false;  // Parsing multiplication operator flag
    };

    int line_no_ = 0;
    std::vector<int> sc_stack_;
    std::unordered_map<std::string, std::string> options_;
    std::unordered_map<std::string, std::unique_ptr<Node>> definitions_;
    std::vector<std::string> start_conditions_;
    std::vector<Pattern> patterns_;

    std::unique_ptr<Node> parseRegex(const std::string& reg_expr, RegexParserState& state);

    static bool isodigit(char ch) { return ch >= '0' && ch <= '7'; }
    static int dig(char ch) { return static_cast<int>(ch - '0'); }
    static int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) { return static_cast<int>(ch - 'a') + 10; }
        if ((ch >= 'A') && (ch <= 'F')) { return static_cast<int>(ch - 'A') + 10; }
        return static_cast<int>(ch - '0');
    }

    static void getMoreChars(lex_detail::StateData& data);
    static int str2int(const char* str, size_t length);
    static char readEscape(const std::string& reg_expr, RegexParserState& state);
    static int lexRegex(const std::string& reg_expr, RegexParserState& state, TokenValue& val);
    int lex(TokenValue& val);
};
