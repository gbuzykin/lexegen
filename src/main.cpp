#include "dfabld.h"
#include "node.h"
#include "parser.h"

#include "uxs/algorithm.h"
#include "uxs/io/filebuf.h"

template<typename Iter>
void outputData(uxs::iobuf& outp, Iter from, Iter to, size_t ntab = 0) {
    if (from == to) { return; }
    const unsigned length_limit = 120;
    std::string tab(ntab, ' '), line = tab + uxs::to_string(*from);
    while (++from != to) {
        auto sval = uxs::to_string(*from);
        if (line.length() + sval.length() + 3 > length_limit) {
            outp.write(line).put(',').put('\n');
            line = tab + sval;
        } else {
            line += ", " + sval;
        }
    }
    outp.write(line).put('\n');
}

template<typename Iter>
void outputArray(uxs::iobuf& outp, std::string_view state_type, std::string_view array_name, Iter from, Iter to) {
    uxs::fprint(outp, "\nstatic {} {}", state_type, array_name);
    if (from == to) {
        outp.write("[1] = { 0 };\n");
    } else {
        uxs::fprintln(outp, "[{}] = {{", std::distance(from, to));
        outputData(outp, from, to, 4);
        outp.write("};\n");
    }
}

struct EngineInfo {
    int compress_level = 2;
    bool any_has_trail_context = true;
    std::string_view state_type;
};

void outputLexEngine(uxs::iobuf& outp, const EngineInfo& info) {
    // clang-format off
    static constexpr std::string_view text0[] = {
        "static int lex(const char* first, const char* last, {}** p_sptr, unsigned* p_llen, int has_more) {{",
        "    {} *sptr = *p_sptr, *sptr0 = sptr - *p_llen;",
        "    {} state = *(sptr - 1);",
        "    while (first < last) {{ /* Analyze till transition is impossible */",
    };
    static constexpr std::string_view text1[] = {
        "        uint8_t meta = symb2meta[(unsigned char)*first];",
        "        do {",
        "            int l = base[state] + meta;",
        "            if (check[l] == state) {",
        "                state = next[l];",
        "                break;",
        "            }",
        "            state = def[state];",
        "        } while (state >= 0);",
    };
    static constexpr std::string_view text1_compress0[] = {
        "        state = Dtran[256 * state + (unsigned char)*first];",
    };
    static constexpr std::string_view text1_compress1[] = {
        "        state = Dtran[dtran_width * state + symb2meta[(unsigned char)*first]];",
    };
    static constexpr std::string_view text2[] = {
        "        if (state < 0) { goto unroll; }",
        "        *sptr++ = state, ++first;",
        "    }",
        "    if (has_more || sptr == sptr0) {",
        "        *p_sptr = sptr;",
        "        *p_llen = (unsigned)(sptr - sptr0);",
        "        return err_end_of_input;",
        "    }",
        "unroll:",
        "    *p_sptr = sptr0;",
        "    while (sptr != sptr0) { /* Unroll down-to last accepting state */",
        "        int n_pat = accept[(state = *(sptr - 1))];",
        "        if (n_pat > 0) {",
    };
    static constexpr std::string_view text3_any_has_trail_context[] = {
        "            enum { kTrailContFlag = 1, kFlagCount = 1 };",
        "            int i;",
        "            if (!(n_pat & kTrailContFlag)) {",
        "                *p_llen = (unsigned)(sptr - sptr0);",
        "                return n_pat >> kFlagCount;",
        "            }",
        "            n_pat >>= kFlagCount;",
        "            do {",
        "                for (i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                    if (lls_list[i] == n_pat) {",
        "                        *p_llen = (unsigned)(sptr - sptr0);",
        "                        return n_pat;",
        "                    }",
        "                }",
        "                state = *(--sptr - 1);",
        "            } while (sptr != sptr0);",
        "            *p_llen = (unsigned)(sptr - sptr0);",
        "            return n_pat;",
        "        }",
        "        --sptr;",
        "    }",
        "    *p_llen = 1; /* Accept at least one symbol as default pattern */",
        "    return predef_pat_default;",
        "}",
    };
    static constexpr std::string_view text3[] = {
        "            *p_llen = (unsigned)(sptr - sptr0);",
        "            return n_pat;",
        "        }",
        "        --sptr;",
        "    }",
        "    *p_llen = 1; /* Accept at least one symbol as default pattern */",
        "    return predef_pat_default;",
        "}",
    };
    // clang-format on
    outp.put('\n');
    for (const auto& l : text0) { uxs::fprintln(outp, l, info.state_type); }
    if (info.compress_level == 0) {
        for (const auto& l : text1_compress0) { outp.write(l).put('\n'); }
    } else if (info.compress_level == 1) {
        for (const auto& l : text1_compress1) { outp.write(l).put('\n'); }
    } else {
        for (const auto& l : text1) { outp.write(l).put('\n'); }
    }
    for (const auto& l : text2) { outp.write(l).put('\n'); }
    if (info.any_has_trail_context) {
        for (const auto& l : text3_any_has_trail_context) { outp.write(l).put('\n'); }
    } else {
        for (const auto& l : text3) { outp.write(l).put('\n'); }
    }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        bool case_insensitive = false;
        bool use_int8_if_possible = false;
        int optimization_level = 1;
        std::string input_file_name;
        std::string analyzer_file_name("lex_analyzer.inl");
        std::string defs_file_name("lex_defs.h");
        EngineInfo eng_info;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "-o") {
                if (++i < argc) { analyzer_file_name = argv[i]; }
            } else if (arg == "-h") {
                if (++i < argc) { defs_file_name = argv[i]; }
            } else if (arg == "--no-case") {
                case_insensitive = true;
            } else if (arg == "--compress0") {
                eng_info.compress_level = 0;
            } else if (arg == "--compress1") {
                eng_info.compress_level = 1;
            } else if (arg == "--compress2") {
            } else if (arg == "--use-int8-if-possible") {
                use_int8_if_possible = true;
            } else if (arg == "-O0") {
                optimization_level = 0;
            } else if (arg == "-O1") {
            } else if (arg == "--help") {
                // clang-format off
                static constexpr std::string_view text[] = {
                    "Usage: lexegen [options] file",
                    "Options:",
                    "    -o <file>                Place the output analyzer into <file>.",
                    "    -h <file>                Place the output definitions into <file>.",
                    "    --no-case                Build case insensitive analyzer.",
                    "    --compress0              Do not compress analyzer table, do not use `meta` table.",
                    "    --compress1              Do not compress analyzer table.",
                    "    --compress2              Default compression.",
                    "    --use-int8-if-possible   Use `int8_t` instead of `int` for states if state count is < 128.",
                    "    -O0                      Do not optimize analyzer states.",
                    "    -O1                      Default analyzer optimization.",
                    "    --help                   Display this information.",
                };
                // clang-format on
                for (const auto& l : text) { uxs::stdbuf::out.write(l).endl(); }
                return 0;
            } else if (arg[0] != '-') {
                input_file_name = arg;
            } else {
                logger::fatal().format("unknown command line option `{}`", arg);
                return -1;
            }
        }

        if (input_file_name.empty()) {
            logger::fatal().format("no input file specified");
            return -1;
        }

        uxs::filebuf ifile(input_file_name.c_str(), "r");
        if (!ifile) {
            logger::fatal().format("could not open input file `{}`", input_file_name);
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
        uxs::stdbuf::out.write("\033[1;34mBuilding lexer...\033[0m").endl();
        dfa_builder.build(static_cast<unsigned>(start_conditions.size()), case_insensitive);

        size_t state_sz = sizeof(int);
        if (use_int8_if_possible && dfa_builder.getDtran().size() < 128) {
            eng_info.state_type = "int8_t", state_sz = 1;
        } else {
            eng_info.state_type = "int", state_sz = sizeof(int);
        }

        uxs::println(" transition table size: {} bytes",
                     dfa_builder.getMetaCount() * dfa_builder.getDtran().size() * state_sz);
        uxs::stdbuf::out.write("\033[0;32mDone.\033[0m").endl();

        if (optimization_level > 0) {
            uxs::stdbuf::out.write("\033[1;34mOptimizing states...\033[0m").endl();
            dfa_builder.optimize();

            if (use_int8_if_possible && dfa_builder.getDtran().size() < 128) {
                eng_info.state_type = "int8_t", state_sz = 1;
            } else {
                eng_info.state_type = "int", state_sz = sizeof(int);
            }

            uxs::println(" transition table size: {} bytes",
                         dfa_builder.getMetaCount() * dfa_builder.getDtran().size() * state_sz);
            uxs::stdbuf::out.write("\033[0;32mDone.\033[0m").endl();
        }

        if (uxs::filebuf ofile(defs_file_name.c_str(), "w"); ofile) {
            ofile.write("/* Lexegen autogenerated definition file - do not edit! */\n");
            ofile.write("/* clang-format off */\n");
            ofile.write("\nenum {\n");
            ofile.write("    err_end_of_input = -1,\n");
            ofile.write("    predef_pat_default = 0,\n");
            for (size_t i = 0; i < patterns.size(); ++i) { uxs::fprintln(ofile, "    pat_{},", patterns[i].id); }
            ofile.write("    total_pattern_count\n");
            ofile.write("};\n");
            if (!start_conditions.empty()) {
                ofile.write("\nenum {\n");
                if (start_conditions.size() > 1) {
                    uxs::fprintln(ofile, "    sc_{} = 0,", start_conditions[0]);
                    for (size_t i = 1; i < start_conditions.size() - 1; ++i) {
                        uxs::fprintln(ofile, "    sc_{},", start_conditions[i]);
                    }
                    uxs::fprintln(ofile, "    sc_{}", start_conditions[start_conditions.size() - 1]);
                } else {
                    uxs::fprintln(ofile, "    sc_{} = 0", start_conditions[0]);
                }
                ofile.write("};\n");
            }
        } else {
            logger::error().format("could not open output file `{}`", defs_file_name);
        }

        if (uxs::filebuf ofile(analyzer_file_name.c_str(), "w"); ofile) {
            ofile.write("/* Lexegen autogenerated analyzer file - do not edit! */\n");
            ofile.write("/* clang-format off */\n");
            const auto& symb2meta = dfa_builder.getSymb2Meta();
            const auto& Dtran = dfa_builder.getDtran();
            if (eng_info.compress_level > 0) {
                outputArray(ofile, "uint8_t", "symb2meta", symb2meta.begin(), symb2meta.end());
                if (eng_info.compress_level < 2) {
                    if (!Dtran.empty()) {
                        std::vector<int> dtran_data;
                        int dtran_width = dfa_builder.getMetaCount();
                        dtran_data.reserve(dtran_width * Dtran.size());
                        for (size_t j = 0; j < Dtran.size(); ++j) {
                            std::copy_n(Dtran[j].data(), dtran_width, std::back_inserter(dtran_data));
                        }
                        uxs::fprintln(ofile, "\nenum {{ dtran_width = {} }};", dtran_width);
                        outputArray(ofile, eng_info.state_type, "Dtran", dtran_data.begin(), dtran_data.end());
                    }
                } else {
                    std::vector<int> def, base, next, check;
                    uxs::stdbuf::out.write("\033[1;34mCompressing tables...\033[0m").endl();
                    dfa_builder.makeCompressedDtran(def, base, next, check);

                    uxs::println(" total compressed transition table size: {} bytes",
                                 (def.size() + next.size() + check.size()) * state_sz + base.size() * sizeof(int));
                    uxs::stdbuf::out.write("\033[0;32mDone.\033[0m").endl();

                    outputArray(ofile, eng_info.state_type, "def", def.begin(), def.end());
                    outputArray(ofile, "int", "base", base.begin(), base.end());
                    outputArray(ofile, eng_info.state_type, "next", next.begin(), next.end());
                    outputArray(ofile, eng_info.state_type, "check", check.begin(), check.end());
                }
            } else if (!Dtran.empty()) {
                std::vector<int> dtran_data;
                dtran_data.reserve(256 * Dtran.size());
                for (size_t j = 0; j < Dtran.size(); ++j) {
                    uxs::transform(symb2meta, std::back_inserter(dtran_data),
                                   [&row = Dtran[j]](int meta) { return row[meta]; });
                }
                outputArray(ofile, eng_info.state_type, "Dtran", dtran_data.begin(), dtran_data.end());
            }

            std::vector<int> accept = dfa_builder.getAccept();
            eng_info.any_has_trail_context = false;
            for (unsigned state = 0; state < accept.size(); ++state) {
                if (unsigned n_pat = accept[state]; n_pat > 0) {
                    if (dfa_builder.isPatternWithTrailCont(n_pat)) { eng_info.any_has_trail_context = true; }
                }
            }

            if (eng_info.any_has_trail_context) {
                for (unsigned state = 0; state < accept.size(); ++state) {
                    if (unsigned n_pat = accept[state]; n_pat > 0) {
                        enum { kTrailContFlag = 1, kFlagCount = 1 };
                        accept[state] <<= kFlagCount;
                        if (dfa_builder.isPatternWithTrailCont(n_pat)) { accept[state] |= kTrailContFlag; }
                    }
                }
            }

            outputArray(ofile, "int", "accept", accept.begin(), accept.end());

            if (eng_info.any_has_trail_context) {
                const auto& lls = dfa_builder.getLLS();
                std::vector<int> lls_idx, lls_list;
                lls_idx.reserve(lls.size() + 1);
                lls_list.reserve(lls.size());
                lls_idx.push_back(0);
                for (const auto& pat_set : lls) {
                    for (unsigned n_pat : pat_set) { lls_list.push_back(n_pat); }
                    lls_idx.push_back(static_cast<int>(lls_list.size()));
                }
                outputArray(ofile, "int", "lls_idx", lls_idx.begin(), lls_idx.end());
                outputArray(ofile, "int", "lls_list", lls_list.begin(), lls_list.end());
            }
            outputLexEngine(ofile, eng_info);
        } else {
            logger::error().format("could not open output file `{}`", analyzer_file_name);
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal().format("exception caught: {}", e.what()); }
    return -1;
}
