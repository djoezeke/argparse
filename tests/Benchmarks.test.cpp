#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "argparse.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

using argparse::ArgumentParser;

namespace
{
    class Argv
    {
    public:
        explicit Argv(std::initializer_list<std::string> args)
        {
            m_Args.emplace_back("bench");
            m_Args.insert(m_Args.end(), args.begin(), args.end());
            rebuild();
        }

        explicit Argv(std::vector<std::string> args)
            : m_Args(std::move(args))
        {
            if (m_Args.empty())
            {
                m_Args.emplace_back("bench");
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

    std::vector<std::string> make_args(std::initializer_list<std::string> args)
    {
        std::vector<std::string> result;
        result.emplace_back("bench");
        result.insert(result.end(), args.begin(), args.end());
        return result;
    }

    ArgumentParser make_parser_with_options(std::size_t count)
    {
        ArgumentParser parser("bench", "Benchmark parser", "Generated options.");
        for (std::size_t index = 0; index < count; ++index)
        {
            parser.add_argument("--option-" + std::to_string(index))
                .help("Generated option number " + std::to_string(index))
                .default_value(static_cast<int>(index));
        }
        parser.add_argument("-v", "--verbose")
            .action("store_true")
            .help("Enable verbose output");
        return parser;
    }
} // namespace

TEST_CASE("benchmark: parser construction", "[!benchmark][benchmark][construction]")
{
    BENCHMARK("construct parser with 20 options and help text")
    {
        ArgumentParser parser("bench", "Benchmark parser", "Epilog.");
        for (int index = 0; index < 20; ++index)
        {
            parser.add_argument("--option-" + std::to_string(index))
                .help("Generated option number " + std::to_string(index))
                .default_value(index);
        }
        return parser.format_usage().size();
    };

    BENCHMARK("construct parser with 100 options and help text")
    {
        ArgumentParser parser("bench", "Benchmark parser", "Epilog.");
        for (int index = 0; index < 100; ++index)
        {
            parser.add_argument("--option-" + std::to_string(index))
                .help("Generated option number " + std::to_string(index))
                .default_value(index);
        }
        return parser.format_usage().size();
    };
}

TEST_CASE("benchmark: boolean and value parsing", "[!benchmark][benchmark][parse]")
{
    SECTION("five boolean flags")
    {
        ArgumentParser parser("bench");
        parser.add_argument("-a").action("store_true");
        parser.add_argument("-b").action("store_true");
        parser.add_argument("-c").action("store_true");
        parser.add_argument("-d").action("store_true");
        parser.add_argument("-e").action("store_true");
        Argv argv({"-a", "-b", "-c", "-d", "-e"});

        BENCHMARK("parse five boolean flags")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<bool>("a") && ns.get<bool>("b") &&
                   ns.get<bool>("c") && ns.get<bool>("d") &&
                   ns.get<bool>("e");
        };
    }

    SECTION("three long options with values")
    {
        ArgumentParser parser("bench");
        parser.add_argument("--input");
        parser.add_argument("--output");
        parser.add_argument("--level").default_value("info");
        Argv argv({"--input", "source.txt", "--output", "result.txt",
                   "--level", "debug"});

        BENCHMARK("parse three long options with values")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.raw_values("input").front().size() +
                   ns.raw_values("output").front().size() +
                   ns.raw_values("level").front().size();
        };
    }

    SECTION("short cluster with attached value")
    {
        ArgumentParser parser("bench");
        parser.add_argument("-v", "--verbose").action("store_true");
        parser.add_argument("-o", "--output");
        Argv argv({"-voresult.txt"});

        BENCHMARK("parse short cluster with attached value")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<bool>("verbose") &&
                   ns.get<std::string>("output") == "result.txt";
        };
    }

    SECTION("negative numeric value")
    {
        ArgumentParser parser("bench");
        parser.add_argument("--offset");
        Argv argv({"--offset", "-1234"});

        BENCHMARK("parse negative numeric option value")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<int>("offset");
        };
    }
}

TEST_CASE("benchmark: positional and high-cardinality parsing", "[!benchmark][benchmark][parse]")
{
    SECTION("three positionals and 48 variadic files")
    {
        ArgumentParser parser("bench");
        parser.add_argument("input");
        parser.add_argument("output");
        parser.add_argument("files").nargs('*').required(false);

        std::vector<std::string> args = make_args({"in.txt", "out.txt"});
        for (int index = 0; index < 48; ++index)
        {
            args.push_back("file-" + std::to_string(index) + ".txt");
        }
        Argv argv(std::move(args));

        BENCHMARK("parse 3 positionals with 48 variadic files")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<std::string>("input").size() +
                   ns.get<std::string>("output").size() +
                   ns.raw_values("files").size();
        };
    }

    SECTION("100 append occurrences")
    {
        ArgumentParser parser("bench");
        parser.add_argument("--tag").action("append");

        std::vector<std::string> args = make_args({});
        for (int index = 0; index < 100; ++index)
        {
            args.push_back("--tag");
            args.push_back("tag-" + std::to_string(index));
        }
        Argv argv(std::move(args));

        BENCHMARK("parse 100 append occurrences")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.getlist<std::string>("tag").size();
        };
    }

    SECTION("100-character count cluster")
    {
        ArgumentParser parser("bench");
        parser.add_argument("-v", "--verbose").action("count");

        std::string cluster(1, '-');
        cluster.append(100, 'v');
        Argv argv({cluster});

        BENCHMARK("parse 100-character count cluster")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<int>("verbose");
        };
    }

    SECTION("realistic mixed command line")
    {
        ArgumentParser parser("deploy", "Deploy an application", "Examples...");
        parser.add_argument("service").help("service name");
        parser.add_argument("-v", "--verbose").action("store_true");
        parser.add_argument("--threads").default_value(4);
        parser.add_argument("--config").default_value("deploy.toml");
        parser.add_argument("--tag").action("append");
        parser.add_argument("--mode")
            .choices({"fast", "safe"})
            .default_value("safe");

        Argv argv({"--verbose", "--threads", "8", "--config", "prod.toml",
                   "--tag", "blue", "--tag", "stable", "--mode", "fast",
                   "api"});

        BENCHMARK("parse realistic mixed command line")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<std::string>("service").size() +
                   static_cast<std::size_t>(ns.get<int>("threads")) +
                   ns.raw_values("tag").size() +
                   static_cast<std::size_t>(ns.get<bool>("verbose"));
        };
    }
}

TEST_CASE("benchmark: defaults and validation", "[!benchmark][benchmark][validation]")
{
    SECTION("20 defaulted options, no arguments")
    {
        ArgumentParser parser = make_parser_with_options(20);
        Argv argv({});

        BENCHMARK("finalize 20 defaulted options")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.has("option-19");
        };
    }

    SECTION("choices validation")
    {
        ArgumentParser parser("bench");
        parser.add_argument("--mode")
            .choices({"fast", "safe", "balanced"})
            .default_value("safe");
        Argv argv({"--mode", "fast"});

        BENCHMARK("validate a choice value")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<std::string>("mode");
        };
    }

    SECTION("custom validator")
    {
        ArgumentParser parser("bench");
        parser.add_argument("--port")
            .check([](const std::string &value)
                   {
                       const int port = std::stoi(value);
                       return port > 0 && port <= 65535;
                   },
                   "port out of range");
        Argv argv({"--port", "8080"});

        BENCHMARK("run a custom validator")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.get<int>("port");
        };
    }
}

TEST_CASE("benchmark: subcommands and known-arg parsing", "[!benchmark][benchmark][subcommands]")
{
    SECTION("nested subcommand dispatch")
    {
        ArgumentParser parser("git");
        auto &remote = parser.add_subcommand("remote", "Manage remotes");
        auto &add = remote.add_subcommand("add", "Add a remote");
        add.add_argument("name");
        add.add_argument("url");

        Argv argv({"remote", "add", "origin", "https://example.com/repo.git"});

        BENCHMARK("parse nested subcommand")
        {
            const auto &ns = parser.parse_args(argv.argc(), argv.argv());
            return ns.subcommand_namespace()
                       .subcommand_namespace()
                       .get<std::string>("url")
                       .size();
        };
    }

    SECTION("parse_known_args with unknowns")
    {
        ArgumentParser parser("bench");
        parser.add_argument("-v", "--verbose").action("store_true");
        Argv argv({"--verbose", "--unknown", "--other=1"});

        BENCHMARK("parse_known_args with two unknown options")
        {
            const auto &ns = parser.parse_known_args(argv.argc(), argv.argv());
            return ns.get<bool>("verbose") && parser.unknown_args().size() == 2;
        };
    }
}

TEST_CASE("benchmark: namespace conversions", "[!benchmark][benchmark][namespace]")
{
    ArgumentParser parser("bench");
    parser.add_argument("--numbers").nargs('*');

    std::vector<std::string> args = make_args({"--numbers"});
    for (int index = 0; index < 100; ++index)
    {
        args.push_back(std::to_string(index));
    }
    Argv argv(std::move(args));

    const auto &ns = parser.parse_args(argv.argc(), argv.argv());
    REQUIRE(ns.raw_values("numbers").size() == 100);

    BENCHMARK("convert 100 strings to ints with getlist")
    {
        std::vector<int> values = ns.getlist<int>("numbers");
        return values.front() + values.back();
    };
}

TEST_CASE("benchmark: help generation", "[!benchmark][benchmark][help]")
{
    ArgumentParser parser = make_parser_with_options(20);
    parser.add_argument("input").help("Input file");

    BENCHMARK("format_help with 20 options (colored)")
    {
        return parser.format_help().size();
    };

    parser.formatter().format(argparse::HelpFormatter::Format::Plain);

    BENCHMARK("format_help with 20 options (plain)")
    {
        return parser.format_help().size();
    };

    BENCHMARK("format_usage")
    {
        return parser.format_usage().size();
    };
}
