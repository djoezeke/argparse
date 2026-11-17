# Improvement Roadmap

This document proposes concrete improvements and future work for `argparse`. It
builds on the verified findings in [REVIEW.md](REVIEW.md) and is ordered by
impact: release blockers first, then correctness, API ergonomics, formatting,
platform support, and finally documentation and tooling.

The parser implementation was intentionally left unchanged while the test suite
and this roadmap were written. Every item below is therefore a proposal, not a
description of current behavior.

## Guiding principles

1. **Correctness before features.** A parser that silently accepts or drops
   tokens is worse than one with fewer options.
2. **One predictable precedence model.** CLI value, environment, default, and
   implicit value must have a documented and testable order.
3. **Stable public references.** Adding an argument must never invalidate a
   reference the caller already holds.
4. **Real header-only support.** Every consumer must be able to include the
   header from any number of translation units.
5. **Test everything twice.** Unit tests prove behavior; microbenchmarks catch
   accidental performance regressions. Golden files pin user-visible output.
6. **No silent failure.** Unsupported combinations are rejected at registration
   time, not discovered at parse time or, worse, ignored.

## Roadmap at a glance

| Phase | Theme | Representative items | Effort | Risk |
| --- | --- | --- | --- | --- |
| 0 | Release blockers | Inline definitions, multi-TU test, stable argument storage | 1-2 days | Low |
| 1 | Parsing correctness | Tokenizer rewrite, positional assignment, `parse_known_args`, subcommand dispatch | 1-2 weeks | Medium |
| 2 | API ergonomics | Strong `Nargs` type, composable validators, typed namespace access | 1 week | Medium |
| 3 | Help and diagnostics | Help output model, usage synthesis, `--help-all`, stable errors | 1 week | Low |
| 4 | Platform and performance | Locale-independent conversion, filesystem validators, completion | 1 week | Low |
| 5 | Quality infrastructure | CI matrix, fuzzing, sanitizers, benchmark baselines, docs | Ongoing | Low |

## Phase 0 - Release blockers

### 0.1 Make the header genuinely header-only

**Problem:** Out-of-class member functions are defined without `inline`,
causing multiple-definition linker errors in multi-TU programs
(REVIEW.md issue 1).

**Plan:**

- Mark every out-of-class function definition `inline`, or restructure into a
  declaration header plus an `ARGPARSE_IMPLEMENTATION` section.
- Keep the single-file distribution model; do not require a compiled library.
- Add `tests/multi_tu/` with `a.cpp`, `b.cpp`, and `main.cpp`, each including
  `argparse.h` and using `HelpFormatter`, `ArgumentParser`, and `Namespace`.
  Link them into one executable. This test must run in CI on every platform.

**Acceptance criteria:**

- `argparse_tests` may be split into multiple translation units again.
- The multi-TU executable links with GCC, Clang, and MSVC.
- No ODR violations under `-fsanitize=address,undefined` or `-fno-common`.

### 0.2 Stabilize `Argument&`, `ArgumentGroup&`, and `Section&`

**Problem:** References returned by `add_argument()`, `add_argument_group()`,
and `HelpFormatter::add_section()` dangle after later additions reallocate the
backing `std::vector` (REVIEW.md issue 3).

**Options, in preference order:**

1. Store arguments, groups, and sections in `std::deque` or
   `std::vector<std::unique_ptr<...>>` so element addresses remain stable.
2. Return lightweight handles (`ArgumentHandle`, `GroupHandle`, `SectionHandle`)
   that index into the container and are validated on use.
3. Keep references but reserve capacity up front - not viable because the
   number of arguments is unbounded.

**Acceptance criteria:**

- Registering 1,000 arguments and then configuring the first one is safe and
  passes under ASan.
- A regression test configures arguments in reverse order and after all
  registrations.
- Help output and parse behavior are unchanged.

### 0.3 Decide the fate of `ARGPARSE_NO_EXCEPTIONS`

**Problem:** The macro suppresses throws but leaves control flow continuing
with missing or invalid data (REVIEW.md issue 2).

**Plan:** Either implement a complete error-code API or remove the macro until
it is safe. A reasonable API:

```cpp
auto result = parser.try_parse_args(argc, argv);
if (!result)
{
    std::cerr << result.error().message << '\n';
    return 2;
}
const auto &args = *result;
```

**Acceptance criteria:**

- No code path reaches an invalid state when an error occurs.
- The no-exception configuration is covered by its own test executable, or the
  macro is removed from the public surface.

## Phase 1 - Parsing correctness

A single scanner should own token classification. Today, option parsing,
positional assignment, `--` handling, subcommand detection, and unknown-token
capture are spread across several loops with inconsistent rules.

### 1.1 Introduce a token model

```cpp
enum class TokenKind { Option, LongOption, ShortCluster, Positional, Terminator };
struct Token { TokenKind kind; std::string_view text; std::size_t index; };
```

Rules to encode in one place:

- Long option with inline value: `--name=value` (split on the first `=`).
- Short option attached value: `-ovalue`, and an explicit policy for `-o=value`
  (recommended: strip `=`, matching long-option behavior).
- Short clusters: every character is a flag until an option that requires a
  value; the remainder of the cluster becomes its value.
- Registered option tokens terminate value collection for options with
  optional or greedy `nargs` (REVIEW.md issue 6).
- Inline values on flags (`--verbose=false`) are an error, not silently
  ignored. Support `--no-verbose` via a dedicated negation API instead.
- `--` terminates option parsing even in the middle of an option value list.
- Negative numeric literals remain values.

**Acceptance criteria:**

- A table-driven test covers every token shape and boundary.
- Existing documented behavior is preserved unless a change is listed in the
  changelog.

### 1.2 Rewrite positional assignment around explicit minimums and maximums

Replace the ad-hoc greedy loop with a reservation algorithm:

1. Compute `min` and `max` accepted values per positional (`nargs`).
2. Reserve `sum(min)` for all later positionals.
3. Assign the current positional `min(current_max, remaining - reserve)`.
4. Report a dedicated error when the totals are unsatisfiable.

Also fix the positional semantics from REVIEW.md issue 4:

- Increment the occurrence counter so `provided()` works.
- Let `nargs('?')` and `nargs('*')` imply `required(false)` unless explicitly
  overridden, or require the existing explicit call but reject the combination
  `required(true)` with a zero minimum.
- Apply defaults before the missing-value check, so scalar positional defaults
  become reachable.
- Support `nargs` ranges as a future extension: `.nargs(1, 3)`.

**Acceptance criteria:**

- Property test: for random `nargs` specifications and token counts, the
  parser either assigns all tokens exactly once or fails with a precise error.
- `provided()` is true for every parsed positional.
- `.default_value(...)` on a scalar positional works.

### 1.3 Make `parse_known_args` preserve everything unknown

Rewrite unknown handling so that:

- Unknown option tokens are preserved verbatim, including the dash.
- A following value is attached when the unknown option consumes one, using a
  conservative rule (do not steal a known option token or `--`).
- Unknown positionals are returned separately instead of throwing.
- The API exposes both lists:

```cpp
struct UnknownArguments
{
    std::vector<std::string> options;
    std::vector<std::string> positionals;
};
```

**Acceptance criteria:**

- `-vz`, `--unknown value`, `--unknown=value`, and extra positionals are
  captured exactly.
- Strict `parse_args()` continues to reject the same inputs.
- Round-tripping the unknown list reproduces the original tail order.

### 1.4 Parse parent options before dispatching subcommands

Current subcommand detection can mistake an option value for a subcommand and
forces parent options to appear first (REVIEW.md issue 8).

**Plan:**

- Scan and consume parent options until the first non-option token.
- Treat that token as a subcommand only if it matches a registered name.
- Hand the remaining tokens to the child parser.
- Emit a dedicated `UnknownSubcommandError` with suggestions.
- Add `.required(true)` and a default subcommand on the parser.
- Allow subcommand aliases: `add_subcommand({"co", "checkout"}, "...")`.

**Acceptance criteria:**

- `--output run` parses `run` as a value when `run` is also a subcommand only
  if the value position is unambiguous; the documented rule must be tested.
- Parent options before and after the subcommand follow one documented rule.
- Unknown subcommands produce a subcommand-specific error.

### 1.5 Define a single precedence model

Recommended order for absent arguments:

1. CLI value.
2. Environment variable (when configured and set).
3. Explicit default value(s).
4. Implicit value, applied only when the option occurred.
5. Action-specific default.

Make the order configurable only if a real use case appears. Update tests,
documentation, and error messages together. See REVIEW.md issues 10 and 11.

**Acceptance criteria:**

- A precedence matrix test covers every combination of CLI presence,
  environment presence, default presence, implicit value, and action.
- `implicit_value` never applies when the option is absent.
- `required().default_value(...)` is either rejected at registration or
  documented as satisfied by the default.

## Phase 2 - API ergonomics

### 2.1 Replace the `nargs(char)` overload with a strong type

The current overloads make `.nargs(2)` ambiguous (REVIEW.md issue 14).

```cpp
parser.add_argument("--pair").nargs(Nargs::exactly(2));
parser.add_argument("files").nargs(Nargs::zero_or_more());
parser.add_argument("--include").nargs(Nargs::one_or_more());
parser.add_argument("--color").nargs(Nargs::optional());
```

Alternatively keep a single `nargs(Nargs)` overload with implicit
constructors from a count and from a pattern enum, and add deprecated shims
for the old spellings. Add compile-time tests that `.nargs(2)` is unambiguous.

### 2.2 Composable validators

**Problem:** Only the last `.check(...)` is retained (REVIEW.md issue 12).

```cpp
parser.add_argument("--percent")
    .check(Validator::range<int>(0, 100))
    .check(Validator::not_equal(13), "13 is reserved")
    .check([](std::string_view v) { return v.size() <= 3; });
```

Recommended design:

- Store a `std::vector<Validator>` instead of one callback.
- Add `and_then`/`or_else` combinators.
- Accept `std::string_view` to avoid copies.
- Convert validator exceptions into `ArgumentError` with the argument name.
- Make range validation type-aware so `in_range<int>(1, 2)` rejects `"2.9"`
  instead of truncating it.
- Use `std::filesystem` for `is_file`/`is_dir`; return the error reason in the
  exception message.

### 2.3 Typed namespace access

```cpp
std::optional<int> port = args.get_if<int>("port");
int retries = args.get_or<int>("retries", 3);
std::vector<std::string> tags = args.getlist<std::string>("tag");
```

Additional goals:

- Support enums via an explicit mapping or `magic_enum`-style opt-in.
- Support `std::optional<T>` and `std::string_view` where lifetime permits.
- Use locale-independent conversion (`std::from_chars`/`std::to_chars`) and
  report the offending value and argument in the error.
- Reject narrowing that changes the value (negative to unsigned, overflow,
  fractional to integral) unless explicitly requested.
- Keep `has()` (registered) and `provided()` (occurred) clearly distinct;
  consider renaming `has()` to `contains()` or `is_registered()`.

### 2.4 Group and dependency improvements

- Replace booleans with a cardinality enum:
  `GroupCardinality::at_most_one`, `exactly_one`, `at_least_one`, `all`.
- Resolve `depends_on("--user")` to the destination automatically, and reject
  unknown dependency names at registration.
- Decide whether defaults/environment satisfy dependencies, then test it.
- Support `requires_all`, `requires_any`, and `conflicts_with` relationships.
- Give `add_mutually_exclusive_group(true)` a usable generated group title so
  error messages identify the group.

## Phase 3 - Help and diagnostics

### 3.1 Build help from a render model

Instead of writing strings directly into a buffer, produce a structured model:

```cpp
struct HelpLine { std::string left; std::string right; std::size_t indent; };
struct HelpBlock { std::string title; std::vector<HelpLine> lines; };
std::vector<HelpBlock> blocks = parser.help_blocks(show_hidden);
```

Then render to plain text, ANSI, Markdown, or compact output from the same
model. This fixes section-heading concatenation, ANSI width miscalculation,
and the unimplemented `Markdown`/`Compact` formats in one change
(REVIEW.md issue 7).

### 3.2 Synthesize a complete usage line

Generate usage from registered arguments by default:

```text
usage: tool [-h] [--verbose] [--mode {fast,safe}] [--threads N] input [files ...]
```

Rules:

- `[-h]` for built-in help, hidden arguments omitted.
- Options with required values show `--name VALUE`; optional values show
  `[--color [WHEN]]`.
- Choices are rendered as `{a,b,c}`.
- Positionals use `metavar` or the destination name and `nargs` ellipses.
- A user-supplied `parser.usage(...)` still wins verbatim.

### 3.3 Separate help actions

- `-h`/`--help` raise `HelpError` with a flag indicating hidden arguments
  should be omitted.
- `--help-all` raises the same error type with `show_hidden = true`.
- `print_help()` and `format_help()` honor that flag by default.
- Add golden-output tests for both modes.

### 3.4 Terminal-aware colors and width

- Detect `isatty` and honor `NO_COLOR`, `CLICOLOR`, and
  `CLICOLOR_FORCE`.
- Default to plain output when redirected.
- Cache terminal width; do not spawn `tput` on every formatter call.
- Use a portable width query and a fixed fallback.
- Strip ANSI escapes when measuring column widths.
- Remove or implement `Alignment::option_width`,
  `Alignment::min_description_width`, and `sort_sections()`; unused knobs are
  worse than absent ones.

### 3.5 Stable, actionable diagnostics

- Introduce error codes: `ErrorCode::UnknownOption`,
  `MissingValue`, `InvalidChoice`, `UnknownSubcommand`, and so on.
- Include the program name, the offending token, and a one-line usage hint.
- Make suggestions deterministic: sort candidate names before choosing the
  closest match and apply a documented tie-break rule.
- Suggest subcommands as well as options.
- Report token positions so editors and wrappers can highlight the error.
- Add a `format_error(const ArgumentError&)` helper for consistent CLI output.

## Phase 4 - Platform, performance, and tools

### 4.1 Locale-independent conversions

Replace iostream extraction and `std::stod` with `std::from_chars` where
available, with a fallback for older standard libraries. Document the accepted
grammar for integers, floating point, and booleans. Reject trailing garbage
explicitly. This also removes locale-dependent decimal separators
(REVIEW.md issue 19).

### 4.2 Platform path handling

- Use `std::filesystem` for validators and error messages.
- Adopt UTF-8 or wide-character APIs on Windows rather than ANSI
  `GetFileAttributesA`.
- Normalize path separators in diagnostics only, never in stored values.
- Add tests for relative paths, paths with spaces, and non-ASCII names.

### 4.3 Environment access

- Cache environment values at parse time.
- Document thread-safety limits of `std::getenv`.
- Add `env_prefix("APP_")` for automatic variable names.
- Add `.env_required()` and a way to distinguish "unset" from "set but empty".

### 4.4 Completion and generators

- Exclude hidden arguments and arguments not applicable at the cursor.
- Sanitize the program name used in shell function identifiers.
- Support Bash, Zsh, Fish, and PowerShell.
- Use choices to complete values; use file completion for path-like arguments.
- Consider generating a man page and Markdown reference from the same metadata.

### 4.5 Interactive fallback

Replace the built-in `std::cin` prompt with a callback:

```cpp
parser.interactive([](const Argument &arg) -> std::optional<std::string> {
    std::cout << arg.help() << ": ";
    std::string line;
    if (!std::getline(std::cin, line)) return std::nullopt;
    return line;
});
```

Requirements: terminal detection, retry limits, cancellation, and tests that
do not block.

## Phase 5 - Quality infrastructure

### 5.1 Test suite structure

Split tests by area once the header is multi-TU safe:

```text
tests/
  Main.test.cpp
  registration.test.cpp
  positional.test.cpp
  optional.test.cpp
  validation.test.cpp
  groups.test.cpp
  subcommands.test.cpp
  namespace.test.cpp
  help.test.cpp
  errors.test.cpp
  multi_tu/
  golden/
```

Test categories to add:

| Category | Purpose | Tools |
| --- | --- | --- |
| Golden help output | Pin user-visible formatting | Checked-in `.txt` files |
| Property tests | Positional/token assignment invariants | Randomized generators |
| Fuzz tests | Crash and hang resistance | libFuzzer/AFL harness |
| Differential tests | Compare behavior with Python argparse/CLI11 | Scripted corpus |
| Multi-TU link test | Catch ODR violations | Three-TU executable |
| No-exception test | Validate error-code mode | Separate executable |
| Sanitizer runs | Memory and UB safety | ASan, UBSan, TSan |

### 5.2 Benchmark discipline

The current Catch2 benchmarks are a good starting point. To make them useful
for regression detection:

- Record a baseline on a fixed machine and compiler.
- Publish results as a table in CI job summaries.
- Fail a dedicated (non-blocking) job when a scenario regresses by more than an
  agreed threshold, for example 15 percent.
- Keep scenarios stable and named.
- Add adversarial cases: 1,000 options, 10,000 positional tokens, deeply
  nested subcommands, very long option values, repeated failed lookups.
- Separate construction, parse, finalize, conversion, and help generation so a
  regression points at one stage.
- Consider Google Benchmark if Catch2's statistics become limiting; keep the
  Catch2 executable for developer convenience.

### 5.3 CI matrix and static checks

Proposed GitHub Actions jobs:

- Linux: GCC 11/13, Clang 14/17, Debug and Release.
- macOS: Apple Clang, Debug and Release.
- Windows: MSVC 2022 and MinGW-w64, Debug and Release.
- Sanitizers on Linux: ASan+UBSan, and a separate TSan run for documented
  thread-safety boundaries.
- `clang-format --dry-run --Werror` and `clang-tidy`.
- `-Wall -Wextra -Wpedantic -Werror` for the header and tests.
- Install test: configure, install to a prefix, then build a consumer with
  `find_package(Argparser CONFIG REQUIRED)`.
- CMake presets for common configurations so contributors do not copy flags.

### 5.4 Documentation improvements

- Keep `docs/README.md` as the guide and add a generated API reference.
- Add a migration guide when breaking API changes land.
- Document the precedence model and token grammar in one canonical table.
- Add a "compatibility" page comparing behavior with Python argparse.
- Link each limitation in REVIEW.md to the roadmap item that addresses it.
- Include a copy-paste `CMakeLists.txt` and a single-file `g++` command that CI
  verifies.
- Add a changelog and semantic-versioning policy; the current version is
  0.1.0, so breaking changes are still acceptable.

## Suggested milestones

### v0.2 - Safe and stable

- [ ] Inline header definitions and add the multi-TU link test.
- [ ] Stable argument and group storage.
- [ ] Remove or fully implement `ARGPARSE_NO_EXCEPTIONS`.
- [ ] Fix `nargs(2)` ambiguity with the new `Nargs` type.
- [ ] Correct positional `provided()`, requiredness, and defaults.
- [ ] CI matrix and install smoke test.

### v0.3 - Correct parsing

- [ ] Tokenizer rewrite with explicit boundaries.
- [ ] `parse_known_args` preserves options, values, and positionals.
- [ ] Subcommand dispatch after parent option parsing, plus aliases and a
      dedicated unknown-subcommand error.
- [ ] Single precedence model for CLI, environment, defaults, and implicit
      values.
- [ ] Composable validators and type-aware ranges.
- [ ] Golden help tests and complete usage synthesis.

### v1.0 - Complete and documented

- [ ] Markdown/compact help and terminal-aware colors.
- [ ] Structured diagnostics with error codes.
- [ ] Bash/Zsh/Fish/PowerShell completion.
- [ ] Locale-independent conversions and filesystem-based validators.
- [ ] Fuzzing and sanitizer coverage in CI.
- [ ] Stable API reference, changelog, and migration guide.

## Non-goals

- Becoming a general configuration-file framework. Config files may be layered
  on top of `parse_args()` by callers.
- A GUI or TUI.
- Full Python argparse compatibility at the cost of C++ ergonomics. Document
  differences instead.
- Supporting compilers without C++17.

## How to pick up an item

1. Choose the smallest item that unblocks others (usually Phase 0).
2. Write a failing test that captures the desired behavior.
3. Implement the change behind the existing API when possible.
4. Update `docs/README.md`, `REVIEW.md`, and this roadmap in the same change.
5. Run the unit tests, benchmarks, demo, and install smoke test.
6. Add a changelog entry before opening the pull request.

Items are intentionally small enough to become individual issues. The issue
title can reuse the section heading, and the acceptance criteria can become the
issue checklist.
