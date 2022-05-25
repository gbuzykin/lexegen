#pragma once

#include "logger.h"
#include "valset.h"

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
        Pattern(std::string_view in_id, const ValueSet& in_sc, std::unique_ptr<Node> in_syn_tree)
            : id(in_id), sc(in_sc), syn_tree(std::move(in_syn_tree)) {}
        std::string_view id;
        ValueSet sc;
        std::unique_ptr<Node> syn_tree;
    };

    Parser(util::iobuf& input, std::string file_name);
    bool parse();
    const std::string& getFileName() const { return file_name_; }
    const std::string& getCurrentLine() const { return current_line_; }
    const std::vector<Pattern>& getPatterns() const { return patterns_; }
    const std::vector<std::string_view>& getStartConditions() const { return start_conditions_; }
    std::unique_ptr<Node> extractPatternTree(size_t n) { return std::move(patterns_[n].syn_tree); }

 private:
    using TokenVal = std::variant<unsigned, std::string_view, ValueSet>;

    struct TokenInfo {
        TokenVal val;
        TokenLoc loc;
    };

    util::iobuf& input_;
    std::string file_name_;
    std::unique_ptr<char[]> text_;
    std::string current_line_;
    char* first_ = nullptr;
    char* last_ = nullptr;
    unsigned ln_ = 1, col_ = 1;
    std::vector<int> state_stack_;
    TokenInfo tkn_;
    std::unordered_map<std::string_view, std::string_view> options_;
    std::unordered_map<std::string_view, std::unique_ptr<Node>> definitions_;
    std::vector<std::string_view> start_conditions_;
    std::vector<Pattern> patterns_;

    std::pair<std::unique_ptr<Node>, int> parseRegex(int tt);

    int lex();
    void logSyntaxError(int tt) const;
};
