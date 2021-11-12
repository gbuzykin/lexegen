#include "dfabld.h"

#include "node.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <stdexcept>

void DfaBuilder::addPattern(std::unique_ptr<Node> syn_tree, const ValueSet& sc) {
    unsigned n_pat = 1 + static_cast<unsigned>(patterns_.size());
    if (n_pat > ValueSet::kMaxValue) { throw std::runtime_error("too many patterns"); }
    auto cat_node = std::make_unique<Node>(NodeType::kCat);
    cat_node->setRight(std::make_unique<TermNode>(n_pat));  // Add $end node
    cat_node->setLeft(std::move(syn_tree));
    patterns_.emplace_back(Pattern{sc, std::move(cat_node)});
}

bool DfaBuilder::isPatternWithTrailCont(unsigned n_pat) const {
    return std::any_of(patterns_.begin(), patterns_.end(), [n_pat](const auto& pat) {
        return static_cast<const TermNode*>(pat.syn_tree->getRight())->getPatternNo() == n_pat &&
               pat.syn_tree->getLeft()->getType() == NodeType::kTrailCont;
    });
}

void DfaBuilder::build(unsigned sc_count, bool case_insensitive) {
    std::cout << "Building lexer..." << std::endl;

    std::vector<PositionalNode*> positions;
    std::vector<ValueSet> states;
    sc_count_ = sc_count;
    case_insensitive_ = case_insensitive;

    // Scatter positions and calculate node functions
    for (const auto& pat : patterns_) { pat.syn_tree->calcFunctions(positions); }

    std::cout << " - pattern count: " << patterns_.size() << std::endl;
    std::cout << " - S-state count: " << sc_count_ << std::endl;
    std::cout << " - position count: " << positions.size() << std::endl;

    auto calc_eps_closure = [&positions](const ValueSet& T) {
        ValueSet closure = T;
        int p = T.getFirstValue();
        while (p != -1) {
            if (positions[p]->getType() == NodeType::kTrailCont) { closure |= positions[p]->getFollowpos(); }
            p = T.getNextValue(p);
        }
        return closure;
    };

    auto add_state = [&Dtran = Dtran_, &states](const ValueSet& T) {
        states.push_back(T);
        Dtran.emplace_back();
        Dtran.back().fill(-1);
        return static_cast<unsigned>(states.size()) - 1;
    };

    std::vector<unsigned> pending_states;
    states.reserve(100 * sc_count_);
    Dtran_.reserve(100 * sc_count_);
    pending_states.reserve(100 * sc_count_);

    // Add start states
    for (unsigned sc = 0; sc < sc_count_; ++sc) {
        ValueSet S;
        for (const auto& pat : patterns_) {
            if (pat.sc.contains(sc)) { S |= pat.syn_tree->getFirstpos(); }
        }
        pending_states.push_back(add_state(calc_eps_closure(S)));
    }

    // Calculate other states and build DFA
    do {
        unsigned T_idx = pending_states.back();
        pending_states.pop_back();

        auto node_contains_symb = [case_insensitive](const auto* pos_node, unsigned symb) {
            auto type = pos_node->getType();
            if (type == NodeType::kSymbol) {
                const auto* symb_node = static_cast<const SymbNode*>(pos_node);
                return symb_node->getSymbol() == symb ||
                       (case_insensitive && symb_node->getSymbol() == std::tolower(symb));
            } else if (type == NodeType::kSymbSet) {
                const auto* sset_node = static_cast<const SymbSetNode*>(pos_node);
                return sset_node->getSymbSet().contains(symb) ||
                       (case_insensitive && sset_node->getSymbSet().contains(std::tolower(symb)));
            }
            return false;
        };

        ValueSet T = states[T_idx];

        for (unsigned symb = 0; symb < kSymbCount; ++symb) {
            if (case_insensitive && std::islower(symb)) { continue; }

            ValueSet U;
            int p = T.getFirstValue();
            while (p != -1) {
                if (node_contains_symb(positions[p], symb)) { U |= positions[p]->getFollowpos(); }
                p = T.getNextValue(p);
            }

            if (!U.empty()) {
                auto U_closure = calc_eps_closure(U);
                if (auto found = std::find(states.begin(), states.end(), U_closure); found != states.end()) {
                    Dtran_[T_idx][symb] = static_cast<unsigned>(found - states.begin());
                } else {
                    pending_states.push_back(Dtran_[T_idx][symb] = add_state(U_closure));
                }
            }
        }

        if (case_insensitive) {
            for (unsigned symb = 'a'; symb <= 'z'; ++symb) { Dtran_[T_idx][symb] = Dtran_[T_idx][std::toupper(symb)]; }
        }
    } while (pending_states.size() > 0);

    auto get_accept = [&positions](const ValueSet& T) -> int {
        int p = T.getFirstValue();
        while (p != -1) {
            if (positions[p]->getType() == NodeType::kTerm) {
                return static_cast<const TermNode*>(positions[p])->getPatternNo();
            }
            p = T.getNextValue(p);
        }
        return 0;
    };

    auto get_lls_patterns = [&positions](const ValueSet& T) {
        ValueSet patterns;
        int position_count = static_cast<int>(positions.size());
        int p = T.getFirstValue();
        while (p != -1) {
            // Termination node should have the next position number
            if (positions[p]->getType() == NodeType::kTrailCont && p + 1 < position_count &&
                positions[p + 1]->getType() == NodeType::kTerm) {
                patterns.addValue(static_cast<const TermNode*>(positions[p + 1])->getPatternNo());
            }
            p = T.getNextValue(p);
        }
        return patterns;
    };

    accept_.reserve(states.size());
    lls_.reserve(states.size());
    for (const auto& T : states) {
        accept_.push_back(get_accept(T));
        lls_.emplace_back(get_lls_patterns(T));
    }

    std::cout << " - state count: " << Dtran_.size() << std::endl;
    std::cout << " - transition table size: " << Dtran_.size() * sizeof(*Dtran_.begin()) << " bytes" << std::endl;
    std::cout << "Done." << std::endl;
}

void DfaBuilder::optimize() {
    std::cout << "Optimizing states..." << std::endl;

    // Initialize working arrays
    unsigned state_count = static_cast<unsigned>(Dtran_.size());
    unsigned group_count = sc_count_ + static_cast<unsigned>(patterns_.size());
    std::vector<int> state_group(state_count);
    std::vector<bool> state_used(state_count);
    std::vector<int> group_main_state(group_count);
    for (unsigned group = 0; group < group_count; ++group) { group_main_state[group] = -1; }

    // Initial classification
    for (unsigned state = 0; state < state_count; ++state) {
        unsigned group_no = 0;
        if (!lls_[state].empty()) {
            std::vector<bool> state_mark(state_count);
            std::vector<unsigned> state_stack;
            bool is_dead = true;

            // Is state belongs to 'dead' state group
            for (unsigned i = 0; i < state_count; ++i) { state_mark[i] = false; }
            state_stack.push_back(state);  // Add current state
            do {
                unsigned cur_state = state_stack.back();
                state_stack.pop_back();

                // Mark state
                state_mark[cur_state] = true;
                // Add adjucent states
                for (unsigned symb = 0; symb < kSymbCount; ++symb) {
                    int new_state = Dtran_[cur_state][symb];
                    if (new_state != -1) {
                        if (accept_[new_state] > 0) {
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
        } else if (accept_[state] > 0) {
            group_no = sc_count_ + accept_[state] - 1;
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
        for (unsigned symb = 0; symb < kSymbCount; ++symb) {
            std::vector<int> old_state_group = state_group;
            std::vector<std::map<int, int>> group_trans(group_count);
            for (unsigned state = 0; state < state_count; ++state) {
                int group = old_state_group[state];  // Current state group
                int new_state = Dtran_[state][symb];
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
    for (unsigned state = 0; state < state_count; ++state) {
        bool is_dead = true;
        if (state_used[state] && (state >= sc_count_) && (accept_[state] == 0)) {  // Is not start or accepting state
            // Check for 'dead' state
            for (unsigned symb = 0; symb < kSymbCount; ++symb) {
                int group = state_group[state];  // Current state group
                int new_state = Dtran_[state][symb];
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
    unsigned new_state_count = 0;
    std::vector<int> new_state_indices(state_count);
    for (unsigned state = 0; state < state_count; ++state) {
        if (state_used[state]) {
            new_state_indices[state] = new_state_count++;
        } else {
            new_state_indices[state] = -1;
        }
    }
    // Build optimized DFA table
    for (unsigned state = 0; state < state_count; ++state) {
        int new_state_idx = new_state_indices[state];
        if (new_state_idx != -1) {
            for (unsigned symb = 0; symb < kSymbCount; ++symb) {
                int tran = Dtran_[state][symb];
                if (tran != -1) { tran = new_state_indices[group_main_state[state_group[tran]]]; }
                Dtran_[new_state_idx][symb] = tran;
            }
            accept_[new_state_idx] = accept_[state];
            lls_[new_state_idx] = lls_[state];
        } else {
            new_state_idx = new_state_indices[group_main_state[state_group[state]]];
            if (new_state_idx != -1) { lls_[new_state_idx] |= lls_[state]; }
        }
    }
    Dtran_.resize(new_state_count);
    accept_.resize(new_state_count);
    lls_.resize(new_state_count);

    std::cout << " - new state count: " << Dtran_.size() << std::endl;
    std::cout << " - transition table size: " << Dtran_.size() * sizeof(*Dtran_.begin()) << " bytes" << std::endl;
    std::cout << "Done." << std::endl;
}

void DfaBuilder::makeCompressedDtran(std::vector<int>& symb2meta, std::vector<int>& def, std::vector<int>& base,
                                     std::vector<int>& next, std::vector<int>& check) const {
    std::cout << "Compressing tables..." << std::endl;

    // Clear compressed Dtran
    unsigned state_count = static_cast<unsigned>(Dtran_.size());
    assert(state_count > 0);
    // Build used symbols set
    ValueSet used_symbols;
    for (unsigned state = 0; state < state_count; ++state) {
        for (unsigned symb = 0; symb < kSymbCount; ++symb) {
            if (Dtran_[state][symb] != -1) { used_symbols.addValue(symb); }
        }
    }
    // Build symb2meta table
    symb2meta.resize(kSymbCount);
    std::vector<int> meta2symb;  // Inversed table
    unsigned used_symb_count = 0;
    for (unsigned symb = 0; symb < kSymbCount; ++symb) {
        if (used_symbols.contains(symb)) {
            symb2meta[symb] = used_symb_count;
            if (case_insensitive_ && std::islower(symb)) { symb2meta[std::toupper(symb)] = used_symb_count; }
            meta2symb.push_back(symb);
            ++used_symb_count;
        } else {
            symb2meta[symb] = -1;
        }
    }
    // Initialize arrays
    next.clear();
    check.clear();
    def.resize(state_count);
    base.resize(state_count);
    std::vector<unsigned> difs(used_symb_count);
    unsigned first_free = 0;
    // Run through all states
    for (unsigned state = 0; state < state_count; ++state) {
        unsigned sim_state = state;
        const auto& T = Dtran_[state];
        // Find similar state
        unsigned dif_count = 0;
        unsigned dif_seg_size = 0;
        // Compare with 'all failed' state
        for (unsigned meta = 0; meta < used_symb_count; ++meta) {
            if (T[meta2symb[meta]] != -1) { difs[dif_count++] = meta; }
        }
        if (dif_count > 0) {
            dif_seg_size = difs[dif_count - 1] - difs[0] + 1;
            // Compare with other compressed states
            for (unsigned state2 = 0; state2 < state; ++state2) {
                unsigned first_dif = 0;
                unsigned dif_count2 = 0;
                unsigned dif_seg_size2 = 0;
                const auto& U = Dtran_[state2];
                for (unsigned meta = 0; meta < used_symb_count; ++meta) {
                    int symb = meta2symb[meta];
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
            const auto& U = Dtran_[sim_state];
            for (unsigned meta = 0; meta < used_symb_count; ++meta) {
                int symb = meta2symb[meta];
                if (T[symb] != U[symb]) { difs[dif_count++] = meta; }
            }
            // Save default state
            def[state] = sim_state;
        } else {
            def[state] = -1;
        }

        unsigned compr_tbl_size = static_cast<unsigned>(next.size());
        assert(compr_tbl_size == check.size());
        unsigned b = first_free;
        if (dif_count) {
            // Find unused space
            assert(difs.size() > 0);
            unsigned i = first_free;
            if (difs[0] > first_free) { i = difs[0]; }
            b = i - difs[0];
            for (; i < compr_tbl_size; ++i, ++b) {
                bool match = true;
                for (unsigned j = 0; j < dif_count; ++j) {
                    unsigned l = b + difs[j];
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
        unsigned upper_bound = b + used_symb_count;
        if (upper_bound > compr_tbl_size) {
            next.resize(upper_bound);
            check.resize(upper_bound);
            for (unsigned i = compr_tbl_size; i < upper_bound; i++) { check[i] = -1; }
            compr_tbl_size = upper_bound;
        }
        // Save compressed state
        for (unsigned j = 0; j < dif_count; ++j) {
            unsigned l = b + difs[j];
            next[l] = Dtran_[state][meta2symb[difs[j]]];
            check[l] = state;
        }
        // Correct first_free
        for (; first_free < compr_tbl_size; first_free++) {
            if (check[first_free] == -1) { break; }
        }
    }

    // Fill unused next & check cells
    for (unsigned state = 0; state < state_count; ++state) {
        for (unsigned meta = 0; meta < used_symb_count; ++meta) {
            unsigned l = base[state] + meta;
            if (check[l] == -1) {  // Unused
                next[l] = Dtran_[state][meta2symb[meta]];
                check[l] = state;
            }
        }
    }

    std::cout << " - total compressed transition table size: "
              << (symb2meta.size() + def.size() + base.size() + next.size() + check.size()) * sizeof(int) << " bytes"
              << std::endl;
    std::cout << "Done." << std::endl;
}
