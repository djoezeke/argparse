# Review: Limitations and Known Issues

This document records limitations and defects found while adding the Catch2
unit test suite and microbenchmarks to the current `argparse.h` implementation.
It describes behavior as of version 0.1.0. The parser implementation was not
changed while producing this review; the tests encode both intended behavior
and current quirks.

## Verification scope

- Unit tests: `tests/Main.test.cpp`, 50 test cases and 383 assertions covering
  registration, positional/optional parsing, actions, counts, defaults,
  environment fallbacks, validation, groups, dependencies, subcommands,
  namespace conversions, help formatting, errors, and edge cases.
- Microbenchmarks: `tests/Benchmarks.test.cpp`, measuring construction,
  parsing shapes, validation, subcommands, conversions, and help output.
- Manual review of `argparse.h` (3205 lines).
- Reproductions below were run with GCC 16.2.0 (MinGW-w64, UCRT) on Windows.

## Severity summary

| Severity | Issues |
| --- | --- |
| Critical | Multi-translation-unit link failure; unusable `ARGPARSE_NO_EXCEPTIONS` mode |
| High | Dangling `Argument&`/`ArgumentGroup&`/`Section&`; positional semantics; `parse_known_args`; option token swallowing; help system defects; subcommand detection |
| Medium | Defaults/environment/implicit-value precedence; validator behavior; conversion limits; `nargs(2)` ambiguity; hidden/completion handling |
| Low | Help formatter placeholders; includes and dead code; version check; diagnostics and locale; thread safety |

## Critical issues

### 1. The header is not safe in multiple translation units

Every out-of-class member function is defined in `argparse.h` without
`inline`, for example `HelpFormatter::ColorTheme::nord()` at
`argparse.h:1153`, `HelpFormatter::format_help()` at `argparse.h:1357`, the
`ArgumentParser` members starting around `argparse.h:1654`, and the nested
namespace functions around `argparse.h:2792`.

Including the header in two `.cpp` files and linking them produces hundreds of
`multiple definition` errors:

```text
ld.exe: b.cpp:(.text+0x0): multiple definition of
`argparse::HelpFormatter::ColorTheme::nord()';
a.cpp:(.text+0x0): first defined here
```

Consequences:

- The project is advertised as header-only, but a real program with more than
  one translation unit cannot use it.
- The test executable is deliberately a single translation unit to avoid this
  failure. Tests and benchmarks are separate executables.
- Any consumer that splits tests, examples, or application code across files
  will fail to link.

Workarounds:

- Include `argparse.h` in exactly one translation unit and expose only wrapper
  functions elsewhere.
- Locally mark every out-of-class definition `inline`, or split declarations
  and definitions using an `ARGPARSE_IMPLEMENTATION` guard.

Recommended fix: make the header genuinely header-only by marking definitions
`inline` and add a multi-TU link test to CI.

### 2. `ARGPARSE_NO_EXCEPTIONS` is not usable

The exception classes and checks are guarded by
`#ifndef ARGPARSE_NO_EXCEPTIONS`, but the code continues after failed checks.
Examples:

- `Namespace::convert<T>()` has code paths with no `return` when an exception
  would have been thrown (`argparse.h:2897`).
- `add_argument()` silently continues after invalid names or duplicate options
  (`argparse.h:1890`).
- `parse_impl()` silently continues when unknown options are found
  (`argparse.h:2013`).
- `assign_positionals()` and `finalize_and_validate()` skip errors and return
  with missing/invalid values.

This can produce undefined behavior, uninitialized values, and silently
accepted invalid input. The tests run with exceptions enabled and the
documentation recommends keeping exceptions enabled.

Recommended fix: either implement a real `std::expected`/error-code API, or
remove the macro until it is supported.

## High-severity issues

### 3. References returned by `add_argument`, `add_argument_group`, and `add_section` dangle

`ArgumentParser::m_Arguments` is a `std::vector<Argument>`
(`argparse.h:1115`), and `add_argument()` returns a reference to
`m_Arguments.back()` (`argparse.h:1955`). Adding another argument may
reallocate the vector and invalidate every previously returned reference.
Likewise, `m_Groups` is a `std::vector<ArgumentGroup>` (`argparse.h:1116`),
and adding another group can invalidate earlier `ArgumentGroup&` references.
`HelpFormatter::m_Sections` is a `std::vector<Section>` (`argparse.h:741`),
and `add_section()` returns `m_Sections.back()` (`argparse.h:1329`), so adding
a later section can invalidate a previously returned `Section&` as well.

The test suite originally observed this by reading `.dest()` from two earlier
references after adding more arguments; they returned empty strings from
freed storage. The tests now check each reference immediately.

Example:

```cpp
auto &first = parser.add_argument("--first");
auto &second = parser.add_argument("--second"); // may invalidate first
first.required();                               // undefined behavior
```

The same pattern applies to `auto &section = formatter.add_section(...)`
followed by another `add_section()` call.

Recommended fix: store arguments, groups, and sections as `std::deque`,
`std::unique_ptr`, or return stable integer handles.

### 4. Positional argument semantics are inconsistent

- **`provided()` is always false for positionals.** `assign_positionals()`
  (`argparse.h:2521`) stores values but never increments `m_Occurrences`.
  `build_namespace()` derives `provided` from `m_Occurrences > 0`
  (`argparse.h:2774`), so positionals never report as provided.
- **Scalar positional defaults are unreachable.** A positional is marked
  required at registration (`argparse.h:1952`). With no tokens,
  `assign_positionals()` throws `RequiredError` before
  `finalize_and_validate()` can apply `.default_value(...)`.
- **Zero-value `nargs` still requires the argument.** A positional declared
  with `.nargs('?')` or `.nargs('*')` remains required unless the user also
  calls `.required(false)`.
- **Multiple positionals are greedy.** A variadic positional consumes as many
  tokens as possible while reserving the minimum for later positionals. This
  matches the implementation but differs from some users' expectations.
- **Fixed counts are ambiguous at the call site.** `.nargs(2)` is ambiguous
  between `nargs(std::size_t)` and `nargs(char)` on common 64-bit compilers and
  fails to compile. Use `.nargs(std::size_t{2})`.

Recommended fix: derive requiredness from `nargs`, increment occurrences for
positionals, apply defaults before the missing-value check, and replace the
overloads with a strong `Nargs` type or a differently named API.

### 5. `parse_known_args` does not behave like its name suggests

`parse_known_args()` only captures unknown option-like tokens:

- Unknown positional tokens still throw
  `unrecognized positional argument` in `assign_positionals()`
  (`argparse.h:2563`).
- A separate value after an unknown long option becomes a positional token
  and may throw. `--unknown value` does not capture both tokens.
- Unknown short options inside a cluster are captured incorrectly. Parsing
  `-vz` with known `-v` stores `"vz"` in `unknown_args()` instead of `"-z"`
  because `token.substr(pos - 1)` is used (`argparse.h:2490`).
- Unknown options that require values are not consumed as a pair.

Tests in `parse_known_args does not preserve unknown positionals` document
these cases. Recommended fix: use a Python-style unknown parser that captures
both unknown option tokens and following values, preserves unknown
positionals, and returns them separately.

### 6. Option value collection swallows following options

`collect_option_values()` (`argparse.h:2374`) consumes the next token whenever
the minimum count is not met, even if that token looks like an option:

```cpp
parser.add_argument("--name");
parser.add_argument("--verbose").action("store_true");

// --name consumes "--verbose" as its value.
parser.parse_args(argc, argv);
```

Results:

- `name == "--verbose"` and `verbose == false` for `--name --verbose`.
- `--verbose=false` silently ignores `false` because flags take no values.
- `-n=5` stores `"=5"` for short options (unlike long options, which split on
  `=`).
- Unbounded options (`nargs('*')`, `nargs('+')`) stop at any dash-prefixed
  token. A file named `-input` cannot be passed through the option; users must
  use `--files=-input`, which only works for options that accept inline
  values.
- `--` after an option is not assigned to the option's value list; it switches
  the main parser into positional mode.

Recommended fix: treat registered option tokens as boundaries for optional
arguments, support `--opt=value` consistently, reject inline values on flags,
and provide an explicit way to accept dash-prefixed values.

### 7. Help system defects

- **`--help-all` does not show hidden arguments.** `--help-all` is registered
  with the same `Help` action as `--help` (`argparse.h:1647`). Both throw the
  same `HelpError`, and `print_help()` defaults to `show_hidden = false`
  (`argparse.h:2138`). `format_help(true)` works only when called directly.
- **Section headings are concatenated with their first row.** `format_section()`
  writes the title without a trailing newline (`argparse.h:1408`). Generated
  help currently contains lines such as
  `optional arguments:  -h, --help ...`.
- **`format_usage()` is incomplete.** It emits only `usage: <program> `
  (`argparse.h:1401`), with no options, positionals, or arguments.
- **`Markdown` and `Compact` formats are placeholders.** `Markdown` behaves
  like colored output, and `Compact` only changes the heading color.
- **`sort_sections()` has no effect.** `m_SortSections` is set
  (`argparse.h:1325`) but never read.
- **Alignment fields are unused.** `Alignment::option_width` and
  `Alignment::min_description_width` are declared but never used.
- **Colors are enabled by default even when redirected.** `HelpFormatter`
  defaults to `Format::Colored` and `ColorTheme::enabled = true`
  (`argparse.h:1237`). There is no `isatty`/`NO_COLOR` detection.
- **ANSI escapes break alignment.** `format_item()` measures the uncolored
  option length but emits color escapes, so columns shift when colors are on.
- **Terminal width detection is platform-specific and expensive.**
  Windows always returns 80; POSIX runs `popen("tput cols")` for each
  auto-width formatter (`argparse.h:1465`), which spawns a shell and is not
  cached.

Recommended fix: add an explicit help-action result type, implement
`format_help(true)` for `--help-all`, add newlines after section titles,
synthesize full usage text, implement Markdown, sort sections, detect terminal
capability, and strip ANSI when measuring widths. Add golden-output tests.

### 8. Subcommand detection has false positives and rigid ordering

`parse_impl()` scans for subcommand tokens before parsing parent options
(`argparse.h:2032`). It does not skip option values, so a value equal to a
subcommand name is mistaken for a subcommand:

```cpp
auto &run = parser.add_subcommand("run");
parser.add_argument("--output");
// argv: --output run
// "run" is treated as the subcommand, and --output is left without a value.
```

Other constraints:

- Parent options must appear before the subcommand. A parent option after the
  subcommand is parsed by the child and usually produces
  `unrecognized argument`.
- Unknown subcommands are reported as unrecognized positional arguments rather
  than a dedicated subcommand error.
- There is no required-subcommand, default-subcommand, or subcommand-alias
  support.
- The first matching token wins; duplicate names update the existing parser's
  description only.
- `add_subcommand()` constructs a child parser with the subcommand name as its
  program name, while parse-time `argv[0]` is built as `parent child`; help
  output from different paths can differ.

Recommended fix: parse parent options first with a real command-line scanner,
then dispatch on the first remaining positional known to be a subcommand.
Add aliases, required/default subcommands, and a dedicated unknown-subcommand
error.

### 9. Groups and dependencies have edge-case semantics

- A group can be both `exclusive(true)` and `required(true)`, which yields
  exactly-one semantics, but this is undocumented in code and error messages
  for `add_mutually_exclusive_group(true)` use an empty group title.
- `depends_on()` compares the dependency name against destination names only.
  Passing an option alias such as `"--user"` silently never matches.
- A dependent argument with a default value still fails the dependency check
  when its dependency is present, because the check uses occurrence count
  rather than final values (`argparse.h:2750`).
- A dependency argument supplied only by a default/environment is not
  considered "provided", so dependents are not required.
- Group references are invalidated when later groups are added (see issue 3).

Recommended fix: validate dependency names at registration, resolve aliases to
destinations, define whether defaults satisfy dependencies, use stable group
storage, and add a group cardinality model (`exactly_one`, `at_most_one`,
`at_least_one`).

## Medium-severity issues

### 10. Default, environment, and implicit-value precedence

`finalize_and_validate()` checks `m_DefaultValues` before `m_EnvVar`
(`argparse.h:2653`). Therefore an environment variable is ignored whenever a
default exists. This is documented in the usage guide but is surprising if a
default is meant as a last resort.

`implicit_value` is applied during finalization whenever `m_NargsMin == 0`,
even when the option was absent (`argparse.h:2679`). This makes the following
return `"auto"` without `--color`:

```cpp
parser.add_argument("--color").nargs('?').implicit_value("auto");
```

Recommended fix: define a clear precedence model (CLI > environment > default >
implicit > action default, or make it configurable) and apply implicit values
only when the argument occurred.

### 11. Required arguments and defaults

`required` is satisfied by a default value because the check only fires when
both values and defaults are empty (`argparse.h:2685`). The documentation
states this, but it means a "required" option can never fail if a default is
configured. Consider rejecting `required().default_value(...)` at registration
or documenting it as an explicitly supported pattern.

### 12. Validator behavior

- `BuiltinValidators::in_range` uses `std::stod` and `static_cast<T>`
  (`argparse.h:208`). Decimal input for an integer range is truncated:
  `in_range(1, 2)("2.9")` returns true because `2.9` becomes `2`.
  `in_range` also accepts leading whitespace and floating-point spellings such
  as `inf` before comparison.
- `is_file` uses `fopen` and does not distinguish directories on all
  platforms; `is_dir` uses a bitmask test rather than `S_ISDIR` on POSIX and
  ANSI Windows APIs on Windows.
- Only one `.check()` callback is retained per argument; a second call silently
  replaces the first. The usage guide documents this.
- Exceptions thrown by a custom validator propagate unchanged (not converted to
  `ArgumentError`). Validators should catch their own conversion errors or use
  a non-throwing parser such as `std::from_chars`.
- Validators run on defaults and environment values, which can turn a
  configuration mistake into a parse error.

Recommended fix: type-aware range validation, `std::filesystem`-based path
checks, validator composition, and wrapping user exceptions in
`ArgumentError`.

### 13. Value conversion limitations

`Namespace::convert<T>()` (`argparse.h:2897`) supports only `std::string`,
`bool`, integral types, and floating-point types.

- Enums are not supported despite an old README claim about enum input.
- `const char *` is not supported.
- `get<T>()` returns only the first value; `getlist<T>()` is required for
  lists.
- No range or signedness checking beyond stream state. Converting a negative
  value to an unsigned type can produce surprising results or a conversion
  error depending on the standard library.
- Character types are extracted as characters, not numeric digit values.
- Conversions use locale-dependent iostreams. Decimal separators and accepted
  numeric syntax can vary by locale.
- `std::optional<T>` is not supported as a return type; users must call
  `provided()` and / or `raw_values().empty()` first.
- `has()` reports registration, not presence; this is useful but easy to
  confuse with `provided()`.

Recommended fix: `std::optional<T> get_if<T>()`, `std::expected`, `string_view`
inputs, enum-name registration, locale-independent conversions, and explicit
range validation.

### 14. `nargs(2)` overload ambiguity

The two overloads

```cpp
Argument &nargs(std::size_t count);
Argument &nargs(char pattern);
```

make `.nargs(2)` ambiguous because an `int` converts to both `std::size_t` and
`char`. Test and documentation examples must use
`.nargs(std::size_t{2})`. This is an API ergonomics defect.

Recommended fix: replace the `char` overload with named methods such as
`nargs_optional()`, `nargs_zero_or_more()`, `nargs_one_or_more()`, or use a
strong `Nargs` type with implicit constructors from count and pattern.

### 15. Hidden arguments and completion

- Hidden arguments are registered in `m_OptionLookup` and therefore appear in
  Bash completion output. Hiding only affects help formatting.
- Only Bash completion is generated. Other names return a string beginning
  with `Unsupported shell:` rather than throwing or returning an error type.
- The completion function name is derived directly from the program name.
  Program names containing spaces, slashes, or punctuation produce invalid
  shell functions.
- Completion lists all options for every position; it does not understand
  values, subcommands' options, choices, or file arguments.

Recommended fix: exclude hidden arguments, sanitize identifiers, support
Fish/Zsh/PowerShell, and generate position-aware completions.

## Low-severity issues and code quality

### 16. Header hygiene and dead code

- `<thread>`, `<atomic>`, and `<sys/stat.h>` are included but not used.
- `<thread>`, `<atomic>`, `<sys/stat.h>`, and `<unistd.h>` are included again
  inside `namespace argparse` (`argparse.h:1140`). Include guards currently
  prevent namespace pollution because the headers were already included at the
  top, but this is fragile and non-portable.
- The file ends with `#endif // ARGPARSE_IMPLEMENTATION` even though no
  `ARGPARSE_IMPLEMENTATION` block exists (`argparse.h:3178`).
- `argument_display()` is defined but unused; `lookup_option()` is unused
  because parsing calls `try_lookup_option()` directly.
- The file header calls the file `argparse.hpp` while the actual name is
  `argparse.h`.
- The constructor initializer order triggers `-Wreorder` warnings because
  `m_Epilog` is initialized after `m_Description` in the initializer list but
  declared after it (`argparse.h:1629`).
- The version check compares against a hard-coded 1.0.0 (`argparse.h:97`) while
  the library version is 0.1.0, and the include guard prevents the check from
  running on a second include of the same header.

Recommended fix: remove unused includes, add a real declarations/implementation
split, fix initialization order, remove dead code, and make the version check
consistent.

### 17. Diagnostics and suggestions

- Unknown-option errors report a Levenshtein suggestion when the distance is at
  most 2 (`argparse.h:2285`). The search iterates an `std::unordered_map`, so
  ties are non-deterministic.
- Suggestions are computed for every unknown option and are not available for
  subcommands.
- Error messages do not include usage text, option lists, or program names.
- Different failure categories are inconsistent: unknown option raises
  `ParseError`, unknown positional raises `ArgumentError`, and an unknown
  subcommand raises a positional error.
- `what()` strings are not localized and some include internal destination
  names rather than user-facing aliases.

Recommended fix: stable suggestion ordering, subcommand suggestions, error
codes, usage snippets, and structured diagnostics with source token positions.

### 18. Thread safety and reentrancy

`ArgumentParser` stores mutable parsed state in the object: `m_Namespace`,
`m_UnknownArgs`, and per-argument `m_Values`/`m_Occurrences`
(`argparse.h:2020`). Parsing mutates all of these. Concurrent parsing with a
single parser, or reading a returned namespace while another parse runs, is a
data race. Subparsers are also mutable and owned by the parent.

Recommended fix: either document single-threaded use explicitly or make
`parse_args()` return an independent namespace and keep parser definitions
immutable after construction.

### 19. Locale and platform behavior

- Numeric conversions use iostreams and `std::stod`; behavior follows the
  active C++ locale.
- `get_terminal_width()` uses `popen("tput cols")` on POSIX, spawning a
  subprocess per formatter with auto width, and is not available on Windows
  beyond a fixed 80 columns.
- `BuiltinValidators::is_dir` uses platform-specific path APIs; Windows uses
  ANSI `GetFileAttributesA`, so non-ASCII paths may fail.
- Environment access uses `std::getenv`, which is not thread-safe on all
  platforms and is not validated in `env()`.

Recommended fix: use locale-independent `std::from_chars`/`to_chars`, cache
terminal width, use `std::filesystem`, and adopt UTF-8/wide APIs on Windows.

### 20. Table formatting has an out-of-bounds risk

`HelpFormatter::Table::format()` derives the column count from the first row
(`argparse.h:1195`) and then indexes `col_widths[i]` for every column of every
row. A later row with more columns than the first writes past the vector.
Rows with fewer columns are fine. Add a shape check or normalize rows.

### 21. Interactive fallback is untested and fragile

`interactive(true)` prompts on `std::cin` inside validation
(`argparse.h:2689`, `argparse.h:2754`). There is no terminal detection, prompt
retry, cancellation handling, or test coverage. A non-interactive process can
block indefinitely. Prefer an explicit callback or keep prompting outside the
parser.

### 22. Testability limitations caused by the header

Because of issue 1, all unit tests live in `tests/Main.test.cpp` and all
benchmarks in `tests/Benchmarks.test.cpp`, each compiled as a single
translation unit. This constrains test organization: adding a second test file
currently breaks the link. A multi-TU link test is the first regression test to
add once the header is fixed.

## Confirmed by tests and benchmarks

The following behaviors are intentionally asserted by the test suite:

- Built-in `-h`, `--help`, and `--help-all` raise `HelpError`.
- Duplicate and malformed argument registrations throw `ArgumentError`.
- Positional arguments accept negative numbers and a bare `-`.
- `--` converts all following tokens to positionals.
- Long `--opt=value`, long separate values, short attached values, and short
  clusters parse as implemented.
- `store_true`, `store_false`, `count`, `append`, and `store` action defaults.
- Environment fallback precedence and validation.
- Choice/custom/range/file validators.
- Mutually exclusive and required groups.
- `depends_on` uses destination names and occurrence counts.
- `parse_known_args` captures unknown option tokens only.
- Nested subcommands and child help errors.
- Namespace copy/move semantics and typed conversions.
- Help formatter sections, visibility, wrapping, colors, and tables.
- Documented limitations: positional `provided()` is false, scalar positional
  defaults are unreachable, implicit values apply when absent, flags ignore
  inline values, short `-n=5` keeps `=`, `parse_known_args` rejects unknown
  positionals, and unknown short clusters are captured without a dash.

## Suggested next steps

The highest-value fixes are:

1. Make the header inline and add a multi-TU link test.
2. Replace raw `Argument&`/`ArgumentGroup&` returns with stable handles or
   stable containers.
3. Correct positional requiredness/defaults/occurrence tracking.
4. Reimplement option and unknown-token scanning with explicit boundaries.
5. Split help actions and fix help formatting/usage generation.
6. Define a single precedence model for defaults, environment, and implicit
   values.
7. Expand tests with golden help output, fuzzing, and a multi-translation-unit
   link test.

See [IMPROVE.md](IMPROVE.md) for a prioritized roadmap.
