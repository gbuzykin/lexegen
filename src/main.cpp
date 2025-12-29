#include "dfa_builder.h"
#include "parser.h"

#include <uxs/algorithm.h>
#include <uxs/cli/parser.h>
#include <uxs/io/filebuf.h>

#include <exception>

#define XSTR(s) STR(s)
#define STR(s)  #s

template<typename Iter>
void outputData(uxs::iobuf& outp, Iter from, Iter to, std::size_t ntab = 0) {
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
    uxs::print(outp, "\nstatic {} {}", state_type, array_name);
    if (from == to) {
        uxs::print(outp, "[1] = {{ 0 }};\n");
    } else {
        uxs::print(outp, "[{}] = {{\n", std::distance(from, to));
        outputData(outp, from, to, 4);
        uxs::print(outp, "}};\n");
    }
}

struct EngineInfo {
    int compress_level = 2;
    bool has_trailing_context = false;
    bool has_left_nl_anchoring = false;
    std::string_view state_type{"int"};
};

void outputLexEngine(uxs::iobuf& outp, const EngineInfo& info) {
    static constexpr std::string_view text0[] = {
        "static int lex(const char* first, const char* last, {0}** p_sptr, size_t* p_llen, int flags) {{",
        "    {0}* sptr = *p_sptr;",
        "    {0}* sptr0 = sptr - *p_llen;",
        "    {0} state = {1};",
        "    while (first != last) {{ /* Analyze till transition is impossible */",
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
        "    if ((flags & flag_has_more) || sptr == sptr0) {",
        "        *p_sptr = sptr;",
        "        *p_llen = (size_t)(sptr - sptr0);",
        "        return err_end_of_input;",
        "    }",
        "unroll:",
        "    *p_sptr = sptr0;",
        "    while (sptr != sptr0) { /* Unroll down to last accepting state */",
    };
    static constexpr std::string_view text3_any_has_trail_context[] = {
        "        int n_pat = accept[(state = *(sptr - 1))];",
        "        if (n_pat > 0) {",
        "            enum { trailing_context_flag = 1, flag_count = 1 };",
        "            int i;",
        "            if (!(n_pat & trailing_context_flag)) {",
        "                *p_llen = (size_t)(sptr - sptr0);",
        "                return n_pat >> flag_count;",
        "            }",
        "            n_pat >>= flag_count;",
        "            do {",
        "                for (i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                    if (lls_list[i] == n_pat) {",
        "                        *p_llen = (size_t)(sptr - sptr0);",
        "                        return n_pat;",
        "                    }",
        "                }",
        "                state = *(--sptr - 1);",
        "            } while (sptr != sptr0);",
    };
    static constexpr std::string_view text3[] = {
        "        int n_pat = accept[*(sptr - 1)];",
        "        if (n_pat > 0) {",
    };
    static constexpr std::string_view text4[] = {
        "            *p_llen = (size_t)(sptr - sptr0);",
        "            return n_pat;",
        "        }",
        "        --sptr;",
        "    }",
        "    *p_llen = 1; /* Accept at least one symbol as default pattern */",
        "    return predef_pat_default;",
        "}",
    };
    outp.put('\n');
    for (const auto& l : text0) {
        uxs::print(
            outp, uxs::runtime_format{l}, info.state_type,
            info.has_left_nl_anchoring ? "(*(sptr - 1) << 1) + ((flags & flag_at_beg_of_line) ? 1 : 0)" : "*(sptr - 1)")
            .put('\n');
    }
    if (info.compress_level == 0) {
        for (const auto& l : text1_compress0) { outp.write(l).put('\n'); }
    } else if (info.compress_level == 1) {
        for (const auto& l : text1_compress1) { outp.write(l).put('\n'); }
    } else {
        for (const auto& l : text1) { outp.write(l).put('\n'); }
    }
    for (const auto& l : text2) { outp.write(l).put('\n'); }
    if (info.has_trailing_context) {
        for (const auto& l : text3_any_has_trail_context) { outp.write(l).put('\n'); }
    } else {
        for (const auto& l : text3) { outp.write(l).put('\n'); }
    }
    for (const auto& l : text4) { outp.write(l).put('\n'); }
}

//---------------------------------------------------------------------------------------

int main(int argc, char** argv) {
    try {
        bool case_insensitive = false;
        bool use_int8_if_possible = false;
        bool show_help = false, show_version = false;
        int optimization_level = 1;
        std::string input_file_name;
        std::string analyzer_file_name("lex_analyzer.inl");
        std::string defs_file_name("lex_defs.h");
        EngineInfo eng_info;
        auto cli = uxs::cli::command(argv[0])
                   << uxs::cli::overview("A tool for regular-expression based lexical analyzer generation")
                   << uxs::cli::value("file", input_file_name)
                   << (uxs::cli::option({"-o", "--outfile="}) & uxs::cli::value("<file>", analyzer_file_name)) %
                          "Place the output analyzer into <file>."
                   << (uxs::cli::option({"--header-file="}) & uxs::cli::value("<file>", defs_file_name)) %
                          "Place the output definitions into <file>."
                   << uxs::cli::option({"--no-case"}).set(case_insensitive) % "Build case insensitive analyzer."
                   << (uxs::cli::option({"--compress"}) & uxs::cli::value("<n>", eng_info.compress_level)) %
                          "Set compression level to <n>:\n"
                          "    0 - do not compress analyzer table, do not use `meta` table;\n"
                          "    1 - do not compress analyzer table;\n"
                          "    2 - Default compression."
                   << uxs::cli::option({"--use-int8-if-possible"}).set(use_int8_if_possible) %
                          "Use `int8_t` instead of `int` for states if state count is < 128."
                   << (uxs::cli::option({"-O"}) & uxs::cli::value("<n>", optimization_level)) %
                          "Set optimization level to <n>:\n"
                          "    0 - Do not optimize analyzer states;\n"
                          "    1 - Default analyzer optimization."
                   << uxs::cli::option({"-h", "--help"}).set(show_help) % "Display this information."
                   << uxs::cli::option({"-V", "--version"}).set(show_version) % "Display version.";

        auto parse_result = cli->parse(argc, argv);
        if (show_help) {
            uxs::stdbuf::out().write(parse_result.node->get_command()->make_man_page(uxs::cli::text_coloring::colored));
            return 0;
        } else if (show_version) {
            uxs::println(uxs::stdbuf::out(), "{}", XSTR(VERSION));
            return 0;
        } else if (parse_result.status != uxs::cli::parsing_status::ok) {
            switch (parse_result.status) {
                case uxs::cli::parsing_status::unknown_option: {
                    logger::fatal().println("unknown command line option `{}`", argv[parse_result.argc_parsed]);
                } break;
                case uxs::cli::parsing_status::invalid_value: {
                    if (parse_result.argc_parsed < argc) {
                        logger::fatal().println("invalid command line argument `{}`", argv[parse_result.argc_parsed]);
                    } else {
                        logger::fatal().println("expected command line argument after `{}`",
                                                argv[parse_result.argc_parsed - 1]);
                    }
                } break;
                case uxs::cli::parsing_status::unspecified_value: {
                    if (input_file_name.empty()) { logger::fatal().println("no input file specified"); }
                } break;
                default: break;
            }
            return -1;
        }

        uxs::filebuf ifile(input_file_name.c_str(), "r");
        if (!ifile) {
            logger::fatal().println("could not open input file `{}`", input_file_name);
            return -1;
        }

        Parser parser(ifile, input_file_name);
        if (!parser.parse()) { return -1; }

        DfaBuilder dfa_builder(input_file_name);
        const auto& start_conditions = parser.getStartConditions();

        unsigned n_pat = 0;
        for (auto& pat : parser.getPatterns()) { dfa_builder.addPattern(std::move(pat.syn_tree), ++n_pat, pat.sc); }

        // Build analyzer
        logger::info(input_file_name).println("\033[1;34mbuilding analyzer...\033[0m");
        dfa_builder.build(static_cast<unsigned>(start_conditions.size()), case_insensitive);

        std::size_t state_sz = sizeof(int);
        if (use_int8_if_possible && dfa_builder.getDtran().size() < 128) {
            eng_info.state_type = "int8_t", state_sz = 1;
        }

        logger::info(input_file_name)
            .println(" - transition table size: {} bytes",
                     dfa_builder.getMetaCount() * dfa_builder.getDtran().size() * state_sz);
        logger::info(input_file_name).println("\033[1;32mdone\033[0m");

        if (optimization_level > 0) {
            logger::info(input_file_name).println("\033[1;34moptimizing states...\033[0m");
            dfa_builder.optimize();
            if (use_int8_if_possible && dfa_builder.getDtran().size() < 128) {
                eng_info.state_type = "int8_t", state_sz = 1;
            }

            logger::info(input_file_name)
                .println(" - transition table size: {} bytes",
                         dfa_builder.getMetaCount() * dfa_builder.getDtran().size() * state_sz);
            logger::info(input_file_name).println("\033[1;32mdone\033[0m");
        }

        if (uxs::filebuf ofile(defs_file_name.c_str(), "w"); ofile) {
            uxs::print(ofile, "/* Lexegen autogenerated definition file - do not edit! */\n");
            uxs::print(ofile, "/* clang-format off */\n");
            uxs::print(ofile, "\nenum {{\n");
            uxs::print(ofile, "    flag_has_more = 1,\n");
            uxs::print(ofile, "    flag_at_beg_of_line = 2\n");
            uxs::print(ofile, "}};\n");
            uxs::print(ofile, "\nenum {{\n");
            uxs::print(ofile, "    err_end_of_input = -1,\n");
            uxs::print(ofile, "    predef_pat_default = 0,\n");
            for (const auto& pat : parser.getPatterns()) { uxs::print(ofile, "    pat_{},\n", pat.id); }
            uxs::print(ofile, "    total_pattern_count\n");
            uxs::print(ofile, "}};\n");
            if (!start_conditions.empty()) {
                uxs::print(ofile, "\nenum {{\n");
                if (start_conditions.size() > 1) {
                    uxs::print(ofile, "    sc_{} = 0,\n", start_conditions[0]);
                    for (std::size_t i = 1; i < start_conditions.size() - 1; ++i) {
                        uxs::print(ofile, "    sc_{},\n", start_conditions[i]);
                    }
                    uxs::print(ofile, "    sc_{}\n", start_conditions[start_conditions.size() - 1]);
                } else {
                    uxs::print(ofile, "    sc_{} = 0\n", start_conditions[0]);
                }
                uxs::print(ofile, "}};\n");
            }
        } else {
            logger::error().println("could not open output file `{}`", defs_file_name);
        }

        if (uxs::filebuf ofile(analyzer_file_name.c_str(), "w"); ofile) {
            uxs::print(ofile, "/* Lexegen autogenerated analyzer file - do not edit! */\n");
            uxs::print(ofile, "/* clang-format off */\n");
            const auto& symb2meta = dfa_builder.getSymb2Meta();
            const auto& Dtran = dfa_builder.getDtran();
            if (eng_info.compress_level > 0) {
                outputArray(ofile, "uint8_t", "symb2meta", symb2meta.begin(), symb2meta.end());
                if (eng_info.compress_level == 1) {
                    if (!Dtran.empty()) {
                        std::vector<int> dtran_data;
                        int dtran_width = dfa_builder.getMetaCount();
                        dtran_data.reserve(dtran_width * Dtran.size());
                        for (std::size_t j = 0; j < Dtran.size(); ++j) {
                            std::copy_n(Dtran[j].data(), dtran_width, std::back_inserter(dtran_data));
                        }
                        uxs::print(ofile, "\nenum {{ dtran_width = {} }};\n", dtran_width);
                        outputArray(ofile, eng_info.state_type, "Dtran", dtran_data.begin(), dtran_data.end());
                    }
                } else {
                    std::vector<int> def, base, next, check;
                    logger::info(input_file_name).println("\033[1;34mcompressing tables...\033[0m");
                    dfa_builder.makeCompressedDtran(def, base, next, check);

                    logger::info(input_file_name)
                        .println(" - total compressed transition table size: {} bytes",
                                 (def.size() + next.size() + check.size()) * state_sz + base.size() * sizeof(int));
                    logger::info(input_file_name).println("\033[1;32mdone\033[0m");

                    outputArray(ofile, eng_info.state_type, "def", def.begin(), def.end());
                    outputArray(ofile, "int", "base", base.begin(), base.end());
                    outputArray(ofile, eng_info.state_type, "next", next.begin(), next.end());
                    outputArray(ofile, eng_info.state_type, "check", check.begin(), check.end());
                }
            } else if (!Dtran.empty()) {
                std::vector<int> dtran_data;
                dtran_data.reserve(256 * Dtran.size());
                for (std::size_t j = 0; j < Dtran.size(); ++j) {
                    uxs::transform(symb2meta, std::back_inserter(dtran_data),
                                   [&row = Dtran[j]](int meta) { return row[meta]; });
                }
                outputArray(ofile, eng_info.state_type, "Dtran", dtran_data.begin(), dtran_data.end());
            }

            std::vector<int> accept = dfa_builder.getAccept();
            eng_info.has_left_nl_anchoring = dfa_builder.hasPatternsWithLeftNlAnchoring();

            for (unsigned state = 0; state < accept.size(); ++state) {
                if (unsigned n_pat = accept[state]; n_pat > 0) {
                    if (dfa_builder.isPatternWithTrailingContext(n_pat)) { eng_info.has_trailing_context = true; }
                }
            }

            if (eng_info.has_trailing_context) {
                for (unsigned state = 0; state < accept.size(); ++state) {
                    if (unsigned n_pat = accept[state]; n_pat > 0) {
                        enum { kTrailingContextFlag = 1, kFlagCount = 1 };
                        accept[state] <<= kFlagCount;
                        if (dfa_builder.isPatternWithTrailingContext(n_pat)) { accept[state] |= kTrailingContextFlag; }
                    }
                }
            }

            outputArray(ofile, "int", "accept", accept.begin(), accept.end());

            if (eng_info.has_trailing_context) {
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
            logger::error().println("could not open output file `{}`", analyzer_file_name);
        }

        return 0;
    } catch (const std::exception& e) { logger::fatal().println("exception caught: {}", e.what()); }
    return -1;
}
