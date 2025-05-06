#pragma once

#include "node.h"

#include <list>
#include <string>

// DFA builder class
class DfaBuilder {
 public:
    static const unsigned kSymbCount = 256;
    static const unsigned kCountWeight = 1;
    static const unsigned kSegSizeWeight = 1;

    explicit DfaBuilder(std::string file_name) : file_name_(std::move(file_name)) {}

    void addPattern(std::unique_ptr<Node> syn_tree, unsigned n_pat, const ValueSet& sc);
    bool isPatternWithTrailingContext(unsigned n_pat) const;
    bool hasPatternsWithLeftNlAnchoring() const;
    void build(unsigned sc_count,     // Start condition count
               bool case_insensitive  // Case insensitive DFA?
    );
    void optimize();
    unsigned getMetaCount() const { return meta_count_; }
    const std::vector<int>& getSymb2Meta() const { return symb2meta_; }
    const std::vector<std::array<int, kSymbCount>>& getDtran() const { return Dtran_; }
    const std::vector<int>& getAccept() const { return accept_; }
    const std::vector<ValueSet>& getLLS() const { return lls_; }
    void makeCompressedDtran(std::vector<int>& def, std::vector<int>& base, std::vector<int>& next,
                             std::vector<int>& check) const;

 protected:
    struct Pattern {
        Pattern(const ValueSet& in_sc, std::unique_ptr<Node> in_syn_tree)
            : sc(in_sc), syn_tree(std::move(in_syn_tree)) {}
        ValueSet sc;
        std::unique_ptr<Node> syn_tree;
    };

    std::string file_name_;
    unsigned start_state_count_ = 0;
    unsigned meta_count_ = 0;
    std::list<Pattern> patterns_;
    std::vector<int> symb2meta_;
    std::vector<std::array<int, kSymbCount>> Dtran_;
    std::vector<int> accept_;
    std::vector<ValueSet> lls_;
};
