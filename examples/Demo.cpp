#include "argparse.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    argparse::ArgumentParser parser(
        "argparse-demo",
        "Demonstrates positional arguments, options, validation, and subcommands.",
        "Documentation: docs/README.md");

    parser.formatter()
        .format(argparse::HelpFormatter::Format::Plain)
        .width(88)
        .use_color(false);

    parser.add_argument("-v", "--verbose")
        .action("count")
        .help("Increase verbosity; repeat the flag for more detail.");

    parser.add_argument("--format")
        .choices({"text", "json"})
        .default_value("text")
        .help("Output format.");

    parser.add_argument("--color")
        .nargs('?')
        .implicit_value("auto")
        .default_value("never")
        .choices({"never", "auto", "always"})
        .help("Color mode; use --color for auto.");

    auto &run = parser.add_subcommand("run", "Run a job");
    run.add_argument("path").help("Input file to process.");
    run.add_argument("-j", "--jobs")
        .default_value(1)
        .check(argparse::BuiltinValidators::in_range(1, 64),
               "jobs must be between 1 and 64")
        .help("Number of worker threads.");
    run.add_argument("--tag")
        .action("append")
        .help("Attach a tag; may be repeated.");

    auto &check = parser.add_subcommand("check", "Validate a file");
    check.add_argument("path").help("File to validate.");

    auto &examples = parser.formatter().add_section(
        "examples", "", argparse::HelpFormatter::Section::Type::Examples);
    examples.add_example("argparse-demo run data.txt --jobs 4",
                         "Run a job with four workers.");
    examples.add_example("argparse-demo check data.txt",
                         "Validate a file without processing it.");

    try
    {
        const auto &ns = parser.parse_args(argc, argv);

        if (!ns.has_subcommand())
        {
            parser.print_help();
            return 0;
        }

        const int verbosity = ns.get<int>("verbose");
        const std::string format = ns.get<std::string>("format");
        const std::string color = ns.get<std::string>("color");

        std::cout << "verbosity: " << verbosity << '\n'
                  << "format:    " << format << '\n'
                  << "color:     " << color << '\n';

        const auto &child = ns.subcommand_namespace();
        if (ns.subcommand() == "run")
        {
            std::cout << "path:      " << child.get<std::string>("path") << '\n'
                      << "jobs:      " << child.get<int>("jobs") << '\n';

            for (const auto &tag : child.getlist<std::string>("tag"))
            {
                std::cout << "tag:       " << tag << '\n';
            }
        }
        else if (ns.subcommand() == "check")
        {
            std::cout << "checking:  " << child.get<std::string>("path") << '\n';
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
    catch (const std::exception &error)
    {
        std::cerr << "unexpected error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
