#!/bin/bash -e
LEXEGEN_PATH=${LEXEGEN_PATH:-lexegen}
PARSEGEN_PATH=${PARSEGEN_PATH:-parsegen}
$LEXEGEN_PATH src/lex.lex --header-file=src/lex_defs.h --outfile=src/lex_analyzer.inl
$PARSEGEN_PATH src/regex.gr --header-file=src/parser_defs.h --outfile=src/parser_analyzer.inl
