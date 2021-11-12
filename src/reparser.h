#pragma once

#include "valset.h"

#include <map>
#include <memory>
#include <string>

// Token types
const int ttSymb = 256;
const int ttSSet = 257;
const int ttID = 258;
const int ttStr = 259;
const int ttNum = 260;

class Node;

// Regular expression parser
class REParser {
 public:
    std::string::size_type getErrorPos() const { return pos_; };
    std::unique_ptr<Node> parse(const std::map<std::string, std::unique_ptr<Node>>& definitions,
                                const std::string& reg_expr);

 protected:
    std::string::size_type pos_ = 0;  // Current regular expression position
    bool mult_op_ = false;            // Parsing multiplication operator flag
    std::string id_;                  // Identifier
    int symb_ = 0;                    // Symbol
    ValueSet sset_;                   // Symbol set
    std::string str_;                 // Symbol string
    int num_ = -1;                    // Number

    static int hexDigit(char ch);
    static int octDigit(char ch);
    char readEscape(const std::string& reg_expr);
    int getNextToken(const std::string& reg_expr);
};
