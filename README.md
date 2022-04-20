# Regular Expression Based Lexical Analyzer Generator

This tool generates lexical analyzer upon a list of pattern descriptions in terms of regular
expressions as an input.  Its input file is very similar to input of standard
[lex](https://en.wikipedia.org/wiki/Lex_(software)) tool by structure and syntax.  But contrary to
*lex* instead of generating full analyzer with pattern matching inlined handlers `lexegen` generates
only tables and pattern matching C++-function as files, which can be included into the rest
analyzer's implementation.

This README file briefly describes this tool and can be not up-do-date, it is also gives guidelines
how to use this stuff.  For more detailed information see
[wiki](https://github.com/gbuzykin/lexegen/wiki) pages.

## A Simple Usage Example

Assume that we already have built this tool as `lexegen` executable, and we want to create a simple
lexical analyzer, which extracts the following tokens from an input buffer: C-identifiers, unsigned
integers, floating point numbers and C-strings.  Also we want to skip C-comments and white-spaces.

Here is a source input file `test.lex` for this analyzer:

```lex
# This is a simple lexical analyzer.
# This section describes named regular expression definitions.
# Each line has the form: <definition-name>  <regex>.
# Note that only end-of-line character terminates each regular expression, so
# a comment can't be placed on the same line with the expression.

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
ws        [ \t\n]       

# C-style comment
comment    \/\* ( [^*\\] | \\(.|\n) | \*+([^*/\\]|\\(.|\n)) )* \*+\/

# A C string
string     \" ( [^"\\] | \\(.|\n) )* \"

%% # section separator

# This section describes patterns as regular expressions.
# Each line has the form: <pattern-name>  <regex>.
# The analyzer always tries to match one of patterns to the next longest possible
# chunk of text. If one or more patterns fit this chunk, then pattern priority is
# used. The first pattern has the highest priority, and the last has the lowest.

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
Building lexer...
 - pattern count: 7
 - S-state count: 1
 - position count: 44
 - meta-symbol count: 12
 - state count: 24
 - transition table size: 1152 bytes
Done.
Optimizing states...
 - state group count: 19
 - dead group count: 0
 - new state count: 19
 - transition table size: 912 bytes
Done.
Compressing tables...
 - total compressed transition table size: 648 bytes
Done.
```

As the result two files with default names `lex_defs.h` and `lex_analyzer.inl` are generated.  If it
is needed to specify the names explicitly the following should be issued:

```bash
./lexegen test.lex -h <new-defs-file-name> -o <new-analyzer-file-name>
```

File `lex_defs.h` contains numerical identifiers for patterns and start conditions (or start
analyzer state).  Only one `sc_initial` start condition is defined for our example.  The prototype
of the analyzer function is also defined in this file as

```cpp
int lex(const char* first, const char* last, std::vector<int>& state_stack, unsigned& llen, bool has_more);
```

where:

- `first` - pointer to the first character of input buffer
- `last` - pointer to the character after the last character of input buffer
- `state_stack` - reference to user-provided `std::vector<int>` used as the analyzer's DFA stack
- `llen` - where to save matched text length
- `has_more` - should be `false` to treat the end of input buffer as the end of input sequence

returns: matched pattern identifier

notes:

1. The starting state must be on the top of `state_stack` before calling `lex()`.

2. After the function returns and the pattern is matched the state of `state_stack` in the same as
   before calling the function.  In case if `has_more` is `false` the state of `state_stack` is
   *always* the same as before the calling.  The function matches at least one character from the
   input buffer (returns `predef_pat_default` if no user-provided pattern is matched), and returns
   `err_end_of_input` if input buffer is empty.

3. If `has_more` is `true`, in case of reaching the end of input buffer the analyzer leaves
   `state_stack` as it is and returns `err_end_of_input`.  It gives a chance to add more characters
   to the input sequence and call the `lex()` function again to continue the analysis.  The input
   buffer is allowed to be reallocated, but already analyzed part (of length `llen`) of the input
   buffer must be retained.

4. Hint: it is convenient to use the `state_stack` stack also as start condition (state) stack.

File `lex_analyzer.inl` contains necessary tables and `lex()` function implementation.

Summing this up, the simplest user's code can be something like this:

```cpp
#include <vector>
...
namespace lex_detail {
#include "lex_defs.h"
#include "lex_analyzer.inl"
}
...
int main() {
    ...
    const char* first = .... ; // the first char
    const char* last = .... ; // after the last char
    ...
    unsigned llen = 0;
    std::vector<int> state_stack;
    state_stack.reserve(256);
    state_stack.push_back(lex_detail::sc_initial);
    ...
    while (true) {
        first += llen;
        int pat = lex_detail::lex(first, last, state_stack, llen, false);
        if (pat == lex_detail::err_end_of_input) { break; } // end of sequence
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

## How to Build `lexegen`

Perform this steps to build the project (in linux, for other platforms the steps are similar):

1. Clone `lexegen` repository and enter it

    ```bash
    git clone https://github.com/gbuzykin/lexegen
    cd lexegen
    ```

2. Initialize and update `std-ext` submodule

    ```bash
    git submodule update --init
    ```

3. Then, compilation script should be created using `cmake` tool.  To use the default compiler just
   issue e.g.

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

4. Enter folder for building and run `make`

    ```bash
    cd build
    make
    ```

    to use several parallel processes (e.g. 8) for building run `make` with `-j` key

    ```bash
    make -j 8
    ```
