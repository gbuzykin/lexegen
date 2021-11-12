// Lexegen autogenerated definition file - do not edit!

enum {
    predef_pat_default = 0,
    pat_sc_list_begin,
    pat_escape_oct,
    pat_escape_hex,
    pat_escape_a,
    pat_escape_b,
    pat_escape_f,
    pat_escape_r,
    pat_escape_n,
    pat_escape_t,
    pat_escape_v,
    pat_escape_other,
    pat_string_seq,
    pat_string_close,
    pat_regex_sset_seq,
    pat_regex_sset_range,
    pat_regex_sset_close,
    pat_whitespace,
    pat_regex_sset,
    pat_regex_sset_inv,
    pat_regex_dot,
    pat_regex_eof_symb,
    pat_regex_id,
    pat_regex_br,
    pat_regex_nl,
    pat_regex_br_close,
    pat_regex_symb,
    pat_unterminated_token,
    pat_start,
    pat_option,
    pat_sep,
    pat_id,
    pat_num,
    pat_comment,
    pat_string,
    pat_other,
    pat_nl,
    pat_eof,
};

enum {
    sc_initial = 0,
    sc_string,
    sc_regex,
    sc_sset,
    sc_regex_br,
    sc_sc_list,
};

struct CtxData {
    char* text_last = nullptr;
    char* text_unread = nullptr;
    char* text_boundary = nullptr;
};
