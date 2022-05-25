#include "dfabld.h"

#include "node.h"
#include "util/algorithm.h"
#include "util/format.h"

#include <cctype>
#include <unordered_map>

void DfaBuilder::addPattern(std::unique_ptr<Node> syn_tree, unsigned n_pat, const ValueSet& sc) {
    if (n_pat > ValueSet::kMaxValue) { throw std::runtime_error("too many patterns"); }
    auto cat_node = std::make_unique<Node>(NodeType::kCat);
    cat_node->setRight(std::make_unique<TermNode>(n_pat));  // Add $end node
    cat_node->setLeft(std::move(syn_tree));
    patterns_.emplace_back(sc, std::move(cat_node));
}

bool DfaBuilder::isPatternWithTrailCont(unsigned n_pat) const {
    return util::any_of(patterns_, [n_pat](const auto& pat) {
        return static_cast<const TermNode*>(pat.syn_tree->getRight())->getPatternNo() == n_pat &&
               pat.syn_tree->getLeft()->getType() == NodeType::kTrailCont;
    });
}

void DfaBuilder::build(unsigned sc_count, bool case_insensitive) {
    util::stdbuf::out.write("\033[1;34mBuilding lexer...\033[0m").endl();

    std::vector<PositionalNode*> positions;
    std::vector<ValueSet> states;
    sc_count_ = sc_count;

    // Scatter positions and calculate node functions
    positions.reserve(1024);
    for (const auto& pat : patterns_) { pat.syn_tree->calcFunctions(positions); }

    util::println(" - pattern count: {}", patterns_.size());
    util::println(" - S-state count: {}", sc_count_);
    util::println(" - position count: {}", positions.size());

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
                       (case_insensitive && symb_node->getSymbol() == static_cast<unsigned>(std::tolower(symb)));
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
                if (auto [it, found] = util::find(states, U_closure); found) {
                    Dtran_[T_idx][symb] = static_cast<unsigned>(it - states.begin());
                } else {
                    pending_states.push_back(Dtran_[T_idx][symb] = add_state(U_closure));
                }
            }
        }
    } while (pending_states.size() > 0);

    auto is_dead_symb = [&Dtran = Dtran_](unsigned s) {
        return util::all_of(Dtran, [s](const auto& T) { return T[s] == -1; });
    };

    auto get_equiv_symb = [&Dtran = Dtran_](unsigned s) {
        for (unsigned s2 = 0; s2 < s; ++s2) {
            if (util::all_of(Dtran, [s, s2](const auto& T) { return T[s] == T[s2]; })) { return s2; }
        }
        return s;
    };

    // Build `symb->meta` table
    symb2meta_.resize(kSymbCount);
    symb2meta_[0] = meta_count_++;  // '\0' is always a dead symbol
    for (unsigned symb = 1; symb < kSymbCount; ++symb) {
        if (case_insensitive && std::islower(symb)) {
            symb2meta_[symb] = symb2meta_[std::toupper(symb)];
        } else if (is_dead_symb(symb)) {
            symb2meta_[symb] = 0;
        } else if (unsigned equiv = get_equiv_symb(symb); equiv < symb) {
            symb2meta_[symb] = symb2meta_[equiv];
        } else {
            symb2meta_[symb] = meta_count_++;
        }
    }

    // Replace symbol codes with meta codes in Dtran
    for (auto& T : Dtran_) {
        int meta = 0;
        T[meta++] = T[0];
        for (unsigned symb = 1; symb < kSymbCount; ++symb) {
            if (symb2meta_[symb] >= meta) { T[meta++] = T[symb]; }
        }
    }

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

    // Build `accept` and `LLS` tables
    accept_.reserve(states.size());
    lls_.reserve(states.size());
    for (const auto& T : states) {
        accept_.push_back(get_accept(T));
        lls_.emplace_back(get_lls_patterns(T));
    }

    util::println(" - meta-symbol count: {}", meta_count_);
    util::println(" - state count: {}", Dtran_.size());
    util::println(" - transition table size: {} bytes", meta_count_ * Dtran_.size() * sizeof(int));
    util::stdbuf::out.write("\033[0;32mDone.\033[0m").endl();
}

void DfaBuilder::optimize() {
    util::stdbuf::out.write("\033[1;34mOptimizing states...\033[0m").endl();

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
        for (unsigned meta = 0; meta < meta_count_; ++meta) {
            std::vector<std::unordered_map<int, unsigned>> group_tran(group_main_state.size());
            std::vector<unsigned> saved_state_group = state_group;  // Use unmodified group numbers
            for (unsigned state = 0; state < Dtran_.size(); ++state) {
                unsigned group = saved_state_group[state];
                int next_group = Dtran_[state][meta] >= 0 ? saved_state_group[Dtran_[state][meta]] : -1;
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

    util::println(" - state group count: {}", group_count);

    auto is_dead_group = [&state_group, &group_main_state, meta_count = meta_count_, &Dtran = Dtran_,
                          &accept = accept_](unsigned group) {
        std::vector<bool> is_visited(group_main_state.size(), false);
        std::vector<unsigned> group_stack;
        group_stack.reserve(group_main_state.size());
        group_stack.push_back(group);
        is_visited[group] = true;
        do {
            group = group_stack.back();
            group_stack.pop_back();
            const auto& T = Dtran[group_main_state[group]];
            for (unsigned meta = 0; meta < meta_count; ++meta) {  // Add adjucent groups
                int next = T[meta];
                if (next < 0) { continue; }
                if (accept[next] > 0) { return false; }  // Can lead to accepting state
                unsigned next_group = state_group[next];
                if (!is_visited[next_group]) {
                    group_stack.push_back(next_group);
                    is_visited[next_group] = true;
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

    util::println(" - dead group count: {}", dead_group_count);

    auto get_main_state = [&state_group, &group_main_state](unsigned state) {
        return group_main_state[state_group[state]];
    };

    auto is_used_state = [&get_main_state](unsigned state) { return get_main_state(state) == static_cast<int>(state); };

    // Select new main states
    unsigned new_state_count = 0;
    std::vector<int> new_state_indices(Dtran_.size());
    for (unsigned state = 0; state < Dtran_.size(); ++state) {
        new_state_indices[state] = is_used_state(state) ? new_state_count++ : -1;
    }

    // Build optimized DFA table
    for (unsigned state = 0; state < Dtran_.size(); ++state) {
        if (int new_state_idx = new_state_indices[state]; new_state_idx >= 0) {
            for (unsigned meta = 0; meta < meta_count_; ++meta) {
                int next = Dtran_[state][meta] >= 0 ? get_main_state(Dtran_[state][meta]) : -1;
                Dtran_[new_state_idx][meta] = next >= 0 ? new_state_indices[next] : -1;
            }
            accept_[new_state_idx] = accept_[state];
            lls_[new_state_idx] = lls_[state];
        }
    }
    Dtran_.resize(new_state_count);
    accept_.resize(new_state_count);
    lls_.resize(new_state_count);

    util::println(" - new state count: {}", Dtran_.size());
    util::println(" - transition table size: {} bytes", meta_count_ * Dtran_.size() * sizeof(int));
    util::stdbuf::out.write("\033[0;32mDone.\033[0m").endl();
}

void DfaBuilder::makeCompressedDtran(std::vector<int>& def, std::vector<int>& base, std::vector<int>& next,
                                     std::vector<int>& check) const {
    util::stdbuf::out.write("\033[1;34mCompressing tables...\033[0m").endl();

    assert(!Dtran_.empty());
    def.resize(Dtran_.size());
    base.resize(Dtran_.size());
    next.reserve(10000);
    check.reserve(10000);

    auto calc_diffs_weight = [](const auto& diffs) {
        return kCountWeight * static_cast<unsigned>(diffs.size()) +
               kSegSizeWeight * static_cast<unsigned>(diffs.back() - diffs.front() + 1);
    };

    auto compare_with_all_failed_state = [meta_count = meta_count_, calc_diffs_weight](const auto& T, auto& diffs) {
        diffs.clear();
        for (unsigned meta = 0; meta < meta_count; ++meta) {
            if (T[meta] != -1) { diffs.push_back(meta); }
        }
        return !diffs.empty() ? calc_diffs_weight(diffs) : 0u;
    };

    auto compare_states = [meta_count = meta_count_, calc_diffs_weight](const auto& T, const auto& U, auto& diffs) {
        diffs.clear();
        for (unsigned meta = 0; meta < meta_count; ++meta) {
            if (T[meta] != U[meta]) { diffs.push_back(meta); }
        }
        return !diffs.empty() ? calc_diffs_weight(diffs) : 0u;
    };

    unsigned first_free = 0;
    std::vector<unsigned> diffs;
    diffs.reserve(meta_count_);

    for (unsigned state = 0; state < Dtran_.size(); ++state) {
        const auto& T = Dtran_[state];

        // Find similar state minimizing `diffs` weight
        int sim_state = -1;
        unsigned min_weight = compare_with_all_failed_state(T, diffs);
        if (min_weight > 0) {
            for (unsigned state2 = 0; state2 < state; ++state2) {
                unsigned weight = compare_states(T, Dtran_[state2], diffs);
                if (weight < min_weight) {
                    sim_state = state2;
                    if (weight == 0) { break; }
                    min_weight = weight;
                }
            }
        }

        // Save default state
        def[state] = sim_state;

        // Restore `diffs` vector
        if (sim_state >= 0) {  // `all-failed` is default state
            compare_states(T, Dtran_[sim_state], diffs);
        } else {
            compare_with_all_failed_state(T, diffs);
        }

        unsigned base_offset = first_free;
        if (!diffs.empty()) {
            auto base_offset_fits = [&diffs, &check](unsigned offset) {
                for (unsigned meta : diffs) {
                    unsigned l = offset + meta;
                    if (l >= check.size()) { break; }
                    if (check[l] >= 0) { return false; }
                }
                return true;
            };

            // Find unused space
            base_offset = first_free > diffs[0] ? first_free - diffs[0] : 0;
            while (base_offset < check.size() && !base_offset_fits(base_offset)) { ++base_offset; }
        }

        // Save compressed table base offset
        base[state] = base_offset;

        // Append compressed table
        unsigned upper_bound = base_offset + meta_count_;
        if (upper_bound > check.size()) { check.resize(upper_bound, -1); }

        // Save compressed state
        next.resize(check.size());
        for (unsigned meta : diffs) {
            unsigned l = base_offset + meta;
            next[l] = Dtran_[state][meta], check[l] = state;
        }

        // Move to the nearest free cell
        while (first_free < check.size() && check[first_free] >= 0) { ++first_free; }
    }

    // Fill free next & check cells
    for (unsigned state = 0; state < base.size(); ++state) {
        for (unsigned meta = 0; meta < meta_count_; ++meta) {
            unsigned l = base[state] + meta;
            if (check[l] < 0) { next[l] = Dtran_[state][meta], check[l] = state; }
        }
    }

    util::println(" - total compressed transition table size: {} bytes",
                  (def.size() + base.size() + next.size() + check.size()) * sizeof(int));
    util::stdbuf::out.write("\033[0;32mDone.\033[0m").endl();
}
