#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "argparse.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using argparse::ArgumentError;
using argparse::ArgumentParser;
using argparse::BuiltinValidators;
using argparse::ErrorCode;
using argparse::HelpError;
using argparse::HelpFormatter;
using argparse::Nargs;
using argparse::ParseError;
using argparse::RequiredError;
using argparse::RequiredIfError;
using argparse::UnknownSubcommandError;
using argparse::UsageError;
using Catch::Matchers::ContainsSubstring;

namespace
{
    // ---------------------------------------------------------------------
    // Test helpers
    // ---------------------------------------------------------------------

    // Builds an argv array with a synthetic program name. The string storage
    // outlives the pointers for the lifetime of the object.
    class Argv
    {
    public:
        explicit Argv(std::initializer_list<std::string> args)
        {
            m_Args.emplace_back("test_program");
            m_Args.insert(m_Args.end(), args.begin(), args.end());
            rebuild();
        }

        explicit Argv(std::vector<std::string> args)
            : m_Args(std::move(args))
        {
            if (m_Args.empty())
            {
                m_Args.emplace_back("test_program");
            }
            rebuild();
        }

        Argv(const Argv &) = delete;
        Argv &operator=(const Argv &) = delete;

        int argc() const { return static_cast<int>(m_Ptrs.size()); }
        char **argv() { return m_Ptrs.data(); }

    private:
        void rebuild()
        {
            m_Ptrs.clear();
            m_Ptrs.reserve(m_Args.size());
            for (auto &arg : m_Args)
            {
                m_Ptrs.push_back(arg.data());
            }
        }

        std::vector<std::string> m_Args;
        std::vector<char *> m_Ptrs;
    };

    const ArgumentParser::Namespace &parse(
        ArgumentParser &parser,
        std::initializer_list<std::string> args)
    {
        Argv argv(args);
        return parser.parse_args(argv.argc(), argv.argv());
    }

    const ArgumentParser::Namespace &parse_known(
        ArgumentParser &parser,
        std::initializer_list<std::string> args)
    {
        Argv argv(args);
        return parser.parse_known_args(argv.argc(), argv.argv());
    }

    template <typename Exception, typename Callable>
    std::string thrown_message(Callable &&callable)
    {
        try
        {
            callable();
        }
        catch (const Exception &error)
        {
            return error.what();
        }
        catch (...)
        {
            return "<wrong exception type>";
        }
        return "<no exception thrown>";
    }

    template <typename Exception, typename Callable>
    ErrorCode thrown_code(Callable &&callable)
    {
        try
        {
            callable();
        }
        catch (const Exception &error)
        {
            return error.code();
        }
        catch (...)
        {
            return ErrorCode::None;
        }
        return ErrorCode::None;
    }

    class ScopedEnv
    {
    public:
        ScopedEnv(std::string name, std::string value)
            : m_Name(std::move(name))
        {
            capture_previous();
            set_value(value);
        }

        explicit ScopedEnv(std::string name)
            : m_Name(std::move(name))
        {
            capture_previous();
            unset_value();
        }

        ScopedEnv(const ScopedEnv &) = delete;
        ScopedEnv &operator=(const ScopedEnv &) = delete;

        ~ScopedEnv()
        {
            if (m_HadPrevious)
            {
                set_value(m_Previous);
            }
            else
            {
                unset_value();
            }
        }

        void set(const std::string &value) { set_value(value); }
        void unset() { unset_value(); }

    private:
        void capture_previous()
        {
            const char *previous = std::getenv(m_Name.c_str());
            if (previous != nullptr)
            {
                m_HadPrevious = true;
                m_Previous = previous;
            }
        }

        void set_value(const std::string &value)
        {
#ifdef _WIN32
            _putenv_s(m_Name.c_str(), value.c_str());
#else
            setenv(m_Name.c_str(), value.c_str(), 1);
#endif
        }

        void unset_value()
        {
#ifdef _WIN32
            _putenv_s(m_Name.c_str(), "");
#else
            unsetenv(m_Name.c_str());
#endif
        }

        std::string m_Name;
        std::string m_Previous;
        bool m_HadPrevious{false};
    };

    std::vector<std::string> list_of(const ArgumentParser::Namespace &ns,
                                     const std::string &name)
    {
        return ns.raw_values(name);
    }

    enum class TestMode
    {
        Fast = 1,
        Safe = 2
    };
} // namespace

// -------------------------------------------------------------------------
// Library metadata and parser configuration
// -------------------------------------------------------------------------

TEST_CASE("version macros match the documented release", "[api][version]")
{
    STATIC_REQUIRE(ARGPARSE_VERSION_MAJOR == 0);
    STATIC_REQUIRE(ARGPARSE_VERSION_MINOR == 2);
    STATIC_REQUIRE(ARGPARSE_VERSION_PATCH == 0);
    CHECK(std::string(ARGPARSE_VERSION) == "0.2.0");
}

TEST_CASE("parser stores program, description, usage, and epilog", "[api][config]")
{
    ArgumentParser parser("tool", "Process files.", "See the manual.");

    CHECK(parser.program() == "tool");
    CHECK(parser.description() == "Process files.");
    CHECK(parser.epilog() == "See the manual.");

    parser.program("renamed");
    parser.description("Updated description.");
    parser.usage("renamed [options] FILE");
    parser.epilog("Updated epilog.");

    CHECK(parser.program() == "renamed");
    CHECK(parser.description() == "Updated description.");
    CHECK(parser.usage() == "renamed [options] FILE");
    CHECK(parser.epilog() == "Updated epilog.");

    parser.formatter().format(HelpFormatter::Format::Plain);
    const std::string help = parser.format_help();
    CHECK_THAT(help, ContainsSubstring("usage: renamed [options] FILE"));
    CHECK_THAT(help, ContainsSubstring("Updated description."));
    CHECK_THAT(help, ContainsSubstring("Updated epilog."));
}

TEST_CASE("an empty program name is filled from argv[0]", "[api][config]")
{
    ArgumentParser parser;
    REQUIRE(parser.program().empty());

    (void)parse(parser, {});

    CHECK(parser.program() == "test_program");
    CHECK_THAT(parser.format_usage(), ContainsSubstring("usage: test_program"));
}

TEST_CASE("argument registration rejects invalid names", "[api][registration]")
{
    SECTION("no names")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS(parser.add_argument(), ArgumentError);
    }

    SECTION("mixed optional and positional names")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("-v", "verbose")), ArgumentError);
    }

    SECTION("multiple positional names")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("input", "output")), ArgumentError);
    }

    SECTION("duplicate option aliases and destinations")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-v", "--verbose");
        CHECK_THROWS_AS((parser.add_argument("-v", "--version")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--verbose")), ArgumentError);
    }

    SECTION("malformed optional names")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("-")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("-ab")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--bad=name")), ArgumentError);
    }

    SECTION("duplicate destinations after normalization")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--foo-bar");
        CHECK_THROWS_AS((parser.add_argument("--foo_bar")), ArgumentError);
    }

    SECTION("duplicate positional destination")
    {
        ArgumentParser parser("tool");
        parser.add_argument("input");
        CHECK_THROWS_AS((parser.add_argument("input")), ArgumentError);
    }

    SECTION("built-in help option is already registered")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("-h")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--help")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--help-all")), ArgumentError);
    }

    SECTION("unknown action")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("--bad").action("explode")),
                        ArgumentError);
    }

    SECTION("unknown nargs pattern")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("--bad").nargs('!')), ArgumentError);
    }

    SECTION("invalid environment variable and validator")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("--bad").env("")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--bad").check(nullptr)),
                        ArgumentError);
    }

    SECTION("invalid subcommand names and aliases")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS(parser.add_subcommand(""), ArgumentError);
        CHECK_THROWS_AS(parser.add_subcommand("-run"), ArgumentError);
        CHECK_THROWS_AS(parser.add_subcommand_alias("co", "checkout"),
                        ArgumentError);
    }
}

TEST_CASE("destination names are derived from option aliases", "[api][registration]")
{
    {
        ArgumentParser parser("tool");
        CHECK(parser.add_argument("-o", "--output-file").dest() == "output_file");
    }
    {
        ArgumentParser parser("tool");
        CHECK(parser.add_argument("-q").dest() == "q");
    }
    {
        ArgumentParser parser("tool");
        CHECK(parser.add_argument("plain-file").dest() == "plain-file");
    }
    {
        ArgumentParser parser("tool");
        CHECK(parser.add_argument("--dry-run").dest() == "dry_run");
    }
    {
        ArgumentParser parser("tool");
        CHECK(parser.add_argument("-x", "-y", "--xylophone").dest() ==
              "xylophone");
    }
}

TEST_CASE("returned references remain stable across later registrations",
          "[api][registration][stability]")
{
    ArgumentParser parser("tool");
    auto &first = parser.add_argument("--first");
    auto &group = parser.add_argument_group("group");
    auto &grouped = group.add_argument("--grouped");

    for (int index = 0; index < 500; ++index)
    {
        parser.add_argument("--option-" + std::to_string(index));
        parser.add_argument_group("group-" + std::to_string(index));
    }

    first.help("first help");
    grouped.help("grouped help");

    CHECK(first.dest() == "first");
    CHECK(grouped.dest() == "grouped");
    CHECK_THAT(parser.format_help(), ContainsSubstring("first help"));
    CHECK_THAT(parser.format_help(), ContainsSubstring("grouped help"));

    HelpFormatter formatter;
    formatter.format(HelpFormatter::Format::Plain);
    auto &section = formatter.add_section("custom");
    for (int index = 0; index < 100; ++index)
    {
        formatter.add_section("section-" + std::to_string(index));
    }
    section.add_text("stable section text");
    CHECK_THAT(formatter.format_help(), ContainsSubstring("stable section text"));
}

TEST_CASE("moved parsers keep working", "[api][move]")
{
    ArgumentParser original("tool");
    auto &group = original.add_argument_group("group");
    group.add_argument("--value");

    ArgumentParser parser = std::move(original);
    const auto &ns = parse(parser, {"--value", "42"});

    CHECK(ns.get<int>("value") == 42);
    CHECK_THAT(parser.format_help(), ContainsSubstring("--value"));
}

TEST_CASE("Nargs is a strong, composable value type", "[api][nargs]")
{
    CHECK(Nargs().min() == 1);
    CHECK(Nargs().max() == 1);
    CHECK(Nargs::exactly(2) == Nargs::range(2, 2));
    CHECK(Nargs::optional().min() == 0);
    CHECK(Nargs::optional().max() == 1);
    CHECK(Nargs::zero_or_more().unbounded());
    CHECK(Nargs::zero_or_more().min() == 0);
    CHECK(Nargs::one_or_more().min() == 1);
    CHECK(Nargs::none().max() == 0);
    CHECK(Nargs::range(3, 1) == Nargs::exactly(3));
}

// -------------------------------------------------------------------------
// Positional arguments
// -------------------------------------------------------------------------

TEST_CASE("positional arguments parse as strings and report provided",
          "[parse][positional]")
{
    ArgumentParser parser("cp");
    auto &input = parser.add_argument("input").help("input file");

    const auto &ns = parse(parser, {"file.txt"});

    CHECK(input.dest() == "input");
    CHECK(ns.has("input"));
    CHECK(ns.provided("input"));
    CHECK(ns.get<std::string>("input") == "file.txt");
    CHECK(list_of(ns, "input") == std::vector<std::string>({"file.txt"}));
}

TEST_CASE("missing and extra positionals are rejected", "[parse][positional][errors]")
{
    ArgumentParser parser("cp");
    parser.add_argument("input");

    CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    CHECK_THROWS_AS((parse(parser, {"one", "two"})), ParseError);

    CHECK_THAT(thrown_message<RequiredError>([&]
                                             { (void)parse(parser, {}); }),
               ContainsSubstring("missing positional argument: input"));
    CHECK_THAT(thrown_message<ParseError>([&]
                                          { (void)parse(parser, {"one", "two"}); }),
               ContainsSubstring("unrecognized positional argument: two"));
    CHECK(thrown_code<ParseError>([&]
                                  { (void)parse(parser, {"one", "two"}); }) ==
          ErrorCode::UnknownPositional);
}

TEST_CASE("scalar positional defaults are reachable", "[parse][positional][defaults]")
{
    ArgumentParser parser("cp");
    parser.add_argument("input").default_value("stdin");

    const auto &ns = parse(parser, {});

    CHECK(ns.has("input"));
    CHECK_FALSE(ns.provided("input"));
    CHECK(ns.get<std::string>("input") == "stdin");
    CHECK_THAT(parser.format_usage(), ContainsSubstring("[input]"));

    const auto &explicit_value = parse(parser, {"file.txt"});
    CHECK(explicit_value.get<std::string>("input") == "file.txt");
    CHECK(explicit_value.provided("input"));
}

TEST_CASE("positional nargs patterns derive requiredness", "[parse][positional][nargs]")
{
    SECTION("question mark accepts zero or one and is optional")
    {
        ArgumentParser parser("tool");
        parser.add_argument("maybe").nargs('?');

        const auto &empty = parse(parser, {});
        CHECK(empty.has("maybe"));
        CHECK_FALSE(empty.provided("maybe"));
        CHECK(empty.raw_values("maybe").empty());
        CHECK_FALSE(empty.get_if<std::string>("maybe").has_value());

        const auto &one = parse(parser, {"value"});
        CHECK(one.get<std::string>("maybe") == "value");
        CHECK(one.provided("maybe"));

        CHECK_THROWS_AS((parse(parser, {"one", "two"})), ParseError);
    }

    SECTION("star accepts zero or more without an explicit required(false)")
    {
        ArgumentParser parser("tool");
        parser.add_argument("files").nargs('*');

        const auto &empty = parse(parser, {});
        CHECK(empty.raw_values("files").empty());

        const auto &many = parse(parser, {"a", "b", "c"});
        CHECK(many.getlist<std::string>("files") ==
              std::vector<std::string>({"a", "b", "c"}));
    }

    SECTION("plus requires at least one value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("files").nargs('+');

        CHECK_THROWS_AS((parse(parser, {})), RequiredError);

        const auto &many = parse(parser, {"a", "b"});
        CHECK(many.getlist<std::string>("files") ==
              std::vector<std::string>({"a", "b"}));
    }

    SECTION("fixed counts and explicit ranges")
    {
        ArgumentParser parser("tool");
        parser.add_argument("pair").nargs(2);

        CHECK_THROWS_AS((parse(parser, {"a"})), RequiredError);
        CHECK_THROWS_AS((parse(parser, {"a", "b", "c"})), ParseError);

        const auto &pair = parse(parser, {"a", "b"});
        CHECK(pair.getlist<std::string>("pair") ==
              std::vector<std::string>({"a", "b"}));
    }

    SECTION("range reserves values for later positionals")
    {
        ArgumentParser parser("tool");
        parser.add_argument("first").nargs(Nargs::range(1, 3));
        parser.add_argument("last");

        const auto &ns = parse(parser, {"a", "b", "c", "d"});
        CHECK(ns.getlist<std::string>("first") ==
              std::vector<std::string>({"a", "b", "c"}));
        CHECK(ns.get<std::string>("last") == "d");
    }
}

TEST_CASE("scalar positionals can be made optional explicitly",
          "[parse][positional][required]")
{
    ArgumentParser parser("tool");
    parser.add_argument("input").required(false);

    const auto &empty = parse(parser, {});
    CHECK(empty.has("input"));
    CHECK(empty.raw_values("input").empty());
    CHECK_THAT(parser.format_usage(), ContainsSubstring("[input]"));

    const auto &value = parse(parser, {"file.txt"});
    CHECK(value.get<std::string>("input") == "file.txt");
}

TEST_CASE("multiple positionals reserve minimums for later arguments",
          "[parse][positional][assignment]")
{
    ArgumentParser parser("cp");
    parser.add_argument("sources").nargs('*');
    parser.add_argument("destination");

    const auto &ns = parse(parser, {"a", "b", "c"});
    CHECK(ns.getlist<std::string>("sources") ==
          std::vector<std::string>({"a", "b"}));
    CHECK(ns.get<std::string>("destination") == "c");

    CHECK_THROWS_AS((parse(parser, {})), RequiredError);
}

TEST_CASE("negative numbers and a bare dash are positional values",
          "[parse][positional][negative]")
{
    ArgumentParser parser("tool");
    parser.add_argument("values").nargs('*');

    const auto &ns = parse(parser, {"-42", "-3.14e2", "-"});
    CHECK(ns.getlist<std::string>("values") ==
          std::vector<std::string>({"-42", "-3.14e2", "-"}));
}

TEST_CASE("double dash turns every remaining token into a positional",
          "[parse][positional][terminator]")
{
    ArgumentParser parser("tool");
    parser.add_argument("files").nargs('*');

    const auto &ns = parse(parser, {"--", "-strange", "--looks-like-option"});
    CHECK(ns.getlist<std::string>("files") ==
          std::vector<std::string>({"-strange", "--looks-like-option"}));
}

// -------------------------------------------------------------------------
// Optional arguments, token forms, and actions
// -------------------------------------------------------------------------

TEST_CASE("long options accept separate and inline values",
          "[parse][optional][syntax]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-n", "--name");

    CHECK(parse(parser, {"--name", "alice"}).get<std::string>("name") == "alice");
    CHECK(parse(parser, {"--name=alice"}).get<std::string>("name") == "alice");
    CHECK(parse(parser, {"--name="}).get<std::string>("name").empty());
    CHECK(parse(parser, {"--name=-weird"}).get<std::string>("name") == "-weird");
    CHECK(parse(parser, {"-n", "bob"}).get<std::string>("name") == "bob");
    CHECK(parse(parser, {"-nbob"}).get<std::string>("name") == "bob");
    CHECK(parse(parser, {"-n=bob"}).get<std::string>("name") == "bob");
}

TEST_CASE("short clusters parse flags until an option takes a value",
          "[parse][optional][cluster]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("count");
    parser.add_argument("-q", "--quiet").action("store_true");
    parser.add_argument("-o", "--output");

    const auto &ns = parse(parser, {"-vvqo", "result.txt"});
    CHECK(ns.get<int>("verbose") == 2);
    CHECK(ns.get<bool>("quiet"));
    CHECK(ns.get<std::string>("output") == "result.txt");

    const auto &attached = parse(parser, {"-voattached.txt"});
    CHECK(attached.get<int>("verbose") == 1);
    CHECK(attached.get<std::string>("output") == "attached.txt");

    const auto &equals = parse(parser, {"-vo=equals.txt"});
    CHECK(equals.get<std::string>("output") == "equals.txt");
}

TEST_CASE("option value collection stops at option-like tokens",
          "[parse][optional][boundaries]")
{
    SECTION("missing separate value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");
        parser.add_argument("--verbose").action("store_true");

        CHECK_THROWS_AS((parse(parser, {"--name"})), RequiredError);
        CHECK_THROWS_AS((parse(parser, {"--name", "--verbose"})), RequiredError);
        CHECK(thrown_code<RequiredError>([&]
                                         { (void)parse(parser, {"--name"}); }) ==
              ErrorCode::MissingValue);
        CHECK_THAT(thrown_message<RequiredError>(
                       [&]
                       { (void)parse(parser, {"--name", "--verbose"}); }),
                   ContainsSubstring("requires a value"));
    }

    SECTION("unbounded values stop at a registered option")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--files").nargs('+');
        parser.add_argument("--verbose").action("store_true");

        const auto &ns = parse(parser, {"--files", "a", "b", "--verbose"});
        CHECK(ns.getlist<std::string>("files") ==
              std::vector<std::string>({"a", "b"}));
        CHECK(ns.get<bool>("verbose"));
    }

    SECTION("unbounded values stop at an unknown option and strict parsing fails")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--files").nargs('*');

        CHECK_THROWS_AS((parse(parser, {"--files", "a", "--mystery"})),
                        ParseError);
    }

    SECTION("negative numeric option values are accepted")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--offset");
        parser.add_argument("--ratio");

        const auto &ns = parse(parser, {"--offset", "-42", "--ratio", "-1.5e2"});
        CHECK(ns.get<int>("offset") == -42);
        CHECK(ns.get<double>("ratio") == Catch::Approx(-150.0));
    }

    SECTION("allow_hyphen_values accepts dash-prefixed values")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--offset").allow_hyphen_values();

        const auto &ns = parse(parser, {"--offset", "-custom"});
        CHECK(ns.get<std::string>("offset") == "-custom");
    }

    SECTION("inline values can contain option-like text")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");
        parser.add_argument("--verbose").action("store_true");

        const auto &ns = parse(parser, {"--name=--verbose"});
        CHECK(ns.get<std::string>("name") == "--verbose");
        CHECK_FALSE(ns.get<bool>("verbose"));
    }

    SECTION("double dash terminates option values")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");

        CHECK_THROWS_AS((parse(parser, {"--name", "--", "value"})),
                        RequiredError);
    }
}

TEST_CASE("flags reject inline values", "[parse][optional][errors]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");
    parser.add_argument("-q").action("store_true");

    CHECK_THROWS_AS((parse(parser, {"--verbose=false"})), ParseError);
    CHECK_THROWS_AS((parse(parser, {"-q=false"})), ParseError);
    CHECK(thrown_code<ParseError>([&]
                                  { (void)parse(parser, {"--verbose=false"}); }) ==
          ErrorCode::InvalidValue);
    CHECK_THAT(thrown_message<ParseError>(
                   [&]
                   { (void)parse(parser, {"--verbose=false"}); }),
               ContainsSubstring("does not take a value"));
}

TEST_CASE("actions store values and action-specific defaults",
          "[parse][actions]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("store_true");
    parser.add_argument("--quiet").action("store_false");
    parser.add_argument("-d", "--debug").action("count");
    parser.add_argument("-t", "--tag").action("append");
    parser.add_argument("--name");

    const auto &empty = parse(parser, {});
    CHECK_FALSE(empty.get<bool>("verbose"));
    CHECK(empty.get<bool>("quiet"));
    CHECK(empty.get<int>("debug") == 0);
    CHECK(empty.raw_values("tag").empty());
    CHECK(empty.raw_values("name").empty());
    CHECK_FALSE(empty.provided("verbose"));
    CHECK_FALSE(empty.provided("name"));

    const auto &ns = parse(parser, {"-v", "--quiet", "-ddd", "-t", "a",
                                    "--tag", "b", "--name", "alice"});
    CHECK(ns.get<bool>("verbose"));
    CHECK_FALSE(ns.get<bool>("quiet"));
    CHECK(ns.get<int>("debug") == 3);
    CHECK(ns.getlist<std::string>("tag") ==
          std::vector<std::string>({"a", "b"}));
    CHECK(ns.get<std::string>("name") == "alice");
}

TEST_CASE("append composes with nargs", "[parse][actions][nargs]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--pair").action("append").nargs(2);

    const auto &ns = parse(parser, {"--pair", "a", "b", "--pair", "c", "d"});
    CHECK(ns.getlist<std::string>("pair") ==
          std::vector<std::string>({"a", "b", "c", "d"}));
}

TEST_CASE("parsing twice resets counts, values, and unknowns",
          "[parse][reuse]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("count");
    parser.add_argument("--name").default_value("default");

    const auto &first = parse_known(parser, {"-vv", "--name", "first",
                                             "--unknown"});
    CHECK(first.get<int>("verbose") == 2);
    CHECK(first.get<std::string>("name") == "first");
    CHECK(parser.unknown_args() == std::vector<std::string>({"--unknown"}));

    const auto &second = parse(parser, {});
    CHECK(second.get<int>("verbose") == 0);
    CHECK(second.get<std::string>("name") == "default");
    CHECK(parser.unknown_args().empty());
    CHECK(parser.unknown_options().empty());
    CHECK(parser.unknown_positionals().empty());
}

// -------------------------------------------------------------------------
// Defaults, environment, and implicit values
// -------------------------------------------------------------------------

TEST_CASE("value precedence is CLI, environment, explicit default, action default",
          "[parse][precedence]")
{
    ScopedEnv port("PREFIX_ARGPARSE_TEST_PORT", "7000");
    ScopedEnv label("PREFIX_ARGPARSE_TEST_LABEL", "from-env");

    ArgumentParser parser("tool");
    parser.env_prefix("PREFIX_");
    parser.add_argument("--port").env("ARGPARSE_TEST_PORT").default_value(9000);
    parser.add_argument("--label").env("ARGPARSE_TEST_LABEL").default_value("default");
    parser.add_argument("--flag").action("store_true");

    SECTION("CLI wins over environment and default")
    {
        const auto &ns = parse(parser, {"--port", "8080", "--label", "cli"});
        CHECK(ns.get<int>("port") == 8080);
        CHECK(ns.get<std::string>("label") == "cli");
        CHECK(ns.provided("port"));
    }

    SECTION("environment wins over default")
    {
        const auto &ns = parse(parser, {});
        CHECK(ns.get<int>("port") == 7000);
        CHECK(ns.get<std::string>("label") == "from-env");
        CHECK_FALSE(ns.provided("port"));
    }
}

TEST_CASE("defaults are used when the environment is not set",
          "[parse][defaults][environment]")
{
    ScopedEnv unset("ARGPARSE_TEST_UNSET");
    ArgumentParser parser("tool");
    parser.add_argument("--port").env("ARGPARSE_TEST_UNSET").default_value(9000);

    const auto &ns = parse(parser, {});
    CHECK(ns.get<int>("port") == 9000);
    CHECK_FALSE(ns.provided("port"));
}

TEST_CASE("implicit values apply only when an option occurs without a value",
          "[parse][implicit]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--color")
        .nargs('?')
        .implicit_value("auto")
        .default_value("never");

    const auto &absent = parse(parser, {});
    CHECK(absent.get<std::string>("color") == "never");
    CHECK_FALSE(absent.provided("color"));

    const auto &implicit = parse(parser, {"--color"});
    CHECK(implicit.get<std::string>("color") == "auto");
    CHECK(implicit.provided("color"));

    const auto &explicit_value = parse(parser, {"--color", "always"});
    CHECK(explicit_value.get<std::string>("color") == "always");
    CHECK(explicit_value.provided("color"));

    const auto &inline_value = parse(parser, {"--color=always"});
    CHECK(inline_value.get<std::string>("color") == "always");
}

TEST_CASE("implicit values are not applied when no default exists",
          "[parse][implicit][regression]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--color").nargs('?').implicit_value("auto");

    const auto &absent = parse(parser, {});
    CHECK(absent.raw_values("color").empty());
    CHECK_FALSE(absent.get_if<std::string>("color").has_value());
}

TEST_CASE("action defaults fill absent values", "[parse][defaults][actions]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--on").action("store_true");
    parser.add_argument("--off").action("store_false");
    parser.add_argument("--count").action("count");

    const auto &ns = parse(parser, {});
    CHECK_FALSE(ns.get<bool>("on"));
    CHECK(ns.get<bool>("off"));
    CHECK(ns.get<int>("count") == 0);
}

TEST_CASE("environment state is fully reset between parses",
          "[parse][environment][regression]")
{
    ScopedEnv mode("ARGPARSE_TEST_MODE", "fast");

    ArgumentParser parser("tool");
    parser.add_argument("--mode").env("ARGPARSE_TEST_MODE");
    parser.add_argument("--other").conflicts_with("mode");

    CHECK_THROWS_AS((parse(parser, {"--other", "x"})), ArgumentError);

    mode.unset();
    const auto &ns = parse(parser, {"--other", "x"});
    CHECK(ns.get<std::string>("other") == "x");
}

// -------------------------------------------------------------------------
// Validation
// -------------------------------------------------------------------------

TEST_CASE("choices reject invalid CLI, environment, and default values",
          "[validation][choices]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--mode")
        .choices({"fast", "safe"})
        .default_value("fast");

    CHECK_THROWS_AS((parse(parser, {"--mode", "turbo"})), ArgumentError);
    CHECK(thrown_code<ArgumentError>([&]
                                     { (void)parse(parser, {"--mode", "turbo"}); }) ==
          ErrorCode::InvalidChoice);
    CHECK_THAT(thrown_message<ArgumentError>(
                   [&]
                   { (void)parse(parser, {"--mode", "turbo"}); }),
               ContainsSubstring("choose from"));

    ArgumentParser bad_default("tool");
    bad_default.add_argument("--mode").choices({"fast", "safe"}).default_value("turbo");
    CHECK_THROWS_AS((parse(bad_default, {})), ArgumentError);

    ScopedEnv mode("ARGPARSE_TEST_CHOICE", "turbo");
    ArgumentParser bad_env("tool");
    bad_env.add_argument("--mode").choices({"fast", "safe"}).env("ARGPARSE_TEST_CHOICE");
    CHECK_THROWS_AS((parse(bad_env, {})), ArgumentError);
}

TEST_CASE("custom validators compose with AND semantics",
          "[validation][custom]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--percent")
        .check([](const std::string &value)
               { return std::stoi(value) >= 0; },
               "percent must be non-negative")
        .check([](const std::string &value)
               { return std::stoi(value) <= 100; },
               "percent must be at most 100")
        .check([](const std::string &value)
               { return std::stoi(value) % 2 == 0; },
               "percent must be even");

    CHECK(parse(parser, {"--percent", "42"}).get<int>("percent") == 42);
    CHECK_THROWS_AS((parse(parser, {"--percent", "-2"})), ArgumentError);
    CHECK_THROWS_AS((parse(parser, {"--percent", "101"})), ArgumentError);
    CHECK_THROWS_AS((parse(parser, {"--percent", "3"})), ArgumentError);
    CHECK_THAT(thrown_message<ArgumentError>(
                   [&]
                   { (void)parse(parser, {"--percent", "3"}); }),
               ContainsSubstring("percent must be even"));
}

TEST_CASE("validator exceptions become ArgumentError diagnostics",
          "[validation][custom][errors]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--value").check(
        [](const std::string &)
        {
            throw std::runtime_error("validator exploded");
            return true;
        });

    CHECK_THROWS_AS((parse(parser, {"--value", "x"})), ArgumentError);
    CHECK(thrown_code<ArgumentError>([&]
                                     { (void)parse(parser, {"--value", "x"}); }) ==
          ErrorCode::InvalidValue);
    CHECK_THAT(thrown_message<ArgumentError>(
                   [&]
                   { (void)parse(parser, {"--value", "x"}); }),
               ContainsSubstring("validator exploded"));
}

TEST_CASE("built-in range validators are type-aware", "[validation][builtin]")
{
    const auto integer_range = BuiltinValidators::in_range(1, 2);
    CHECK(integer_range("1"));
    CHECK(integer_range("2"));
    CHECK_FALSE(integer_range("2.9"));
    CHECK_FALSE(integer_range(" 1"));
    CHECK_FALSE(integer_range("3"));
    CHECK_FALSE(integer_range("-1"));

    const auto double_range = BuiltinValidators::in_range(1.0, 3.0);
    CHECK(double_range("2.9"));
    CHECK_FALSE(double_range("3.1"));

    ArgumentParser parser("tool");
    parser.add_argument("--workers")
        .check(BuiltinValidators::in_range(1, 8), "workers out of range");
    CHECK_THROWS_AS((parse(parser, {"--workers", "2.5"})), ArgumentError);
    CHECK(parse(parser, {"--workers", "4"}).get<int>("workers") == 4);
}

TEST_CASE("filesystem validators distinguish files and directories",
          "[validation][builtin][filesystem]")
{
    const auto directory = std::filesystem::temp_directory_path();
    CHECK(BuiltinValidators::is_dir(directory.string()));
    CHECK(BuiltinValidators::exists(directory.string()));
    CHECK_FALSE(BuiltinValidators::is_file(directory.string()));

    const auto file = directory / "argparse_validator_file.txt";
    {
        std::ofstream stream(file);
        stream << "data";
    }
    CHECK(BuiltinValidators::is_file(file.string()));
    CHECK(BuiltinValidators::exists(file.string()));
    CHECK_FALSE(BuiltinValidators::is_dir(file.string()));

    std::error_code error;
    std::filesystem::remove(file, error);
    CHECK_FALSE(BuiltinValidators::exists(file.string()));
}

TEST_CASE("validators run on defaults and environment values",
          "[validation][defaults][environment]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--value")
        .default_value("bad")
        .check([](const std::string &value)
               { return value == "good"; },
               "value must be good");

    CHECK_THROWS_AS((parse(parser, {})), ArgumentError);

    ScopedEnv value("ARGPARSE_TEST_VALIDATED", "bad");
    ArgumentParser env_parser("tool");
    env_parser.add_argument("--value")
        .env("ARGPARSE_TEST_VALIDATED")
        .check([](const std::string &v)
               { return v == "good"; },
               "value must be good");
    CHECK_THROWS_AS((parse(env_parser, {})), ArgumentError);
}

// -------------------------------------------------------------------------
// Groups and dependencies
// -------------------------------------------------------------------------

TEST_CASE("mutually exclusive groups reject multiple members",
          "[groups][exclusive]")
{
    ArgumentParser parser("tool");
    auto &output = parser.add_mutually_exclusive_group();
    output.add_argument("--json").action("store_true");
    output.add_argument("--yaml").action("store_true");

    const auto &empty = parse(parser, {});
    CHECK_FALSE(empty.get<bool>("json"));
    CHECK_FALSE(empty.get<bool>("yaml"));

    CHECK_THROWS_AS((parse(parser, {"--json", "--yaml"})), ArgumentError);
    CHECK(thrown_code<ArgumentError>([&]
                                     { (void)parse(parser, {"--json", "--yaml"}); }) ==
          ErrorCode::MutuallyExclusive);
    CHECK_THAT(thrown_message<ArgumentError>(
                   [&]
                   { (void)parse(parser, {"--json", "--yaml"}); }),
               ContainsSubstring("mutually exclusive"));
}

TEST_CASE("required groups enforce their cardinality", "[groups][required]")
{
    ArgumentParser parser("tool");
    auto &auth = parser.add_argument_group("authentication");
    auth.exclusive(true).required(true);
    auth.add_argument("--token");
    auth.add_argument("--password");

    CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    CHECK_THROWS_AS((parse(parser, {"--token", "a", "--password", "b"})),
                    ArgumentError);
    CHECK(parse(parser, {"--token", "a"}).get<std::string>("token") == "a");
    CHECK_THAT(thrown_message<RequiredError>([&]
                                             { (void)parse(parser, {}); }),
               ContainsSubstring("authentication"));
}

TEST_CASE("at-least-one and exactly-one cardinality", "[groups][cardinality]")
{
    ArgumentParser parser("tool");
    auto &one = parser.add_argument_group("one-of");
    one.cardinality(ArgumentParser::GroupCardinality::ExactlyOne);
    one.add_argument("--a").action("store_true");
    one.add_argument("--b").action("store_true");

    CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    CHECK_THROWS_AS((parse(parser, {"--a", "--b"})), ArgumentError);
    CHECK(parse(parser, {"--b"}).get<bool>("b"));

    ArgumentParser at_least("tool");
    auto &group = at_least.add_argument_group("at-least-one");
    group.cardinality(ArgumentParser::GroupCardinality::AtLeastOne);
    group.add_argument("--a").action("store_true");
    group.add_argument("--b").action("store_true");

    CHECK_THROWS_AS((parse(at_least, {})), RequiredError);
    CHECK(parse(at_least, {"--a", "--b"}).get<bool>("a"));
}

TEST_CASE("grouped arguments appear under their group heading",
          "[groups][help]")
{
    ArgumentParser parser("tool");
    parser.formatter().format(HelpFormatter::Format::Plain).width(80);
    auto &network = parser.add_argument_group("network", "Network options.");
    network.add_argument("--host").help("Server host.");

    const std::string help = parser.format_help();
    CHECK_THAT(help, ContainsSubstring("network:"));
    CHECK_THAT(help, ContainsSubstring("Network options."));
    CHECK_THAT(help, ContainsSubstring("--host"));
}

TEST_CASE("dependencies resolve aliases and respect defaults and environment",
          "[dependencies]")
{
    SECTION("alias dependency")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--user");
        parser.add_argument("--password").depends_on("--user");

        CHECK_THROWS_AS((parse(parser, {"--user", "alice"})), RequiredIfError);
        CHECK(parse(parser, {"--user", "alice", "--password", "secret"})
                  .get<std::string>("password") == "secret");
        CHECK(parse(parser, {"--password", "secret"}).get<std::string>("password") ==
              "secret");
    }

    SECTION("a default satisfies the dependent argument")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--user").action("store_true");
        parser.add_argument("--password").depends_on("user").default_value("secret");

        const auto &ns = parse(parser, {"--user"});
        CHECK(ns.get<bool>("user"));
        CHECK(ns.get<std::string>("password") == "secret");
    }

    SECTION("a defaulted dependency triggers the dependent argument")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--user").action("store_true").default_value(true);
        parser.add_argument("--password").depends_on("user");

        CHECK_THROWS_AS((parse(parser, {})), RequiredIfError);
    }

    SECTION("environment values satisfy dependencies")
    {
        ScopedEnv token("ARGPARSE_TEST_TOKEN", "secret");
        ArgumentParser parser("tool");
        parser.add_argument("--user").action("store_true");
        parser.add_argument("--token").env("ARGPARSE_TEST_TOKEN").depends_on("user");

        CHECK(parse(parser, {"--user"}).get<std::string>("token") == "secret");
    }
}

TEST_CASE("requires_all and conflicts_with relationships", "[dependencies]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--user");
    parser.add_argument("--password");
    parser.add_argument("--login").action("store_true").requires_all({"user", "password"});
    parser.add_argument("--json").action("store_true").conflicts_with("--yaml");
    parser.add_argument("--yaml").action("store_true");

    CHECK(parse(parser, {}).get<bool>("login") == false);
    CHECK_THROWS_AS((parse(parser, {"--login"})), RequiredIfError);
    CHECK_THROWS_AS((parse(parser, {"--login", "--user", "a"})), RequiredIfError);
    CHECK(parse(parser, {"--login", "--user", "a", "--password", "b"})
              .get<bool>("login"));

    CHECK_THROWS_AS((parse(parser, {"--json", "--yaml"})), ArgumentError);
    CHECK(thrown_code<ArgumentError>([&]
                                     { (void)parse(parser, {"--json", "--yaml"}); }) ==
          ErrorCode::MutuallyExclusive);
}

TEST_CASE("unknown dependency and conflict names are usage errors",
          "[dependencies][errors]")
{
    ArgumentParser depends("tool");
    depends.add_argument("--a").depends_on("missing");
    CHECK_THROWS_AS((parse(depends, {"--a", "x"})), UsageError);

    ArgumentParser conflicts("tool");
    conflicts.add_argument("--a").conflicts_with("missing");
    CHECK_THROWS_AS((parse(conflicts, {"--a", "x"})), UsageError);
}

// -------------------------------------------------------------------------
// Subcommands
// -------------------------------------------------------------------------

TEST_CASE("subcommands parse their own arguments", "[subcommands]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("store_true");
    auto &run = parser.add_subcommand("run", "Run a job");
    run.add_argument("path");
    run.add_argument("--mode").choices({"fast", "safe"}).default_value("fast");

    const auto &ns = parse(parser, {"--verbose", "run", "data.txt", "--mode", "safe"});
    REQUIRE(ns.has_subcommand());
    CHECK(ns.subcommand() == "run");
    CHECK(ns.get<bool>("verbose"));

    const auto &child = ns.subcommand_namespace();
    CHECK(child.get<std::string>("path") == "data.txt");
    CHECK(child.get<std::string>("mode") == "safe");
}

TEST_CASE("subcommands support aliases and nested dispatch", "[subcommands][aliases]")
{
    ArgumentParser parser("git");
    auto &checkout = parser.add_subcommand({"checkout", "co"}, "Check out a branch");
    checkout.add_argument("branch");

    const auto &direct = parse(parser, {"checkout", "main"});
    CHECK(direct.subcommand() == "checkout");
    CHECK(direct.subcommand_namespace().get<std::string>("branch") == "main");

    const auto &alias = parse(parser, {"co", "dev"});
    CHECK(alias.subcommand() == "checkout");
    CHECK(alias.subcommand_namespace().get<std::string>("branch") == "dev");

    ArgumentParser nested("git");
    auto &remote = nested.add_subcommand("remote", "Manage remotes");
    auto &add = remote.add_subcommand("add", "Add a remote");
    add.add_argument("name");
    add.add_argument("url");

    const auto &ns = parse(nested, {"remote", "add", "origin", "https://example.com"});
    CHECK(ns.subcommand() == "remote");
    const auto &remote_ns = ns.subcommand_namespace();
    CHECK(remote_ns.subcommand() == "add");
    CHECK(remote_ns.subcommand_namespace().get<std::string>("name") == "origin");
    CHECK(remote_ns.subcommand_namespace().get<std::string>("url") ==
          "https://example.com");
}

TEST_CASE("parent option values are not mistaken for subcommands",
          "[subcommands][regression]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--output");
    parser.add_subcommand("run", "Run a job");

    const auto &ns = parse(parser, {"--output", "run"});
    CHECK_FALSE(ns.has_subcommand());
    CHECK(ns.get<std::string>("output") == "run");
}

TEST_CASE("parent options must precede the subcommand", "[subcommands][ordering]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");
    auto &run = parser.add_subcommand("run");
    run.add_argument("path");

    CHECK_THROWS_AS((parse(parser, {"run", "data.txt", "--verbose"})), ParseError);

    const auto &ns = parse(parser, {"--verbose", "run", "data.txt"});
    CHECK(ns.get<bool>("verbose"));
    CHECK(ns.subcommand_namespace().get<std::string>("path") == "data.txt");
}

TEST_CASE("unknown subcommands raise a dedicated error",
          "[subcommands][errors]")
{
    ArgumentParser parser("tool");
    parser.add_subcommand("run", "Run a job");
    parser.add_subcommand("build", "Build a job");

    CHECK_THROWS_AS((parse(parser, {"rnu"})), UnknownSubcommandError);
    CHECK(thrown_code<UnknownSubcommandError>([&]
                                              { (void)parse(parser, {"rnu"}); }) ==
          ErrorCode::UnknownSubcommand);
    CHECK_THAT(thrown_message<UnknownSubcommandError>(
                   [&]
                   { (void)parse(parser, {"rnu"}); }),
               ContainsSubstring("Did you mean run?"));

    try
    {
        (void)parse(parser, {"rnu"});
    }
    catch (const UnknownSubcommandError &error)
    {
        CHECK(error.token() == "rnu");
    }
}

TEST_CASE("a parent positional can accept an unknown subcommand token",
          "[subcommands][positional]")
{
    ArgumentParser parser("tool");
    parser.add_argument("command");
    parser.add_subcommand("run", "Run a job");

    const auto &ns = parse(parser, {"custom"});
    CHECK_FALSE(ns.has_subcommand());
    CHECK(ns.get<std::string>("command") == "custom");
}

TEST_CASE("required and default subcommands", "[subcommands][required]")
{
    SECTION("required subcommand")
    {
        ArgumentParser parser("tool");
        parser.require_subcommand();
        parser.add_subcommand("run", "Run a job");

        CHECK_THROWS_AS((parse(parser, {})), RequiredError);
        CHECK_THAT(thrown_message<RequiredError>([&]
                                                 { (void)parse(parser, {}); }),
                   ContainsSubstring("choose from run"));
        CHECK(parse(parser, {"run"}).subcommand() == "run");
    }

    SECTION("default subcommand receives unclaimed tokens")
    {
        ArgumentParser parser("tool");
        auto &run = parser.add_subcommand("run", "Run a job");
        run.add_argument("path").nargs('?');
        parser.default_subcommand("run");

        const auto &empty = parse(parser, {});
        CHECK(empty.subcommand() == "run");
        CHECK(empty.subcommand_namespace().raw_values("path").empty());

        const auto &ns = parse(parser, {"data.txt"});
        CHECK(ns.subcommand() == "run");
        CHECK(ns.subcommand_namespace().get<std::string>("path") == "data.txt");
    }

    SECTION("unknown default subcommand")
    {
        ArgumentParser parser("tool");
        parser.default_subcommand("missing");

        CHECK_THROWS_AS((parse(parser, {})), UsageError);
    }
}

TEST_CASE("subcommand namespace access without a subcommand throws",
          "[subcommands][namespace]")
{
    ArgumentParser parser("tool");
    const auto &ns = parse(parser, {});
    CHECK_FALSE(ns.has_subcommand());
    CHECK_THROWS_AS(ns.subcommand_namespace(), ParseError);
}

// -------------------------------------------------------------------------
// parse_known_args and non-throwing parsing
// -------------------------------------------------------------------------

TEST_CASE("parse_known_args preserves unknown options and positionals",
          "[known][errors]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");

    const auto &ns = parse_known(parser, {"--verbose", "--unknown", "value",
                                          "--other=1", "extra"});
    CHECK(ns.get<bool>("verbose"));
    CHECK(parser.unknown_args() ==
          std::vector<std::string>({"--unknown", "value", "--other=1", "extra"}));
    CHECK(parser.unknown_options() ==
          std::vector<std::string>({"--unknown", "value", "--other=1"}));
    CHECK(parser.unknown_positionals() ==
          std::vector<std::string>({"extra"}));

    const auto unknown = parser.unknown();
    CHECK(unknown.options == parser.unknown_options());
    CHECK(unknown.positionals == parser.unknown_positionals());

    CHECK_THROWS_AS((parse(parser, {"--unknown"})), ParseError);
}

TEST_CASE("parse_known_args preserves unknown short clusters",
          "[known][short]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("store_true");

    const auto &ns = parse_known(parser, {"-vz"});
    CHECK(ns.get<bool>("verbose"));
    CHECK(parser.unknown_options() == std::vector<std::string>({"-z"}));
}

TEST_CASE("parse_known_args assigns known positionals and reports extras",
          "[known][positional]")
{
    ArgumentParser parser("tool");
    parser.add_argument("input");

    const auto &ns = parse_known(parser, {"file.txt", "--unknown", "extra.txt"});
    CHECK(ns.get<std::string>("input") == "file.txt");
    CHECK(parser.unknown_options() == std::vector<std::string>({"--unknown"}));
    CHECK(parser.unknown_positionals() ==
          std::vector<std::string>({"extra.txt"}));
}

TEST_CASE("parse_known_args merges unknown tokens from subcommands",
          "[known][subcommands]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");
    auto &run = parser.add_subcommand("run");
    run.add_argument("input");
    run.add_argument("--child-flag").action("store_true");

    (void)parse_known(parser, {"--ghost", "value", "run", "--child-ghost",
                               "known", "extra"});

    CHECK(parser.unknown_args() ==
          std::vector<std::string>({"--ghost", "value", "--child-ghost", "extra"}));
    CHECK(parser.unknown_options() ==
          std::vector<std::string>({"--ghost", "value", "--child-ghost"}));
    CHECK(parser.unknown_positionals() ==
          std::vector<std::string>({"extra"}));
}

TEST_CASE("try_parse_args reports success and failure without throwing",
          "[known][try]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--count").default_value(1);

    Argv success_args({"--count", "3"});
    auto success = parser.try_parse_args(success_args.argc(), success_args.argv());
    REQUIRE(success);
    CHECK(success.code == ErrorCode::None);
    CHECK(success->get<int>("count") == 3);
    CHECK(success.value().get<int>("count") == 3);

    Argv failure_args({"--unknown"});
    auto failure = parser.try_parse_args(failure_args.argc(), failure_args.argv());
    CHECK_FALSE(failure);
    CHECK(failure.code == ErrorCode::UnknownOption);
    CHECK_THAT(failure.error(), ContainsSubstring("unrecognized argument"));
    CHECK_THROWS_AS(failure.value(), ArgumentError);

    Argv missing_args({"--count"});
    auto missing = parser.try_parse_args(missing_args.argc(), missing_args.argv());
    CHECK_FALSE(missing);
}

TEST_CASE("try_parse_args remembers the help mode",
          "[known][try][help][regression]")
{
    ArgumentParser parser("tool");
    parser.formatter().format(HelpFormatter::Format::Plain).width(80);
    parser.add_argument("--visible");
    parser.add_argument("--secret").hidden();

    Argv basic_args({"--help"});
    const auto basic = parser.try_parse_args(basic_args.argc(), basic_args.argv());
    CHECK_FALSE(basic);
    CHECK(basic.code == ErrorCode::HelpRequested);
    CHECK_FALSE(parser.format_help().find("--secret") != std::string::npos);

    Argv all_args({"--help-all"});
    const auto all = parser.try_parse_args(all_args.argc(), all_args.argv());
    CHECK_FALSE(all);
    CHECK(all.code == ErrorCode::HelpRequested);
    CHECK_THAT(parser.format_help(), ContainsSubstring("--secret"));

    Argv empty_args({});
    const auto reset = parser.try_parse_args(empty_args.argc(), empty_args.argv());
    REQUIRE(reset);
    CHECK_FALSE(parser.format_help().find("--secret") != std::string::npos);
}

TEST_CASE("try_parse_known_args keeps unknowns",
          "[known][try]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");

    Argv argv({"--verbose", "--unknown"});
    auto result = parser.try_parse_known_args(argv.argc(), argv.argv());
    REQUIRE(result);
    CHECK(result->get<bool>("verbose"));
    CHECK(parser.unknown_options() == std::vector<std::string>({"--unknown"}));
}

TEST_CASE("format_error includes the message and usage line",
          "[errors][format]")
{
    ArgumentParser required("tool");
    required.add_argument("--token").required();

    try
    {
        (void)parse(required, {});
        FAIL("expected a RequiredError");
    }
    catch (const ArgumentError &error)
    {
        const std::string formatted = required.format_error(error);
        CHECK_THAT(formatted, ContainsSubstring("error:"));
        CHECK_THAT(formatted, ContainsSubstring("usage: tool"));
    }

    // Conversion errors are detected when the namespace is queried, not while
    // parsing (the parser intentionally stores raw strings).
    ArgumentParser parser("tool");
    parser.add_argument("--count");
    const auto &ns = parse(parser, {"--count", "not-a-number"});
    CHECK_THROWS_AS(ns.get<int>("count"), ArgumentError);
}

TEST_CASE("error codes have stable names", "[errors][codes]")
{
    CHECK(std::string(argparse::error_code_name(ErrorCode::None)) == "none");
    CHECK(std::string(argparse::error_code_name(ErrorCode::UnknownOption)) ==
          "unknown-option");
    CHECK(std::string(argparse::error_code_name(ErrorCode::UnknownPositional)) ==
          "unknown-positional");
    CHECK(std::string(argparse::error_code_name(ErrorCode::UnknownSubcommand)) ==
          "unknown-subcommand");
    CHECK(std::string(argparse::error_code_name(ErrorCode::MissingValue)) ==
          "missing-value");
    CHECK(std::string(argparse::error_code_name(ErrorCode::MissingRequired)) ==
          "missing-required");
    CHECK(std::string(argparse::error_code_name(ErrorCode::InvalidChoice)) ==
          "invalid-choice");
    CHECK(std::string(argparse::error_code_name(ErrorCode::InvalidValue)) ==
          "invalid-value");
    CHECK(std::string(argparse::error_code_name(ErrorCode::MutuallyExclusive)) ==
          "mutually-exclusive");
    CHECK(std::string(argparse::error_code_name(ErrorCode::DependencyMissing)) ==
          "dependency-missing");
    CHECK(std::string(argparse::error_code_name(ErrorCode::InvalidRegistration)) ==
          "invalid-registration");
    CHECK(std::string(argparse::error_code_name(ErrorCode::HelpRequested)) ==
          "help-requested");
    CHECK(std::string(argparse::error_code_name(ErrorCode::Conversion)) ==
          "conversion");
    CHECK(std::string(argparse::error_code_name(ErrorCode::Io)) == "io");
}

// -------------------------------------------------------------------------
// Namespace API
// -------------------------------------------------------------------------

TEST_CASE("namespace has, provided, and registration semantics",
          "[namespace]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--value");
    parser.add_argument("--flag").action("store_true");

    const auto &ns = parse(parser, {"--flag"});

    CHECK(ns.has("value"));
    CHECK(ns.contains("value"));
    CHECK(ns.is_registered("value"));
    CHECK_FALSE(ns.provided("value"));
    CHECK(ns.provided("flag"));
    CHECK_FALSE(ns.has("missing"));
    CHECK_THROWS_AS(ns.raw_values("missing"), ParseError);
    CHECK_THROWS_AS(ns.get<std::string>("missing"), ParseError);
}

TEST_CASE("typed namespace access", "[namespace][conversion]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--number");
    parser.add_argument("--ratio");
    parser.add_argument("--enabled");
    parser.add_argument("--mode");
    parser.add_argument("--letter");
    parser.add_argument("--values").nargs('*');
    parser.add_argument("--names").action("append");

    const auto &ns = parse(parser, {"--number", "42", "--ratio", "2.5",
                                    "--enabled", "yes", "--mode", "2",
                                    "--letter", "x", "--values", "1", "2", "3",
                                    "--names", "alice", "--names", "bob"});

    CHECK(ns.get<int>("number") == 42);
    CHECK(ns.get<double>("ratio") == Catch::Approx(2.5));
    CHECK(ns.get<bool>("enabled"));
    CHECK(ns.get<TestMode>("mode") == TestMode::Safe);
    CHECK(ns.get<char>("letter") == 'x');
    CHECK(ns.getlist<int>("values") == std::vector<int>({1, 2, 3}));
    CHECK(ns.getlist<std::string>("names") ==
          std::vector<std::string>({"alice", "bob"}));
}

TEST_CASE("get_if and get_or handle absent, empty, and invalid values",
          "[namespace][get_if]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--present");
    parser.add_argument("--empty").nargs('?');
    parser.add_argument("--invalid");

    const auto &ns = parse(parser, {"--present", "10", "--empty", "--invalid", "x"});

    CHECK(ns.get_if<int>("present") == 10);
    CHECK_FALSE(ns.get_if<int>("empty").has_value());
    CHECK_FALSE(ns.get_if<int>("invalid").has_value());
    CHECK_FALSE(ns.get_if<int>("missing").has_value());

    CHECK(ns.get_or<int>("missing", 7) == 7);
    CHECK(ns.get_or<int>("empty", 7) == 7);
    CHECK(ns.get_or<int>("present", 7) == 10);
    CHECK_THROWS_AS(ns.get_or<int>("invalid", 7), ArgumentError);

    const auto optional_value = ns.get_if<std::optional<int>>("present");
    REQUIRE(optional_value.has_value());
    CHECK(optional_value->value() == 10);
}

TEST_CASE("string_view and const char pointers reference namespace storage",
          "[namespace][conversion]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--name");

    const auto &ns = parse(parser, {"--name", "alice"});
    const std::string_view view = ns.get<std::string_view>("name");
    const char *pointer = ns.get<const char *>("name");

    CHECK(view == "alice");
    CHECK(std::string(pointer) == "alice");
    CHECK(pointer == ns.raw_values("name").front().c_str());
}

TEST_CASE("conversion failures report the argument name",
          "[namespace][conversion][errors]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--number");
    parser.add_argument("--unsigned");
    parser.add_argument("--letter");
    parser.add_argument("--toggle");

    const auto &ns = parse(parser, {"--number", "999999999999999999999999",
                                    "--unsigned", "-1", "--letter", "ab",
                                    "--toggle", "maybe"});

    const auto number_error = thrown_message<ArgumentError>(
        [&]
        { (void)ns.get<int>("number"); });
    CHECK_THAT(number_error, ContainsSubstring("number"));
    CHECK_THAT(number_error, ContainsSubstring("integer"));
    CHECK(thrown_code<ArgumentError>([&]
                                     { (void)ns.get<int>("number"); }) ==
          ErrorCode::Conversion);

    CHECK_THROWS_AS(ns.get<unsigned>("unsigned"), ArgumentError);
    CHECK_THROWS_AS(ns.get<char>("letter"), ArgumentError);
    CHECK_THROWS_AS(ns.get<bool>("toggle"), ArgumentError);
}

TEST_CASE("bool conversion accepts common spellings",
          "[namespace][conversion][bool]")
{
    for (const char *value : {"1", "true", "TRUE", "yes", "on"})
    {
        ArgumentParser parser("tool");
        parser.add_argument("--value");
        CHECK(parse(parser, {"--value", value}).get<bool>("value"));
    }

    for (const char *value : {"0", "false", "FALSE", "no", "off"})
    {
        ArgumentParser parser("tool");
        parser.add_argument("--value");
        CHECK_FALSE(parse(parser, {"--value", value}).get<bool>("value"));
    }
}

TEST_CASE("namespace copies preserve subcommand state",
          "[namespace][copy]")
{
    ArgumentParser parser("tool");
    auto &run = parser.add_subcommand("run");
    run.add_argument("path");

    const auto &ns = parse(parser, {"run", "data.txt"});
    ArgumentParser::Namespace copy(ns);
    CHECK(copy.has_subcommand());
    CHECK(copy.subcommand() == "run");
    CHECK(copy.subcommand_namespace().get<std::string>("path") == "data.txt");

    ArgumentParser::Namespace assigned;
    assigned = copy;
    CHECK(assigned.subcommand() == "run");
    CHECK(assigned.subcommand_namespace().get<std::string>("path") == "data.txt");
}

// -------------------------------------------------------------------------
// Help formatter
// -------------------------------------------------------------------------

TEST_CASE("help options raise HelpError and expose show_hidden",
          "[help][actions]")
{
    ArgumentParser parser("tool");

    CHECK_THROWS_AS((parse(parser, {"-h"})), HelpError);
    CHECK_THROWS_AS((parse(parser, {"--help"})), HelpError);
    CHECK_THROWS_AS((parse(parser, {"--help-all"})), HelpError);
    CHECK_THROWS_AS((parse(parser, {"--help"})), ArgumentError);
    CHECK_THROWS_AS((parse(parser, {"--help"})), std::runtime_error);

    try
    {
        (void)parse(parser, {"--help"});
    }
    catch (const HelpError &error)
    {
        CHECK_FALSE(error.show_hidden());
        CHECK(error.code() == ErrorCode::HelpRequested);
    }

    try
    {
        (void)parse(parser, {"--help-all"});
    }
    catch (const HelpError &error)
    {
        CHECK(error.show_hidden());
    }
}

TEST_CASE("help actions are present in the namespace", "[help][namespace]")
{
    ArgumentParser parser("tool");
    const auto &ns = parse(parser, {});

    CHECK(ns.has("help"));
    CHECK(ns.has("help_all"));
    CHECK_FALSE(ns.provided("help"));
    CHECK_FALSE(ns.provided("help_all"));
    CHECK_FALSE(ns.get<bool>("help"));
    CHECK_FALSE(ns.get<bool>("help_all"));
}

TEST_CASE("help sections put headings on their own line",
          "[help][format]")
{
    ArgumentParser parser("tool", "Description.");
    parser.formatter().format(HelpFormatter::Format::Plain).width(80);
    parser.add_argument("--verbose").action("store_true").help("Verbose.");
    parser.add_argument("input").help("Input.");

    const std::string help = parser.format_help();
    CHECK_THAT(help, ContainsSubstring("optional arguments:\n"));
    CHECK_THAT(help, ContainsSubstring("positional arguments:\n"));
    CHECK_THAT(help, ContainsSubstring("--verbose"));
    CHECK_FALSE(help.find("optional arguments:  -h") != std::string::npos);
}

TEST_CASE("hidden arguments are omitted unless requested",
          "[help][hidden]")
{
    ArgumentParser parser("tool");
    parser.formatter().format(HelpFormatter::Format::Plain).width(80);
    parser.add_argument("--visible").help("Visible.");
    parser.add_argument("--secret").hidden().help("Secret.");

    const std::string normal = parser.format_help();
    CHECK_THAT(normal, ContainsSubstring("--visible"));
    CHECK_FALSE(normal.find("--secret") != std::string::npos);
    CHECK_FALSE(parser.format_usage().find("--secret") != std::string::npos);

    const std::string all = parser.format_help(true);
    CHECK_THAT(all, ContainsSubstring("--secret"));
}

TEST_CASE("usage synthesis covers flags, values, choices, nargs, and positionals",
          "[help][usage]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--flag").action("store_true");
    parser.add_argument("--pair").nargs(2);
    parser.add_argument("--maybe").nargs('?');
    parser.add_argument("--many").nargs('*');
    parser.add_argument("--some").nargs('+');
    parser.add_argument("--mode").choices({"fast", "safe"});
    parser.add_argument("--required").required();
    parser.add_argument("--hidden-option").hidden();
    parser.add_argument("input").nargs('?');
    parser.add_argument("files").nargs('*');

    const std::string usage = parser.format_usage();
    CHECK_THAT(usage, ContainsSubstring("usage: tool"));
    CHECK_THAT(usage, ContainsSubstring("[--flag]"));
    CHECK_THAT(usage, ContainsSubstring("--pair PAIR PAIR"));
    CHECK_THAT(usage, ContainsSubstring("[--maybe [MAYBE]]"));
    CHECK_THAT(usage, ContainsSubstring("[--many [MANY ...]]"));
    CHECK_THAT(usage, ContainsSubstring("[--some SOME [SOME ...]]"));
    CHECK_THAT(usage, ContainsSubstring("[--mode {fast,safe}]"));
    CHECK_THAT(usage, ContainsSubstring("--required REQUIRED"));
    CHECK_FALSE(usage.find("--hidden-option") != std::string::npos);
    CHECK_THAT(usage, ContainsSubstring("[input]"));
    CHECK_THAT(usage, ContainsSubstring("[files ...]"));
    CHECK_THAT(usage, ContainsSubstring("[--help]"));
    CHECK(usage.back() == '\n');
}

TEST_CASE("a custom usage string wins over synthesis", "[help][usage]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--flag");
    parser.usage("tool [custom]");

    CHECK_THAT(parser.format_usage(), ContainsSubstring("usage: tool [custom]"));
    CHECK_THAT(parser.format_help(), ContainsSubstring("usage: tool [custom]"));
}

TEST_CASE("markdown and compact help formats are implemented",
          "[help][format]")
{
    ArgumentParser parser("tool", "Description.");
    parser.add_argument("--verbose").action("store_true").help("Verbose output.");
    parser.add_argument("input").help("Input file.");

    parser.formatter()
        .format(HelpFormatter::Format::Markdown)
        .width(80)
        .use_color(false);
    const std::string markdown = parser.format_help();
    CHECK_THAT(markdown, ContainsSubstring("# tool"));
    CHECK_THAT(markdown, ContainsSubstring("## Usage"));
    CHECK_THAT(markdown, ContainsSubstring("```text"));
    CHECK_THAT(markdown, ContainsSubstring("- `--verbose`"));
    CHECK_THAT(markdown, ContainsSubstring("## positional arguments"));

    parser.formatter()
        .format(HelpFormatter::Format::Compact)
        .width(80)
        .use_color(false);
    const std::string compact = parser.format_help();
    CHECK_THAT(compact, ContainsSubstring("--verbose  Verbose output."));
}

TEST_CASE("sections can be hidden, sorted, and retrieved", "[help][sections]")
{
    HelpFormatter formatter(80, "tool");
    formatter.format(HelpFormatter::Format::Plain).use_color(false);

    auto &zebra = formatter.add_section("zebra");
    zebra.add_item("--z", "", "Z help.");
    auto &alpha = formatter.add_section("alpha");
    alpha.add_item("--a", "", "A help.");
    auto &hidden = formatter.add_section("hidden");
    hidden.add_item("--h", "", "H help.");
    hidden.visible(false);

    CHECK(&formatter.get_section("alpha") == &alpha);
    CHECK(&formatter.get_section("missing") != &alpha);
    CHECK(formatter.get_section("missing").title() == "missing");

    formatter.sort_sections(true);
    const std::string help = formatter.format_help();
    const std::size_t alpha_position = help.find("alpha:");
    const std::size_t zebra_position = help.find("zebra:");
    REQUIRE(alpha_position != std::string::npos);
    REQUIRE(zebra_position != std::string::npos);
    CHECK(alpha_position < zebra_position);
    CHECK_FALSE(help.find("hidden:") != std::string::npos);
}

TEST_CASE("formatter decorations and wrapping", "[help][format][wrap]")
{
    HelpFormatter formatter(36, "tool");
    formatter.format(HelpFormatter::Format::Plain)
        .use_color(false)
        .prefix_info("PREFIX")
        .suffix_info("SUFFIX")
        .description("A description that should wrap onto more than one line.")
        .epilog("EPILOG")
        .line_separator('-', 20);

    auto &section = formatter.add_section("examples");
    section.add_example("tool run data.txt", "Run the tool on a data file.");
    section.add_text("Extra text for the section.");

    const std::string help = formatter.format_help();
    CHECK_THAT(help, ContainsSubstring("PREFIX"));
    CHECK_THAT(help, ContainsSubstring("SUFFIX"));
    CHECK_THAT(help, ContainsSubstring("EPILOG"));
    CHECK_THAT(help, ContainsSubstring("--------------------"));
    CHECK_THAT(help, ContainsSubstring("$ tool run data.txt"));
    CHECK_THAT(help, ContainsSubstring("Extra text"));
    CHECK(help.find("A description that should wrap") != std::string::npos);
}

TEST_CASE("visible_length ignores ANSI escapes and table rows align",
          "[help][format][ansi]")
{
    CHECK(HelpFormatter::visible_length("plain") == 5);
    CHECK(HelpFormatter::visible_length("\033[31mred\033[0m") == 3);
    CHECK(HelpFormatter::visible_length("\033[1;34mblue\033[0m text") == 9);

    HelpFormatter::Table table;
    table.add_row({"a"});
    table.add_row({"long", "second"});

    HelpFormatter::Alignment alignment;
    alignment.indent_size = 2;
    const std::string formatted = table.format(alignment);
    CHECK_THAT(formatted, ContainsSubstring("  a"));
    CHECK_THAT(formatted, ContainsSubstring("  long  second"));
}

TEST_CASE("help colors honor NO_COLOR and CLICOLOR_FORCE", "[help][color]")
{
    ScopedEnv no_color("NO_COLOR");
    ScopedEnv force("CLICOLOR_FORCE");
    ScopedEnv clicolor("CLICOLOR", "0");

    CHECK_FALSE(HelpFormatter::supports_color());

    force.set("1");
    CHECK(HelpFormatter::supports_color());

    no_color.set("1");
    CHECK_FALSE(HelpFormatter::supports_color());

    no_color.unset();
    force.unset();
    clicolor.set("0");
    CHECK_FALSE(HelpFormatter::supports_color());

    clicolor.unset();
    force.set("1");
    HelpFormatter formatter(80, "tool");
    formatter.format(HelpFormatter::Format::Colored).use_color(true);
    CHECK_THAT(formatter.colorize("x", HelpFormatter::Color::Red),
               ContainsSubstring("\033[31m"));
    formatter.format(HelpFormatter::Format::Plain);
    CHECK(formatter.colorize("x", HelpFormatter::Color::Red) == "x");
}

TEST_CASE("interactive fallback calls a custom handler for missing required arguments",
          "[help][interactive]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--token").required();

    std::string prompted;
    parser.interactive([&](const ArgumentParser::Argument &argument)
                       {
                           prompted = argument.dest();
                           return std::optional<std::string>("secret");
                       });

    const auto &ns = parse(parser, {});
    CHECK(prompted == "token");
    CHECK(ns.get<std::string>("token") == "secret");
    CHECK(ns.provided("token"));
}

// -------------------------------------------------------------------------
// Completion scripts
// -------------------------------------------------------------------------

TEST_CASE("completion scripts support all documented shells",
          "[completion]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--visible").choices({"one", "two"}).help("Visible.");
    parser.add_argument("--secret").hidden();
    parser.add_subcommand("run", "Run a job");

    const std::string bash = parser.generate_completion_script("bash");
    CHECK_THAT(bash, ContainsSubstring("_tool_completion"));
    CHECK_THAT(bash, ContainsSubstring("--visible"));
    CHECK_THAT(bash, ContainsSubstring("one two"));
    CHECK_THAT(bash, ContainsSubstring("run"));
    CHECK_FALSE(bash.find("--secret") != std::string::npos);

    const std::string zsh = parser.generate_completion_script(argparse::ArgumentParser::Shell::Zsh);
    CHECK_THAT(zsh, ContainsSubstring("#compdef tool"));
    CHECK_THAT(zsh, ContainsSubstring("Visible."));

    const std::string fish = parser.generate_completion_script("fish");
    CHECK_THAT(fish, ContainsSubstring("complete -c tool"));
    CHECK_THAT(fish, ContainsSubstring("-l visible"));

    const std::string powershell = parser.generate_completion_script("pwsh");
    CHECK_THAT(powershell, ContainsSubstring("Register-ArgumentCompleter"));
    CHECK_THAT(powershell, ContainsSubstring("'--visible'"));

    CHECK_THROWS_AS(parser.generate_completion_script("tcsh"), ArgumentError);
    CHECK(thrown_code<ArgumentError>(
              [&]
              { (void)parser.generate_completion_script("tcsh"); }) ==
          ErrorCode::Usage);
}

TEST_CASE("completion identifiers are sanitized", "[completion][regression]")
{
    ArgumentParser parser("my tool/v2");
    const std::string bash = parser.generate_completion_script("bash");
    CHECK_THAT(bash, ContainsSubstring("_my_tool_v2_completion"));
}
