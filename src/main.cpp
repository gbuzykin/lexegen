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

void outputLexDefs(std::ostream& outp, bool inplace) {
    // clang-format off
    static constexpr std::string_view text[] = {
        "struct OutCtxData {",
        "    char* last = nullptr;",
        "    char* boundary = nullptr;",
        "};",
        "struct InCtxData {",
        "    const char* next = nullptr;",
        "    const char* boundary = nullptr;",
        "};",
    };
    static constexpr std::string_view text_inplace[] = {
        "struct CtxData {",
        "    char* out_last = nullptr;",
        "    char* in_next = nullptr;",
        "    char* in_boundary = nullptr;",
        "};",
    };
    // clang-format on
    outp << std::endl;
    if (inplace) {
        for (const auto& l : text_inplace) { outp << l << std::endl; }
    } else {
        for (const auto& l : text) { outp << l << std::endl; }
    }
}

void outputLexEngine(std::ostream& outp, bool inplace, bool no_compress) {
    // clang-format off
    static constexpr std::string_view text0[] = {
        "int lex(OutCtxData& out_ctx, InCtxData& in_ctx, std::vector<int>& state_stack, int state) {",
        "    enum { kTrailContFlag = 1, kFlagCount = 1 };",
        "",
        "    // Fill buffers till transition is impossible",
        "    char symb = \'\\0\';",
        "    do {",
        "        if (in_ctx.next == in_ctx.boundary) { return -1; }",
        "        symb = *in_ctx.next;",
        "        int meta = symb2meta[static_cast<unsigned char>(symb)];",
        "        if (meta < 0) { break; }",
    };
    static constexpr std::string_view text0_inplace[] = {
        "int lex(CtxData& ctx, std::vector<int>& state_stack, int state) {",
        "    enum { kTrailContFlag = 1, kFlagCount = 1 };",
        "",
        "    // Fill buffers till transition is impossible",
        "    char symb = \'\\0\';",
        "    do {",
        "        if (ctx.in_next == ctx.in_boundary) { return -1; }",
        "        symb = *ctx.in_next;",
        "        int meta = symb2meta[static_cast<unsigned char>(symb)];",
        "        if (meta < 0) { break; }",
    };
    static constexpr std::string_view text1[] = {
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
        "        state = Dtran[state][meta];",
    };
    static constexpr std::string_view text2[] = {
        "        if (state < 0) { break; }",
        "        if (out_ctx.last == out_ctx.boundary) { return -1; }",
        "        ++in_ctx.next, *out_ctx.last++ = symb;",
        "        state_stack.push_back(state);",
        "    } while (symb != 0);",
        "",
        "    // Unroll downto last accepting state",
        "    while (!state_stack.empty()) {",
        "        int n_pat = accept[state_stack.back()];",
        "        if (n_pat > 0) {",
        "            bool has_trailling_context = n_pat & kTrailContFlag;",
        "            n_pat >>= kFlagCount;",
        "            if (has_trailling_context) {",
        "                do {",
        "                    state = state_stack.back();",
        "                    for (int i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                        if (lls_list[i] == n_pat) {",
        "                            state_stack.clear();",
        "                            return n_pat;",
        "                        }",
        "                    }",
        "                    --in_ctx.next, --out_ctx.last;",
        "                    state_stack.pop_back();",
        "                } while (!state_stack.empty());",
        "            }",
        "            state_stack.clear();",
        "            return n_pat;",
        "        }",
        "        --in_ctx.next, --out_ctx.last;",
        "        state_stack.pop_back();",
        "    }",
        "",
        "    // Default pattern",
        "    if (out_ctx.last == out_ctx.boundary) { return -1; }",
        "    *out_ctx.last++ = *in_ctx.next++;",
        "    return predef_pat_default;",
        "}",
    };
    static constexpr std::string_view text2_inplace[] = {
        "        if (state < 0) { break; }",
        "        ++ctx.in_next, *ctx.out_last++ = symb;",
        "        state_stack.push_back(state);",
        "    } while (symb != 0);",
        "",
        "    // Unroll downto last accepting state",
        "    while (!state_stack.empty()) {",
        "        int n_pat = accept[state_stack.back()];",
        "        if (n_pat > 0) {",
        "            bool has_trailling_context = n_pat & kTrailContFlag;",
        "            n_pat >>= kFlagCount;",
        "            if (has_trailling_context) {",
        "                do {",
        "                    state = state_stack.back();",
        "                    for (int i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                        if (lls_list[i] == n_pat) {",
        "                            state_stack.clear();",
        "                            return n_pat;",
        "                        }",
        "                    }",
        "                    *(--ctx.in_next) = *(--ctx.out_last);",
        "                    state_stack.pop_back();",
        "                } while (!state_stack.empty());",
        "            }",
        "            state_stack.clear();",
        "            return n_pat;",
        "        }",
        "        *(--ctx.in_next) = *(--ctx.out_last);",
        "        state_stack.pop_back();",
        "    }",
        "",
        "    // Default pattern",
        "    *ctx.out_last++ = *ctx.in_next++;",
        "    return predef_pat_default;",
        "}",
    };
    // clang-format on
    outp << std::endl;
    if (inplace) {
        for (const auto& l : text0_inplace) { outp << l << std::endl; }
    } else {
        for (const auto& l : text0) { outp << l << std::endl; }
    }
    if (no_compress) {
        for (const auto& l : text1_no_compress) { outp << l << std::endl; }
    } else {
        for (const auto& l : text1) { outp << l << std::endl; }
    }
    if (inplace) {
        for (const auto& l : text2_inplace) { outp << l << std::endl; }
    } else {
        for (const auto& l : text2) { outp << l << std::endl; }
    }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        bool case_insensitive = false;
        bool no_compress = false;
        bool inplace_analyzer = false;
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
            } else if (arg == "--inplace") {
                inplace_analyzer = true;
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
                    "    -o <file>      Place the output analyzer into <file>.",
                    "    -h <file>      Place the output definitions into <file>.",
                    "    --inplace      Build analyzer which uses the same buffer for input and output text.",
                    "    --no-case      Build case insensitive analyzer.",
                    "    --no-compress  Do not compress analyzer table.",
                    "    -O0            Do not optimize analyzer.",
                    "    --help         Display this information.",
                };
                // clang-format on
                for (const char* l : text) { std::cout << l << std::endl; }
                return 0;
            } else if (arg[0] != '-') {
                input_file_name = argv[i];
            } else {
                logger::fatal() << "unknown flag \'" << arg << "\'";
                return -1;
            }
        }

        if (input_file_name.empty()) {
            logger::fatal() << "no input file specified";
            return -1;
        }

        std::ifstream ifile(input_file_name);
        if (!ifile) {
            logger::fatal() << "can\'t open input file \'" << input_file_name << "\'";
            return -1;
        }

        Parser parser(ifile, input_file_name);
        if (!parser.parse()) { return -1; }

        const auto& patterns = parser.getPatterns();
        const auto& start_conditions = parser.getStartConditions();

        DfaBuilder dfa_builder;
        unsigned n_pat = 0;
        for (size_t i = 0; i < patterns.size(); ++i) {
            dfa_builder.addPattern(parser.extractPatternTree(i), ++n_pat, patterns[i].sc);
        }

        // Build lexer
        dfa_builder.build(static_cast<unsigned>(start_conditions.size()), case_insensitive);
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
            outputLexDefs(ofile, inplace_analyzer);
        } else {
            logger::error() << "can\'t open output file \'" << defs_file_name << "\'";
        }

        if (std::ofstream ofile(analyzer_file_name); ofile) {
            ofile << "// Lexegen autogenerated analyzer file - do not edit!" << std::endl;
            const auto& symb2meta = dfa_builder.getSymb2Meta();
            const auto& Dtran = dfa_builder.getDtran();
            outputArray(ofile, "symb2meta", symb2meta.begin(), symb2meta.end());
            if (no_compress) {
                if (!Dtran.empty()) {
                    ofile << std::endl
                          << "static int Dtran[" << Dtran.size() << "][" << dfa_builder.getMetaCount() << "] = {"
                          << std::endl;
                    for (size_t j = 0; j < Dtran.size(); ++j) {
                        ofile << (j == 0 ? "    {" : ", {") << std::endl;
                        outputData(ofile, Dtran[j].begin(), Dtran[j].begin() + dfa_builder.getMetaCount(), 8);
                        ofile << "    }";
                    }
                    ofile << std::endl << "};" << std::endl;
                }
            } else {
                std::vector<int> def, base, next, check;
                dfa_builder.makeCompressedDtran(def, base, next, check);
                outputArray(ofile, "def", def.begin(), def.end());
                outputArray(ofile, "base", base.begin(), base.end());
                outputArray(ofile, "next", next.begin(), next.end());
                outputArray(ofile, "check", check.begin(), check.end());
            }

            std::vector<int> accept = dfa_builder.getAccept();
            for (unsigned state = 0; state < accept.size(); ++state) {
                if (unsigned n_pat = accept[state]; n_pat > 0) {
                    enum { kTrailContFlag = 1, kFlagCount = 1 };
                    accept[state] <<= kFlagCount;
                    if (dfa_builder.isPatternWithTrailCont(n_pat)) { accept[state] |= kTrailContFlag; }
                }
            }

            const auto& lls = dfa_builder.getLLS();
            std::vector<int> lls_idx, lls_list;
            lls_idx.push_back(0);
            for (size_t i = 0; i < lls.size(); ++i) {
                for (unsigned n_pat : lls[i]) { lls_list.push_back(static_cast<int>(n_pat)); }
                lls_idx.push_back(static_cast<int>(lls_list.size()));
            }
            outputArray(ofile, "accept", accept.begin(), accept.end());
            outputArray(ofile, "lls_idx", lls_idx.begin(), lls_idx.end());
            outputArray(ofile, "lls_list", lls_list.begin(), lls_list.end());
            outputLexEngine(ofile, inplace_analyzer, no_compress);
        } else {
            logger::error() << "can\'t open output file \'" << analyzer_file_name << "\'";
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal() << "exception catched: " << e.what(); }
    return -1;
}
