#pragma once

#include "logger.h"
#include "valset.h"

#include <iostream>
#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

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
    bool parse();
    const std::string& getFileName() const { return file_name_; }
    const std::string& getCurrentLine() const { return current_line_; }
    const std::vector<Pattern>& getPatterns() const { return patterns_; }
    const std::vector<std::string>& getStartConditions() const { return start_conditions_; }
    std::unique_ptr<Node> extractPatternTree(size_t n) { return std::move(patterns_[n].syn_tree); }

 private:
    struct TokenInfo {
        TokenLoc loc;
        std::variant<unsigned, std::string_view, ValueSet> val;
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
    void logSyntaxError(int tt) const;
};
