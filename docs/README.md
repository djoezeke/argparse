# Argparse Documentation

`argparse` is a single-header C++17 command line parser inspired by Python's
`argparse` module. It supports positional arguments, optional flags, typed
values, composable validation, environment fallbacks, argument groups and
relationships, nested subcommands, configurable help output, and shell
completion.

This guide describes version 0.2.0. [REVIEW.md](../REVIEW.md) records the
historical 0.1.0 review that motivated the release, and
[IMPROVE.md](../IMPROVE.md) tracks the roadmap and completed improvements.

## Contents

- [Installation and build](#installation-and-build)
- [Quick start](#quick-start)
- [Argument model](#argument-model)
- [Positional arguments](#positional-arguments)
- [Optional arguments and flags](#optional-arguments-and-flags)
- [Defaults, environment fallbacks, and implicit values](#defaults-environment-fallbacks-and-implicit-values)
- [Validation](#validation)
- [Argument groups and relationships](#argument-groups-and-relationships)
- [Parsing modes](#parsing-modes)
- [Subcommands](#subcommands)
- [The Namespace API](#the-namespace-api)
- [Help formatting](#help-formatting)
- [Error handling](#error-handling)
- [Shell completion](#shell-completion)
- [Testing and benchmarks](#testing-and-benchmarks)
- [API quick reference](#api-quick-reference)

## Installation and build

The canonical header is `include/argparse/argparse.h`. A forwarding header,
`include/argparse.h`, supports the traditional include spelling. Add
`include/` to your include path and use either form:

```cpp
#include <argparse/argparse.h>
// or
#include <argparse.h>
```

Every function in the header is `inline`, so it may be included from any number
of translation units in the same program.

With CMake, add the repository as a subdirectory and link the interface target:

```cmake
add_subdirectory(path/to/argparse)
target_link_libraries(your_app PRIVATE Argparser::Argparser)
```

To install and consume the package:

```sh
cmake -S . -B build -DARGPARSER_ENABLE_INSTALL=ON
cmake --build build
cmake --install build --prefix ./install
```

```cmake
find_package(Argparser CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE Argparser::Argparser)
```

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `ARGPARSER_BUILD_TESTS` | `OFF` | Build the Catch2 unit tests, golden tests, and multi-TU test. |
| `ARGPARSER_BUILD_BENCHMARKS` | `ON` | Build the Catch2 microbenchmark executable when tests are enabled. |
| `ARGPARSER_RUN_BENCHMARKS` | `OFF` | Register a short benchmark pass with CTest. |
| `ARGPARSER_BUILD_EXAMPLES` | `OFF` | Build `examples/Demo.cpp`. |
| `ARGPARSER_ENABLE_INSTALL` | `OFF` | Generate install and package rules. |
| `ARGPARSER_USE_SYSTEM_CATCH2` | `OFF` | Use an installed Catch2 v3 instead of downloading it. |
| `ARGPARSER_CATCH2_SOURCE_DIR` | empty | Use a local Catch2 checkout instead of downloading it. |

## Quick start

```cpp
#include <argparse/argparse.h>

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

Compile it with:

```sh
g++ -std=c++17 -Iinclude main.cpp -o copy
```

Typical invocations:

```sh
./copy source.txt backup.txt
./copy source.txt backup.txt -v --force
./copy --help
```

## Argument model

An argument is positional when its first name does not start with `-`. An
argument is optional when its first name starts with `-`. All aliases for an
optional argument must also start with `-`.

```cpp
parser.add_argument("input");                 // positional
parser.add_argument("-v", "--verbose");       // optional with two aliases
parser.add_argument("--output-file");         // optional with a long alias
```

Registration validates names immediately:

- Option names cannot contain `=`.
- Short option names must be exactly two characters (`-v`, not `-verbose`).
- Long option names use the `--name` form.
- Positional arguments accept exactly one name.
- Duplicate option names and duplicate destinations are rejected with
  `ErrorCode::InvalidRegistration`.

The destination name, used as the key in the parsed namespace, is derived as
follows:

- If any long alias exists, the last long alias is used without the leading
  `--`.
- Otherwise the first short alias is used without the leading `-`.
- Dashes in an optional destination become underscores.
- A positional destination is the positional name unchanged.

```cpp
parser.add_argument("-o", "--output-file");   // dest: output_file
parser.add_argument("-q");                    // dest: q
parser.add_argument("plain-file");            // dest: plain-file
```

References returned by `add_argument()`, `add_argument_group()`,
`add_mutually_exclusive_group()`, and `add_section()` remain valid after later
registrations. Arguments, groups, and formatter sections are stored in
`std::deque`.

### Value syntax

| Syntax | Example | Meaning |
| --- | --- | --- |
| Long, separate value | `--name alice` | `name = alice` |
| Long, inline value | `--name=alice` | `name = alice` |
| Short, separate value | `-o result.txt` | `o = result.txt` |
| Short, attached value | `-oresult.txt` | `o = result.txt` |
| Short, equals form | `-n=5` | `n = 5` |
| Boolean cluster | `-vvv` | three occurrences of `v` |
| Cluster ending in a value | `-voresult.txt` | `v`, then `o = result.txt` |
| Option terminator | `-- --not-an-option` | positional value `--not-an-option` |

Additional token rules:

- Negative numeric tokens such as `-42`, `-3.14`, and `-1e6` are values, not
  options, whether assigned to an option or a positional. A bare `-` is also a
  value.
- Value collection stops at the next option-like token. Use the inline form
  (`--name=-weird-value`), `.allow_hyphen_values()`, or `--` when a value itself
  starts with `-`.
- Inline values on flags are rejected. `--verbose=yes` raises
  `ErrorCode::InvalidValue`; use a `store_true`/`store_false` pair instead.
- Unknown long options suggest a close registered spelling when one exists.

## Positional arguments

A scalar positional argument is required by default and accepts one value:

```cpp
parser.add_argument("input");
```

Access the value through the namespace:

```cpp
const auto &args = parser.parse_args(argc, argv);
std::string input = args.get<std::string>("input");
```

### Positional counts with `nargs`

`nargs` accepts a count, a range, a `Nargs` value, or a Python-style pattern:

| Call | Accepted values | Required |
| --- | --- | --- |
| `.nargs('?')` | zero or one | no |
| `.nargs('*')` | zero or more | no |
| `.nargs('+')` | one or more | yes |
| `.nargs(2)` | exactly two | yes |
| `.nargs(Nargs::exactly(2))` | exactly two | yes |
| `.nargs(Nargs::range(1, 3))` | one to three | yes |

```cpp
parser.add_argument("sources").nargs('*');
parser.add_argument("destination");
```

The plain integer overload is implemented with a constrained template, so
`.nargs(2)` is unambiguous; no `std::size_t{2}` cast is needed.

When several positionals are declared, values are assigned greedily from left
to right while reserving the minimum required count for later positionals. In
the example above, `a b c` gives `sources = [a, b]` and `destination = c`.

Requiredness for positionals follows `nargs`: `?` and `*` positionals are
optional, `+` and fixed counts are required. Scalar positionals are required.
Call `.required(false)` to make a scalar positional optional:

```cpp
parser.add_argument("mode").nargs('?').required(false);
```

Scalar positional defaults are applied when the positional is absent:

```cpp
parser.add_argument("config").default_value("app.toml");
```

A default or environment fallback also satisfies the positional's requirement.
`provided(name)` reports whether a positional actually consumed a command line
value.

## Optional arguments and flags

### Long options

Long options accept a separate value or an inline value after `=`:

```cpp
parser.add_argument("-n", "--name");
```

```sh
./tool --name alice
./tool --name=alice
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
./tool -n=5
```

### Actions

The default action stores one value. Other actions are selected with
`.action("name")`:

| Action | Behavior when present | Default when absent |
| --- | --- | --- |
| `store` | Store the provided value. | no value |
| `store_true` | Store `true`. | `false` |
| `store_false` | Store `false`. | `true` |
| `append` | Append each value to a list. | empty list |
| `count` | Count occurrences. | `0` |
| `help` | Raise `HelpError(false)`. | `false` |
| `help_all` | Raise `HelpError(true)`. | `false` |

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
parser.add_argument("--pair").action("append").nargs(2);
// --pair a b --pair c d  ->  [a, b, c, d]
```

`count` increments once per occurrence, including each member of a short
cluster.

### Required options

Options are optional by default. Mark one required with `.required()`:

```cpp
parser.add_argument("--token").required().help("API token (required).");
```

A required option with a default or environment value is accepted without CLI
input; the fallback satisfies the required check.

### Option counts with `nargs`

```cpp
parser.add_argument("--files").nargs('*');            // any number, incl. zero
parser.add_argument("--include").nargs('+');          // one or more
parser.add_argument("--point").nargs(2);              // exactly two
parser.add_argument("--limit").nargs(Nargs::range(1, 3));
```

Values are consumed until the next token that looks like an option. Numeric
tokens such as `-42` and `-3.14e2` count as values. For `nargs('?')`, the next
non-option token is consumed when available; otherwise the implicit value is
used, or no value is stored when no implicit value is configured.

Use `.allow_hyphen_values()` when an option must consume a value that begins
with `-`:

```cpp
parser.add_argument("--pattern").allow_hyphen_values();
```

## Defaults, environment fallbacks, and implicit values

```cpp
parser.add_argument("--retries").default_value(3);
parser.add_argument("--ratio").default_value(1.5);
parser.add_argument("--label").default_value("none");
parser.add_argument("--enabled").default_value(true);
parser.add_argument("--coords").nargs(2).default_values({"0", "0"});
```

The value precedence for every argument is:

1. A command line value, when provided.
2. `.env("NAME")`, when the environment variable is set.
3. `.default_value(...)` / `.default_values(...)`.
4. The action-specific default (`false`, `true`, `0`, or empty list).

Thus the environment takes precedence over an explicit default:

```cpp
parser.add_argument("--port").env("APP_PORT").default_value(8080);
```

```sh
APP_PORT=7000 ./tool           # port = 7000
APP_PORT=7000 ./tool --port 9000  # port = 9000
./tool                          # port = 8080
```

The environment value is converted and validated like a CLI value. An
environment prefix applies to every configured name:

```cpp
parser.env_prefix("APP_");
parser.add_argument("--port").env("PORT"); // reads APP_PORT
```

`implicit_value` supplies a value when an option is present without a value. It
is not a fallback for an absent option. Pair it with a `nargs` pattern that
allows zero values, and normally with an explicit default:

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

Without a default, an absent `--color` has no value at all:

```cpp
parser.add_argument("--color").nargs('?').implicit_value("auto");
// ./tool -> raw_values("color") is empty
```

`provided(name)` reports only command line occurrence. Values supplied by the
environment or by a default return `false`. Required checks, dependency checks,
choices, and validators all consider environment and default values. Group
cardinality is the exception: it counts only explicit command line occurrences.

## Validation

### Choices

```cpp
parser.add_argument("--mode")
    .choices({"fast", "safe", "balanced"})
    .default_value("safe");
```

An invalid value raises `ArgumentError` with `ErrorCode::InvalidChoice` and a
message such as:

```text
invalid choice for argument 'mode': turbo (choose from fast, safe, balanced)
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

The callback receives the raw string. Return `false` to reject the value. When a
non-empty message is supplied, it becomes the error text; otherwise the parser
generates a generic invalid-value message.

Multiple `.check(...)` calls compose with AND semantics:

```cpp
parser.add_argument("--percent")
    .check([](const std::string &value)
           {
               const int percent = std::stoi(value);
               return percent >= 0 && percent <= 100;
           },
           "percent must be in [0, 100]")
    .check([](const std::string &value)
           {
               return value != "13";
           },
           "percent must not be 13");
```

Exceptions thrown by a validator are converted into `ArgumentError`
diagnostics with `ErrorCode::InvalidValue`, so a throwing conversion inside a
validator does not escape as an unrelated exception.

### Built-in validators

| Validator | Meaning |
| --- | --- |
| `BuiltinValidators::is_file` | Path names an existing regular file. |
| `BuiltinValidators::is_dir` | Path names an existing directory. |
| `BuiltinValidators::exists` | Path exists (file, directory, or other). |
| `BuiltinValidators::in_range(min, max)` | Type-aware inclusive numeric check. |
| `BuiltinValidators::one_of({...})` | Value belongs to the supplied set. |
| `BuiltinValidators::not_empty` | String has non-zero length. |

```cpp
parser.add_argument("--config").check(argparse::BuiltinValidators::is_file);
parser.add_argument("--output-dir").check(argparse::BuiltinValidators::is_dir);
parser.add_argument("--workers")
    .check(argparse::BuiltinValidators::in_range(1, 32));
parser.add_argument("--level")
    .check(argparse::BuiltinValidators::in_range(1.0, 5.0));
```

`in_range` parses into the requested type. Integral ranges reject fractional
input instead of truncating it, accept a leading `+`, and use
locale-independent conversion. Floating-point ranges parse as floating-point
values. Filesystem validators use `std::filesystem` with error codes and return
`false` instead of throwing.

## Argument groups and relationships

### Mutually exclusive groups

```cpp
auto &output = parser.add_mutually_exclusive_group();
output.add_argument("--json").action("store_true");
output.add_argument("--yaml").action("store_true");
```

Supplying both options raises `ArgumentError` with
`ErrorCode::MutuallyExclusive`. Supplying neither is allowed unless the group is
declared required.

### Required and cardinality groups

```cpp
auto &auth = parser.add_argument_group("authentication",
                                       "Choose one authentication method.");
auth.cardinality(argparse::ArgumentParser::GroupCardinality::ExactlyOne);
auth.add_argument("--token");
auth.add_argument("--password");
```

`ArgumentGroup::cardinality()` supports:

- `GroupCardinality::All` - no cardinality restriction.
- `GroupCardinality::AtMostOne` - zero or one member.
- `GroupCardinality::ExactlyOne` - exactly one member.
- `GroupCardinality::AtLeastOne` - one or more members.

The older `.exclusive(true)` and `.required(true)` helpers map to `AtMostOne`
and `AtLeastOne` respectively; using both produces `ExactlyOne`.

Group cardinality counts only arguments supplied on the command line. Defaults
and environment values do not satisfy a required group, and they do not trigger
an exclusivity error. Use `depends_on` or `requires_all` when fallback values
should participate.

Groups also control help layout: arguments added to a group appear under that
group's section instead of the default "optional arguments" or "positional
arguments" section.

### Dependencies, conflicts, and all-of requirements

```cpp
parser.add_argument("--user");
parser.add_argument("--password").depends_on("user");

parser.add_argument("--login")
    .action("store_true")
    .requires_all({"user", "password"});

parser.add_argument("--json").conflicts_with("yaml");
```

- `depends_on(name)` makes the dependent argument required when the named
  argument is supplied. The name may be a destination or an option alias.
- `requires_all({...})` requires every named argument whenever the owning
  argument is supplied.
- `conflicts_with(name)` rejects the pair when both are supplied.
- Defaults and environment values count as "supplied" for dependency checks.
  A property such as `--user` with `.default_value(true)` therefore triggers
  `--password` even when `--user` is omitted.
- Unknown dependency or conflict names raise `UsageError`.

A missing dependent value raises `RequiredIfError` with
`ErrorCode::DependencyMissing`.

## Parsing modes

### Strict parsing with `parse_args`

`parse_args(argc, argv)` throws:

- `ParseError` with `ErrorCode::UnknownOption` for unknown options.
- `ParseError` with `ErrorCode::UnknownPositional` for extra positionals.
- `RequiredError` for missing required arguments or option values.
- `ArgumentError` for invalid choices, validation failures, conflicts, or
  group cardinality violations.
- `UnknownSubcommandError` for an unrecognized subcommand token when the parent
  has no positionals that could accept it.

### Permissive parsing with `parse_known_args`

`parse_known_args(argc, argv)` parses known arguments and preserves the rest:

```cpp
const auto &args = parser.parse_known_args(argc, argv);

for (const std::string &token : parser.unknown_options())
{
    std::cout << "unknown option: " << token << '\n';
}
for (const std::string &token : parser.unknown_positionals())
{
    std::cout << "unknown positional: " << token << '\n';
}
```

The parser exposes three views:

| Method | Contents |
| --- | --- |
| `unknown_options()` | Unknown option tokens and their attached values. |
| `unknown_positionals()` | Positional tokens that could not be assigned. |
| `unknown_args()` | Ordered union of both collections. |
| `unknown()` | `UnknownArguments{options, positionals}`. |

Grouping rules:

- When the parser has no positionals, a non-option token immediately following
  an unknown long option is grouped with that unknown option. For example
  `--unknown value` records both tokens in `unknown_options()`.
- When positionals are registered, the following token is offered to the
  positionals first; only surplus tokens become unknown positionals.
- Unknown short clusters preserve the unrecognized suffix. With known `-v`,
  the token `-vz` records `-z`.
- Unknown tokens from subcommands are merged into the parent parser's unknown
  collections. Known parent options remain available before the subcommand.

### Non-throwing parsing

`try_parse_args()` and `try_parse_known_args()` catch parser exceptions and
return an `ArgumentParser::ParseResult`:

```cpp
auto result = parser.try_parse_args(argc, argv);
if (!result)
{
    switch (result.code)
    {
    case argparse::ErrorCode::HelpRequested:
        std::cout << parser.format_help(); // remembers --help vs --help-all
        return 0;
    default:
        std::cerr << result.error() << '\n';
        return 2;
    }
}

const auto &args = *result;
int count = result->get<int>("count");
```

`ParseResult` provides:

- `operator bool` - true on success.
- `code` - `ErrorCode::None` on success.
- `error()` - diagnostic text.
- `operator*`, `operator->`, `value()` - namespace access. `value()` throws the
  recorded `ArgumentError` when the parse failed.
- `namespace_ptr` - direct pointer to the namespace, or `nullptr` on failure.

After a help request, `format_help()` with no argument uses the remembered
`--help` / `--help-all` mode. Calling `format_help(true)` always includes hidden
arguments and `format_help(false)` always excludes them.

### Negative numbers and `--`

Numeric tokens such as `-42`, `-3.14`, and `-1e6` are values rather than
options, whether they are passed to an option or assigned to a positional. A
bare `-` is also a positional value.

Use `--` to stop option parsing. Every token after it is treated as a
positional value:

```sh
./tool -- -strange-file --looks-like-an-option
```

### Reusing a parser

A parser can parse several command lines sequentially. Parsed values,
occurrences, environment/default flags, unknown arguments, subcommand results,
and the remembered help mode are reset at the start of every parse:

```cpp
parser.parse_args(argc1, argv1);
parser.parse_args(argc2, argv2);
```

The parser is not thread-safe. Do not parse concurrently with the same
instance.

## Subcommands

Register subcommands with `add_subcommand()` and configure the returned child
parser:

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
- Use `add_subcommand({"checkout", "co"}, "Check out")` to register aliases;
  `subcommand()` returns the canonical name, and `subcommand_namespace()` is the
  child namespace.
- `add_subcommand_alias("alias", "canonical")` adds an alias after registration.
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

- `require_subcommand()` makes a subcommand mandatory. Omitting one raises
  `RequiredError` with the available choices.
- `default_subcommand("run")` dispatches unclaimed tokens to the named child
  when no explicit subcommand is present. The child receives non-option tokens
  that the parent does not claim, including its own positionals.
- An unknown subcommand raises `UnknownSubcommandError` with
  `ErrorCode::UnknownSubcommand` when the parent has no positionals that could
  accept the token. A parent positional takes precedence over unknown-subcommand
  detection.
- Each parser has its own built-in help. `tool run --help` raises `HelpError`
  from the child parser.
- Unknown child options propagate to the parent's unknown collections in
  `parse_known_args` mode.

## The Namespace API

`parse_args()` and `parse_known_args()` return a reference to a `Namespace`
owned by the parser. The reference remains valid until the next parse or until
the parser is destroyed. Namespaces are copyable and movable, including their
subcommand state.

| Method | Meaning |
| --- | --- |
| `has(name)` / `contains(name)` / `is_registered(name)` | Argument is registered and has an entry. |
| `provided(name)` | Argument occurred on the command line. |
| `raw_values(name)` | Raw string values. |
| `get<T>(name)` | Convert the first value to `T`. |
| `getlist<T>(name)` | Convert every value to `T`. |
| `get_if<T>(name)` | Convert when present and valid, else `std::nullopt`. |
| `get_or<T>(name, fallback)` | Convert when present, else the fallback. |
| `has_subcommand()` | A subcommand was selected. |
| `subcommand()` | Selected canonical subcommand name. |
| `subcommand_namespace()` | Namespace produced by the child parser. |

`has()` reports registration, not presence on the command line. Use
`provided()` for presence. Querying an unregistered name with `raw_values()`,
`get()`, or `getlist()` throws `ParseError`.

Supported conversions:

- `std::string`
- `std::string_view` (references namespace storage)
- `const char *` (references namespace storage)
- `bool`: `1`, `true`, `yes`, `on` and `0`, `false`, `no`, `off` in any case
- `char`: exactly one byte
- integral types such as `int`, `long`, `std::size_t`
- floating-point types such as `float` and `double`
- enums, converted through their underlying integer type
- `std::optional<T>` with `get_if<T>()`

```cpp
const auto &args = parser.parse_args(argc, argv);

bool verbose = args.get<bool>("verbose");
int retries = args.get<int>("retries");
double ratio = args.get<double>("ratio");
std::vector<std::string> tags = args.getlist<std::string>("tag");

std::optional<int> maybe = args.get_if<int>("retries");
int retries_or_default = args.get_or<int>("retries", 3);
```

Behavior details:

- `get<T>()` throws `ArgumentError` with `ErrorCode::InvalidValue` when the
  argument has no values, and `ErrorCode::Conversion` when conversion fails.
  Conversion messages include the argument name.
- `getlist<T>()` applies the same conversion rules to every stored value.
- `get_if<T>()` returns `std::nullopt` for an unregistered name, an empty
  argument, or a failed conversion.
- `get_or<T>()` returns the fallback for an unregistered or empty argument, but
  propagates a conversion error for a present invalid value.
- `namespace.subcommand()` returns an empty string when no subcommand was
  selected; `subcommand_namespace()` throws `ParseError` in that case.

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

### Formats

- `Format::Plain` - unstyled text without colors.
- `Format::Colored` - ANSI-colored text.
- `Format::Markdown` - Markdown document with headings, a fenced usage block,
  and lists.
- `Format::Compact` - single-line option rows without wrapping.

The formatter selects `Colored` by default when standard output supports color,
and `Plain` otherwise.

### Hidden arguments and help actions

Every parser automatically registers `-h`, `--help`, and `--help-all`. All
three raise `HelpError`; catch it in `main()`.

```cpp
parser.add_argument("--secret").hidden().help("Not shown by default.");

try
{
    parser.parse_args(argc, argv);
}
catch (const argparse::HelpError &help)
{
    std::cout << parser.format_help(help.show_hidden());
    return 0;
}
```

`HelpError::show_hidden()` is `true` for `--help-all`. Calling `print_help()`
always uses the normal hidden policy; use `format_help(help.show_hidden())` when
you need to honor the requested mode. Hidden arguments are also omitted from
synthesized usage lines and completion scripts.

### Usage synthesis

Usage text is generated from registered arguments unless a custom usage string
is supplied:

```cpp
parser.usage("tool [options] input");
```

Synthesis covers flags, required and optional options, choices, `nargs` counts,
variadic positionals, hidden filtering, and the built-in help options.

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

Sections added directly to the formatter are retained when the parser builds its
help text. `add_section()` appends a new section; `get_section()` returns an
existing section by title or creates it. Section references are stable.

```cpp
formatter.sort_sections(true);   // alphabetical ordering
section.visible(false);          // omit a section
```

Section types are `Custom`, `Notes`, `Examples`, `Warnings`, and `Arguments`.

### Alignment, colors, and themes

```cpp
argparse::HelpFormatter::Alignment alignment;
alignment.indent_size = 4;
alignment.indent_char = ' ';
alignment.option_width = 24;
alignment.max_help_position = 32;
alignment.min_description_width = 20;
alignment.wrap_help_text = true;
parser.formatter().alignment(alignment);

parser.formatter()
    .use_color(true)
    .color_theme(argparse::HelpFormatter::ColorTheme::nord());
```

Predefined themes are `nord()`, `monokai()`, and `dracula()`. You can also
modify a `ColorTheme` field by field. Color detection honors `NO_COLOR`,
`CLICOLOR`, and `CLICOLOR_FORCE`; width detection honors `COLUMNS` and the
console size, and ANSI escapes are ignored when measuring printable width.
`HelpFormatter::visible_length(text)` exposes that measurement.

### Printing help and usage

```cpp
parser.print_help();          // stdout
parser.print_help(std::cerr);
parser.print_usage();

std::string help = parser.format_help();       // uses hidden policy
std::string all = parser.format_help(true);    // includes hidden arguments
std::string usage = parser.format_usage();
```

The formatter also exposes `format_section(section)`, `wrap_text(text)`,
`colorize(text, color)`, and `hyperlink(text, url)`. `Table` renders aligned
rows for custom sections.

## Error handling

Exceptions are enabled by default. The hierarchy is:

```text
std::runtime_error
└── ArgumentError                 ErrorCode::Usage (default)
    ├── ParseError                Parse / UnknownOption / UnknownPositional
    │   └── UnknownSubcommandError  UnknownSubcommand
    ├── UsageError                Usage
    ├── RequiredError             MissingRequired / MissingValue
    ├── RequiredIfError           DependencyMissing
    └── HelpError                 HelpRequested
```

Every `ArgumentError` exposes `code()` and every error has a stable printable
name from `error_code_name()`:

| Code | Typical cause |
| --- | --- |
| `None` | Successful non-throwing parse. |
| `Usage` | Parser misuse. |
| `Parse` | Generic parse failure. |
| `UnknownOption` | Unrecognized option. |
| `UnknownPositional` | Extra positional token. |
| `UnknownSubcommand` | Unrecognized subcommand. |
| `MissingValue` | Option or nargs value missing. |
| `MissingRequired` | Required argument missing. |
| `InvalidChoice` | Value outside the configured choices. |
| `InvalidValue` | Validator, action, or syntax failure. |
| `MutuallyExclusive` | Conflicting options or group cardinality. |
| `DependencyMissing` | Dependency or all-of requirement missing. |
| `InvalidRegistration` | Invalid name, action, or registration request. |
| `HelpRequested` | Help action raised. |
| `Conversion` | Namespace conversion failed. |
| `Io` | Non-parser standard exception caught by a non-throwing parse. |

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
    catch (const std::exception &error)
    {
        std::cerr << "unexpected error: " << error.what() << '\n';
        return 1;
    }
}
```

`format_error(error)` returns the diagnostic and the parser's usage line:

```text
error: required argument missing: --token

usage: tool [--token TOKEN]
```

For non-throwing control flow, use `try_parse_args()` or
`try_parse_known_args()` as described in [Parsing modes](#parsing-modes).

`ARGPARSE_NO_EXCEPTIONS` is intentionally rejected at compile time in 0.2.0.
Use the non-throwing parse API instead; the macro produced unsafe control flow
in earlier releases.

## Shell completion

`generate_completion_script()` returns a completion script for Bash, Zsh, Fish,
or PowerShell:

```cpp
std::cout << parser.generate_completion_script("bash");
std::cout << parser.generate_completion_script("zsh");
std::cout << parser.generate_completion_script("fish");
std::cout << parser.generate_completion_script("powershell");
```

The `Shell` enum overload avoids string parsing:

```cpp
using Shell = argparse::ArgumentParser::Shell;
std::cout << parser.generate_completion_script(Shell::Bash);
```

Shell names are case-insensitive. `"pwsh"` is accepted as an alias for
PowerShell. Completion output includes visible options, choice values for
value-taking options, and subcommands. Hidden arguments are excluded. The
generated function name is sanitized from the program name. An unsupported
shell throws `ArgumentError` with `ErrorCode::Usage`.

## Testing and benchmarks

Configure with tests, benchmarks, and examples enabled:

```sh
cmake -S . -B build -G Ninja \
  -DARGPARSER_BUILD_TESTS=ON \
  -DARGPARSER_BUILD_BENCHMARKS=ON \
  -DARGPARSER_BUILD_EXAMPLES=ON
cmake --build build
```

Run the test suite:

```sh
ctest --test-dir build --output-on-failure
```

The suite includes:

- `argparse_tests` - 84 unit test cases and 462 assertions covering argument
  registration, stable references, positional assignment, token syntax,
  actions, precedence, validation, groups, dependencies, subcommands,
  `parse_known_args`, non-throwing parsing, namespace conversions, help
  formatting, completion, and environment handling.
- `argparse_golden_tests` - golden-file checks for plain help, help with hidden
  arguments, usage output, and Markdown help. Expected files live in
  `tests/golden/`.
- `argparse_multi_tu` - links multiple translation units that include the
  header and verifies parsing still works.

Run the benchmark executable directly:

```sh
./build/tests/argparse_benchmarks "[!benchmark]" --benchmark-samples 100
```

Benchmark categories can be selected with Catch2 tags:

```sh
./build/tests/argparse_benchmarks "[!benchmark][construction]"
./build/tests/argparse_benchmarks "[!benchmark][parse]"
./build/tests/argparse_benchmarks "[!benchmark][validation]"
./build/tests/argparse_benchmarks "[!benchmark][subcommands]"
./build/tests/argparse_benchmarks "[!benchmark][namespace]"
./build/tests/argparse_benchmarks "[!benchmark][scale]"
./build/tests/argparse_benchmarks "[!benchmark][help]"
```

The microbenchmark suite measures:

- parser construction with 20, 100, and 1,000 options
- boolean flags, long options, short clusters, negative numbers, and repeated
  append/count occurrences
- 10,000 positional values and realistic mixed command lines
- default finalization, choices, and composable custom validators
- 20 nested subcommands and `parse_known_args`
- 100,000-character option values and repeated failed option lookups
- namespace conversions with `get` and `getlist`
- plain/colored help and usage generation

On multi-config generators, executables are under the configuration directory,
for example `build/Debug/tests/argparse_tests.exe`. Catch2 v3.8.1 is downloaded
automatically when it is not available. For offline or vendored builds, set
`ARGPARSER_CATCH2_SOURCE_DIR` to a local Catch2 checkout or configure with
`ARGPARSER_USE_SYSTEM_CATCH2=ON`.

To register the benchmark with CTest as well, configure with
`-DARGPARSER_RUN_BENCHMARKS=ON`. CTest then runs a short benchmark pass with the
`benchmark` label.

## API quick reference

### `ArgumentParser`

| Method | Purpose |
| --- | --- |
| `ArgumentParser(program, description, epilog)` | Create a parser. |
| `program(...)`, `description(...)`, `usage(...)`, `epilog(...)` | Set metadata. |
| `env_prefix(prefix)` | Prefix every configured environment variable name. |
| `add_argument(names...)` | Add a positional or optional argument. |
| `add_argument_group(title, description)` | Add a help/validation group. |
| `add_mutually_exclusive_group(required)` | Add a group allowing at most one member. |
| `add_subcommand(name, description)` | Add a child parser. |
| `add_subcommand({names...}, description)` | Add a child parser with aliases. |
| `add_subcommand_alias(alias, canonical)` | Alias an existing child parser. |
| `require_subcommand(bool)` | Require a subcommand. |
| `default_subcommand(name)` | Select a fallback child parser. |
| `parse_args(argc, argv)` | Strict parse. |
| `parse_known_args(argc, argv)` | Parse and capture unknown tokens. |
| `try_parse_args(argc, argv)` | Non-throwing strict parse. |
| `try_parse_known_args(argc, argv)` | Non-throwing permissive parse. |
| `unknown_args()` | Ordered union of unknown tokens. |
| `unknown_options()` | Unknown option tokens. |
| `unknown_positionals()` | Unknown positional tokens. |
| `unknown()` | Both unknown collections. |
| `format_help(show_hidden)` | Build the help string. |
| `format_usage()` | Build the usage string. |
| `format_error(error)` | Build a diagnostic plus usage. |
| `print_help(stream)`, `print_usage(stream)` | Write help or usage. |
| `formatter()` | Access the `HelpFormatter`. |
| `generate_completion_script(shell)` | Generate completion for Bash, Zsh, Fish, or PowerShell. |
| `interactive(bool)` / `interactive(handler)` | Prompt for missing required values. |

### `Argument`

| Method | Purpose |
| --- | --- |
| `.help(text)` | Set help text. |
| `.hidden(bool)` | Hide from default help output and completion. |
| `.required(bool)` | Require the argument. |
| `.metavar(text)` | Set the placeholder shown in help. |
| `.action(name)` | Select an action (`store`, `store_true`, `store_false`, `append`, `count`, `help`, `help_all`). |
| `.nargs(count)` / `.nargs(pattern)` / `.nargs(Nargs)` | Set the accepted value count. |
| `.default_value(value)` | Set one default value. |
| `.default_values({...})` | Set several default values. |
| `.implicit_value(value)` | Value used when the option is present without data. |
| `.env(name)` | Read a fallback from an environment variable. |
| `.choices({...})` | Restrict values to a set. |
| `.check(callback, message)` | Add a validator; multiple calls compose. |
| `.depends_on(name)` | Require this argument when another argument is supplied. |
| `.requires_all({...})` | Require several arguments when this one is supplied. |
| `.conflicts_with(name)` | Reject this argument together with another. |
| `.allow_hyphen_values(bool)` | Allow values beginning with `-`. |
| `.dest()` | Return the namespace key. |
| `.option_names()` | Return the registered option aliases. |

### `ArgumentGroup`

| Method | Purpose |
| --- | --- |
| `.add_argument(names...)` | Add an argument to the group. |
| `.cardinality(value)` | Set `All`, `AtMostOne`, `ExactlyOne`, or `AtLeastOne`. |
| `.exclusive(bool)` | Set `AtMostOne` (or clear the restriction). |
| `.required(bool)` | Set `AtLeastOne` (or clear the requirement). |
| `.title()` | Return the group title. |

### `Namespace`

| Method | Purpose |
| --- | --- |
| `has(name)`, `contains(name)`, `is_registered(name)` | Registration checks. |
| `provided(name)` | Command line occurrence check. |
| `raw_values(name)` | Raw stored strings. |
| `get<T>(name)`, `getlist<T>(name)` | Typed conversion. |
| `get_if<T>(name)`, `get_or<T>(name, fallback)` | Optional/fallback conversion. |
| `has_subcommand()`, `subcommand()` | Subcommand inspection. |
| `subcommand_namespace()` | Child namespace. |

### `HelpFormatter`

| Method | Purpose |
| --- | --- |
| `format(Format)` | Select `Plain`, `Colored`, `Markdown`, or `Compact`. |
| `width(size)`, `alignment(Alignment)` | Control layout and wrapping. |
| `use_color(bool)`, `color_theme(ColorTheme)` | Control colors. |
| `sort_sections(bool)` | Sort sections alphabetically. |
| `prefix_info(...)`, `suffix_info(...)` | Add text before/after help. |
| `line_separator(...)` | Add a separator line. |
| `add_section(...)`, `get_section(...)` | Manage custom sections. |
| `format_help()`, `format_usage()` | Build output strings. |
| `format_section(...)` | Render one section. |
| `wrap_text(...)`, `colorize(...)`, `hyperlink(...)` | Formatting helpers. |
| `visible_length(text)` | Printable width ignoring ANSI escapes. |
| `supports_color()` | Detect terminal color support. |
