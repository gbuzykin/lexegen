%start string
%start regex
%start symb_set
%start regex_curly_braces
%start sc_list

dig     [0-9]
odig    [0-7]
hdig    [0-9a-fA-F]
letter  [a-zA-Z]
num     {dig}+
id      ({letter}|_)({letter}|{dig}|_)*
ws      [ \f\r\t\v]

%%

sc_list_begin    <sc_list> < / {ws}?({id}{ws}?)+>

escape_oct    <string regex symb_set sc_list> \\{odig}{1,3}
escape_hex    <string regex symb_set sc_list> \\x{hdig}{1,2}
escape_a      <string regex symb_set sc_list> \\a
escape_b      <string regex symb_set sc_list> \\b
escape_f      <string regex symb_set sc_list> \\f
escape_r      <string regex symb_set sc_list> \\r
escape_n      <string regex symb_set sc_list> \\n
escape_t      <string regex symb_set sc_list> \\t
escape_v      <string regex symb_set sc_list> \\v
escape_other  <string regex symb_set sc_list> \\.

string_seq    <string> [^"\\\n]+
string_close  <string> \"

regex_symb_set_seq      <symb_set> [^\]\-\\\n]+
regex_symb_set_range    <symb_set> -
regex_symb_set_close    <symb_set> ]

unexpected_nl  <string symb_set> \n

whitespace    {ws}+

regex_symb_set            <regex sc_list> \[
regex_symb_set_inv        <regex sc_list> \[\^
regex_dot                 <regex sc_list> \.
regex_id                  <regex sc_list> \{{id}}
regex_left_curly_brace    <regex sc_list> \{
regex_nl                  <regex sc_list regex_curly_braces> \n
regex_right_curly_brace   <regex_curly_braces> }
regex_symb                <regex sc_list> [^"|/*+?^$!()]

start    <initial> "%start"
option   <initial> "%option"
sep      <initial> "%%"
id       <initial> {id}
num      <initial regex_curly_braces> {num}
comment  <initial> #

nl         \n
string     \"
other      .

%%
