#include "dfabld.h"
#include "node.h"
#include "parser.h"

#include <algorithm>
#include <fstream>

template<typename Iter>
void outputData(std::ostream& outp, Iter from, Iter to, size_t ntab = 0) {
    if (from == to) { return; }
    const unsigned length_limit = 120;
    std::string tab(ntab, ' '), line = tab + std::to_string(*from);
    while (++from != to) {
        auto sval = std::to_string(*from);
        if (line.length() + sval.length() + 3 > length_limit) {
            outp << line << "," << std::endl;
            line = tab + sval;
        } else {
            line += ", " + sval;
        }
    }
    outp << line << std::endl;
}

template<typename Iter>
void outputArray(std::ostream& outp, const std::string& array_name, Iter from, Iter to) {
    outp << std::endl << "static int " << array_name;
    if (from == to) {
        outp << "[1] = { 0 };" << std::endl;
    } else {
        outp << "[" << std::distance(from, to) << "] = {" << std::endl;
        outputData(outp, from, to, 4);
        outp << "};" << std::endl;
    }
}

void outputLexDefs(std::ostream& outp) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "struct StateData {",
        "    size_t pat_length = 0;",
        "    char* unread_text = nullptr;",
        "    std::vector<char> text;",
        "    std::vector<int> state_stack;",
        "    void (*get_more)(StateData& data) = nullptr;",
        "};",
    };
    // clang-format on
    outp << std::endl;
    for (const auto& l : text) { outp << l << std::endl; }
}

void outputLexEngine(std::ostream& outp, bool no_compress) {
    // clang-format off
    static constexpr std::string_view text0[] = {
        "int lex(StateData& data, int state) {",
        "    enum { kDeadFlag = 1, kTrailContFlag = 2, kFlagCount = 2 };",
        "    data.pat_length = 0;",
        "    data.state_stack.clear();",
        "",
        "    // Fill buffers till transition is impossible",
        "    char symb = \'\\0\';",
        "    do {",
        "        if (data.unread_text == data.text.data() + data.text.size()) { data.get_more(data); }",
        "        symb = *data.unread_text;",
    };
    static constexpr std::string_view text1[] = {
        "        int meta = symb2meta[static_cast<unsigned char>(symb)];",
        "        if (meta < 0) { break; }",
        "        do {",
        "            int l = base[state] + meta;",
        "            if (check[l] == state) {",
        "                state = next[l];",
        "                break;",
        "            }",
        "            state = def[state];",
        "        } while (state >= 0);",
    };
    static constexpr std::string_view text1_no_compress[] = {
        "        state = Dtran[state][static_cast<unsigned char>(symb)];",
    };
    static constexpr std::string_view text2[] = {
        "        if (state < 0) { break; }",
        "        data.text[data.pat_length++] = symb;",
        "        ++data.unread_text;",
        "        data.state_stack.push_back(state);",
        "    } while (symb != 0 && !(accept[state] & kDeadFlag));",
        "",
        "    // Unroll downto last accepting state",
        "    while (!data.state_stack.empty()) {",
        "        int n_pat = accept[data.state_stack.back()];",
        "        if (n_pat > 0) {",
        "            bool has_trailling_context = n_pat & kTrailContFlag;",
        "            n_pat >>= kFlagCount;",
        "            if (has_trailling_context) {",
        "                do {",
        "                    state = data.state_stack.back();",
        "                    for (int i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                        if (lls_list[i] == n_pat) { return n_pat; }",
        "                    }",
        "                    *(--data.unread_text) = data.text[--data.pat_length];",
        "                    data.state_stack.pop_back();",
        "                } while (!data.state_stack.empty());",
        "            }",
        "            return n_pat;",
        "        }",
        "        *(--data.unread_text) = data.text[--data.pat_length];",
        "        data.state_stack.pop_back();",
        "    }",
        "",
        "    // Default pattern",
        "    data.text[data.pat_length++] = *data.unread_text++;",
        "    return predef_pat_default;",
        "}",
    };
    // clang-format on
    outp << std::endl;
    for (const auto& l : text0) { outp << l << std::endl; }
    if (no_compress) {
        for (const auto& l : text1_no_compress) { outp << l << std::endl; }
    } else {
        for (const auto& l : text1) { outp << l << std::endl; }
    }
    for (const auto& l : text2) { outp << l << std::endl; }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        bool case_insensitive = false;
        bool no_compress = false;
        int optimization_level = 1;
        std::string input_file_name;
        std::string analyzer_file_name("lex_analyzer.inl");
        std::string defs_file_name("lex_defs.h");
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "-o") {
                if (++i < argc) { analyzer_file_name = argv[i]; }
            } else if (arg == "-h") {
                if (++i < argc) { defs_file_name = argv[i]; }
            } else if (arg == "--no-case") {
                case_insensitive = true;
            } else if (arg == "--no-compress") {
                no_compress = true;
            } else if (arg == "-O0") {
                optimization_level = 0;
            } else if (arg == "--help") {
                // clang-format off
                static const char* text[] = {
                    "Usage: lexegen [options] file",
                    "Options:",
                    "    -o <file>           Place the output analyzer into <file>.",
                    "    -h <file>           Place the output definitions into <file>.",
                    "    --no-case           Build case insensitive analyzer.",
                    "    --no-compress       Do not compress analyzer table.",
                    "    -O0                 Do not optimize analyzer.",
                    "    --help              Display this information.",
                };
                // clang-format on
                for (const char* l : text) { std::cout << l << std::endl; }
                return 0;
            } else if (arg[0] != '-') {
                input_file_name = argv[i];
            } else {
                std::cerr << "lexegen: unknown flag `" << arg << "`." << std::endl;
                return -1;
            }
        }

        if (input_file_name.empty()) {
            std::cerr << "lexegen: no input file specified." << std::endl;
            return -1;
        }

        std::ifstream ifile(input_file_name);
        if (!ifile) {
            std::cerr << "lexegen: cannot open input file `" << input_file_name << "`." << std::endl;
            return -1;
        }

        Parser parser(ifile);
        int ret = parser.parse();
        if (ret != 0) { return ret; }

        const auto& patterns = parser.getPatterns();
        const auto& start_conditions = parser.getStartConditions();

        DfaBuilder dfa_builder(case_insensitive, static_cast<int>(start_conditions.size()));
        for (size_t i = 0; i < patterns.size(); ++i) {
            dfa_builder.addPattern(parser.extractPatternTree(i), patterns[i].sc);
        }

        // Build lexer
        dfa_builder.build();
        if (optimization_level > 0) { dfa_builder.optimize(); }

        if (std::ofstream ofile(defs_file_name); ofile) {
            ofile << "// Lexegen autogenerated definition file - do not edit!" << std::endl;
            ofile << std::endl << "enum {" << std::endl;
            ofile << "    predef_pat_default = 0," << std::endl;
            for (size_t i = 0; i < patterns.size(); ++i) { ofile << "    pat_" << patterns[i].id << "," << std::endl; }
            ofile << "};" << std::endl;
            if (!start_conditions.empty()) {
                ofile << std::endl << "enum {" << std::endl;
                ofile << "    sc_" << start_conditions[0] << " = 0," << std::endl;
                for (size_t i = 1; i < start_conditions.size(); ++i) {
                    ofile << "    sc_" << start_conditions[i] << "," << std::endl;
                }
                ofile << "};" << std::endl;
            }
            outputLexDefs(ofile);
        } else {
            std::cerr << "lexegen: cannot open output file `" << defs_file_name << "`." << std::endl;
        }

        if (std::ofstream ofile(analyzer_file_name); ofile) {
            ofile << "// Lexegen autogenerated analyzer file - do not edit!" << std::endl;
            const auto& Dtran = dfa_builder.getDtran();
            if (no_compress) {
                if (!Dtran.empty()) {
                    ofile << std::endl
                          << "static int Dtran[" << Dtran.size() << "][" << Dtran[0].size() << "] = {" << std::endl;
                    for (size_t j = 0; j < Dtran.size(); ++j) {
                        ofile << (j == 0 ? "    {" : ", {") << std::endl;
                        outputData(ofile, Dtran[j].begin(), Dtran[j].end(), 8);
                        ofile << "    }";
                    }
                    ofile << std::endl << "};" << std::endl;
                }
            } else {
                std::vector<int> symb2meta, def, base, next, check;
                dfa_builder.makeCompressedDtran(symb2meta, def, base, next, check);
                outputArray(ofile, "symb2meta", symb2meta.begin(), symb2meta.end());
                outputArray(ofile, "def", def.begin(), def.end());
                outputArray(ofile, "base", base.begin(), base.end());
                outputArray(ofile, "next", next.begin(), next.end());
                outputArray(ofile, "check", check.begin(), check.end());
            }

            enum {
                kDeadFlag = 1,       // Can't continue from this state
                kTrailContFlag = 2,  // Accepting state with trailing context
                kAcceptFlagCount = 2,
            };
            std::vector<int> accept = dfa_builder.getAccept();
            for (unsigned state = 0; state < accept.size(); ++state) {
                if (unsigned n_pat = accept[state]; n_pat > 0) {
                    accept[state] <<= kAcceptFlagCount;
                    if (dfa_builder.isPatternWithTrailCont(n_pat)) { accept[state] |= kTrailContFlag; }
                    if (std::all_of(Dtran[state].begin(), Dtran[state].end(),
                                    [](int new_state) { return new_state < 0; })) {
                        accept[state] |= kDeadFlag;
                    }
                }
            }

            const auto& lls = dfa_builder.getLLS();
            std::vector<int> lls_idx, lls_list;
            lls_idx.push_back(0);
            for (size_t i = 0; i < lls.size(); ++i) {
                int n_pat = lls[i].getFirstValue();
                while (n_pat != -1) {
                    lls_list.push_back(n_pat);
                    n_pat = lls[i].getNextValue(n_pat);
                }
                lls_idx.push_back(static_cast<int>(lls_list.size()));
            }
            outputArray(ofile, "accept", accept.begin(), accept.end());
            outputArray(ofile, "lls_idx", lls_idx.begin(), lls_idx.end());
            outputArray(ofile, "lls_list", lls_list.begin(), lls_list.end());
            outputLexEngine(ofile, no_compress);
        } else {
            std::cerr << "lexegen: cannot open output file `" << analyzer_file_name << "`." << std::endl;
        }

        return 0;
    } catch (const std::exception& e) { std::cerr << "lexegen: exception catched: " << e.what() << "." << std::endl; }
    return -1;
}
