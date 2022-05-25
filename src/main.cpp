#include "dfabld.h"
#include "node.h"
#include "parser.h"
#include "util/io/filebuf.h"

template<typename Iter>
void outputData(util::iobuf& outp, Iter from, Iter to, size_t ntab = 0) {
    if (from == to) { return; }
    const unsigned length_limit = 120;
    std::string tab(ntab, ' '), line = tab + util::to_string(*from);
    while (++from != to) {
        auto sval = util::to_string(*from);
        if (line.length() + sval.length() + 3 > length_limit) {
            outp.write(line).put(',').endl();
            line = tab + sval;
        } else {
            line += ", " + sval;
        }
    }
    outp.write(line).endl();
}

template<typename Iter>
void outputArray(util::iobuf& outp, const std::string& array_name, Iter from, Iter to) {
    outp.endl().write("static int ").write(array_name);
    if (from == to) {
        outp.write("[1] = { 0 };").endl();
    } else {
        util::fprintln(outp, "[{}] = {{", std::distance(from, to));
        outputData(outp, from, to, 4);
        outp.write("};").endl();
    }
}

void outputLexEngine(util::iobuf& outp, bool no_compress) {
    // clang-format off
    static constexpr std::string_view text0[] = {
        "static int lex(const char* first, const char* last, int** p_sptr, unsigned* p_llen, int has_more) {",
        "    enum { kTrailContFlag = 1, kFlagCount = 1 };",
        "    int *sptr = *p_sptr, *sptr0 = sptr - *p_llen;",
        "    int state = *(sptr - 1);",
        "    while (first < last) {  // Analyze till transition is impossible",
    };
    static constexpr std::string_view text1[] = {
        "        int meta = symb2meta[static_cast<unsigned char>(*first)];",
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
        "        state = Dtran[state][symb2meta[static_cast<unsigned char>(*first)]];",
    };
    static constexpr std::string_view text2[] = {
        "        if (state < 0) { goto unroll; }",
        "        *sptr++ = state, ++first;",
        "    }",
        "    if (has_more || sptr == sptr0) {",
        "        *p_sptr = sptr;",
        "        *p_llen = static_cast<unsigned>(sptr - sptr0);",
        "        return err_end_of_input;",
        "    }",
        "unroll:",
        "    *p_sptr = sptr0;",
        "    while (sptr != sptr0) {  // Unroll down-to last accepting state",
        "        state = *(sptr - 1);",
        "        int n_pat = accept[state];",
        "        if (n_pat > 0) {",
        "            if (!(n_pat & kTrailContFlag)) {",
        "                *p_llen = static_cast<unsigned>(sptr - sptr0);",
        "                return n_pat >> kFlagCount;",
        "            }",
        "            n_pat >>= kFlagCount;",
        "            do {",
        "                for (int i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                    if (lls_list[i] == n_pat) {",
        "                        *p_llen = static_cast<unsigned>(sptr - sptr0);",
        "                        return n_pat;",
        "                    }",
        "                }",
        "                state = *(--sptr - 1);",
        "            } while (sptr != sptr0);",
        "            *p_llen = static_cast<unsigned>(sptr - sptr0);",
        "            return n_pat;",
        "        }",
        "        --sptr;",
        "    }",
        "    *p_llen = 1;  // Accept at least one symbol as default pattern",
        "    return predef_pat_default;",
        "}",
    };
    // clang-format on
    outp.endl();
    for (const auto& l : text0) { outp.write(l).endl(); }
    if (no_compress) {
        for (const auto& l : text1_no_compress) { outp.write(l).endl(); }
    } else {
        for (const auto& l : text1) { outp.write(l).endl(); }
    }
    for (const auto& l : text2) { outp.write(l).endl(); }
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
                static constexpr std::string_view text[] = {
                    "Usage: lexegen [options] file",
                    "Options:",
                    "    -o <file>      Place the output analyzer into <file>.",
                    "    -h <file>      Place the output definitions into <file>.",
                    "    --no-case      Build case insensitive analyzer.",
                    "    --no-compress  Do not compress analyzer table.",
                    "    -O0            Do not optimize analyzer.",
                    "    --help         Display this information.",
                };
                // clang-format on
                for (const auto& l : text) { util::stdbuf::out.write(l).endl(); }
                return 0;
            } else if (arg[0] != '-') {
                input_file_name = arg;
            } else {
                logger::fatal().format("unknown flag `{}`", arg);
                return -1;
            }
        }

        if (input_file_name.empty()) {
            logger::fatal().format("no input file specified");
            return -1;
        }

        util::filebuf ifile(input_file_name.c_str(), "r");
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
        dfa_builder.build(static_cast<unsigned>(start_conditions.size()), case_insensitive);
        if (optimization_level > 0) { dfa_builder.optimize(); }

        if (util::filebuf ofile(defs_file_name.c_str(), "w"); ofile) {
            ofile.write("// Lexegen autogenerated definition file - do not edit!").endl();
            ofile.endl().write("enum {").endl();
            ofile.write("    err_end_of_input = -1,").endl();
            ofile.write("    predef_pat_default = 0,").endl();
            for (size_t i = 0; i < patterns.size(); ++i) { util::fprintln(ofile, "    pat_{},", patterns[i].id); }
            ofile.write("    total_pattern_count,").endl();
            ofile.write("};").endl();
            if (!start_conditions.empty()) {
                ofile.endl().write("enum {").endl();
                util::fprintln(ofile, "    sc_{} = 0,", start_conditions[0]);
                for (size_t i = 1; i < start_conditions.size(); ++i) {
                    util::fprintln(ofile, "    sc_{},", start_conditions[i]);
                }
                ofile.write("};").endl();
            }
        } else {
            logger::error().format("could not open output file `{}`", defs_file_name);
        }

        if (util::filebuf ofile(analyzer_file_name.c_str(), "w"); ofile) {
            ofile.write("// Lexegen autogenerated analyzer file - do not edit!").endl();
            const auto& symb2meta = dfa_builder.getSymb2Meta();
            const auto& Dtran = dfa_builder.getDtran();
            outputArray(ofile, "symb2meta", symb2meta.begin(), symb2meta.end());
            if (no_compress) {
                if (!Dtran.empty()) {
                    ofile.endl();
                    util::fprintln(ofile, "static int Dtran[{}][{}] = {{", Dtran.size(), dfa_builder.getMetaCount())
                        .endl();
                    for (size_t j = 0; j < Dtran.size(); ++j) {
                        ofile.write(j == 0 ? "    {" : ", {").endl();
                        outputData(ofile, Dtran[j].begin(), Dtran[j].begin() + dfa_builder.getMetaCount(), 8);
                        ofile.write("    }");
                    }
                    ofile.endl().write("};").endl();
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
            lls_idx.reserve(lls.size() + 1);
            lls_list.reserve(lls.size());
            lls_idx.push_back(0);
            for (const auto& pat_set : lls) {
                for (unsigned n_pat : pat_set) { lls_list.push_back(n_pat); }
                lls_idx.push_back(static_cast<int>(lls_list.size()));
            }
            outputArray(ofile, "accept", accept.begin(), accept.end());
            outputArray(ofile, "lls_idx", lls_idx.begin(), lls_idx.end());
            outputArray(ofile, "lls_list", lls_list.begin(), lls_list.end());
            outputLexEngine(ofile, no_compress);
        } else {
            logger::error().format("could not open output file `{}`", analyzer_file_name);
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal().format("exception catched: {}", e.what()); }
    return -1;
}
