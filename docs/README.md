# Argparse Documentation

`argparse` is a single-file C++17 command line parser inspired by Python's
`argparse` module. It supports positional arguments, optional flags, typed
values, validation, environment fallbacks, argument groups, subcommands, and
custom help formatting.

This document is the practical usage guide. See [REVIEW.md](../REVIEW.md) for
current limitations and [IMPROVE.md](../IMPROVE.md) for the suggested roadmap.

## Contents

- [Installation and build](#installation-and-build)
- [Quick start](#quick-start)
- [Argument model](#argument-model)
- [Positional arguments](#positional-arguments)
- [Optional arguments and flags](#optional-arguments-and-flags)
- [Defaults, environment fallbacks, and implicit values](#defaults-environment-fallbacks-and-implicit-values)
- [Validation](#validation)
- [Argument groups and dependencies](#argument-groups-and-dependencies)
- [Parsing modes](#parsing-modes)
- [Subcommands](#subcommands)
- [The Namespace API](#the-namespace-api)
- [Help formatting](#help-formatting)
- [Error handling](#error-handling)
- [Shell completion](#shell-completion)
- [Testing and benchmarks](#testing-and-benchmarks)
- [API quick reference](#api-quick-reference)

## Installation and build

The library is a single header named `argparse.h`. Add the repository root to
your include path and include the header:

```cpp
#include "argparse.h"
```

With CMake, add the repository as a subdirectory and link the interface target:

```cmake
add_subdirectory(path/to/argparse)
target_link_libraries(your_app PRIVATE Argparser::Argparser)
```

Or configure, build, and install it:

```sh
cmake -S . -B build -DARGPARSER_ENABLE_INSTALL=ON
cmake --build build
cmake --install build --prefix ./install
```

Then consume the installed package:

```cmake
find_package(Argparser CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Argparser::Argparser)
```

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `ARGPARSER_BUILD_TESTS` | `OFF` | Build the Catch2 unit tests. |
| `ARGPARSER_BUILD_BENCHMARKS` | `ON` | Build the Catch2 microbenchmark executable when tests are enabled. |
| `ARGPARSER_RUN_BENCHMARKS` | `OFF` | Register the benchmark executable with CTest. |
| `ARGPARSER_BUILD_EXAMPLES` | `OFF` | Build `examples/Demo.cpp`. |
| `ARGPARSER_ENABLE_INSTALL` | `OFF` | Generate install and package rules. |
| `ARGPARSER_USE_SYSTEM_CATCH2` | `OFF` | Use an installed Catch2 v3 instead of downloading it. |
| `ARGPARSER_CATCH2_SOURCE_DIR` | empty | Use a local Catch2 checkout instead of downloading it. |

> **Known issue:** the current header defines its out-of-class member
> functions without `inline`. Including `argparse.h` in more than one
> translation unit of the same executable causes multiple-definition linker
> errors. Until this is fixed, include the parser in one translation unit or
> apply the fix described in [REVIEW.md](../REVIEW.md).

## Quick start

```cpp
#include "argparse.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    argparse::ArgumentParser parser(
        "copy",
        "Copy a file from one location to another.",
        "Example: copy source.txt backup.txt -v");

    parser.add_argument("source").help("Path to the source file.");
    parser.add_argument("destination").help("Path to the destination file.");
    parser.add_argument("-v", "--verbose")
        .action("store_true")
        .help("Print progress information.");
    parser.add_argument("-f", "--force")
        .action("store_true")
        .help("Overwrite the destination if it exists.");

    try
    {
        const auto &args = parser.parse_args(argc, argv);

        const std::string source = args.get<std::string>("source");
        const std::string destination = args.get<std::string>("destination");
        const bool verbose = args.get<bool>("verbose");
        const bool force = args.get<bool>("force");

        std::cout << "copying " << source << " to " << destination << '\n';
        if (verbose)
        {
            std::cout << "force: " << std::boolalpha << force << '\n';
        }
    }
    catch (const argparse::HelpError &)
    {
        parser.print_help();
        return 0;
    }
    catch (const argparse::ArgumentError &error)
    {
        std::cerr << "error: " << error.what() << "\n\n";
        parser.print_usage(std::cerr);
        return 2;
    }

    return 0;
}
```

Compile it with:

```sh
g++ -std=c++17 -I. main.cpp -o copy
```

Typical invocations:

```sh
./copy source.txt backup.txt
./copy source.txt backup.txt -v --force
./copy --help
```

## Argument model

An argument is positional when its first name does not start with `-`.
An argument is optional when its first name starts with `-`. All aliases for an
optional argument must start with `-`.

```cpp
parser.add_argument("input");                 // positional
parser.add_argument("-v", "--verbose");       // optional with two aliases
parser.add_argument("--output-file");         // optional with a long alias
```

The destination name, used as the key in the parsed namespace, is derived as
follows:

- If any long alias exists, the last long alias is used without the leading
  `--`.
- Otherwise the first short alias is used without the leading `-`.
- Dashes in an optional destination become underscores.

```cpp
parser.add_argument("-o", "--output-file");   // dest: output_file
parser.add_argument("-q");                    // dest: q
parser.add_argument("plain-file");            // dest: plain-file
```

`Argument::dest()` returns the destination, but references returned by
`add_argument()` can be invalidated by later calls that grow the parser's
argument vector. Configure each argument immediately after creating it.

## Positional arguments

A positional argument is required by default and accepts one value:

```cpp
parser.add_argument("input");
```

Access the value through the namespace:

```cpp
const auto &args = parser.parse_args(argc, argv);
std::string input = args.get<std::string>("input");
```

### Positional counts with `nargs`

`nargs` accepts either a count or a Python-style pattern:

| Call | Accepted values |
| --- | --- |
| `.nargs('?')` | zero or one |
| `.nargs('*')` | zero or more |
| `.nargs('+')` | one or more |
| `.nargs(std::size_t{2})` | exactly two |

```cpp
parser.add_argument("sources").nargs('*').required(false);
parser.add_argument("destination");
```

When several positionals are declared, values are assigned greedily from left
to right while reserving the minimum required count for later positionals. In
the example above, `a b c` gives `sources = [a, b]` and
`destination = c`.

Use the explicit `std::size_t{2}` form rather than `.nargs(2)`. The current
overloads for `std::size_t` and `char` make a plain integer literal ambiguous.

A positional remains required even when `nargs` allows zero values. Call
`.required(false)` when zero values are valid. Scalar positional defaults are
also unreachable because the missing-positional check runs before defaults are
applied; use `.nargs('*').required(false).default_value(...)` if a default is
needed. Both behaviors are recorded in [REVIEW.md](../REVIEW.md).

## Optional arguments and flags

### Long options

Long options accept a separate value or an inline value after `=`:

```cpp
parser.add_argument("-n", "--name");
```

```sh
./tool --name alice
./tool --name=alice
```

The inline form is the safest way to pass a value that itself starts with `-`:

```sh
./tool --name=-weird-value
```

### Short options and clusters

Short options accept separate values, attached values, and boolean clusters:

```cpp
parser.add_argument("-v", "--verbose").action("store_true");
parser.add_argument("-o", "--output");
```

```sh
./tool -o result.txt
./tool -oresult.txt
./tool -voresult.txt     # -v, then -o with value result.txt
./tool -vvv              # repeated -v (useful with the count action)
```

### Actions

The default action stores a value. Other actions are selected with
`.action("name")`:

| Action | Behavior | Default when absent |
| --- | --- | --- |
| `store` | Store the provided value. | no value |
| `store_true` | Store `true` when present. | `false` |
| `store_false` | Store `false` when present. | `true` |
| `append` | Append each value to a list. | empty list |
| `count` | Count occurrences. | `0` |
| `help` | Raise `HelpError`. | `false` |

```cpp
parser.add_argument("-v", "--verbose").action("store_true");
parser.add_argument("--quiet").action("store_false");
parser.add_argument("-d", "--debug").action("count");
parser.add_argument("-t", "--tag").action("append");

// -d -d -t a --tag b
// verbose = true, quiet = true, debug = 2, tag = [a, b]
```

`append` can be combined with `nargs` to append several values per occurrence:

```cpp
parser.add_argument("--pair").action("append").nargs(std::size_t{2});
// --pair a b --pair c d  ->  [a, b, c, d]
```

Inline values on flags are ignored. Use a `store_true`/`store_false` pair
instead of `--flag=false`:

```cpp
parser.add_argument("--color").action("store_true");
parser.add_argument("--no-color").action("store_false");
```

### Required options

Options are optional by default. Mark one required with `.required()`:

```cpp
parser.add_argument("--token").required().help("API token (required).");
```

A required option with a default value is accepted without CLI input; the
default satisfies the required check.

### Option counts with `nargs`

```cpp
parser.add_argument("--files").nargs('*');          // any number, incl. zero
parser.add_argument("--include").nargs('+');        // one or more
parser.add_argument("--point").nargs(std::size_t{2}); // exactly two
```

Values are consumed until the next token that looks like an option. Numeric
tokens such as `-42` and `-3.14e2` are treated as values, not options.

## Defaults, environment fallbacks, and implicit values

```cpp
parser.add_argument("--retries").default_value(3);
parser.add_argument("--ratio").default_value(1.5);
parser.add_argument("--label").default_value("none");
parser.add_argument("--enabled").default_value(true);
parser.add_argument("--coords").nargs(std::size_t{2}).default_values({"0", "0"});
```

The current precedence for an absent argument is:

1. CLI value, when provided.
2. `.default_value(...)` / `.default_values(...)`.
3. `.env("NAME")`, when the environment variable is set.
4. Action-specific default (`false`, `true`, `0`, or empty list).

An environment fallback is therefore only observed when no default is
configured:

```cpp
parser.add_argument("--port").env("APP_PORT");
```

The environment value is converted and validated like a CLI value.

`implicit_value` supplies a value when an option is present without one. Pair
it with a `nargs` pattern that allows zero values and with an explicit default:

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

Without an explicit default, the current finalization logic also applies the
implicit value when the option is absent. This is a known issue.

## Validation

### Choices

```cpp
parser.add_argument("--mode")
    .choices({"fast", "safe", "balanced"})
    .default_value("safe");
```

An invalid value raises `ArgumentError` with a message such as:

```text
invalid choice for argument 'mode': turbo
```

Choices are checked for CLI values, environment values, and defaults.

### Custom validators

```cpp
parser.add_argument("--port")
    .check([](const std::string &value)
           {
               const int port = std::stoi(value);
               return port > 0 && port <= 65535;
           },
           "port must be between 1 and 65535")
    .help("TCP port.");
```

The callback receives the raw string. Return `false` to reject the value.
When a non-empty message is supplied, it becomes the error text; otherwise the
parser generates a generic invalid-value message.

An argument stores a single validator callback, so only the last
`.check(...)` call is retained. Combine conditions inside one callback:

```cpp
parser.add_argument("--percent")
    .check([](const std::string &value)
           {
               const int percent = std::stoi(value);
               return percent >= 0 && percent <= 100 && percent != 13;
           },
           "percent must be in [0, 100] and must not be 13")
    .help("Percentage to apply.");
```

### Built-in validators

| Validator | Meaning |
| --- | --- |
| `BuiltinValidators::is_file` | Path exists and can be opened for reading. |
| `BuiltinValidators::is_dir` | Path exists and is a directory. |
| `BuiltinValidators::in_range(min, max)` | Numeric value is within the inclusive range. |

```cpp
parser.add_argument("--config").check(argparse::BuiltinValidators::is_file);
parser.add_argument("--output-dir").check(argparse::BuiltinValidators::is_dir);
parser.add_argument("--workers").check(argparse::BuiltinValidators::in_range(1, 32));
```

`in_range` converts through `std::stod` and casts to the requested type, so
decimal input for an integer range is truncated before comparison.

## Argument groups and dependencies

### Mutually exclusive groups

```cpp
auto &output = parser.add_mutually_exclusive_group();
output.add_argument("--json").action("store_true");
output.add_argument("--yaml").action("store_true");
```

Supplying both options raises `ArgumentError`. Supplying neither is allowed.

### Required groups

```cpp
auto &auth = parser.add_argument_group("authentication",
                                       "Choose one authentication method.");
auth.exclusive(true).required(true);
auth.add_argument("--token");
auth.add_argument("--password");
```

`exclusive(true)` allows at most one member. `required(true)` requires at least
one member. Together they require exactly one. Use a titled group when the
error message should identify the group.

Groups also control help layout: arguments added to a group appear under that
group's section instead of the default "optional arguments" or
"positional arguments" section.

### Conditional dependencies

```cpp
parser.add_argument("--user");
parser.add_argument("--password").depends_on("user");
```

When `--user` is provided, `--password` becomes mandatory and a missing value
raises `RequiredIfError`. When `--user` is absent, `--password` remains
optional. The dependency name is the destination name, not an option alias.
A value supplied only by a default does not count as "provided" for this check.

## Parsing modes

### Strict parsing with `parse_args`

`parse_args(argc, argv)` throws `ParseError` for unknown options and
`ArgumentError` for invalid values, missing values, and extra positionals.

### Permissive parsing with `parse_known_args`

`parse_known_args(argc, argv)` captures unknown option tokens in
`parser.unknown_args()`:

```cpp
const auto &args = parser.parse_known_args(argc, argv);
for (const std::string &token : parser.unknown_args())
{
    std::cout << "unknown: " << token << '\n';
}
```

The current implementation only preserves unknown option-like tokens. Unknown
positional tokens, and separate values that follow an unknown option, still
raise an error. See [REVIEW.md](../REVIEW.md).

### Negative numbers and `--`

Numeric tokens such as `-42`, `-3.14`, and `-1e6` are values rather than
options, whether they are passed to an option or assigned to a positional.
A bare `-` is also a positional value.

Use `--` to stop option parsing. Every token after it is treated as a
positional value:

```sh
./tool -- -strange-file --looks-like-an-option
```

### Reusing a parser

A parser can parse several command lines sequentially. Parsed values,
occurrences, unknown arguments, and subcommand results are reset at the start
of every parse:

```cpp
parser.parse_args(argc1, argv1);
parser.parse_args(argc2, argv2);
```

The parser is not thread-safe. Do not parse concurrently with the same
instance.

## Subcommands

Register subcommands with `add_subcommand()` and configure the returned parser:

```cpp
argparse::ArgumentParser parser("tool", "Build and run jobs.");

parser.add_argument("-v", "--verbose").action("store_true");

auto &run = parser.add_subcommand("run", "Run a job");
run.add_argument("path");
run.add_argument("--mode")
    .choices({"fast", "safe"})
    .default_value("fast");

auto &build = parser.add_subcommand("build", "Build artifacts");
build.add_argument("--jobs").default_value(2);

const auto &args = parser.parse_args(argc, argv);

if (args.has_subcommand() && args.subcommand() == "run")
{
    const auto &run_args = args.subcommand_namespace();
    std::string path = run_args.get<std::string>("path");
    std::string mode = run_args.get<std::string>("mode");
}
else if (args.subcommand() == "build")
{
    int jobs = args.subcommand_namespace().get<int>("jobs");
}
```

Notes:

- Parent options must appear before the subcommand name. Tokens after the
  subcommand belong to the child parser.
- Subcommands may be nested:

```cpp
auto &remote = parser.add_subcommand("remote", "Manage remotes");
auto &add = remote.add_subcommand("add", "Add a remote");
add.add_argument("name");
add.add_argument("url");
```

```sh
./git remote add origin https://example.com/repo.git
```

- Each parser has its own built-in help. `tool run --help` raises `HelpError`
  from the child parser.
- Unknown child options propagate to the parent's `unknown_args()` in
  `parse_known_args` mode.
- Unknown subcommand names are currently reported as unrecognized positional
  arguments rather than as a dedicated subcommand error.

## The Namespace API

`parse_args()` and `parse_known_args()` return a reference to a `Namespace`
owned by the parser. The reference remains valid until the next parse or until
the parser is destroyed.

| Method | Meaning |
| --- | --- |
| `has(name)` | Argument was registered and has an entry. |
| `provided(name)` | Argument occurred on the command line. |
| `raw_values(name)` | Raw string values. |
| `get<T>(name)` | Convert the first value to `T`. |
| `getlist<T>(name)` | Convert every value to `T`. |
| `has_subcommand()` | A subcommand was selected. |
| `subcommand()` | Selected subcommand name. |
| `subcommand_namespace()` | Namespace produced by the child parser. |

`has()` reports registration, not presence on the command line. Use
`provided()` for presence, except for the current positional-argument
limitation noted below.

Supported conversions:

- `std::string`
- `bool`: `1`, `true`, `yes`, `on` and `0`, `false`, `no`, `off` in any case
- integral types such as `int`, `long`, `std::size_t`
- floating-point types such as `float` and `double`

```cpp
const auto &args = parser.parse_args(argc, argv);

bool verbose = args.get<bool>("verbose");
int retries = args.get<int>("retries");
double ratio = args.get<double>("ratio");
std::vector<std::string> tags = args.getlist<std::string>("tag");
```

`get<T>()` throws `ArgumentError` when the argument has no values or the value
cannot be converted. Querying an unregistered name throws `ParseError`.

Current conversion limitations:

- Enums are not supported by `get<T>()`; convert from `std::string` yourself.
- `const char *` is not a supported target type.
- `get<T>()` returns only the first value; use `getlist<T>()` for all values.
- `provided()` always returns `false` for positional arguments because their
  occurrence counter is not updated.

## Help formatting

Each parser owns a `HelpFormatter`. Configure it through `parser.formatter()`:

```cpp
parser.formatter()
    .format(argparse::HelpFormatter::Format::Plain)
    .width(88)
    .use_color(false)
    .line_separator('-', 80)
    .prefix_info("MyTool CLI")
    .suffix_info("Documentation: https://example.com/mytool");
```

`Plain` and `Colored` output are implemented. `Markdown` currently falls back
to colored output, and `Compact` only changes the heading color. Colors are
enabled by default regardless of whether output is redirected.

### Custom sections and examples

```cpp
auto &examples = parser.formatter().add_section(
    "examples",
    "",
    argparse::HelpFormatter::Section::Type::Examples);
examples.add_example("mytool run data.txt --jobs 4",
                     "Run with four workers.");
examples.add_example("mytool --help", "Show all options.");

auto &options = parser.formatter().add_section("advanced options");
options.add_item("--experimental", "", "Enable experimental features.", false);
options.add_text("Experimental features may change without notice.");
```

Sections added directly to the formatter are retained when the parser builds
its help text. `add_section()` appends a new section; `get_section()` returns an
existing section by title or creates it.

A `Section&` returned by `add_section()` can be invalidated when another
section is added. Configure each section immediately, or fetch it again with
`get_section()` after adding other sections.

### Alignment and themes

```cpp
argparse::HelpFormatter::Alignment alignment;
alignment.indent_size = 4;
alignment.indent_char = ' ';
alignment.option_width = 24;
alignment.max_help_position = 32;
alignment.wrap_help_text = true;
parser.formatter().alignment(alignment);

parser.formatter()
    .use_color(true)
    .color_theme(argparse::HelpFormatter::ColorTheme::nord());
```

Predefined themes are `nord()`, `monokai()`, and `dracula()`. You can also
modify a `ColorTheme` field by field.

### Printing help and usage

```cpp
parser.print_help();          // stdout
parser.print_help(std::cerr);
parser.print_usage();

std::string help = parser.format_help();       // excludes hidden arguments
std::string all = parser.format_help(true);    // includes hidden arguments
std::string usage = parser.format_usage();
```

Every parser automatically registers `-h`, `--help`, and `--help-all`. All
three raise `HelpError`; catch it in `main()` and print the help text. Mark
individual arguments hidden with `.hidden()`.

In the current implementation `--help-all` raises the same `HelpError` as
`--help`, so a handler that always calls `print_help()` still hides hidden
arguments. Call `format_help(true)` when you need to include them. This is a
known defect recorded in [REVIEW.md](../REVIEW.md).

The generated usage line is generic (`program [options] [arguments]`) unless a
custom usage string is provided with `parser.usage(...)`. The current
`format_usage()` helper prints only the `usage:` prefix and program name, so
many programs provide their own usage text.

## Error handling

Exceptions are enabled by default. The hierarchy is:

```text
std::runtime_error
└── ArgumentError
    ├── ParseError        unknown option, invalid syntax
    ├── UsageError        parser misuse
    ├── RequiredError     missing required argument or value
    ├── RequiredIfError   dependency present but dependent value missing
    └── HelpError         help requested
```

A robust `main()` pattern:

```cpp
int main(int argc, char **argv)
{
    argparse::ArgumentParser parser("tool", "Description.");
    // ... register arguments ...

    try
    {
        const auto &args = parser.parse_args(argc, argv);
        // ... use args ...
        return 0;
    }
    catch (const argparse::HelpError &)
    {
        parser.print_help();
        return 0;
    }
    catch (const argparse::ArgumentError &error)
    {
        std::cerr << "error: " << error.what() << "\n\n";
        parser.print_usage(std::cerr);
        return 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << "unexpected error: " << error.what() << '\n';
        return 1;
    }
}
```

`ARGPARSE_NO_EXCEPTIONS` exists in the header but the current no-exception
code paths are incomplete and unsafe. Use exceptions.

## Shell completion

`generate_completion_script("bash")` returns a Bash completion function based
on the registered option names and subcommands:

```cpp
std::cout << parser.generate_completion_script("bash");
```

Only `"bash"` is implemented. Other shell names return
`"Unsupported shell: <name>"` instead of raising an error.

## Testing and benchmarks

Configure with tests and benchmarks enabled:

```sh
cmake -S . -B build \
  -DARGPARSER_BUILD_TESTS=ON \
  -DARGPARSER_BUILD_BENCHMARKS=ON \
  -DARGPARSER_BUILD_EXAMPLES=ON
cmake --build build
```

Run the unit tests:

```sh
ctest --test-dir build --output-on-failure
```

Run the benchmark executable directly:

```sh
./build/tests/argparse_benchmarks "[!benchmark]" --benchmark-samples 100
```

On multi-config generators, the executable is under the configuration
directory, for example `build/Debug/tests/argparse_benchmarks.exe`.

Benchmark categories can be selected with Catch2 tags:

```sh
./build/tests/argparse_benchmarks "[!benchmark][construction]"
./build/tests/argparse_benchmarks "[!benchmark][parse]"
./build/tests/argparse_benchmarks "[!benchmark][help]"
```

The microbenchmark suite currently measures:

- parser construction with 20 and 100 options
- parsing boolean flags, long options, short clusters, and negative numbers
- positional and variadic argument assignment
- 100 append occurrences and a 100-character `count` cluster
- a realistic mixed command line
- default finalization and choice/custom validation
- nested subcommand dispatch and `parse_known_args`
- 100 string-to-integer conversions with `getlist`
- colored/plain help generation and usage formatting

To register the benchmark with CTest as well, configure with
`-DARGPARSER_RUN_BENCHMARKS=ON`. CTest then runs a short benchmark pass with
the `benchmark` label.

Catch2 v3.8.1 is downloaded automatically when it is not available. For
offline or vendored builds, set `ARGPARSER_CATCH2_SOURCE_DIR` to a local Catch2
checkout or `ARGPARSER_USE_SYSTEM_CATCH2=ON`.

## API quick reference

### `ArgumentParser`

| Method | Purpose |
| --- | --- |
| `ArgumentParser(program, description, epilog)` | Create a parser. |
| `program(...)`, `description(...)`, `usage(...)`, `epilog(...)` | Set metadata. |
| `add_argument(names...)` | Add a positional or optional argument. |
| `add_argument_group(title, description)` | Add a help/validation group. |
| `add_mutually_exclusive_group(required)` | Add an exclusive group. |
| `add_subcommand(name, description)` | Add or retrieve a child parser. |
| `parse_args(argc, argv)` | Strict parse. |
| `parse_known_args(argc, argv)` | Parse and capture unknown options. |
| `unknown_args()` | Unknown options from the last permissive parse. |
| `format_help(show_hidden)` | Build the help string. |
| `format_usage()` | Build the usage string. |
| `print_help(stream)`, `print_usage(stream)` | Write help or usage. |
| `formatter()` | Access the `HelpFormatter`. |
| `generate_completion_script(shell)` | Generate Bash completion. |
| `interactive(bool)` | Prompt for missing required values. |

### `Argument`

| Method | Purpose |
| --- | --- |
| `.help(text)` | Set help text. |
| `.hidden(bool)` | Hide from default help output. |
| `.required(bool)` | Require the argument. |
| `.metavar(text)` | Set the placeholder shown in help. |
| `.action(name)` | Select `store`, `store_true`, `store_false`, `append`, `count`, or `help`. |
| `.nargs(count)` / `.nargs(pattern)` | Set the accepted value count. |
| `.default_value(value)` | Set one default value. |
| `.default_values({...})` | Set several default values. |
| `.implicit_value(value)` | Value used when the option is present without data. |
| `.env(name)` | Read a fallback from an environment variable. |
| `.choices({...})` | Restrict values to a set. |
| `.check(callback, message)` | Set a custom validator. |
| `.depends_on(dest)` | Require this argument when another destination is present. |
| `.dest()` | Return the namespace key. |

### `HelpFormatter`

| Method | Purpose |
| --- | --- |
| `format(Format)` | Select `Plain`, `Colored`, `Markdown`, or `Compact`. |
| `width(size)`, `alignment(Alignment)` | Control layout and wrapping. |
| `use_color(bool)`, `color_theme(ColorTheme)` | Control colors. |
| `program(...)`, `usage(...)`, `description(...)`, `epilog(...)` | Set text blocks. |
| `prefix_info(...)`, `suffix_info(...)` | Add text before/after help. |
| `line_separator(...)` | Add a separator line. |
| `add_section(...)`, `get_section(...)` | Manage custom sections. |
| `format_help()`, `format_usage()` | Build output strings. |
| `wrap_text(...)`, `colorize(...)`, `hyperlink(...)` | Formatting helpers. |

For a complete list of known behaviors and defects, see
[REVIEW.md](../REVIEW.md). For planned work, see [IMPROVE.md](../IMPROVE.md).
