#pragma once

#include "valset.h"

#include <memory>
#include <vector>

class Node;
class PositionalNode;

// DFA builder class
class DfaBuilder {
 public:
    static const unsigned kCountWeight = 1;
    static const unsigned kSegSizeWeight = 1;

    DfaBuilder(bool case_insensitive, unsigned sc_count) : case_insensitive_(case_insensitive), sc_count_(sc_count) {}
    void addPattern(std::unique_ptr<Node> syn_tree, const ValueSet& sc);
    bool isPatternWithTrailCont(unsigned n_pat) const;
    void build();
    void optimize();
    const std::vector<std::vector<int>>& getDtran() const { return Dtran_; }
    const std::vector<int>& getAccept() const { return accept_; }
    const std::vector<ValueSet>& getLLS() const { return lls_; }
    void makeCompressedDtran(std::vector<int>& symb2meta, std::vector<int>& def, std::vector<int>& base,
                             std::vector<int>& next, std::vector<int>& check) const;

 protected:
    struct Pattern {
        ValueSet sc;
        std::unique_ptr<Node> syn_tree;
    };

    bool case_insensitive_;          // Case insensitive DFA?
    unsigned sc_count_;              // Start condition count
    std::vector<Pattern> patterns_;  // Pattern syntax trees
    std::vector<PositionalNode*> positions_;
    std::vector<ValueSet> states_;
    std::vector<std::vector<int>> Dtran_;
    std::vector<int> accept_;
    std::vector<ValueSet> lls_;

    bool addState(const ValueSet& U, unsigned& U_idx, bool find_equal = true);
    int getAccept(const ValueSet& T);
    bool getLlsPatterns(const ValueSet& T, ValueSet& patterns);
};
