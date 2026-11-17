#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "argparse.h"

#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using argparse::ArgumentError;
using argparse::ArgumentParser;
using argparse::BuiltinValidators;
using argparse::HelpError;
using argparse::HelpFormatter;
using argparse::ParseError;
using argparse::RequiredError;
using argparse::RequiredIfError;
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

    class ScopedEnv
    {
    public:
        ScopedEnv(std::string name, std::string value)
            : m_Name(std::move(name))
        {
            const char *previous = std::getenv(m_Name.c_str());
            if (previous != nullptr)
            {
                m_HadPrevious = true;
                m_Previous = previous;
            }
            set_value(value);
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

    private:
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
} // namespace

// -------------------------------------------------------------------------
// Library metadata and parser configuration
// -------------------------------------------------------------------------

TEST_CASE("version macros match the documented release", "[api][version]")
{
    STATIC_REQUIRE(ARGPARSE_VERSION_MAJOR == 0);
    STATIC_REQUIRE(ARGPARSE_VERSION_MINOR == 1);
    STATIC_REQUIRE(ARGPARSE_VERSION_PATCH == 0);
    CHECK(std::string(ARGPARSE_VERSION) == "0.1.0");
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

    SECTION("duplicate option aliases")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-v", "--verbose");
        CHECK_THROWS_AS((parser.add_argument("-v", "--version")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--verbose")), ArgumentError);
    }

    SECTION("built-in help option is already registered")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS((parser.add_argument("-h")), ArgumentError);
        CHECK_THROWS_AS((parser.add_argument("--help")), ArgumentError);
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
        CHECK_THROWS_AS((parser.add_argument("--bad").nargs('!')), ParseError);
    }

    SECTION("invalid subcommand names")
    {
        ArgumentParser parser("tool");
        CHECK_THROWS_AS(parser.add_subcommand(""), ArgumentError);
        CHECK_THROWS_AS(parser.add_subcommand("-run"), ArgumentError);
    }
}

TEST_CASE("destination names are derived from option aliases", "[api][registration]")
{
    // Check each dest immediately: references returned by add_argument are
    // invalidated when a later add_argument reallocates the argument vector.
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

TEST_CASE("built-in help options raise HelpError", "[api][help]")
{
    ArgumentParser parser("tool");

    CHECK_THROWS_AS((parse(parser, {"-h"})), HelpError);
    CHECK_THROWS_AS((parse(parser, {"--help"})), HelpError);
    CHECK_THROWS_AS((parse(parser, {"--help-all"})), HelpError);

    // HelpError derives from the library base class.
    CHECK_THROWS_AS((parse(parser, {"--help"})), ArgumentError);
    CHECK_THROWS_AS((parse(parser, {"--help"})), std::runtime_error);
}

TEST_CASE("help arguments are present in the namespace", "[api][help][namespace]")
{
    ArgumentParser parser("tool");
    const auto &ns = parse(parser, {});

    CHECK(ns.has("help"));
    CHECK(ns.has("help_all"));
    CHECK_FALSE(ns.provided("help"));
    CHECK_FALSE(ns.get<bool>("help"));
}

// -------------------------------------------------------------------------
// Positional arguments
// -------------------------------------------------------------------------

TEST_CASE("positional arguments parse as strings", "[parse][positional]")
{
    ArgumentParser parser("cp");
    auto &input = parser.add_argument("input").help("input file");

    const auto &ns = parse(parser, {"file.txt"});

    CHECK(input.dest() == "input");
    CHECK(ns.has("input"));
    CHECK(ns.get<std::string>("input") == "file.txt");
    CHECK(list_of(ns, "input") == std::vector<std::string>({"file.txt"}));
}

TEST_CASE("positional arguments are never reported as provided", "[namespace][limitations]")
{
    // assign_positionals() stores values but never increments the occurrence
    // counter, so Namespace::provided() always returns false for positionals.
    ArgumentParser parser("tool");
    parser.add_argument("input");

    const auto &ns = parse(parser, {"file.txt"});
    CHECK(ns.has("input"));
    CHECK_FALSE(ns.provided("input"));
}

TEST_CASE("missing and extra positionals are rejected", "[parse][positional][errors]")
{
    ArgumentParser parser("cp");
    parser.add_argument("input");

    CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    CHECK_THROWS_AS((parse(parser, {"one", "two"})), ArgumentError);

    CHECK_THAT(thrown_message<RequiredError>([&]
                                             { (void)parse(parser, {}); }),
               ContainsSubstring("missing positional argument: input"));
    CHECK_THAT(thrown_message<ArgumentError>([&]
                                             { (void)parse(parser, {"one", "two"}); }),
               ContainsSubstring("unrecognized positional argument: two"));
}

TEST_CASE("positional defaults and the required check", "[parse][positional][defaults]")
{
    SECTION("a scalar positional default is unreachable")
    {
        // assign_positionals() throws before finalize_and_validate() can apply
        // the default because the positional still has m_Required == true.
        ArgumentParser parser("cp");
        parser.add_argument("input").default_value("stdin");

        CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    }

    SECTION("defaults work when the positional can be omitted")
    {
        ArgumentParser parser("cp");
        parser.add_argument("input")
            .nargs('*')
            .required(false)
            .default_value("stdin");

        const auto &ns = parse(parser, {});
        CHECK(ns.has("input"));
        CHECK(ns.get<std::string>("input") == "stdin");
    }
}

TEST_CASE("positional nargs patterns", "[parse][positional][nargs]")
{
    SECTION("question mark accepts zero or one")
    {
        ArgumentParser parser("tool");
        parser.add_argument("maybe").nargs('?').required(false);

        const auto &empty = parse(parser, {});
        CHECK(empty.has("maybe"));
        CHECK(empty.raw_values("maybe").empty());

        const auto &one = parse(parser, {"value"});
        CHECK(one.get<std::string>("maybe") == "value");

        CHECK_THROWS_AS((parse(parser, {"one", "two"})), ArgumentError);
    }

    SECTION("star accepts any number")
    {
        ArgumentParser parser("tool");
        parser.add_argument("files").nargs('*').required(false);

        const auto &empty = parse(parser, {});
        CHECK(empty.raw_values("files").empty());

        const auto &many = parse(parser, {"a", "b", "c"});
        CHECK(many.getlist<std::string>("files") ==
              std::vector<std::string>({"a", "b", "c"}));
    }

    SECTION("plus requires at least one")
    {
        ArgumentParser parser("tool");
        parser.add_argument("files").nargs('+');

        CHECK_THROWS_AS((parse(parser, {})), RequiredError);
        const auto &many = parse(parser, {"a", "b"});
        CHECK(many.getlist<std::string>("files") ==
              std::vector<std::string>({"a", "b"}));
    }

    SECTION("fixed count")
    {
        ArgumentParser parser("tool");
        parser.add_argument("pair").nargs(static_cast<std::size_t>(2));

        const auto &ok = parse(parser, {"left", "right"});
        CHECK(ok.getlist<std::string>("pair") ==
              std::vector<std::string>({"left", "right"}));

        CHECK_THROWS_AS((parse(parser, {"only-one"})), RequiredError);
        CHECK_THROWS_AS((parse(parser, {"a", "b", "c"})), ArgumentError);
    }

    SECTION("nargs zero or star does not clear the required flag")
    {
        // A positional is required by default even when nargs permits zero
        // values. Users must call required(false) explicitly.
        ArgumentParser parser("tool");
        parser.add_argument("maybe").nargs('?');
        CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    }
}

TEST_CASE("multiple positionals reserve values for later arguments", "[parse][positional][nargs]")
{
    SECTION("two scalar positionals")
    {
        ArgumentParser parser("tool");
        parser.add_argument("source");
        parser.add_argument("destination");

        const auto &ns = parse(parser, {"a", "b"});
        CHECK(ns.get<std::string>("source") == "a");
        CHECK(ns.get<std::string>("destination") == "b");

        CHECK_THROWS_AS((parse(parser, {"a"})), RequiredError);
        CHECK_THROWS_AS((parse(parser, {"a", "b", "c"})), ArgumentError);
    }

    SECTION("greedy first positional leaves one value for the last")
    {
        ArgumentParser parser("tool");
        parser.add_argument("sources").nargs('*').required(false);
        parser.add_argument("destination");

        const auto &ns = parse(parser, {"a", "b", "c"});
        CHECK(ns.getlist<std::string>("sources") ==
              std::vector<std::string>({"a", "b"}));
        CHECK(ns.get<std::string>("destination") == "c");

        const auto &single = parse(parser, {"c"});
        CHECK(single.raw_values("sources").empty());
        CHECK(single.get<std::string>("destination") == "c");
    }

    SECTION("fixed groups followed by a scalar")
    {
        ArgumentParser parser("tool");
        parser.add_argument("pair").nargs(static_cast<std::size_t>(2));
        parser.add_argument("destination");

        const auto &ns = parse(parser, {"a", "b", "c"});
        CHECK(ns.getlist<std::string>("pair") ==
              std::vector<std::string>({"a", "b"}));
        CHECK(ns.get<std::string>("destination") == "c");
    }
}

TEST_CASE("negative numbers and dash-only tokens are positional values", "[parse][positional][edge]")
{
    SECTION("negative integers")
    {
        ArgumentParser parser("tool");
        parser.add_argument("number");
        const auto &ns = parse(parser, {"-42"});
        CHECK(ns.get<int>("number") == -42);
    }

    SECTION("negative floating point")
    {
        ArgumentParser parser("tool");
        parser.add_argument("ratio");
        const auto &ns = parse(parser, {"-3.14"});
        CHECK(ns.get<double>("ratio") == Catch::Approx(-3.14));
    }

    SECTION("negative scientific notation")
    {
        ArgumentParser parser("tool");
        parser.add_argument("ratio");
        const auto &ns = parse(parser, {"-3.14e2"});
        CHECK(ns.get<double>("ratio") == Catch::Approx(-314.0));
    }

    SECTION("a bare dash is a positional value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("input");
        const auto &ns = parse(parser, {"-"});
        CHECK(ns.get<std::string>("input") == "-");
    }

    SECTION("dash-prefixed words are options, not positionals")
    {
        ArgumentParser parser("tool");
        parser.add_argument("input");
        CHECK_THROWS_AS((parse(parser, {"-file"})), ParseError);
    }
}

TEST_CASE("double dash terminates option parsing", "[parse][positional][edge]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");
    parser.add_argument("files").nargs('*').required(false);

    const auto &ns = parse(parser, {"--", "-v", "--verbose", "plain"});

    CHECK(ns.getlist<std::string>("files") ==
          std::vector<std::string>({"-v", "--verbose", "plain"}));
    CHECK_FALSE(ns.provided("verbose"));
}

// -------------------------------------------------------------------------
// Optional arguments and actions
// -------------------------------------------------------------------------

TEST_CASE("long options accept separate and equals values", "[parse][optional]")
{
    SECTION("separate value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-n", "--name");
        const auto &ns = parse(parser, {"--name", "alice"});
        CHECK(ns.get<std::string>("name") == "alice");
    }

    SECTION("equals value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-n", "--name");
        const auto &ns = parse(parser, {"--name=alice"});
        CHECK(ns.get<std::string>("name") == "alice");
    }

    SECTION("equals value containing equals signs")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");
        const auto &ns = parse(parser, {"--name=a=b=c"});
        CHECK(ns.get<std::string>("name") == "a=b=c");
    }

    SECTION("empty equals value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");
        const auto &ns = parse(parser, {"--name="});
        CHECK(ns.get<std::string>("name").empty());
    }

    SECTION("missing value at end of command line")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");
        CHECK_THROWS_AS((parse(parser, {"--name"})), RequiredError);
    }
}

TEST_CASE("short options support attached values and clusters", "[parse][optional]")
{
    SECTION("separate and attached values")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-n", "--name");

        CHECK(parse(parser, {"-n", "bob"}).get<std::string>("name") == "bob");
        CHECK(parse(parser, {"-nbob"}).get<std::string>("name") == "bob");
    }

    SECTION("boolean clusters")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-a").action("store_true");
        parser.add_argument("-b").action("store_true");
        parser.add_argument("-c").action("store_true");

        const auto &ns = parse(parser, {"-abc"});
        CHECK(ns.get<bool>("a"));
        CHECK(ns.get<bool>("b"));
        CHECK(ns.get<bool>("c"));
    }

    SECTION("cluster ending in an option that takes a value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-v", "--verbose").action("store_true");
        parser.add_argument("-o", "--output");

        const auto &ns = parse(parser, {"-voout.txt"});
        CHECK(ns.get<bool>("verbose"));
        CHECK(ns.get<std::string>("output") == "out.txt");
    }
}

TEST_CASE("store_true, store_false, and count actions", "[parse][optional][actions]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("store_true");
    parser.add_argument("--quiet").action("store_false");
    parser.add_argument("-c", "--count").action("count");

    SECTION("defaults when absent")
    {
        const auto &ns = parse(parser, {});
        CHECK_FALSE(ns.get<bool>("verbose"));
        CHECK(ns.get<bool>("quiet"));
        CHECK(ns.get<int>("count") == 0);
        CHECK_FALSE(ns.provided("verbose"));
        CHECK_FALSE(ns.provided("quiet"));
        CHECK_FALSE(ns.provided("count"));
    }

    SECTION("flags and counters when present")
    {
        const auto &ns = parse(parser, {"-v", "--quiet", "-ccc"});
        CHECK(ns.get<bool>("verbose"));
        CHECK_FALSE(ns.get<bool>("quiet"));
        CHECK(ns.get<int>("count") == 3);
        CHECK(ns.provided("verbose"));
        CHECK(ns.provided("quiet"));
        CHECK(ns.provided("count"));
    }

    SECTION("repeated store_true remains true")
    {
        const auto &ns = parse(parser, {"-v", "-v"});
        CHECK(ns.get<bool>("verbose"));
    }
}

TEST_CASE("append action accumulates values", "[parse][optional][actions]")
{
    SECTION("repeated scalar values")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-t", "--tag").action("append");

        const auto &ns = parse(parser, {"-t", "a", "--tag", "b", "-tc"});
        CHECK(ns.getlist<std::string>("tag") ==
              std::vector<std::string>({"a", "b", "c"}));
    }

    SECTION("append with multiple values per occurrence")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--pair").action("append").nargs(static_cast<std::size_t>(2));

        const auto &ns = parse(parser, {"--pair", "a", "b", "--pair", "c", "d"});
        CHECK(ns.getlist<std::string>("pair") ==
              std::vector<std::string>({"a", "b", "c", "d"}));
    }

    SECTION("store keeps only the last occurrence")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--value");

        const auto &ns = parse(parser, {"--value", "first", "--value", "last"});
        CHECK(ns.get<std::string>("value") == "last");
    }
}

TEST_CASE("defaults and implicit values", "[parse][optional][defaults]")
{
    SECTION("typed defaults")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--retries").default_value(3);
        parser.add_argument("--ratio").default_value(1.5);
        parser.add_argument("--label").default_value("none");
        parser.add_argument("--enabled").default_value(true);

        const auto &ns = parse(parser, {});
        CHECK(ns.get<int>("retries") == 3);
        CHECK(ns.get<double>("ratio") == Catch::Approx(1.5));
        CHECK(ns.get<std::string>("label") == "none");
        CHECK(ns.get<bool>("enabled"));
        CHECK_FALSE(ns.provided("retries"));
    }

    SECTION("multiple defaults")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--coords").nargs(static_cast<std::size_t>(2)).default_values({"0", "0"});

        const auto &ns = parse(parser, {});
        CHECK(ns.getlist<int>("coords") == std::vector<int>({0, 0}));
    }

    SECTION("implicit value with an explicit default")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--color")
            .nargs('?')
            .implicit_value("auto")
            .default_value("none");

        CHECK(parse(parser, {}).get<std::string>("color") == "none");
        CHECK(parse(parser, {"--color"}).get<std::string>("color") == "auto");
        CHECK(parse(parser, {"--color", "red"}).get<std::string>("color") == "red");
    }

    SECTION("implicit value without a default applies even when absent")
    {
        // Current implementation applies implicit_value during finalization
        // whenever nargs allows zero values, even if the option was absent.
        ArgumentParser parser("tool");
        parser.add_argument("--color").nargs('?').implicit_value("auto");

        CHECK(parse(parser, {}).get<std::string>("color") == "auto");
    }
}

TEST_CASE("environment variables provide fallback values", "[parse][optional][env]")
{
    ScopedEnv env("ARGPARSE_TEST_PORT", "9090");

    SECTION("environment is used when no CLI value or default exists")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--port").env("ARGPARSE_TEST_PORT");

        CHECK(parse(parser, {}).get<int>("port") == 9090);
        CHECK(parse(parser, {"--port", "1234"}).get<int>("port") == 1234);
    }

    SECTION("a default takes precedence over the environment")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--port").env("ARGPARSE_TEST_PORT").default_value(8080);

        CHECK(parse(parser, {}).get<int>("port") == 8080);
    }

    SECTION("environment values are validated")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--port")
            .env("ARGPARSE_TEST_PORT")
            .check(BuiltinValidators::in_range(1, 1024));

        CHECK_THROWS_AS((parse(parser, {})), ArgumentError);
    }
}

TEST_CASE("required optional arguments must be provided", "[parse][optional][errors]")
{
    SECTION("missing required option")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--token").required();

        CHECK_THROWS_AS((parse(parser, {})), RequiredError);
        CHECK_THAT(thrown_message<RequiredError>([&]
                                                 { (void)parse(parser, {}); }),
                   ContainsSubstring("required argument missing: token"));
        CHECK(parse(parser, {"--token", "secret"}).get<std::string>("token") ==
              "secret");
    }

    SECTION("a required option with a default is accepted without input")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--token").required().default_value("fallback");

        const auto &ns = parse(parser, {});
        CHECK(ns.get<std::string>("token") == "fallback");
        CHECK_FALSE(ns.provided("token"));
    }
}

TEST_CASE("nargs patterns for optional arguments", "[parse][optional][nargs]")
{
    SECTION("fixed count")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--point").nargs(static_cast<std::size_t>(2));

        const auto &ns = parse(parser, {"--point", "1", "2"});
        CHECK(ns.getlist<int>("point") == std::vector<int>({1, 2}));

        CHECK_THROWS_AS((parse(parser, {"--point", "1"})), RequiredError);
        CHECK_THROWS_AS((parse(parser, {"--point", "1", "2", "3"})),
                        ArgumentError);
    }

    SECTION("star consumes values until the next option")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--files").nargs('*');
        parser.add_argument("--verbose").action("store_true");

        const auto &ns = parse(parser, {"--files", "a", "b", "--verbose"});
        CHECK(ns.getlist<std::string>("files") ==
              std::vector<std::string>({"a", "b"}));
        CHECK(ns.get<bool>("verbose"));

        const auto &empty = parse(parser, {"--files", "--verbose"});
        CHECK(empty.raw_values("files").empty());
        CHECK(empty.get<bool>("verbose"));
    }

    SECTION("plus needs at least one value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--include").nargs('+');

        CHECK_THROWS_AS((parse(parser, {"--include"})), RequiredError);
        const auto &ns = parse(parser, {"--include", "a", "b"});
        CHECK(ns.getlist<std::string>("include") ==
              std::vector<std::string>({"a", "b"}));
    }
}

TEST_CASE("numeric option values including negative values", "[parse][optional][edge]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--number");
    parser.add_argument("--ratio");

    const auto &ns = parse(parser, {"--number", "-42", "--ratio", "-3.14e2"});

    CHECK(ns.get<int>("number") == -42);
    CHECK(ns.get<double>("ratio") == Catch::Approx(-314.0));
}

TEST_CASE("option parsing quirks that are currently observable", "[parse][optional][limitations]")
{
    SECTION("a required value consumes the next option-like token")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--name");
        parser.add_argument("--verbose").action("store_true");

        const auto &ns = parse(parser, {"--name", "--verbose"});
        CHECK(ns.get<std::string>("name") == "--verbose");
        CHECK_FALSE(ns.get<bool>("verbose"));
    }

    SECTION("inline values on flags are ignored")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--verbose").action("store_true");

        const auto &ns = parse(parser, {"--verbose=false"});
        CHECK(ns.get<bool>("verbose"));
    }

    SECTION("short options with equals keep the equals sign")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-n", "--number");

        const auto &ns = parse(parser, {"-n=5"});
        CHECK(ns.get<std::string>("number") == "=5");
    }
}

// -------------------------------------------------------------------------
// Validation and groups
// -------------------------------------------------------------------------

TEST_CASE("unknown options are rejected with suggestions", "[parse][errors]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--verbose").action("store_true");

    CHECK_THROWS_AS((parse(parser, {"--verbse"})), ParseError);
    CHECK_THAT(thrown_message<ParseError>([&]
                                          { (void)parse(parser, {"--verbse"}); }),
               ContainsSubstring("unrecognized argument: --verbse"));
    CHECK_THAT(thrown_message<ParseError>([&]
                                          { (void)parse(parser, {"--verbse"}); }),
               ContainsSubstring("Did you mean --verbose?"));

    CHECK_THROWS_AS((parse(parser, {"-z"})), ParseError);
    CHECK_THROWS_AS((parse(parser, {"--unknown=value"})), ParseError);
}

TEST_CASE("choices restrict accepted values", "[parse][validation]")
{
    SECTION("valid and invalid values")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--mode")
            .choices({"fast", "safe"})
            .default_value("fast");

        CHECK(parse(parser, {"--mode", "safe"}).get<std::string>("mode") == "safe");
        CHECK_THROWS_AS((parse(parser, {"--mode", "turbo"})), ArgumentError);
        CHECK_THAT(thrown_message<ArgumentError>(
                       [&]
                       { (void)parse(parser, {"--mode", "turbo"}); }),
                   ContainsSubstring("invalid choice for argument 'mode': turbo"));
    }

    SECTION("defaults are validated too")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--mode")
            .choices({"fast", "safe"})
            .default_value("turbo");

        CHECK_THROWS_AS((parse(parser, {})), ArgumentError);
    }
}

TEST_CASE("custom and built-in validators", "[parse][validation]")
{
    SECTION("custom validator with a custom message")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--code")
            .check([](const std::string &value)
                   { return value.size() == 3; },
                   "code must be exactly three characters");

        CHECK(parse(parser, {"--code", "abc"}).get<std::string>("code") == "abc");
        CHECK_THAT(thrown_message<ArgumentError>(
                       [&]
                       { (void)parse(parser, {"--code", "ab"}); }),
                   ContainsSubstring("code must be exactly three characters"));
    }

    SECTION("built-in numeric range validator")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--percent")
            .check(BuiltinValidators::in_range(0, 100));

        CHECK(parse(parser, {"--percent", "50"}).get<int>("percent") == 50);
        CHECK_THROWS_AS((parse(parser, {"--percent", "101"})), ArgumentError);
        CHECK_THROWS_AS((parse(parser, {"--percent", "abc"})), ArgumentError);
    }

    SECTION("built-in file and directory validators")
    {
        CHECK(BuiltinValidators::is_file(__FILE__));
        CHECK_FALSE(BuiltinValidators::is_file("__argparse_missing_file__"));
        CHECK(BuiltinValidators::is_dir("."));
        CHECK_FALSE(BuiltinValidators::is_dir("__argparse_missing_dir__"));

        ArgumentParser parser("tool");
        parser.add_argument("--file").check(BuiltinValidators::is_file);

        CHECK(parse(parser, {"--file", __FILE__}).get<std::string>("file") ==
              __FILE__);
        CHECK_THROWS_AS((parse(parser, {"--file", "__argparse_missing_file__"})),
                        ArgumentError);
    }

    SECTION("range validator accepts decimal input for integral types")
    {
        // in_range uses std::stod and casts to T. Decimal input is therefore
        // truncated before comparing, which is observable for int ranges.
        auto validator = BuiltinValidators::in_range(1, 2);
        CHECK(validator("1"));
        CHECK(validator("1.9"));
        CHECK(validator("2.9")); // truncated to 2 before comparison
        CHECK_FALSE(validator("3.9"));
        CHECK_FALSE(validator("not-a-number"));
    }
}

TEST_CASE("mutually exclusive groups", "[parse][groups]")
{
    ArgumentParser parser("tool");
    auto &group = parser.add_mutually_exclusive_group();
    group.add_argument("--json").action("store_true");
    group.add_argument("--yaml").action("store_true");

    SECTION("no option is allowed")
    {
        const auto &ns = parse(parser, {});
        CHECK_FALSE(ns.provided("json"));
        CHECK_FALSE(ns.provided("yaml"));
    }

    SECTION("one option is allowed")
    {
        CHECK(parse(parser, {"--json"}).get<bool>("json"));
    }

    SECTION("two options are rejected")
    {
        CHECK_THROWS_AS((parse(parser, {"--json", "--yaml"})), ArgumentError);
        CHECK_THAT(thrown_message<ArgumentError>([&]
                                                 {
                                                     (void)parse(parser, {"--json", "--yaml"});
                                                 }),
                   ContainsSubstring("arguments are mutually exclusive"));
    }
}

TEST_CASE("required groups demand at least one member", "[parse][groups]")
{
    ArgumentParser parser("tool");
    auto &group = parser.add_argument_group("output", "Output selection");
    group.exclusive(true).required(true);
    group.add_argument("--json").action("store_true");
    group.add_argument("--yaml").action("store_true");

    CHECK_THROWS_AS((parse(parser, {})), RequiredError);
    CHECK_THAT(thrown_message<RequiredError>([&]
                                             { (void)parse(parser, {}); }),
               ContainsSubstring("at least one argument from group 'output' is required"));
    CHECK(parse(parser, {"--yaml"}).get<bool>("yaml"));
    CHECK_THROWS_AS((parse(parser, {"--json", "--yaml"})), ArgumentError);
}

TEST_CASE("depends_on enforces conditional requirements", "[parse][groups]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--user");
    parser.add_argument("--password").depends_on("user");

    SECTION("no dependency means no requirement")
    {
        CHECK_NOTHROW(parse(parser, {}));
    }

    SECTION("dependency present requires the dependent option")
    {
        CHECK_THROWS_AS((parse(parser, {"--user", "alice"})), RequiredIfError);
        CHECK_THAT(thrown_message<RequiredIfError>(
                       [&]
                       { (void)parse(parser, {"--user", "alice"}); }),
                   ContainsSubstring("argument 'password' is required when 'user' is present"));
    }

    SECTION("both options satisfy the dependency")
    {
        const auto &ns = parse(parser, {"--user", "alice", "--password", "secret"});
        CHECK(ns.get<std::string>("user") == "alice");
        CHECK(ns.get<std::string>("password") == "secret");
    }

    SECTION("dependent option may be provided alone")
    {
        CHECK_NOTHROW(parse(parser, {"--password", "secret"}));
    }

    SECTION("a default value does not satisfy the occurrence check")
    {
        ArgumentParser second("tool");
        second.add_argument("--user");
        second.add_argument("--token").depends_on("user").default_value("none");

        CHECK_THROWS_AS((parse(second, {"--user", "alice"})), RequiredIfError);
    }
}

// -------------------------------------------------------------------------
// parse_known_args and unknown arguments
// -------------------------------------------------------------------------

TEST_CASE("parse_known_args captures unknown options", "[parse][known]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("store_true");
    parser.add_argument("--output");

    SECTION("unknown options are captured in order")
    {
        const auto &ns = parse_known(
            parser, {"--verbose", "--unknown", "--other=1", "-z"});

        CHECK(ns.get<bool>("verbose"));
        CHECK(parser.unknown_args() ==
              std::vector<std::string>({"--unknown", "--other=1", "-z"}));
    }

    SECTION("strict parsing rejects the same input")
    {
        CHECK_THROWS_AS((parse(parser, {"--verbose", "--unknown"})), ParseError);
    }

    SECTION("unknown arguments are reset between parses")
    {
        (void)parse_known(parser, {"--bogus"});
        CHECK(parser.unknown_args().size() == 1);

        (void)parse_known(parser, {"--verbose"});
        CHECK(parser.unknown_args().empty());
    }

    SECTION("known arguments still work after the unknown option")
    {
        const auto &ns =
            parse_known(parser, {"--unknown", "--output", "out.txt"});
        CHECK(ns.get<std::string>("output") == "out.txt");
        CHECK(parser.unknown_args() == std::vector<std::string>({"--unknown"}));
    }
}

TEST_CASE("parse_known_args does not preserve unknown positionals", "[parse][known][limitations]")
{
    SECTION("unknown positional values still throw")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--verbose").action("store_true");

        CHECK_THROWS_AS((parse_known(parser, {"extra"})), ArgumentError);
    }

    SECTION("a separate value after an unknown option becomes positional")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--verbose").action("store_true");

        CHECK_THROWS_AS((parse_known(parser, {"--unknown", "value"})),
                        ArgumentError);
    }

    SECTION("unknown short option in a cluster is captured incorrectly")
    {
        ArgumentParser parser("tool");
        parser.add_argument("-v", "--verbose").action("store_true");

        const auto &ns = parse_known(parser, {"-vz"});
        CHECK(ns.get<bool>("verbose"));
        // The current implementation captures "vz" (without the dash)
        // instead of "-z".
        CHECK(parser.unknown_args() == std::vector<std::string>({"vz"}));
    }
}

// -------------------------------------------------------------------------
// Subcommands
// -------------------------------------------------------------------------

namespace
{
    struct SubcommandFixture
    {
        ArgumentParser parser{"tool", "A tool with subcommands."};

        SubcommandFixture()
        {
            parser.add_argument("-v", "--verbose").action("store_true");

            auto &run = parser.add_subcommand("run", "Run a task");
            run.add_argument("path");
            run.add_argument("--mode")
                .choices({"fast", "safe"})
                .default_value("fast");

            auto &build = parser.add_subcommand("build", "Build artifacts");
            build.add_argument("--jobs").default_value(2);
        }
    };
} // namespace

TEST_CASE_METHOD(SubcommandFixture, "subcommands parse parent and child arguments", "[parse][subcommands]")
{
    SECTION("child arguments and defaults")
    {
        const auto &ns = parse(parser, {"run", "file.txt"});

        REQUIRE(ns.has_subcommand());
        CHECK(ns.subcommand() == "run");
        CHECK(ns.subcommand_namespace().get<std::string>("path") == "file.txt");
        CHECK(ns.subcommand_namespace().get<std::string>("mode") == "fast");
        CHECK_FALSE(ns.provided("verbose"));
    }

    SECTION("parent options before the subcommand")
    {
        const auto &ns =
            parse(parser, {"-v", "run", "file.txt", "--mode", "safe"});

        CHECK(ns.get<bool>("verbose"));
        CHECK(ns.subcommand() == "run");
        CHECK(ns.subcommand_namespace().get<std::string>("mode") == "safe");
    }

    SECTION("a second subcommand")
    {
        const auto &ns = parse(parser, {"build", "--jobs", "8"});
        CHECK(ns.subcommand() == "build");
        CHECK(ns.subcommand_namespace().get<int>("jobs") == 8);
    }

    SECTION("parent options after the subcommand belong to the child")
    {
        CHECK_THROWS_AS((parse(parser, {"run", "file.txt", "-v"})), ParseError);
    }
}

TEST_CASE_METHOD(SubcommandFixture, "subcommand errors and metadata", "[parse][subcommands][errors]")
{
    SECTION("missing child positional")
    {
        CHECK_THROWS_AS((parse(parser, {"run"})), RequiredError);
    }

    SECTION("invalid child choice")
    {
        CHECK_THROWS_AS((parse(parser, {"run", "file.txt", "--mode", "turbo"})),
                        ArgumentError);
    }

    SECTION("unknown subcommand is treated as an extra positional")
    {
        CHECK_THROWS_AS((parse(parser, {"serve"})), ArgumentError);
        CHECK_THAT(thrown_message<ArgumentError>([&]
                                                 { (void)parse(parser, {"serve"}); }),
                   ContainsSubstring("unrecognized positional argument: serve"));
    }

    SECTION("child help raises HelpError")
    {
        CHECK_THROWS_AS((parse(parser, {"run", "file.txt", "--help"})),
                        HelpError);
    }

    SECTION("no subcommand selected")
    {
        const auto &ns = parse(parser, {});
        CHECK_FALSE(ns.has_subcommand());
        CHECK(ns.subcommand().empty());
        CHECK_THROWS_AS(ns.subcommand_namespace(), ParseError);
    }

    SECTION("unknown arguments in a child are propagated")
    {
        const auto &ns =
            parse_known(parser, {"run", "file.txt", "--child-extra"});
        CHECK(ns.subcommand() == "run");
        CHECK(parser.unknown_args() ==
              std::vector<std::string>({"--child-extra"}));
    }
}

TEST_CASE("nested subcommands are supported", "[parse][subcommands]")
{
    ArgumentParser parser("git");
    auto &remote = parser.add_subcommand("remote", "Manage remotes");
    auto &add = remote.add_subcommand("add", "Add a remote");
    add.add_argument("name");
    add.add_argument("url");

    const auto &ns =
        parse(parser, {"remote", "add", "origin", "https://example.com/repo.git"});

    REQUIRE(ns.has_subcommand());
    CHECK(ns.subcommand() == "remote");
    REQUIRE(ns.subcommand_namespace().has_subcommand());
    CHECK(ns.subcommand_namespace().subcommand() == "add");
    CHECK(ns.subcommand_namespace().subcommand_namespace().get<std::string>("name") ==
          "origin");
    CHECK(ns.subcommand_namespace().subcommand_namespace().get<std::string>("url") ==
          "https://example.com/repo.git");
}

TEST_CASE("add_subcommand reuses an existing parser", "[api][subcommands]")
{
    ArgumentParser parser("tool");
    auto &first = parser.add_subcommand("run", "First description");
    auto &second = parser.add_subcommand("run", "Second description");

    CHECK(&first == &second);
    CHECK(first.description() == "Second description");
}

TEST_CASE("subcommands appear in help output", "[help][subcommands]")
{
    SubcommandFixture fixture;
    fixture.parser.formatter().format(HelpFormatter::Format::Plain);

    const std::string help = fixture.parser.format_help();
    CHECK_THAT(help, ContainsSubstring("subcommands"));
    CHECK_THAT(help, ContainsSubstring("run"));
    CHECK_THAT(help, ContainsSubstring("Run a task"));
    CHECK_THAT(help, ContainsSubstring("build"));
    CHECK_THAT(help, ContainsSubstring("Build artifacts"));
}

// -------------------------------------------------------------------------
// Namespace queries and conversions
// -------------------------------------------------------------------------

TEST_CASE("namespace queries distinguish registration from provision", "[namespace]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--value").default_value("42");
    parser.add_argument("--flag").action("store_true");
    parser.add_argument("--empty");

    const auto &ns = parse(parser, {"--flag"});

    CHECK(ns.has("value"));
    CHECK(ns.has("flag"));
    CHECK(ns.has("empty"));
    CHECK_FALSE(ns.has("not_registered"));

    CHECK(ns.provided("flag"));
    CHECK_FALSE(ns.provided("value"));
    CHECK_FALSE(ns.provided("empty"));

    CHECK(ns.raw_values("value") == std::vector<std::string>({"42"}));
    CHECK(ns.raw_values("empty").empty());
    CHECK(ns.get<std::string>("value") == "42");
}

TEST_CASE("namespace reports unknown and valueless arguments", "[namespace][errors]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--empty");

    const auto &ns = parse(parser, {});

    CHECK_THROWS_AS(ns.raw_values("missing"), ParseError);
    CHECK_THROWS_AS(ns.get<std::string>("missing"), ParseError);
    CHECK_THROWS_AS(ns.get<std::string>("empty"), ArgumentError);
    CHECK_THAT(thrown_message<ArgumentError>(
                   [&]
                   { (void)ns.get<std::string>("empty"); }),
               ContainsSubstring("argument has no value: empty"));
}

TEST_CASE("namespace converts strings to supported types", "[namespace][convert]")
{
    SECTION("boolean spellings")
    {
        struct BoolCase
        {
            const char *text;
            bool expected;
        };

        const std::vector<BoolCase> cases = {
            {"1", true}, {"true", true}, {"TRUE", true}, {"yes", true},
            {"On", true}, {"0", false}, {"false", false}, {"FALSE", false},
            {"no", false}, {"off", false}};

        for (const auto &test_case : cases)
        {
            INFO("input: " << test_case.text);
            ArgumentParser parser("tool");
            parser.add_argument("--value").default_value(test_case.text);
            const auto &ns = parse(parser, {});
            CHECK(ns.get<bool>("value") == test_case.expected);
        }
    }

    SECTION("invalid boolean")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--value").default_value("maybe");
        const auto &ns = parse(parser, {});
        CHECK_THROWS_AS(ns.get<bool>("value"), ArgumentError);
    }

    SECTION("integer and floating point")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--int").default_value("42");
        parser.add_argument("--negative").default_value("-7");
        parser.add_argument("--double").default_value("2.5");
        parser.add_argument("--float").default_value("1.25");

        const auto &ns = parse(parser, {});
        CHECK(ns.get<int>("int") == 42);
        CHECK(ns.get<long>("negative") == -7L);
        CHECK(ns.get<double>("double") == Catch::Approx(2.5));
        CHECK(ns.get<float>("float") == Catch::Approx(1.25f));
    }

    SECTION("lists")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--number").action("append");
        parser.add_argument("--word").action("append");

        const auto &ns =
            parse(parser, {"--number", "1", "--number", "2", "--word", "a"});

        CHECK(ns.getlist<int>("number") == std::vector<int>({1, 2}));
        CHECK(ns.getlist<std::string>("word") == std::vector<std::string>({"a"}));
    }

    SECTION("invalid numeric values")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--int").default_value("12abc");
        parser.add_argument("--double").default_value("1.2.3");

        const auto &ns = parse(parser, {});
        CHECK_THROWS_AS(ns.get<int>("int"), ArgumentError);
        CHECK_THROWS_AS(ns.get<double>("double"), ArgumentError);
    }

    SECTION("get returns only the first value")
    {
        ArgumentParser parser("tool");
        parser.add_argument("--values").nargs('*');
        const auto &ns = parse(parser, {"--values", "first", "second"});
        CHECK(ns.get<std::string>("values") == "first");
        CHECK(ns.getlist<std::string>("values") ==
              std::vector<std::string>({"first", "second"}));
    }
}

TEST_CASE("namespace copies and moves preserve values and subcommands", "[namespace]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--value").default_value("v");
    auto &run = parser.add_subcommand("run");
    run.add_argument("path");

    const auto &ns = parse(parser, {"run", "file.txt"});
    REQUIRE(ns.has_subcommand());

    ArgumentParser::Namespace copy = ns;
    CHECK(copy.get<std::string>("value") == "v");
    CHECK(copy.has_subcommand());
    CHECK(copy.subcommand() == "run");
    CHECK(copy.subcommand_namespace().get<std::string>("path") == "file.txt");

    ArgumentParser::Namespace assigned;
    assigned = copy;
    CHECK(assigned.subcommand_namespace().get<std::string>("path") == "file.txt");

    assigned = assigned; // self-assignment must be safe
    CHECK(assigned.subcommand() == "run");

    ArgumentParser::Namespace moved = std::move(copy);
    CHECK(moved.has_subcommand());
    CHECK(moved.subcommand_namespace().get<std::string>("path") == "file.txt");
}

// -------------------------------------------------------------------------
// Help formatting
// -------------------------------------------------------------------------

TEST_CASE("help output includes parser metadata and argument sections", "[help][format]")
{
    ArgumentParser parser("app", "Process files.", "See the manual.");
    parser.add_argument("input").help("input file");
    parser.add_argument("-v", "--verbose")
        .action("store_true")
        .help("verbose output");
    parser.add_argument("-n", "--number")
        .metavar("N")
        .default_value(1)
        .help("number of iterations");
    parser.add_argument("--secret").hidden().help("hidden switch");

    parser.formatter().format(HelpFormatter::Format::Plain);
    const std::string help = parser.format_help();

    CHECK_THAT(help, ContainsSubstring("usage: app"));
    CHECK_THAT(help, ContainsSubstring("Process files."));
    CHECK_THAT(help, ContainsSubstring("See the manual."));
    CHECK_THAT(help, ContainsSubstring("positional arguments"));
    CHECK_THAT(help, ContainsSubstring("input file"));
    CHECK_THAT(help, ContainsSubstring("optional arguments"));
    CHECK_THAT(help, ContainsSubstring("-v, --verbose"));
    CHECK_THAT(help, ContainsSubstring("verbose output"));
    CHECK_THAT(help, ContainsSubstring("N"));
    CHECK_THAT(help, ContainsSubstring("number of iterations"));
    CHECK_THAT(help, ContainsSubstring("[REQUIRED]"));
    CHECK_THAT(help, ContainsSubstring("--help"));
    CHECK(help.find("secret switch") == std::string::npos);
    CHECK(help.find("--secret") == std::string::npos);

    SECTION("custom usage is used verbatim")
    {
        parser.usage("app [options] FILE");
        CHECK_THAT(parser.format_help(),
                   ContainsSubstring("usage: app [options] FILE"));
    }

    SECTION("hidden arguments can be shown explicitly")
    {
        const std::string all = parser.format_help(true);
        CHECK_THAT(all, ContainsSubstring("--secret"));
        CHECK_THAT(all, ContainsSubstring("hidden switch"));
    }
}

TEST_CASE("help formatter sections, metadata, and visibility", "[help][formatter]")
{
    HelpFormatter formatter(60, "demo");
    formatter.program("demo")
        .usage("demo [options] FILE")
        .description("A small demonstration.")
        .epilog("Footer text.")
        .prefix_info("PREFIX")
        .suffix_info("SUFFIX")
        .line_separator('-', 10)
        .format(HelpFormatter::Format::Plain)
        .use_color(false);

    CHECK(formatter.program() == "demo");
    CHECK(formatter.usage() == "demo [options] FILE");
    CHECK(formatter.description() == "A small demonstration.");
    CHECK(formatter.epilog() == "Footer text.");
    CHECK(formatter.width() == 60);
    CHECK_FALSE(formatter.color_theme().enabled);

    auto &options = formatter.add_section("options", "Available options.");
    CHECK(&formatter.get_section("options") == &options);
    CHECK(options.title() == "options");
    CHECK(options.description() == "Available options.");
    CHECK(options.type() == HelpFormatter::Section::Type::Custom);
    CHECK(options.visible());

    options.add_item("-h", "", "show help", true)
        .add_text("Additional note.")
        .add_example("demo -h", "Display help.");

    auto &internal = formatter.add_section("internal");
    internal.add_item("--secret", "", "hidden option");
    internal.visible(false);

    const std::string help = formatter.format_help();

    CHECK_THAT(help, ContainsSubstring("PREFIX"));
    CHECK_THAT(help, ContainsSubstring("usage: demo [options] FILE"));
    CHECK_THAT(help, ContainsSubstring("A small demonstration."));
    CHECK_THAT(help, ContainsSubstring("options:"));
    CHECK_THAT(help, ContainsSubstring("-h [REQUIRED]"));
    CHECK_THAT(help, ContainsSubstring("show help"));
    CHECK_THAT(help, ContainsSubstring("Additional note."));
    CHECK_THAT(help, ContainsSubstring("$ demo -h"));
    CHECK_THAT(help, ContainsSubstring("Display help."));
    CHECK_THAT(help, ContainsSubstring("----------"));
    CHECK_THAT(help, ContainsSubstring("Footer text."));
    CHECK_THAT(help, ContainsSubstring("SUFFIX"));
    CHECK(help.find("internal") == std::string::npos);
    CHECK(help.find("hidden option") == std::string::npos);

    internal.visible(true);
    CHECK_THAT(formatter.format_help(), ContainsSubstring("hidden option"));
    CHECK_THAT(formatter.format_help(), ContainsSubstring("internal"));

    CHECK(formatter.format_usage() == "usage: demo ");
}

TEST_CASE("help formatter wraps text and renders colors", "[help][formatter]")
{
    SECTION("wrapping respects the configured width")
    {
        HelpFormatter formatter(24, "demo");
        formatter.format(HelpFormatter::Format::Plain);

        const std::string text =
            "one two three four five six seven eight nine ten";
        const std::string wrapped = formatter.wrap_text(text, 0);

        CHECK(wrapped.find('\n') != std::string::npos);
        std::istringstream stream(wrapped);
        std::string line;
        while (std::getline(stream, line))
        {
            CHECK(line.size() <= 24);
        }
    }

    SECTION("wrapping can be disabled")
    {
        HelpFormatter formatter(24, "demo");
        auto alignment = formatter.alignment();
        alignment.wrap_help_text = false;
        formatter.alignment(alignment);

        const std::string text = "one two three four five six seven";
        CHECK(formatter.wrap_text(text, 0) == text);
    }

    SECTION("ANSI colors and hyperlinks")
    {
        HelpFormatter formatter(40, "demo");
        formatter.format(HelpFormatter::Format::Colored).use_color(true);

        const std::string colored = formatter.colorize("hello", HelpFormatter::Color::Red);
        CHECK(colored.find("\033[31m") != std::string::npos);
        CHECK(colored.find("hello") != std::string::npos);
        CHECK(colored.find("\033[0m") != std::string::npos);

        const std::string link =
            formatter.hyperlink("docs", "https://example.com");
        CHECK(link.find("\033]8;;https://example.com") != std::string::npos);
        CHECK(link.find("docs") != std::string::npos);

        formatter.use_color(false);
        CHECK(formatter.colorize("hello", HelpFormatter::Color::Red) == "hello");
        CHECK(formatter.hyperlink("docs", "https://example.com") == "docs");

        formatter.use_color(true).format(HelpFormatter::Format::Plain);
        CHECK(formatter.colorize("hello", HelpFormatter::Color::Red) == "hello");
    }

    SECTION("predefined color themes")
    {
        const auto nord = HelpFormatter::ColorTheme::nord();
        CHECK(nord.usage_color == HelpFormatter::Color::Cyan);
        CHECK(nord.option_color == HelpFormatter::Color::Blue);

        const auto monokai = HelpFormatter::ColorTheme::monokai();
        CHECK(monokai.usage_color == HelpFormatter::Color::Magenta);
        CHECK(monokai.option_color == HelpFormatter::Color::Green);

        const auto dracula = HelpFormatter::ColorTheme::dracula();
        CHECK(dracula.usage_color == HelpFormatter::Color::Magenta);
        CHECK(dracula.option_color == HelpFormatter::Color::Cyan);
    }
}

TEST_CASE("help formatter table rendering", "[help][formatter]")
{
    HelpFormatter::Table table;
    table.add_row({"--foo", "first"});
    table.add_row({"-b", "second"});

    HelpFormatter::Alignment alignment;
    alignment.indent_size = 2;

    const std::string formatted = table.format(alignment);
    CHECK(formatted == "  --foo  first\n  -b     second\n");

    HelpFormatter::Table empty;
    CHECK(empty.format(alignment).empty());
}

TEST_CASE("print_help and print_usage write to streams", "[help][format]")
{
    ArgumentParser parser("tool", "description");
    parser.add_argument("--verbose").action("store_true");
    parser.formatter().format(HelpFormatter::Format::Plain);

    std::ostringstream help_stream;
    std::ostringstream usage_stream;

    parser.print_help(help_stream);
    parser.print_usage(usage_stream);

    CHECK(help_stream.str() == parser.format_help());
    CHECK(usage_stream.str() == parser.format_usage());
}

TEST_CASE("shell completion scripts", "[help][completion]")
{
    ArgumentParser parser("tool");
    parser.add_argument("-v", "--verbose").action("store_true");
    parser.add_subcommand("run", "Run a task");

    const std::string bash = parser.generate_completion_script("bash");
    CHECK_THAT(bash, ContainsSubstring("_tool_completion"));
    CHECK_THAT(bash, ContainsSubstring("--verbose"));
    CHECK_THAT(bash, ContainsSubstring("-v"));
    CHECK_THAT(bash, ContainsSubstring("run"));
    CHECK_THAT(bash, ContainsSubstring("complete -F _tool_completion tool"));

    CHECK(parser.generate_completion_script("zsh") ==
          "Unsupported shell: zsh");
}

// -------------------------------------------------------------------------
// Exceptions and parser state
// -------------------------------------------------------------------------

TEST_CASE("exception hierarchy and messages", "[errors]")
{
    STATIC_REQUIRE(std::is_base_of<std::runtime_error, ArgumentError>::value);
    STATIC_REQUIRE(std::is_base_of<ArgumentError, ParseError>::value);
    STATIC_REQUIRE(std::is_base_of<ArgumentError, UsageError>::value);
    STATIC_REQUIRE(std::is_base_of<ArgumentError, RequiredError>::value);
    STATIC_REQUIRE(std::is_base_of<ArgumentError, RequiredIfError>::value);
    STATIC_REQUIRE(std::is_base_of<ArgumentError, HelpError>::value);

    const ArgumentError error("message");
    CHECK(std::string(error.what()) == "message");

    const HelpError help;
    CHECK(std::string(help.what()) == "help requested");

    bool caught_as_base = false;
    try
    {
        throw ParseError("bad input");
    }
    catch (const ArgumentError &caught)
    {
        caught_as_base = true;
        CHECK(std::string(caught.what()) == "bad input");
    }
    CHECK(caught_as_base);
}

TEST_CASE("repeated parsing resets state", "[parse][state]")
{
    ArgumentParser parser("tool");
    parser.add_argument("--value");
    parser.add_argument("--tag").action("append");
    parser.add_argument("position").nargs('?').required(false);

    const auto &first = parse_known(
        parser, {"--value", "a", "--tag", "x", "extra", "--unknown"});

    CHECK(first.get<std::string>("value") == "a");
    CHECK(first.getlist<std::string>("tag") == std::vector<std::string>({"x"}));
    CHECK(first.get<std::string>("position") == "extra");
    CHECK(parser.unknown_args() == std::vector<std::string>({"--unknown"}));
    CHECK(first.provided("tag"));

    const auto &second = parse(parser, {"--value", "b"});

    CHECK(second.get<std::string>("value") == "b");
    CHECK(second.raw_values("tag").empty());
    CHECK_FALSE(second.provided("tag"));
    CHECK(second.raw_values("position").empty());
    CHECK_FALSE(second.provided("position"));
    CHECK(parser.unknown_args().empty());
}
