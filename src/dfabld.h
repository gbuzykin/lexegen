#pragma once

#include "valset.h"

#include <memory>
#include <vector>

class Node;

// DFA builder class
class DfaBuilder {
 public:
    bool getCaseSensitive() const { return case_sensitive_; };
    void setCaseSensitive(bool flag) { case_sensitive_ = flag; };
    int getPatternCount() const { return (int)patterns_.size(); };
    int getScCount() const { return sc_count_; };
    void setScCount(int count) { sc_count_ = count; };
    void addPattern(std::unique_ptr<Node> syn_tree, const ValueSet& sc);
    void build(std::vector<std::vector<int>>& Dtran, std::vector<int>& accept, std::vector<ValueSet>& lls);
    void optimize(std::vector<std::vector<int>>& Dtran, std::vector<int>& accept, std::vector<ValueSet>& lls);
    void compressDtran(const std::vector<std::vector<int>>& Dtran, std::vector<int>& symb2meta, std::vector<int>& def,
                       std::vector<int>& base, std::vector<int>& next, std::vector<int>& check);

 protected:
    static const int kTrailContBit = 0x8000;  // Pattern with trailling context
    static const int kCountWeight = 1;
    static const int kSegSizeWeight = 1;

    bool case_sensitive_ = true;
    int sc_count_ = 0;                             // Start condition count
    std::vector<std::unique_ptr<Node>> patterns_;  // Pattern syntax trees
    std::vector<ValueSet> sc_;                     // Pattern start conditions
    std::vector<Node*> positions_;                 // Leaf positions
    std::vector<ValueSet> followpos_;              // followpos(id) function

    void scatterPositions();
    void calcFunctions();
    bool addState(std::vector<ValueSet>& states, std::vector<std::vector<int>>& Dtran, const ValueSet& U, int& U_idx,
                  bool find_equal = true);
    int isAcceptingState(const ValueSet& T);
    bool isLastLexemeState(const ValueSet& T, ValueSet& patterns);
};
