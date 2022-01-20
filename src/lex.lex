%start string
%start regex
%start sset
%start regex_br
%start sc_list

dig     [0-9]
odig    [0-7]
hdig    [0-9a-fA-F]
letter  [a-zA-Z]
num     {dig}+
id      ({letter}|_)({letter}|{dig}|_)*
ws      [ \t]+

%%

sc_list_begin    <sc_list> < / ({ws}?{id})+{ws}?>

escape_oct    <string regex sset sc_list> \\{odig}{1,3}
escape_hex    <string regex sset sc_list> \\x{hdig}{1,2}
escape_a      <string regex sset sc_list> \\a
escape_b      <string regex sset sc_list> \\b
escape_f      <string regex sset sc_list> \\f
escape_r      <string regex sset sc_list> \\r
escape_n      <string regex sset sc_list> \\n
escape_t      <string regex sset sc_list> \\t
escape_v      <string regex sset sc_list> \\v
escape_other  <string regex sset sc_list> \\.

string_seq    <string> [^\n\\"]+
string_close  <string> \"

regex_sset_seq      <sset> [^\n\\\]\-]+
regex_sset_range    <sset> -
regex_sset_close    <sset> ]

whitespace    {ws}

regex_sset       <regex sc_list> \[
regex_sset_inv   <regex sc_list> \[^
regex_dot        <regex sc_list> \.
regex_eof_symb   <regex sc_list> "<<EOF>>"
regex_id         <regex sc_list> \{{id}}
regex_br         <regex sc_list> \{
regex_nl         <regex sc_list regex_br> \n
regex_br_close   <regex_br> }
regex_symb       <regex sc_list> [^"|/*+?()]

start    <initial> "%start"
option   <initial> "%option"
sep      <initial> "%%"
id       <initial> {id}
num      <initial regex_br> {num}
comment  <initial> #

string     \"
other      .
nl         \n

%%
