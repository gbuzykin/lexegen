%start string
%start reg_expr
%start sc_list

dig     [0-9]
oct_dig [0-7]
hex_dig [0-9a-fA-F]
letter  [a-zA-Z]
int     (\+?|-?){dig}+
id      ({letter}|_)({letter}|{dig}|_)*
ws      [ \t]+

%%

sc_list_begin   <sc_list> < / ({ws}?{id})+{ws}?>
eof_expr        <reg_expr sc_list> "<<EOF>>"
reg_expr_begin  <reg_expr sc_list> "" / [^ \t\n]

string_end       <string>\"
string_es_oct    <string>\\{oct_dig}{3}
string_es_hex    <string>\\x{hex_dig}{2}
string_es_a      <string>\\a
string_es_b      <string>\\b
string_es_f      <string>\\f
string_es_r      <string>\\r
string_es_n      <string>\\n
string_es_t      <string>\\t
string_es_v      <string>\\v
string_es_bslash <string>\\\\
string_es_dquot  <string>\\\"
string_cont      <string>[^\\\n\"]*
string_nl        <string>\n
string_eof       <string><<EOF>>

whitespace       {ws}

start            <initial>"%start"
option           <initial>"%option"
sep              <initial>"%%"

int              <initial>{int}
id               <initial>{id}

string_begin     \"
comment          #
other_char       .
nl               \n
eof              <<EOF>>

%%
