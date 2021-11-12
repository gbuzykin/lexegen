#include "dfabld.h"

#include "node.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

void DfaBuilder::addPattern(std::unique_ptr<Node> syn_tree, unsigned n_pat, const ValueSet& sc) {
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
        for (unsigned pos : T) {
            if (positions[pos]->getType() == NodeType::kTrailCont) { closure |= positions[pos]->getFollowpos(); }
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
            for (unsigned pos : T) {
                if (node_contains_symb(positions[pos], symb)) { U |= positions[pos]->getFollowpos(); }
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
        for (unsigned pos : T) {
            if (positions[pos]->getType() == NodeType::kTerm) {
                return static_cast<const TermNode*>(positions[pos])->getPatternNo();
            }
        }
        return 0;
    };

    auto get_lls_patterns = [&positions](const ValueSet& T) {
        ValueSet patterns;
        unsigned position_count = static_cast<unsigned>(positions.size());
        for (unsigned pos : T) {
            // Termination node should have the next position number
            if (positions[pos]->getType() == NodeType::kTrailCont && pos + 1 < position_count &&
                positions[pos + 1]->getType() == NodeType::kTerm) {
                patterns.addValue(static_cast<const TermNode*>(positions[pos + 1])->getPatternNo());
            }
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

    std::vector<unsigned> state_group(Dtran_.size());
    std::vector<int> group_main_state;
    group_main_state.reserve(Dtran_.size());

    // Initial state classification
    // Separate S, accepting the same pattern, and LLS states as mandatory groups
    std::unordered_map<unsigned, unsigned> pattern_groups;
    for (unsigned state = 0; state < Dtran_.size(); ++state) {
        unsigned group = 0;
        if (state < sc_count_ || !lls_[state].empty()) {
            group = static_cast<unsigned>(group_main_state.size());
            group_main_state.push_back(state);
            if (accept_[state] > 0) { pattern_groups.emplace(accept_[state], group); }
        } else if (accept_[state] > 0) {
            auto [it, success] = pattern_groups.emplace(accept_[state], 0);
            if (success) {
                group = static_cast<unsigned>(group_main_state.size());
                group_main_state.push_back(state);
                it->second = group;
            } else {
                group = it->second;
            }
        }
        state_group[state] = group;
    }

    // Classify other not mandatory states
    unsigned prev_group_count = 0;
    do {
        prev_group_count = static_cast<unsigned>(group_main_state.size());
        for (unsigned symb = 0; symb < kSymbCount; ++symb) {
            std::vector<std::unordered_map<int, unsigned>> group_tran(group_main_state.size());
            std::vector<unsigned> saved_state_group = state_group;  // Use unmodified group numbers
            for (unsigned state = 0; state < Dtran_.size(); ++state) {
                unsigned group = saved_state_group[state];
                int next_group = Dtran_[state][symb] >= 0 ? saved_state_group[Dtran_[state][symb]] : -1;
                auto [it, success] = group_tran[group].emplace(next_group, group);
                if (success && group_tran[group].size() > 1) {  // The group needs splitting, add new group
                    state_group[state] = static_cast<unsigned>(group_main_state.size());
                    group_main_state.push_back(state);
                    it->second = state_group[state];
                } else {  // Group is already created for this target group
                    state_group[state] = it->second;
                }
            }
        }
    } while (prev_group_count < group_main_state.size());

    unsigned group_count = static_cast<unsigned>(
        std::count_if(group_main_state.begin(), group_main_state.end(), [](int state) { return state >= 0; }));

    std::cout << " - state group count: " << group_count << std::endl;

    auto is_dead_group = [&state_group, &group_main_state, &Dtran = Dtran_, &accept = accept_](unsigned group) {
        std::vector<bool> is_visited(group_main_state.size(), false);
        std::vector<unsigned> group_stack;
        group_stack.reserve(group_main_state.size());
        group_stack.push_back(group);
        do {
            group = group_stack.back();
            group_stack.pop_back();
            is_visited[group] = true;
            for (int next : Dtran[group_main_state[group]]) {  // Add adjucent groups
                if (next < 0) { continue; }
                if (accept[next] > 0) { return false; }  // Can lead to accepting state
                unsigned next_group = state_group[next];
                if (!is_visited[next_group] && group_main_state[next_group] == next) {
                    group_stack.push_back(next_group);
                }
            }
        } while (!group_stack.empty());
        return true;
    };

    // Delete `dead` groups
    unsigned dead_group_count = 0;
    for (unsigned group = sc_count_; group < group_main_state.size(); ++group) {
        if (group_main_state[group] >= 0 && accept_[group_main_state[group]] == 0 && is_dead_group(group)) {
            group_main_state[group] = -1;  // Mark state group as unused
            ++dead_group_count;
        }
    }

    std::cout << " - dead group count: " << dead_group_count << std::endl;

    auto get_main_state = [&state_group, &group_main_state](unsigned state) {
        return group_main_state[state_group[state]];
    };

    auto is_used_state = [&state_group, &group_main_state, &get_main_state](unsigned state) {
        return get_main_state(state) == state;
    };

    // Select new main states
    unsigned new_state_count = 0;
    std::vector<int> new_state_indices(Dtran_.size());
    for (unsigned state = 0; state < Dtran_.size(); ++state) {
        new_state_indices[state] = is_used_state(state) ? new_state_count++ : -1;
    }

    // Build optimized DFA table
    for (unsigned state = 0; state < Dtran_.size(); ++state) {
        if (int new_state_idx = new_state_indices[state]; new_state_idx >= 0) {
            for (unsigned symb = 0; symb < kSymbCount; ++symb) {
                int next = Dtran_[state][symb] >= 0 ? get_main_state(Dtran_[state][symb]) : -1;
                Dtran_[new_state_idx][symb] = next >= 0 ? new_state_indices[next] : -1;
            }
            accept_[new_state_idx] = accept_[state];
            lls_[new_state_idx] = lls_[state];
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
