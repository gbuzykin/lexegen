#pragma once

#include "valset.h"

#include <array>
#include <memory>
#include <vector>

class Node;
class PositionalNode;

// DFA builder class
class DfaBuilder {
 public:
    static const unsigned kSymbCount = 256;
    static const unsigned kCountWeight = 1;
    static const unsigned kSegSizeWeight = 1;

    void addPattern(std::unique_ptr<Node> syn_tree, const ValueSet& sc);
    bool isPatternWithTrailCont(unsigned n_pat) const;
    void build(unsigned sc_count,     // Start condition count
               bool case_insensitive  // Case insensitive DFA?
    );
    void optimize();
    const std::vector<std::array<int, kSymbCount>>& getDtran() const { return Dtran_; }
    const std::vector<int>& getAccept() const { return accept_; }
    const std::vector<ValueSet>& getLLS() const { return lls_; }
    void makeCompressedDtran(std::vector<int>& symb2meta, std::vector<int>& def, std::vector<int>& base,
                             std::vector<int>& next, std::vector<int>& check) const;

 protected:
    struct Pattern {
        ValueSet sc;
        std::unique_ptr<Node> syn_tree;
    };

    unsigned sc_count_ = 0;
    bool case_insensitive_ = false;
    std::vector<Pattern> patterns_;
    std::vector<std::array<int, kSymbCount>> Dtran_;
    std::vector<int> accept_;
    std::vector<ValueSet> lls_;
};
