#include "dfabld.h"

#include "node.h"

#include <cctype>
#include <map>
#include <stdexcept>

void DfaBuilder::addPattern(std::unique_ptr<Node> syn_tree, const ValueSet& sc) {
    unsigned n_pat = 1 + static_cast<unsigned>(patterns_.size());
    if (n_pat > ValueSet::kMaxValue) { throw std::runtime_error("too many patterns"); }
    auto cat_node = std::make_unique<Node>(NodeType::kCat);
    cat_node->setRight(std::make_unique<TermNode>(n_pat));  // Add $end node
    cat_node->setLeft(std::move(syn_tree));
    patterns_.emplace_back(std::move(cat_node));
    sc_.push_back(sc);
}

bool DfaBuilder::isPatternWithTrailCont(unsigned n_pat) const {
    for (const auto& pat : patterns_) {
        if (static_cast<const TermNode*>(pat->getRight())->getPatternNo() == n_pat &&
            pat->getLeft()->getType() == NodeType::kTrailCont) {
            return true;
        }
    }
    return false;
}

void DfaBuilder::build(std::vector<std::vector<int>>& Dtran, std::vector<int>& accept, std::vector<ValueSet>& lls) {
    for (int i = 0; i < (int)patterns_.size(); i++) { patterns_[i]->calcFunctions(positions_); }
    // Build DFA:
    accept.clear();
    // Unmarked states
    std::vector<ValueSet> states;
    std::vector<int> unmarked_states;
    // Add start states
    for (int sc = 0; sc < sc_count_; sc++) {
        ValueSet S;
        for (int i = 0; i < (int)patterns_.size(); i++) {
            if (sc_[i].contains(sc)) { S |= patterns_[i]->getFirstpos(); }
        }
        int S_idx;
        addState(states, Dtran, S, S_idx, false);
        accept.push_back(0);
        lls.push_back(ValueSet());
        unmarked_states.push_back(S_idx);
    }
    // Build DFA
    do {
        // Get unmarked state
        int T_idx = unmarked_states.back();
        unmarked_states.pop_back();
        ValueSet T = states[T_idx];
        accept[T_idx] = getAccept(T);  // Check for accepting state

        ValueSet patterns;
        if (getLlsPatterns(T, patterns)) { lls[T_idx] |= patterns; }  // Check for last lexeme state
        for (int ch = 0; ch < (int)Dtran[T_idx].size(); ch++) {
            ValueSet U;
            if (!case_sensitive_ && std::islower(ch)) { continue; }
            // Look through all positions in state T
            int p = T.getFirstValue();
            while (p != -1) {
                const auto* pos_node = positions_[p];
                bool incl_pos = false;
                auto type = pos_node->getType();
                if (type == NodeType::kSymbol) {
                    const auto* symb_node = static_cast<const SymbNode*>(pos_node);
                    if (symb_node->getSymbol() == ch) {
                        incl_pos = true;
                    } else if (!case_sensitive_ && (symb_node->getSymbol() == std::tolower(ch))) {
                        incl_pos = true;
                    }
                } else if (type == NodeType::kSymbSet) {
                    const auto* sset_node = static_cast<const SymbSetNode*>(pos_node);
                    if (sset_node->getSymbSet().contains(ch)) {
                        incl_pos = true;
                    } else if (!case_sensitive_ && sset_node->getSymbSet().contains(std::tolower(ch))) {
                        incl_pos = true;
                    }
                }

                if (incl_pos) { U |= positions_[p]->getFollowpos(); }  // Add followpos(p) to U
                p = T.getNextValue(p);
            }

            if (!U.empty()) {
                int U_idx;
                if (addState(states, Dtran, U, U_idx)) {  // New state added
                    accept.push_back(0);
                    lls.push_back(ValueSet());
                    unmarked_states.push_back(U_idx);
                }
                Dtran[T_idx][ch] = U_idx;
            }
        }
    } while (unmarked_states.size() > 0);
}

void DfaBuilder::optimize(std::vector<std::vector<int>>& Dtran, std::vector<int>& accept, std::vector<ValueSet>& lls) {
    // Initialize working arrays
    int state, state_count = (int)Dtran.size();
    int group, group_count = sc_count_ + (int)patterns_.size();
    std::vector<int> state_group(state_count);
    std::vector<bool> state_used(state_count);
    std::vector<int> group_main_state(group_count);
    for (group = 0; group < group_count; group++) group_main_state[group] = -1;
    // Initial classification
    for (state = 0; state < state_count; state++) {
        int group_no = 0;
        if (!lls[state].empty()) {
            std::vector<bool> state_mark(state_count);
            std::vector<int> state_stack;
            bool is_dead = true;

            // Is state belongs to 'dead' state group
            for (int i = 0; i < state_count; i++) state_mark[i] = false;
            state_stack.push_back(state);  // Add current state
            do {
                int cur_state = state_stack.back();
                state_stack.pop_back();

                // Mark state
                state_mark[cur_state] = true;
                // Add adjucent states
                for (int ch = 0; ch < (int)Dtran[cur_state].size(); ch++) {
                    int new_state = Dtran[cur_state][ch];
                    if (new_state != -1) {
                        if (accept[new_state] > 0) {
                            is_dead = false;
                            break;
                        }
                        if (!state_mark[new_state]) { state_stack.push_back(new_state); }
                    }
                }
            } while (state_stack.size() > 0);

            if (!is_dead) {
                // Add to new group
                group_no = group_count;
                group_main_state.push_back(-1);
                group_count++;
            } else if (state < sc_count_) {
                group_no = state;
            }
        } else if (state < sc_count_) {
            group_no = state;
        } else if (accept[state] > 0) {
            group_no = sc_count_ + accept[state] - 1;
        }
        if (group_main_state[group_no] == -1) {
            group_main_state[group_no] = state;
            state_used[state] = true;
        } else {
            state_used[state] = false;
        }
        state_group[state] = group_no;
    }
    // Classify states
    bool change = false;
    do {
        change = false;
        for (int ch = 0; ch < (int)Dtran[0].size(); ch++) {
            std::vector<int> old_state_group = state_group;
            std::vector<std::map<int, int>> group_trans(group_count);
            for (state = 0; state < state_count; state++) {
                int group = old_state_group[state];  // Current state group
                int new_state = Dtran[state][ch];
                int new_group = -1;  // New state group
                if (new_state != -1) { new_group = old_state_group[new_state]; }
                auto& cur_group_trans = group_trans[group];

                auto result = cur_group_trans.emplace(new_group, group);
                if (result.second) {
                    if (cur_group_trans.size() > 1) {  // Is not first found state in the group
                        // Add new group
                        result.first->second = group_count;
                        state_group[state] = group_count;
                        group_count++;
                        group_main_state.push_back(state);
                        state_used[state] = true;
                        change = true;
                    }
                } else {
                    state_group[state] = result.first->second;
                }
            }
        }
    } while (change);

    // Delete 'dead' states
    for (state = 0; state < state_count; state++) {
        bool is_dead = true;
        if (state_used[state] && (state >= sc_count_) && (accept[state] == 0)) {  // Is not start or accepting state
            // Check for 'dead' state
            for (int ch = 0; ch < (int)Dtran[state].size(); ch++) {
                int group = state_group[state];  // Current state group
                int new_state = Dtran[state][ch];
                int new_group = -1;  // New state group
                if (new_state != -1) {
                    new_group = state_group[new_state];
                    if ((group != new_group) && state_used[group_main_state[new_group]]) {
                        is_dead = false;
                        break;
                    }
                }
            }
            if (is_dead) { state_used[state] = false; }  // Delete state
        }
    }

    // Create optimized Dtran:
    // Calculate indices of new states
    int new_state_count = 0;
    std::vector<int> new_state_indices(state_count);
    for (state = 0; state < state_count; state++) {
        if (state_used[state]) {
            new_state_indices[state] = new_state_count++;
        } else {
            new_state_indices[state] = -1;
        }
    }
    // Build optimized DFA table
    for (state = 0; state < state_count; state++) {
        int new_state_idx = new_state_indices[state];
        if (new_state_idx != -1) {
            for (int ch = 0; ch < (int)Dtran[state].size(); ch++) {
                int tran = Dtran[state][ch];
                if (tran != -1) { tran = new_state_indices[group_main_state[state_group[tran]]]; }
                Dtran[new_state_idx][ch] = tran;
            }
            accept[new_state_idx] = accept[state];
            lls[new_state_idx] = lls[state];
        } else {
            new_state_idx = new_state_indices[group_main_state[state_group[state]]];
            if (new_state_idx != -1) { lls[new_state_idx] |= lls[state]; }
        }
    }
    Dtran.resize(new_state_count);
    accept.resize(new_state_count);
    lls.resize(new_state_count);
}

void DfaBuilder::compressDtran(const std::vector<std::vector<int>>& Dtran, std::vector<int>& symb2meta,
                               std::vector<int>& def, std::vector<int>& base, std::vector<int>& next,
                               std::vector<int>& check) {
    // Clear compressed Dtran
    int state, state_count = (int)Dtran.size();
    assert(state_count > 0);
    int symb, symb_count = (int)Dtran[0].size();
    // Build used symbols set
    ValueSet used_symbols;
    for (state = 0; state < state_count; state++) {
        assert(Dtran[state].size() == symb_count);
        for (symb = 0; symb < symb_count; symb++) {
            if (Dtran[state][symb] != -1) { used_symbols.addValue(symb); }
        }
    }
    // Build symb2meta table
    symb2meta.resize(symb_count);
    std::vector<int> meta2symb;  // Inversed table
    int meta, used_symb_count = 0;
    for (symb = 0; symb < symb_count; symb++) {
        if (used_symbols.contains(symb)) {
            symb2meta[symb] = used_symb_count;
            if (!case_sensitive_ && std::islower(symb)) { symb2meta[std::toupper(symb)] = used_symb_count; }
            meta2symb.push_back(symb);
            used_symb_count++;
        } else {
            symb2meta[symb] = -1;
        }
    }
    // Initialize arrays
    next.clear();
    check.clear();
    def.resize(state_count);
    base.resize(state_count);
    std::vector<int> difs(used_symb_count);
    int first_free = 0;
    // Run through all states
    for (state = 0; state < state_count; state++) {
        int sim_state = state;
        const std::vector<int>& T = Dtran[state];
        // Find similar state
        int dif_count = 0;
        int dif_seg_size = 0;
        // Compare with 'all failed' state
        for (meta = 0; meta < used_symb_count; meta++) {
            if (T[meta2symb[meta]] != -1) { difs[dif_count++] = meta; }
        }
        if (dif_count > 0) {
            dif_seg_size = difs[dif_count - 1] - difs[0] + 1;
            // Compare with other compressed states
            for (int state2 = 0; state2 < state; state2++) {
                int first_dif = 0;
                int dif_count2 = 0;
                int dif_seg_size2 = 0;
                const std::vector<int>& U = Dtran[state2];
                for (meta = 0; meta < used_symb_count; meta++) {
                    symb = meta2symb[meta];
                    if (T[symb] != U[symb]) {
                        if (dif_count2 == 0) { first_dif = meta; }
                        dif_seg_size2 = meta - first_dif + 1;
                        dif_count2++;
                    }
                }
                // Find optimum
                if ((kCountWeight * dif_count2 + kSegSizeWeight * dif_seg_size2) <
                    (kCountWeight * dif_count + kSegSizeWeight * dif_seg_size)) {
                    sim_state = state2;
                }
            }
        }
        if (sim_state != state) {
            dif_count = 0;
            const std::vector<int>& U = Dtran[sim_state];
            for (meta = 0; meta < used_symb_count; meta++) {
                symb = meta2symb[meta];
                if (T[symb] != U[symb]) { difs[dif_count++] = meta; }
            }
            // Save default state
            def[state] = sim_state;
        } else {
            def[state] = -1;
        }

        int compr_tbl_size = (int)next.size();
        assert(compr_tbl_size == check.size());
        int b = first_free;
        if (dif_count) {
            // Find unused space
            assert(difs.size() > 0);
            int i = first_free;
            if (difs[0] > first_free) { i = difs[0]; }
            b = i - difs[0];
            for (; i < compr_tbl_size; i++, b++) {
                bool match = true;
                for (int j = 0; j < dif_count; j++) {
                    int l = b + difs[j];
                    if (l >= compr_tbl_size) { break; }
                    if (check[l] != -1) {
                        match = false;
                        break;
                    }
                }
                if (match) { break; }
            }
        }

        // Save base
        base[state] = b;
        // Append compressed Dtran
        int upper_bound = b + used_symb_count;
        if (upper_bound > compr_tbl_size) {
            next.resize(upper_bound);
            check.resize(upper_bound);
            for (int i = compr_tbl_size; i < upper_bound; i++) { check[i] = -1; }
            compr_tbl_size = upper_bound;
        }
        // Save compressed state
        for (int j = 0; j < dif_count; j++) {
            int l = b + difs[j];
            next[l] = Dtran[state][meta2symb[difs[j]]];
            check[l] = state;
        }
        // Correct first_free
        for (; first_free < compr_tbl_size; first_free++) {
            if (check[first_free] == -1) { break; }
        }
    }

    // Fill unused next & check cells
    for (state = 0; state < state_count; state++) {
        for (meta = 0; meta < used_symb_count; meta++) {
            int l = base[state] + meta;
            if (check[l] == -1) {  // Unused
                next[l] = Dtran[state][meta2symb[meta]];
                check[l] = state;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// DfaBuilder private/protected methods

bool DfaBuilder::addState(std::vector<ValueSet>& states, std::vector<std::vector<int>>& Dtran, const ValueSet& U,
                          int& U_idx, bool find_equal) {
    // Calculate eps-closure of the state
    ValueSet closedU = U;
    int p = U.getFirstValue();
    while (p != -1) {
        if (positions_[p]->getType() == NodeType::kTrailCont) { closedU |= positions_[p]->getFollowpos(); }
        p = U.getNextValue(p);
    }

    if (find_equal) {
        for (U_idx = 0; U_idx < (int)states.size(); U_idx++) {
            if (states[U_idx] == closedU) { return false; }  // State found
        }
    } else {
        U_idx = (int)states.size();
    }
    states.push_back(closedU);
    Dtran.push_back(std::vector<int>(256));
    for (int i = 0; i < (int)Dtran[U_idx].size(); i++) { Dtran[U_idx][i] = -1; }
    return true;
}

int DfaBuilder::getAccept(const ValueSet& T) {
    int p = T.getFirstValue();
    while (p != -1) {
        if (positions_[p]->getType() == NodeType::kTerm) {
            return static_cast<const TermNode*>(positions_[p])->getPatternNo();
        }
        p = T.getNextValue(p);
    }
    return 0;
}

bool DfaBuilder::getLlsPatterns(const ValueSet& T, ValueSet& patterns) {
    patterns.clear();
    int position_count = static_cast<int>(positions_.size());
    int p = T.getFirstValue();
    while (p != -1) {
        // Termination node should have the next position number
        if (positions_[p]->getType() == NodeType::kTrailCont && p + 1 < position_count &&
            positions_[p + 1]->getType() == NodeType::kTerm) {
            patterns.addValue(static_cast<const TermNode*>(positions_[p + 1])->getPatternNo());
        }
        p = T.getNextValue(p);
    }
    if (!patterns.empty()) { return true; }
    return false;
}
