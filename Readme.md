<p align="center"><h1 align="center">ARGPARSER</h1></p>
<p align="center">
  <em><code>Modern C/C++ Argument Parser.</code></em>
</p>
<p align="center">
  <img src="https://img.shields.io/github/license/djoezeke/argparser?style=default&logo=opensourceinitiative&logoColor=white&color=0080ff" alt="license">
  <img src="https://img.shields.io/github/last-commit/djoezeke/argparser?style=default&logo=git&logoColor=white&color=0080ff" alt="last-commit">
  <img src="https://img.shields.io/github/languages/top/djoezeke/argparser?style=default&color=0080ff" alt="repo-top-language">
  <img src="https://img.shields.io/github/languages/count/djoezeke/argparser?style=default&color=0080ff" alt="repo-language-count">
</p>
<br>

**Version 0.2.0** - a genuine single-header C++17 command line parser with a
Python `argparse`-style API, stable registration references, predictable value
precedence, non-throwing parsing support, and completion for four shells.

<details><summary>Table of Contents</summary>

- [Overview](#-overview)
- [What changed in 0.2.0](#-what-changed-in-020)
- [Features](#-features)
- [Getting Started](#-getting-started)
  - [Prerequisites](#-prerequisites)
  - [Installation](#-installation)
  - [Quick start](#-quick-start)
- [Argument model](#argument-model)
- [Parsing modes](#parsing-modes)
- [Value precedence](#value-precedence)
- [Typed access](#typed-access)
- [Validation and relationships](#validation-and-relationships)
- [Subcommands](#subcommands)
- [Help formatting](#help-formatting)
- [Shell completion](#shell-completion)
- [Testing and benchmarks](#-testing-and-benchmarks)
- [Documentation](#-documentation)
- [Contributing](#-contributing)
- [License](#-license)

</details>

## 📍 Overview

`Argparser` is a header-only C++17 argument parsing library inspired by Python's
`argparse`. Declare arguments with `add_argument(...)`, parse a command line,
validate values, and render help without adding a build dependency or a
compiled library.

The canonical header is `include/argparse/argparse.h`. A forwarding header,
`include/argparse.h`, supports the traditional include spelling. Every function
is `inline`, so the header can be included from any number of translation
units.

The complete usage guide lives in [docs/README.md](docs/README.md).
[examples/Demo.cpp](examples/Demo.cpp) is a runnable program that exercises
options, validation, help formatting, and subcommands.

## ✨ What changed in 0.2.0

- **Real header-only support.** All out-of-class definitions are `inline`, and
  a dedicated multi-translation-unit test guards the guarantee.
- **Stable references.** Arguments, groups, and help sections are stored in
  `std::deque`, so a reference returned by `add_argument()`,
  `add_argument_group()`, or `add_section()` remains valid after later
  registrations.
- **One value model.** Values are resolved as CLI > environment > explicit
  default > action default. Implicit values apply only when an option is present
  without a value.
- **Permissive and non-throwing parsing.** `parse_known_args()` captures unknown
  options and positionals, while `try_parse_args()` and
  `try_parse_known_args()` return a `ParseResult` instead of throwing.
- **Strong `Nargs` type.** `.nargs(2)`, `.nargs(Nargs::exactly(2))`,
  `.nargs('?')`, `.nargs('*')`, and `.nargs('+')` are all supported without
  ambiguity.
- **Richer namespace access.** `get<T>()`, `getlist<T>()`, `get_if<T>()`,
  `get_or<T>()`, `std::string_view`, `const char *`, optional values, and enum
  conversion are available.
- **Correct positional semantics.** Positionals record occurrences, derive
  requiredness from `nargs`, accept scalar defaults, and can be made optional
  with `.required(false)`.
- **Complete help system.** Synthesized usage, `--help-all`, Markdown and
  Compact formats, section sorting, ANSI-aware width calculations, themes, and
  `NO_COLOR`/`CLICOLOR`/`CLICOLOR_FORCE` handling.
- **Completion for Bash, Zsh, Fish, and PowerShell**, excluding hidden
  arguments.
- **Stable diagnostics.** `ErrorCode`, `error_code_name()`, and
  `format_error()` make failures machine-classifiable and presentation-ready.
- **Expanded quality suite.** 84 unit test cases, golden help/usage tests, a
  multi-TU link test, and adversarial microbenchmarks.

## ✨ Features

- Single-header C++17 implementation with no compiled library
- Python-style `add_argument` API with positional and optional arguments
- Short aliases, long aliases, clusters, attached values, and `--` termination
- Actions: `store`, `store_true`, `store_false`, `append`, `count`, `help`,
  `help_all`
- Python-style `nargs` patterns (`?`, `*`, `+`), fixed counts, and ranges
- Typed defaults, implicit values, and environment fallbacks
- Choices, composable custom validators, and built-in filesystem/numeric
  validators
- Mutually exclusive groups, required groups, and explicit cardinality
- Dependencies, conflicts, and all-of requirements
- Nested subcommands with aliases, required/default dispatch, and dedicated
  unknown-subcommand errors
- Unknown option and positional capture via `parse_known_args(...)`
- Non-throwing `try_parse_args(...)` / `try_parse_known_args(...)`
- Typed namespace access and stable parsed values
- Customizable help with sections, tables, examples, colors, themes, and four
  output formats
- Shell completion generation for Bash, Zsh, Fish, and PowerShell
- Catch2 unit tests, golden output tests, multi-TU verification, and
  microbenchmarks

### Argument model

An argument is positional when its first name does not begin with `-`. It is
optional when its first name begins with `-`.

| Kind | Example | Destination | Notes |
| --- | --- | --- | --- |
| Positional | `add_argument("input")` | `input` | Required when `nargs` minimum is greater than zero. |
| Long option | `add_argument("--output-file")` | `output_file` | The last long alias wins; dashes become underscores. |
| Short option | `add_argument("-v")` | `v` | Short names must be exactly two characters. |
| Aliased option | `add_argument("-o", "--output-file")` | `output_file` | All aliases refer to one destination. |

Positional destinations keep their name verbatim. Optional destinations are
derived from the last long alias, or from the first short alias when no long
alias exists, with `-` replaced by `_`.

### Supported syntax

```text
--verbose
--output=result.txt
--output result.txt
-o result.txt
-oresult.txt
-vvvo result.txt
-- --not-an-option
```

- Long options accept a separate value or an inline value after `=`.
- Short clusters are parsed left to right until an option that takes a value is
  encountered; the remainder of that token becomes the value.
- `-n=5` is accepted and stores `5`.
- Negative numeric tokens such as `-42`, `-3.14`, and `-1e6` are values, not
  options.
- Inline values on flags (`--verbose=yes`) are rejected.
- An option-like token terminates value collection; use `--option=-value`,
  `.allow_hyphen_values()`, or `--` when a value must begin with `-`.

## 🚀 Getting Started

### ☑️ Prerequisites

A C++17 compiler is required. The test suite, examples, and benchmarks are
verified with GCC 16.2 (MinGW-w64, UCRT) on Windows. Newer GCC, Clang, and MSVC
releases are expected to work; older compilers have not been tested.

### ⚙️ Installation

With `include/` on the include path, either spelling works:

```cpp
#include <argparse/argparse.h>
// or
#include <argparse.h>
```

With CMake as a subdirectory:

```cmake
add_subdirectory(path/to/argparse)
target_link_libraries(your_app PRIVATE Argparser::Argparser)
```

With an installed package:

```cmake
find_package(Argparser CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Argparser::Argparser)
```

Install locally:

```sh
cmake -S . -B build -DARGPARSER_ENABLE_INSTALL=ON
cmake --build build
cmake --install build --prefix ./install
```

Project options:

- `ARGPARSER_BUILD_TESTS=ON|OFF`
- `ARGPARSER_BUILD_BENCHMARKS=ON|OFF`
- `ARGPARSER_RUN_BENCHMARKS=ON|OFF`
- `ARGPARSER_BUILD_EXAMPLES=ON|OFF`
- `ARGPARSER_ENABLE_INSTALL=ON|OFF`
- `ARGPARSER_USE_SYSTEM_CATCH2=ON|OFF`
- `ARGPARSER_CATCH2_SOURCE_DIR=<path>`

### 🤖 Quick start

```cpp
#include <argparse/argparse.h>

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    argparse::ArgumentParser parser(
        "sample",
        "Python-style argparse for C++",
        "Run with --help for the full option list.");

    parser.add_argument("input").help("Input file.");
    parser.add_argument("-v", "--verbose")
        .action("store_true")
        .help("Enable verbose output.");
    parser.add_argument("-n", "--number")
        .default_value(1)
        .help("Number of iterations.");

    try
    {
        const auto &args = parser.parse_args(argc, argv);

        const std::string input = args.get<std::string>("input");
        const bool verbose = args.get<bool>("verbose");
        const int number = args.get<int>("number");

        std::cout << "input: " << input << '\n'
                  << "verbose: " << std::boolalpha << verbose << '\n'
                  << "number: " << number << '\n';
    }
    catch (const argparse::HelpError &help)
    {
        std::cout << parser.format_help(help.show_hidden());
        return 0;
    }
    catch (const argparse::ArgumentError &error)
    {
        std::cerr << parser.format_error(error) << '\n';
        return 2;
    }

    return 0;
}
```

Compile with the repository's `include/` directory:

```sh
g++ -std=c++17 -Iinclude main.cpp -o sample
```

Typical invocations:

```sh
./sample data.txt
./sample data.txt -v -n 5
./sample --help
```

## Argument model

### Positional arguments

```cpp
parser.add_argument("input");
parser.add_argument("sources").nargs('*');       // zero or more
parser.add_argument("destination");              // required
```

Values are assigned greedily from left to right while reserving the required
minimum for later positionals. For `a b c` above, `sources = [a, b]` and
`destination = c`.

Requiredness follows `nargs`: `?` and `*` positionals are optional, `+`
positionals are required. Scalar positionals are required by default.
`.required(false)` makes a scalar positional optional, and a scalar default is
applied when the positional is absent.

```cpp
parser.add_argument("config").default_value("app.toml");
parser.add_argument("mode").nargs('?').required(false);
```

### Optional arguments and actions

```cpp
parser.add_argument("-v", "--verbose").action("store_true");
parser.add_argument("-q", "--quiet").action("store_false");
parser.add_argument("-d", "--debug").action("count");
parser.add_argument("-t", "--tag").action("append");
parser.add_argument("--mode").choices({"fast", "safe"}).default_value("safe");
```

| Action | Behavior when present | Default when absent |
| --- | --- | --- |
| `store` | Store the provided value. | no value |
| `store_true` | Store `true`. | `false` |
| `store_false` | Store `false`. | `true` |
| `append` | Append each occurrence. | empty list |
| `count` | Increment an occurrence counter. | `0` |
| `help` | Raise `HelpError` (hidden arguments omitted). | `false` |
| `help_all` | Raise `HelpError` with `show_hidden() == true`. | `false` |

```sh
./sample -vvv -t blue -t stable --mode fast data.txt
```

`append` composes with `nargs`:

```cpp
parser.add_argument("--pair").action("append").nargs(2);
// --pair a b --pair c d  ->  [a, b, c, d]
```

Required options are marked with `.required()`. A default or environment value
satisfies the requirement.

## Parsing modes

### Strict parsing

`parse_args(argc, argv)` throws `ParseError` for unknown options, `RequiredError`
for missing required values, and another `ArgumentError` subclass for invalid
values, choices, conversions requested by validators, or conflicting options.

### Permissive parsing

`parse_known_args(argc, argv)` keeps valid values and preserves the rest:

```cpp
const auto &args = parser.parse_known_args(argc, argv);

for (const auto &token : parser.unknown_options())
{
    std::cout << "unknown option: " << token << '\n';
}
for (const auto &token : parser.unknown_positionals())
{
    std::cout << "unknown positional: " << token << '\n';
}
// unknown_args() is the ordered union of both collections.
// unknown() returns {options, positionals}.
```

When the parser has no positionals, a non-option token immediately following an
unknown long option is grouped with that unknown option. When positionals are
registered, such tokens are offered to the positionals first.

Unknown tokens from subcommands are merged into the parent parser's unknown
collections. Unknown short clusters are preserved as the unrecognized suffix
(for example `-vz` with known `-v` records `-z`).

### Non-throwing parsing

`try_parse_args()` and `try_parse_known_args()` return a `ParseResult`:

```cpp
auto result = parser.try_parse_args(argc, argv);
if (!result)
{
    if (result.code == argparse::ErrorCode::HelpRequested)
    {
        std::cout << parser.format_help(); // honors --help-all automatically
        return 0;
    }
    std::cerr << result.error() << '\n';
    return 2;
}

const auto &args = *result;
```

`ParseResult` is contextually convertible to `bool`, supports `*` and `->`, and
`value()` throws the recorded `ArgumentError` if the parse failed. After a help
request, `format_help()` with no argument uses the remembered `--help` versus
`--help-all` mode.

## Value precedence

For every argument, the effective value is selected in this order:

1. A value supplied on the command line.
2. An environment fallback configured with `.env("NAME")`.
3. An explicit `.default_value(...)` / `.default_values(...)`.
4. The action-specific default (`false`, `true`, `0`, or empty).

An implicit value is **not** a fallback. It is used only when an option is
present on the command line without a value:

```cpp
parser.add_argument("--color")
    .nargs('?')
    .implicit_value("auto")
    .default_value("never");
```

```sh
./tool                 # color = never
./tool --color         # color = auto
./tool --color always  # color = always
```

An environment prefix applies to every configured environment name:

```cpp
parser.env_prefix("APP_");
parser.add_argument("--port").env("PORT"); // reads APP_PORT
```

`provided(name)` reports whether the value came from the command line. A value
from the environment or a default does not count as provided. Group cardinality
also counts only explicit command line occurrences, whereas required checks,
dependencies, choices, and validators consider environment and default values.

## Typed access

`parse_args()` and `parse_known_args()` return a reference to a `Namespace`
owned by the parser. The reference remains valid until the next parse or until
the parser is destroyed.

```cpp
const auto &args = parser.parse_args(argc, argv);

std::string input = args.get<std::string>("input");
int retries = args.get<int>("retries");
bool verbose = args.get<bool>("verbose");
std::vector<int> ports = args.getlist<int>("port");

std::optional<int> maybe = args.get_if<int>("retries"); // nullopt if absent
int fallback = args.get_or<int>("retries", 3);          // fallback if absent
std::string_view view = args.get<std::string_view>("input");
const char *raw = args.get<const char *>("input");
```

Supported conversions include `std::string`, `bool`, `char`, integral and
floating-point types, enums (via their underlying integer), `std::string_view`,
`const char *`, and `std::optional<T>` for `get_if<T>()`.

`has(name)` and `is_registered(name)` report registration. `provided(name)`
reports command line presence. `raw_values(name)` exposes the stored strings.
Querying an unregistered name throws `ParseError`; a failed conversion throws
`ArgumentError` with `ErrorCode::Conversion`.

## Validation and relationships

### Choices and custom validators

```cpp
parser.add_argument("--mode")
    .choices({"fast", "safe", "balanced"})
    .default_value("safe");

parser.add_argument("--port")
    .check([](const std::string &value)
           {
               return argparse::BuiltinValidators::in_range(1, 65535)(value);
           },
           "port must be between 1 and 65535")
    .check(argparse::BuiltinValidators::not_empty);
```

Multiple `.check(...)` calls compose with AND. Validators run for CLI,
environment, and default values. Exceptions thrown by a validator become
`ArgumentError` diagnostics with `ErrorCode::InvalidValue`.

Built-in validators:

| Validator | Meaning |
| --- | --- |
| `is_file` | Path names an existing regular file. |
| `is_dir` | Path names an existing directory. |
| `exists` | Path exists. |
| `in_range(min, max)` | Type-aware inclusive numeric range check. |
| `one_of({...})` | Value belongs to the supplied set. |
| `not_empty` | String has non-zero length. |

`in_range` parses into the requested numeric type, tolerates a leading `+` for
integers, and rejects fractional input for integral ranges instead of truncating
it.

### Groups

```cpp
auto &output = parser.add_mutually_exclusive_group();
output.add_argument("--json").action("store_true");
output.add_argument("--yaml").action("store_true");

auto &auth = parser.add_argument_group("authentication",
                                       "Choose one authentication method.");
auth.cardinality(argparse::ArgumentParser::GroupCardinality::ExactlyOne);
auth.add_argument("--token");
auth.add_argument("--password");
```

`cardinality()` supports `All`, `AtMostOne`, `ExactlyOne`, and `AtLeastOne`.
Arguments in a titled group are displayed under that heading. References
returned by `add_argument_group()` and `add_mutually_exclusive_group()` remain
stable.

### Dependencies and conflicts

```cpp
parser.add_argument("--user");
parser.add_argument("--password").depends_on("user");

parser.add_argument("--login")
    .action("store_true")
    .requires_all({"user", "password"});

parser.add_argument("--json").conflicts_with("yaml");
```

- `depends_on(name)` makes the dependent argument required when the named
  argument is supplied. Dependencies accept a destination or an option alias.
- `requires_all({...})` requires every named argument whenever the owning
  argument is supplied.
- `conflicts_with(name)` rejects the pair when both are supplied.
- Defaults and environment values count as "supplied" for dependency checks;
  unknown names are reported as `UsageError`.

## Subcommands

```cpp
argparse::ArgumentParser parser("tool", "Build and run jobs.");
parser.add_argument("-v", "--verbose").action("store_true");

auto &run = parser.add_subcommand("run", "Run a job");
run.add_argument("path");
run.add_argument("--mode").choices({"fast", "safe"}).default_value("fast");

auto &checkout = parser.add_subcommand({"checkout", "co"}, "Check out");
checkout.add_argument("branch");

const auto &args = parser.parse_args(argc, argv);

if (args.has_subcommand() && args.subcommand() == "run")
{
    const auto &run_args = args.subcommand_namespace();
    std::cout << run_args.get<std::string>("path") << '\n';
}
```

- Parent options must appear before the subcommand name.
- `add_subcommand({"checkout", "co"}, ...)` registers aliases; the canonical
  name is returned by `subcommand()`.
- `add_subcommand_alias("alias", "canonical")` adds an alias later.
- Subcommands may be nested.
- `require_subcommand()` makes dispatch mandatory.
- `default_subcommand("run")` dispatches unclaimed tokens to that child when no
  explicit subcommand is present.
- Unknown subcommands raise `UnknownSubcommandError`, when the parent has no
  unclaimed positionals that can accept the token.
- Each parser has its own help text, so `tool run --help` shows the child's
  options.

## Help formatting

Help text is synthesized from registered arguments and subcommands. `usage:`
lines are generated unless a custom string is supplied with `parser.usage(...)`.

```cpp
parser.formatter()
    .format(argparse::HelpFormatter::Format::Plain)
    .width(88)
    .use_color(false)
    .line_separator('-', 80)
    .prefix_info("MyTool CLI")
    .suffix_info("Documentation: https://example.com/mytool");

auto &examples = parser.formatter().add_section(
    "examples", "", argparse::HelpFormatter::Section::Type::Examples);
examples.add_example("mytool run data.txt --jobs 4",
                     "Run with four workers.");
```

Available formats:

- `Plain` - unstyled text
- `Colored` - ANSI-styled text
- `Markdown` - headings, fenced usage block, and Markdown lists
- `Compact` - single-line option rows without wrapping

Color output respects `NO_COLOR`, `CLICOLOR`, and `CLICOLOR_FORCE`, and width
detection ignores ANSI escapes. Themes include `nord()`, `monokai()`, and
`dracula()`.

```cpp
parser.print_help();                     // stdout, hidden arguments omitted
parser.print_help(std::cerr);
parser.print_usage();

std::string normal = parser.format_help();
std::string all = parser.format_help(true); // include hidden arguments
```

The built-in `-h`, `--help`, and `--help-all` actions raise `HelpError`. Catch
it and print `parser.format_help(help.show_hidden())` to honor the requested
mode. Mark individual arguments hidden with `.hidden()`.

Formatter sections are stored in a deque, so references stay valid. Use
`sort_sections(true)` for alphabetical section ordering, `visible(false)` to
hide a section, and `get_section(title)` to retrieve or create one.

## Shell completion

`generate_completion_script()` supports Bash, Zsh, Fish, and PowerShell:

```cpp
std::cout << parser.generate_completion_script("bash");
std::cout << parser.generate_completion_script("zsh");
std::cout << parser.generate_completion_script("fish");
std::cout << parser.generate_completion_script("powershell");
```

The `Shell` enum overload is available as
`argparse::ArgumentParser::Shell::{Bash,Zsh,Fish,PowerShell}`. Completion
includes visible options, choices, and subcommands, and excludes hidden
arguments. An unsupported shell name throws `ArgumentError` with
`ErrorCode::Usage`.

## 🧪 Testing and benchmarks

Configure with tests, benchmarks, and examples enabled:

```sh
cmake -S . -B build -G Ninja \
  -DARGPARSER_BUILD_TESTS=ON \
  -DARGPARSER_BUILD_BENCHMARKS=ON \
  -DARGPARSER_BUILD_EXAMPLES=ON
cmake --build build
```

Run the full CTest suite:

```sh
ctest --test-dir build --output-on-failure
```

This runs:

- `argparse_tests` - 84 unit test cases and 462 assertions covering
  registration, stable references, positionals, token syntax, actions,
  precedence, validation, groups, dependencies, subcommands, known-argument
  parsing, non-throwing parsing, namespace conversion, help formatting,
  completion, and environment handling.
- `argparse_golden_tests` - pinned plain help, hidden help, usage, and Markdown
  output.
- `argparse_multi_tu` - verifies that the header can be included from multiple
  translation units and linked into one program.

Run the Catch2 microbenchmarks:

```sh
./build/tests/argparse_benchmarks "[!benchmark]" --benchmark-samples 100
```

Benchmark categories:

```sh
./build/tests/argparse_benchmarks "[!benchmark][construction]"
./build/tests/argparse_benchmarks "[!benchmark][parse]"
./build/tests/argparse_benchmarks "[!benchmark][validation]"
./build/tests/argparse_benchmarks "[!benchmark][subcommands]"
./build/tests/argparse_benchmarks "[!benchmark][namespace]"
./build/tests/argparse_benchmarks "[!benchmark][scale]"
./build/tests/argparse_benchmarks "[!benchmark][help]"
```

Benchmarks cover parser construction, boolean/value parsing, 1,000 options,
10,000 positionals, 20 nested subcommands, 100,000-character values, repeated
failed lookups, defaults and validation, subcommand dispatch,
`parse_known_args`, namespace conversions, and help generation.

On multi-config generators, executables are under the configuration directory,
for example `build/Debug/tests/argparse_tests.exe`. Catch2 v3.8.1 is downloaded
automatically if it is not installed; use `ARGPARSER_CATCH2_SOURCE_DIR` for
offline or vendored builds. Configure with `-DARGPARSER_RUN_BENCHMARKS=ON` to
register a short benchmark pass with CTest.

## 📚 Documentation

- [Usage guide](docs/README.md) - detailed examples and explanations
- [examples/Demo.cpp](examples/Demo.cpp) - runnable demonstration
- [REVIEW.md](REVIEW.md) - historical review of the 0.1.0 implementation
- [IMPROVE.md](IMPROVE.md) - improvement roadmap and completed work

## 🔰 Contributing

- **💬 [Join the Discussions](https://github.com/djoezeke/argparser/discussions)**:
  Share your insights, provide feedback, or ask questions.
- **🐛 [Report Issues](https://github.com/djoezeke/argparser/issues)**: Submit
  bugs you find or log feature requests.
- **💡 [Submit Pull Requests](https://github.com/djoezeke/argparser/pulls)**:
  Review open PRs, and submit your own.

<details closed>
<summary>Contributing Guidelines</summary>

1. **Fork the Repository**: Start by forking the project repository to your
   GitHub account.
2. **Clone Locally**: Clone the forked repository to your local machine using a
   git client.
   ```sh
   git clone --recursive https://github.com/djoezeke/argparser
   ```
3. **Create a New Branch**: Always work on a new branch with a descriptive name.
   ```sh
   git checkout -b new-feature-x
   ```
4. **Make Your Changes**: Develop and test your changes locally.
   ```sh
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```
5. **Commit Your Changes**: Commit with a clear message.
   ```sh
   git commit -m 'Implemented feature x.'
   ```
6. **Push to GitHub**: Push your changes to your fork.
   ```sh
   git push origin new-feature-x
   ```
7. **Submit a Pull Request**: Create a PR against the original repository.
   Describe the changes and their motivation.
8. **Review**: Once reviewed and approved, it will be merged into the main
   branch.

</details>

<details closed>
<summary>Contributor Graph</summary>
<br>
<p align="left">
   <a href="https://github.com/djoezeke/argparser/graphs/contributors">
      <img src="https://contrib.rocks/image?repo=djoezeke/argparser">
   </a>
</p>
</details>

---

## 🙌 Acknowledgments

`Argparser` is inspired by Python `argparse` and aims to provide a simple,
lightweight solution for C/C++ projects.

## 📃 License

This project is protected under the [MIT](LICENSE) License. See the
[LICENSE](LICENSE) file for details.
