#include "dfabld.h"
#include "lexer.h"
#include "node.h"
#include "reparser.h"
#include "valset.h"

#include <fstream>

///////////////////////////////////////////////////////////////////////////////
// SmartDefs construction/destruction

class SmartDefs : public std::map<std::string, Node*> {
 public:
    SmartDefs() : map<std::string, Node*>::map() {}
    ~SmartDefs() {
        const_iterator it = begin();
        while (it != end()) {
            it->second->deleteTree();
            it++;
        }
    }
};

///////////////////////////////////////////////////////////////////////////////
// Global functions

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
        "    } while (symb != 0);",
        "",
        "    // Unroll downto last accepting state",
        "    while (!data.state_stack.empty()) {",
        "        int pat = accept[data.state_stack.back()];",
        "        if (pat >= 0) {",
        "            if (pat & 0x8000) {",
        "                pat ^= 0x8000;",
        "                do {",
        "                    state = data.state_stack.back();",
        "                    for (int i = lls_idx[state]; i < lls_idx[state + 1]; ++i) {",
        "                        if (lls_list[i] == pat) { return pat; }",
        "                    }",
        "                    *(--data.unread_text) = data.text[--data.pat_length];",
        "                    data.state_stack.pop_back();",
        "                } while (!data.state_stack.empty());",
        "            }",
        "            return pat;",
        "        }",
        "        *(--data.unread_text) = data.text[--data.pat_length];",
        "        data.state_stack.pop_back();",
        "    }",
        "",
        "    // Default pattern",
        "    data.text[data.pat_length++] = *data.unread_text++;",
        "    return -1;",
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

int syntaxError(int line_no) {
    std::cerr << std::endl << "****Error(" << line_no << "): syntax." << std::endl;
    return -1;
}

int scAlreadyDefError(int line_no, const std::string& sc_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): start condition '" << sc_id << "' is already defined." << std::endl;
    return -1;
}

int regDefAlreadyDefError(int line_no, const std::string& reg_def_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): regular definition '" << reg_def_id << "' is already defined."
              << std::endl;
    return -1;
}

int patIdAlreadyUsed(int line_no, const std::string& pattern_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): pattern identifier '" << pattern_id << "' is already used."
              << std::endl;
    return -1;
}

int undefScIdError(int line_no, const std::string& sc_id) {
    std::cerr << std::endl
              << "****Error(" << line_no << "): undefined start condition identifier '" << sc_id << "'." << std::endl;
    return -1;
}

int regExprError(int line_no, const std::string& reg_expr, std::string::size_type pos) {
    std::cerr << std::endl << "****Error(" << line_no << "): regular expression:" << std::endl;
    std::cerr << "    " << reg_expr.substr(0, pos) << "<=ERROR";
    if (pos < reg_expr.size()) { std::cerr << "  " << reg_expr.substr(pos); }
    std::cerr << "." << std::endl;
    return -1;
}

///////////////////////////////////////////////////////////////////////////////
// Main function

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

        SmartDefs definitions;
        int last_sc_no = 1;
        std::map<std::string, int> sc_ids;
        std::map<std::string, int> pattern_ids;
        std::map<std::string, std::string> options;
        sc_ids.emplace("initial", 0);  // Add initial start condition

        Lexer lexer(ifile);
        int tt = 0;

        // Load definitions
        while (1) {
            tt = lexer.lex();
            if (tt == tt_start) {  // Start condition definition
                tt = lexer.lex();
                if (tt != tt_id) { return syntaxError(lexer.getLineNo()); }

                std::string sc_id = lexer.getVal<std::string>();
                auto result = sc_ids.emplace(sc_id, last_sc_no);
                if (!result.second) { return scAlreadyDefError(lexer.getLineNo(), sc_id); }
                last_sc_no++;
            } else if (tt == tt_option) {  // Option
                tt = lexer.lex();
                if (tt != tt_id) { return syntaxError(lexer.getLineNo()); }
                std::string opt_id = lexer.getVal<std::string>();
                tt = lexer.lex();
                if (tt != '=') { return syntaxError(lexer.getLineNo()); }
                tt = lexer.lex();
                if (tt != tt_string) { return syntaxError(lexer.getLineNo()); }
                options.emplace(opt_id, lexer.getVal<std::string>());
            } else if (tt == tt_id) {  // Regular definition
                std::string reg_def_id = lexer.getVal<std::string>();
                lexer.enterRegExprMode();

                tt = lexer.lex();
                if (tt != tt_reg_expr) { return syntaxError(lexer.getLineNo()); }
                std::string reg_expr = lexer.getVal<std::string>();

                REParser re_parser;
                Node* syn_tree = re_parser.parse(definitions, reg_expr);
                if (!syn_tree) { return regExprError(lexer.getLineNo(), reg_expr, re_parser.getErrorPos()); }
                auto result = definitions.emplace(reg_def_id, syn_tree);
                if (!result.second) { return regDefAlreadyDefError(lexer.getLineNo(), reg_def_id); }
                lexer.popMode();
            } else if (tt == tt_sep) {
                break;
            } else {
                return syntaxError(lexer.getLineNo());
            }
        }

        DfaBuilder dfa_builder;
        dfa_builder.setCaseSensitive(!case_insensitive);
        dfa_builder.setScCount(last_sc_no);
        // Load patterns
        while (1) {
            tt = lexer.lex();
            if (tt == tt_id) {
                // Get pattern identifier
                std::string pattern_id = lexer.getVal<std::string>();
                auto result = pattern_ids.emplace(pattern_id, dfa_builder.getPatternCount());
                if (!result.second) { return patIdAlreadyUsed(lexer.getLineNo(), pattern_id); }

                // Load pattern
                ValueSet sc;
                lexer.enterScListMode();
                tt = lexer.lex();
                if (tt == tt_sc_list_begin) {
                    lexer.popMode();
                    // Parse start conditions
                    while (1) {
                        tt = lexer.lex();
                        if (tt == tt_id) {
                            std::string sc_id = lexer.getVal<std::string>();
                            // Find start condition
                            auto sc_it = sc_ids.find(sc_id);
                            if (sc_it == sc_ids.end()) { return undefScIdError(lexer.getLineNo(), sc_id); }
                            // Add start condition
                            sc.addValue(sc_it->second);
                        } else if (tt == '>') {
                            break;
                        } else {
                            return syntaxError(lexer.getLineNo());
                        }
                    }
                    lexer.enterRegExprMode();
                    tt = lexer.lex();
                } else {
                    sc.addValues(0, last_sc_no - 1);
                }

                Node* syn_tree = 0;
                if (tt == tt_reg_expr) {
                    std::string pattern = lexer.getVal<std::string>();
                    REParser re_parser;
                    syn_tree = re_parser.parse(definitions, pattern);
                    if (!syn_tree) { return regExprError(lexer.getLineNo(), pattern, re_parser.getErrorPos()); }
                } else if (tt == tt_eof_expr) {
                    syn_tree = new SymbNode(0);  // Add <<EOF>> pattern
                } else {
                    return syntaxError(lexer.getLineNo());
                }
                dfa_builder.addPattern(syn_tree, sc);
                lexer.popMode();
            } else if ((tt == 0) || (tt == tt_sep)) {
                break;
            } else {
                return syntaxError(lexer.getLineNo());
            }
        }

        // Build lexer
        std::vector<std::vector<int>> Dtran;
        std::vector<int> accept;
        std::vector<ValueSet> lls;
        std::cout << "Building lexer..." << std::endl;
        dfa_builder.build(Dtran, accept, lls);
        if (optimization_level > 0) {
            std::cout << "Optimizing states..." << std::endl;
            dfa_builder.optimize(Dtran, accept, lls);
        }

        std::cout << "Success. States count = " << Dtran.size() << "." << std::endl;

        if (std::ofstream ofile(defs_file_name); ofile) {
            ofile << "// Lexegen autogenerated definition file - do not edit!" << std::endl;
            if (!pattern_ids.empty()) {
                ofile << std::endl << "enum {" << std::endl;
                for (const auto& pat : pattern_ids) {
                    ofile << "    pat_" << pat.first << " = " << pat.second << "," << std::endl;
                }
                ofile << "};" << std::endl;
            }
            if (!sc_ids.empty()) {
                ofile << std::endl << "enum {" << std::endl;
                for (const auto& sc : sc_ids) {
                    ofile << "    sc_" << sc.first << " = " << sc.second << "," << std::endl;
                }
                ofile << "};" << std::endl;
            }
            outputLexDefs(ofile);
        } else {
            std::cerr << "lexegen: cannot open output file `" << defs_file_name << "`." << std::endl;
        }

        if (std::ofstream ofile(analyzer_file_name); ofile) {
            ofile << "// Lexegen autogenerated analyzer file - do not edit!" << std::endl;
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
                std::cout << "Compressing tables..." << std::endl;
                std::vector<int> symb2meta, def, base, next, check;
                dfa_builder.compressDtran(Dtran, symb2meta, def, base, next, check);
                outputArray(ofile, "symb2meta", symb2meta.begin(), symb2meta.end());
                outputArray(ofile, "def", def.begin(), def.end());
                outputArray(ofile, "base", base.begin(), base.end());
                outputArray(ofile, "next", next.begin(), next.end());
                outputArray(ofile, "check", check.begin(), check.end());
            }

            std::vector<int> lls_idx, lls_list;
            lls_idx.push_back(0);
            for (size_t i = 0; i < lls.size(); ++i) {
                int pat_no = lls[i].getFirstValue();
                while (pat_no != -1) {
                    lls_list.push_back(pat_no);
                    pat_no = lls[i].getNextValue(pat_no);
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
