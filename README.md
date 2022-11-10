# Regular Expression Based Lexical Analyzer Generator

This tool generates lexical analyzer based on a list of pattern descriptions in terms of regular
expressions as an input.  Its input file is very similar to the input of standard
[lex](https://en.wikipedia.org/wiki/Lex_(software)) tool by structure and syntax.  But contrary to
*lex* instead of generating full analyzer with pattern matching inlined handlers `lexegen` generates
only tables and pattern matching C-compliant function as files, which can be included into the rest
analyzer's implementation.

This README file briefly describes this tool and can be not up-do-date, it also gives guidelines how
to use this stuff.  For more detailed information see
[wiki](https://github.com/gbuzykin/lexegen/wiki) pages.

## A Simple Usage Example

Assume that we already have built this tool as `lexegen` executable, and we want to create a simple
lexical analyzer, which extracts the following tokens from an input buffer: C-identifiers, unsigned
integers, floating point numbers and C-strings.  Also we want to skip C-comments and whitespace.

Here is a source input file `test.lex` for this analyzer:

```lex
# This is a simple lexical analyzer.

# This section describes named regular expression definitions, which can be
# expanded in further expressions using `{name}`.
# Each line has the form: <definition-name> <regex>.
# Note that only end-of-line character terminates each regular expression, so
# a comment can't be placed on the same line with the expression.

# Start conditions are also can be defined in this section using the form: %start <sc-name>.
# By default only one `initial` start condition is implicitly defined.

# a digit
dig       [0-9]

# a letter
letter    [a-zA-Z]

# unsigned integer
int       {dig}+

# a C-identifier
id        ({letter}|_)({letter}|{dig}|_)*

# a floating-point number
real      (({dig}+(\.{dig}*)?)|(\.{dig}+))((e|E)(\+|\-)?{dig}+)?

# white-space character
ws        [ \t\r\n]

# C-style comment
comment    \/\* ( [^*\\] | \\(.|\n) | \*+([^*/\\]|\\(.|\n)) )* \*+\/

# a C-string
string     \" ( [^"\\] | \\(.|\n) )* \"

%% # section separator

# This section describes patterns as regular expressions.
# Each line has the form: <pattern-name> <regex>,
# or the form: <pattern-name> <s1, s2, ...> <regex> with start condition list.
# If start condition list is not specified, the pattern participates in all
# defined start conditions.

comment     {comment}
string      {string}
id          {id}
int         {int}
real        {real}
ws          {ws}+
other       .

%% # mandatory end of section
```

Then, in order to generate our lexical analyzer let's issue the following:

```bash
$ ./lexegen test.lex
test.lex: info: building analyzer...
test.lex: info:  - pattern count: 7
test.lex: info:  - S-state count: 1
test.lex: info:  - position count: 44
test.lex: info:  - meta-symbol count: 13
test.lex: info:  - state count: 24
test.lex: info:  - transition table size: 1248 bytes
test.lex: info: done
test.lex: info: optimizing states...
test.lex: info:  - state group count: 19
test.lex: info:  - dead group count: 0
test.lex: info:  - new state count: 19
test.lex: info:  - transition table size: 988 bytes
test.lex: info: done
test.lex: info: compressing tables...
test.lex: info:  - total compressed transition table size: 656 bytes
test.lex: info: done
```

As the result two files with default names `lex_defs.h` and `lex_analyzer.inl` are generated.  If it
is needed to specify the names explicitly the following should be issued:

```bash
./lexegen test.lex -o <new-analyzer-file-name> --header-file=<new-defs-file-name>
```

File `lex_defs.h` contains numerical identifiers for patterns and start conditions (or start
analyzer states).  Only one `sc_initial` start condition is defined for our example.

File `lex_analyzer.inl` contains necessary tables and `lex()` function implementation, defined as
`static`.  This function has the following prototype:

```c
static int lex(const char* first, const char* last, int** p_sptr, unsigned* p_llen, int flags);
```

where:

- `first` - pointer to the first character of input buffer
- `last` - pointer to the character after the last character of input buffer
- `p_sptr` - pointer to current user-provided DFA stack pointer
- `p_llen` - pointer to current matched lexeme length
- `flags` - can be a bitwise `or` of the following flags:
  - `flag_has_more` not to treat the end of input buffer as the end of input sequence
  - `flag_at_beg_of_line` to tell analyzer that it is at the beginning of a new line (activates
    patterns starting with `^`)

returns: matched pattern identifier

## How It Works

The analyzer always tries to match one of patterns to the next longest possible chunk of text.  If
more than one patterns fit this chunk, then pattern priority is used.  The first pattern has the
highest priority, and the last one has the lowest.

The starting state must be on the top of user-provided DFA stack before calling `lex()`.  Current
stack pointer `*p_sptr` must point to the position *after* the last state (the first free stack
cell)

After the function returns and the pattern is matched the stack pointer `*p_sptr` is the same as
before calling the function.

In case if `flag_has_more` is not specified the stack pointer `*p_sptr` is *always* the same as
before the calling.  If no user-provided pattern is matched it skips one character and returns
`predef_pat_default`.  So, the function always matches at least one character from the input buffer.
If input buffer is empty, it returns `err_end_of_input` (negative).

If `flag_has_more` is not specified the analyzer leaves the stack pointer `*p_sptr` as it is and
returns `err_end_of_input` in case of reaching the end of input buffer.  It gives a chance to add
more characters to the input sequence and call the `lex()` function again to continue the analysis.
Already analyzed part of input is no more needed.  All necessary information is in the state stack.
In theory, the old input buffer can be freed, but in practice it will likely be needed in future to
concatenate the full lexeme.

User-provided DFA stack must have the same count of free cells as the length of the longest possible
lexeme, or `last - first` if we must deal with lexemes of arbitrary length.  The other approach is
to trim input buffer in case if it is longer than free range in user-provided DFA stack and to
facilitate `flag_has_more` flag.  The probable code will look like this:

```cpp
unsigned llen = 0;
auto state_stack = std::make_unique<int[]>(kInitialStackSize);
int* slast = state_stack.data() + kInitialStackSize;
int* sptr = state_stack.data();
*sptr++ = lex_detail::sc_initial;
...
const char* trimmed_first = first;
while (true) {
    bool stack_limitation = false;
    const char* trimmed_last = last;
    if (slast - sptr < last - trimmed_first) {
        trimmed_last = first + static_cast<ptrdiff_t>(sptr, slast);
        stack_limitation = true;
    }
    int pat = lex_detail::lex(trimmed_first, trimmed_last, &sptr, &llen, stack_limitation);
    if (pat >= lex_detail::predef_pat_default) {
        break; // the full lexeme is obtained
    } else if (stack_limitation) {
        // enlarge state stack and continue analysis
        <... enlarge state stack ...>
        trimmed_first = trimmed_last;
    } else {
        <...  end of sequence ...>
    }
}
...
```

Note, that it is convenient to use the state stack also as a start condition stack.

## User Code Example

The probable code for the above example can be something like this:

```cpp
...
namespace lex_detail {
#include "lex_defs.h"
#include "lex_analyzer.inl"
}
...
int main() {
    ...
    const char* first = .... ; // the first char pointer
    const char* last = .... ; // after the last char pointer
    ...
    unsigned llen = 0;
    auto state_stack = std::make_unique<int[]>(kInitialStackSize);
    int* slast = state_stack.data() + kInitialStackSize;
    int* sptr = state_stack.data();
    *sptr++ = lex_detail::sc_initial;
    ...
    while (true) {
        first += llen;
        const char* trimmed_first = first;
        while (true) {
            bool stack_limitation = false;
            const char* trimmed_last = last;
            if (slast - sptr < last - trimmed_first) {
                trimmed_last = trimmed_first + static_cast<ptrdiff_t>(sptr, slast);
                stack_limitation = true;
            }
            int pat = lex_detail::lex(trimmed_first, trimmed_last, &sptr, &llen, stack_limitation);
            if (pat >= lex_detail::predef_pat_default) {
                break; // the full lexeme is obtained
            } else if (stack_limitation) {
                // enlarge state stack and continue analysis
                <... enlarge state stack ...>
                trimmed_first = trimmed_last;
            } else {
                return; // end of sequence
           }
        }
        switch (pat) {
        lex_detail::pat_comment: .....; break;
        lex_detail::pat_string: .....; break;
        lex_detail::pat_id: .....; break;
        lex_detail::pat_int: .....; break;
        lex_detail::pat_real: .....; break;
        lex_detail::pat_ws: .....; break;
        pat_other::pat_other: .....; break;
        }
    }
}
```

## Regular Expression Syntax

These rules are used to compose regular expressions for definitions or patterns:

- `x` if this character is not a ' ', FF, CR, HT, or VT matches the character 'x'.
- `.` any character (byte) except newline (NL)
- `[xyz]` a "character class"; in this case, matches 'x', 'y', or 'z'
- `[abj-oZ]` a "character class" with a range in it; matches an 'a', a 'b', any letter from 'j'
  through 'o', or a 'Z'
- `[^A-Z]` a "negated character class", i.e., any character but those in the class.  In this case,
  any character EXCEPT an uppercase letter.
- `[^A-Z\n]` any character EXCEPT an uppercase letter or a newline
- `{name}` the expansion of the "name" definition (see above)
- `"[xyz]\"foo"` the literal string: `[xyz]"foo`
- `\X` if X is an 'a', 'b', 'f', 'n', 'r', 't', or 'v', then the ANSI-C interpretation of \x.
  Otherwise, a literal 'X' (used to escape operators such as '*')
- `\123` the character with octal value 123
- `\x2a` the character with hexadecimal value 2a
- `r*` zero or more r's, where `r` is any regular expression
- `r+` one or more r's
- `r?` zero or one r's (that is, "an optional `r`")
- `r{2,5}` anywhere from two to five r's
- `r{2,}` two or more r's
- `r{,2}` no more than two r's
- `r{4}` exactly 4 r's
- `(r)` match an `r`; parentheses are used to override precedence
- `rs` the regular expression `r` followed by the regular expression `s`; called "concatenation"
- `r|s` either an `r` or an `s`
- `r/s` an `r` but only if it is followed by an `s`.  The text matched by `s` is included when
  determining whether this rule is the "longest match", but is then returned to the input.  So the
  returned lexeme is only the text matched by `r`.  This type of pattern is called "trailing
  context".  It should be the top pattern operator, UB otherwise.
- `^r` an `r`, but only at the beginning of a line (i.e., when `flag_at_beg_of_line` flag is
  specified).  It should be the first pattern operator, it is ignored otherwise.
- `!^r` an `r`, but only *not* at the beginning of a line (i.e., when `flag_at_beg_of_line` flag is
  *not* specified).  It should be the first pattern operator, it is ignored otherwise.
- `r$` an `r`, but only at the end of a line (i.e., just before a newline).  Equivalent to `r/\n`.
  Note that the end of input is not treated as a newline character, so it is not the end of a line.

In addition to characters and ranges of characters, character classes can also contain character
class expressions.  These are expressions enclosed inside `[:` and `:]` delimiters (which themselves
must appear between the `[` and `]` of the character class.  Other elements may occur inside the
character class, too.  The valid expressions are:

- `[:alnum:]` - alphanumeric characters `[A-Za-z0-9]`
- `[:alpha:]` - alphabetic characters `[A-Za-z]`
- `[:blank:]` - space and tab `[ \t]`
- `[:cntrl:]` - control characters `[\x01-\x1F\x7F]`
- `[:digit:]` - digits `[0-9]`
- `[:graph:]` - visible characters `[\x21-\x7E]`
- `[:lower:]` - lowercase letters `[a-z]`
- `[:print:]` - visible characters and the space character `[\x20-\x7E]`
- `[:punct:]` - punctuation characters ``[\]\[!"#$%&'()*+,./:;<=>?@\\^_`{|}~\-]``
- `[:space:]` - whitespace characters `[ \t\r\n\v\f]`
- `[:upper:]` - uppercase letters `[A-Z]`
- `[:xdigit:]` - hexadecimal digits `[A-Fa-f0-9]`

For example, the following character classes are all equivalent:

- `[[:alnum:]]`
- `[[:alpha:][:digit:]]`
- `[[:alpha:][0-9]]`
- `[a-zA-Z0-9]`

Note that ' ', FF, CR, HT, or VT characters (bytes) are skipped while parsing regular expressions,
use `\x20`, `\f`, `\r`, `\t`, or `\v` instead.  Also zero '\0' character (byte) is always treated as
not matchable.

## Command Line Options

```bash
$ ./lexegen --help
OVERVIEW: A tool for regular-expression based lexical analyzer generation
USAGE: ./lexegen.exe file [-o <file>] [--header-file=<file>] [--no-case] [--compress <n>]
           [--use-int8-if-possible] [-O <n>] [-h] [-V]
OPTIONS:
    -o, --outfile=<file>    Place the output analyzer into <file>.
    --header-file=<file>    Place the output definitions into <file>.
    --no-case               Build case insensitive analyzer.
    --compress <n>          Set compression level to <n>:
                                0 - do not compress analyzer table, do not use `meta` table;
                                1 - do not compress analyzer table;
                                2 - Default compression.
    --use-int8-if-possible  Use `int8_t` instead of `int` for states if state count is < 128.
    -O <n>                  Set optimization level to <n>:
                                0 - Do not optimize analyzer states;
                                1 - Default analyzer optimization.
    -h, --help              Display this information.
    -V, --version           Display version.
```

## How to Build `lexegen`

Perform these steps to build the project (in linux, for other platforms the steps are similar):

1. Clone `lexegen` repository and enter it

    ```bash
    git clone https://github.com/gbuzykin/lexegen
    cd lexegen
    ```

2. Initialize and update `uxs` submodule

    ```bash
    git submodule update --init
    ```

3. Then, compilation script should be created using `cmake` tool.  To use the default C++ compiler
   just issue (for new enough version of `cmake`)

    ```bash
    cmake -S . -B build
    ```

    or to make building scripts for debug or optimized configurations issue the following

    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE="Debug"
    ```

    or

    ```bash
    cmake -S . -B build -DCMAKE_BUILD_TYPE="Release"
    ```

4. Enter created folder `build` and run `make`

    ```bash
    cd build
    make
    ```

    to use several parallel processes (e.g. 8) for building run `make` with `-j` key

    ```bash
    make -j 8
    ```
