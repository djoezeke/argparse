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

<details><summary>Table of Contents</summary>

- [Overview](#-overview)
- [Features](#-features)
- [Getting Started](#-getting-started)
  - [Prerequisites](#-prerequisites)
  - [Installation](#-installation)
  - [Usage](#-usage)
  - [Testing and Benchmarks](#-testing-and-benchmarks)
- [Documentation](#-documentation)
- [Contributing](#-contributing)
- [Acknowledgments](#-acknowledgments)
- [License](#-license)

</details>

## 📍 Overview

`Argparser` is a single-file, header-only C++ argument parsing library inspired by
Python's `argparse`. It provides an intuitive API for declaring arguments with
`add_argument(...)`, parsing CLI input, validating values, and printing help text.

The complete usage guide lives in [docs/README.md](docs/README.md). Current
limitations are tracked in [REVIEW.md](REVIEW.md), and planned work is described
in [IMPROVE.md](IMPROVE.md).

## ✨ Features

- Single-header C++17 implementation (`argparse.h`)
- Python-style `add_argument` API
- Positional and optional arguments with aliases
- Actions: `store`, `store_true`, `store_false`, `append`, `count`, `help`
- Python-style `nargs` patterns (`?`, `*`, `+`) and fixed counts
- Typed defaults, implicit values, and environment fallbacks
- Choices, custom validators, and built-in file/directory/range validators
- Mutually exclusive groups, required groups, and conditional dependencies
- Nested subcommands with their own arguments and help
- Unknown option capture via `parse_known_args(...)`
- Customizable help formatter with sections, examples, colors, and themes
- Catch2 unit tests and microbenchmarks

### Argument model

An argument is positional when its first name does not begin with `-`. It is
optional when its first name begins with `-`. Optional arguments can have a
short alias, a long alias, or both.

| Kind | Example | Destination | Notes |
| --- | --- | --- | --- |
| Positional | `add_argument("input")` | `input` | Required by default. |
| Long option | `add_argument("--output-file")` | `output_file` | Dashes become underscores. |
| Short option | `add_argument("-v")` | `v` | May be clustered with other flags. |
| Aliased option | `add_argument("-o", "--output-file")` | `output_file` | The last long alias wins. |

### Supported syntax

```text
--verbose --output=result.txt
--output result.txt
-voresult.txt
-vvvo result.txt
-- --not-an-option
```

## 🚀 Getting Started

### ☑️ Prerequisites

A C++17 compiler is required. The test suite, example, and benchmarks are
verified with GCC 16.2 (MinGW-w64, UCRT) on Windows. Newer GCC, Clang, and MSVC
releases are expected to work; older compilers have not been tested.

### ⚙️ Installation

Include the single header:

```cpp
#include "argparse.h"
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

> **Known issue:** the parser's out-of-class member functions are not marked
> `inline`. Including `argparse.h` in more than one translation unit currently
> causes linker errors. See [REVIEW.md](REVIEW.md) for details and a workaround.

### 🤖 Usage

```cpp
#include "argparse.h"

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

Compile:

```sh
g++ -std=c++17 -I. main.cpp -o sample
```

### Flags and values

```cpp
parser.add_argument("-v", "--verbose").action("store_true");
parser.add_argument("-q", "--quiet").action("store_false");
parser.add_argument("-d", "--debug").action("count");
parser.add_argument("-t", "--tag").action("append");
parser.add_argument("--mode").choices({"fast", "safe"}).default_value("safe");
parser.add_argument("--threads").check(argparse::BuiltinValidators::in_range(1, 64));
parser.add_argument("--config").check(argparse::BuiltinValidators::is_file);
```

```sh
./sample -vvv --tag blue --tag stable --mode fast data.txt
```

### Subcommands

```cpp
#include "argparse.h"

#include <iostream>

int main(int argc, char **argv)
{
    argparse::ArgumentParser parser("tool", "Build and run jobs.");
    parser.add_argument("-v", "--verbose").action("store_true");

    auto &run = parser.add_subcommand("run", "Run a job");
    run.add_argument("path");
    run.add_argument("--mode")
        .choices({"fast", "safe"})
        .default_value("fast");

    try
    {
        const auto &args = parser.parse_args(argc, argv);

        if (args.has_subcommand() && args.subcommand() == "run")
        {
            const auto &run_args = args.subcommand_namespace();
            const std::string path = run_args.get<std::string>("path");
            const std::string mode = run_args.get<std::string>("mode");
            std::cout << path << " (" << mode << ")\n";
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

### Environment fallbacks

```cpp
parser.add_argument("--port").env("APP_PORT");
```

The current precedence for an absent argument is CLI value, then default value,
then environment variable, then the action-specific default. Because the
default is checked first, combining `.env(...)` with `.default_value(...)`
makes the environment fallback unreachable. See
[docs/README.md](docs/README.md#defaults-environment-fallbacks-and-implicit-values)
for the full precedence table.

### Example help formatter

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

The example in [examples/Demo.cpp](examples/Demo.cpp) exercises options,
validation, help formatting, and subcommands.

## 🧪 Testing and Benchmarks

Configure with tests, benchmarks, and examples enabled:

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

Run the Catch2 microbenchmarks:

```sh
./build/tests/argparse_benchmarks "[!benchmark]" --benchmark-samples 100
```

The tests cover valid and invalid arguments, option forms, clusters, actions,
positional assignment, defaults, environment fallbacks, choices, validators,
groups, dependencies, subcommands, namespace conversions, help formatting,
error conditions, and documented limitations. The benchmarks measure parser
construction, common parsing shapes, high-cardinality command lines,
validation, subcommands, conversions, and help generation.

Catch2 v3.8.1 is downloaded automatically if it is not installed. Use
`ARGPARSER_CATCH2_SOURCE_DIR` for offline/vendored builds.

## 📚 Documentation

- [Usage guide](docs/README.md) - examples and explanations for every option and flag
- [REVIEW.md](REVIEW.md) - limitations, defects, and known issues
- [IMPROVE.md](IMPROVE.md) - suggested improvements and future work
- [examples/Demo.cpp](examples/Demo.cpp) - runnable demonstration

## 🔰 Contributing

- **💬 [Join the Discussions](https://github.com/djoezeke/argparser/discussions)**: Share your insights, provide feedback, or ask questions.
- **🐛 [Report Issues](https://github.com/djoezeke/argparser/issues)**: Submit bugs you find or log feature requests.
- **💡 [Submit Pull Requests](https://github.com/djoezeke/argparser/pulls)**: Review open PRs, and submit your own.

<details closed>
<summary>Contributing Guidelines</summary>

1. **Fork the Repository**: Start by forking the project repository to your GitHub account.
2. **Clone Locally**: Clone the forked repository to your local machine using a git client.
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
7. **Submit a Pull Request**: Create a PR against the original repository. Describe the changes and their motivation.
8. **Review**: Once reviewed and approved, it will be merged into the main branch.

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
