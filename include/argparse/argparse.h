/**
 * @file argparse.h
 * @brief Modern Argument Parser for C/C++ (single header, C++17).
 * @author Sackey Ezekiel Etrue (djoezeke)
 * @version 0.2.0
 * @see https://www.github.com/djoezeke/argparse
 * @copyright Copyright (c) 2025 Sackey Ezekiel Etrue
 *
 * Distributed under the MIT License. See the end of this file.
 *
 * Design goals:
 *  - Real header-only support: every definition is inline, so the header may be
 *    included from any number of translation units.
 *  - Stable public references: arguments, groups, and help sections live in
 *    std::deque, so references returned by the registration API never dangle.
 *  - One predictable value model: CLI > environment > default > action default.
 *    Implicit values are applied only when an option occurs without a value.
 *  - No silent failure: invalid registrations and unsupported combinations are
 *    rejected immediately.
 */

#ifndef DJOEZEKE_ARGPARSE_H
#define DJOEZEKE_ARGPARSE_H

// clang-format off

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
    #define _CRT_SECURE_NO_WARNINGS
#endif

#ifdef ARGPARSE_NO_EXCEPTIONS
    #error "ARGPARSE_NO_EXCEPTIONS is not supported in argparse 0.2.0. Use ArgumentParser::try_parse_args() for a non-throwing parsing API instead."
#endif

/** @brief Major version number. */
#ifndef ARGPARSE_VERSION_MAJOR
    #define ARGPARSE_VERSION_MAJOR 0
#endif
/** @brief Minor version number. */
#ifndef ARGPARSE_VERSION_MINOR
    #define ARGPARSE_VERSION_MINOR 2
#endif
/** @brief Patch version number. */
#ifndef ARGPARSE_VERSION_PATCH
    #define ARGPARSE_VERSION_PATCH 0
#endif
/** @brief Library version string in the form "X.Y.Z". */
#ifndef ARGPARSE_VERSION
    #define ARGPARSE_VERSION "0.2.0"
#endif

// clang-format on

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <io.h>
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

namespace argparse
{

    //-----------------------------------------------------------------------------
    // [SECTION] Error model
    //-----------------------------------------------------------------------------

    /**
     * @enum ErrorCode
     * @brief Stable classification for parse and registration failures.
     */
    enum class ErrorCode
    {
        None = 0,
        Usage,
        Parse,
        UnknownOption,
        UnknownPositional,
        UnknownSubcommand,
        MissingValue,
        MissingRequired,
        InvalidChoice,
        InvalidValue,
        MutuallyExclusive,
        DependencyMissing,
        InvalidRegistration,
        HelpRequested,
        Conversion,
        Io
    };

    /**
     * @class ArgumentError
     * @brief Base exception for argument parsing failures.
     */
    class ArgumentError : public std::runtime_error
    {
    public:
        explicit ArgumentError(const std::string &message,
                               ErrorCode code = ErrorCode::Usage)
            : std::runtime_error(message), m_Code(code) {}

        /** @brief Returns the stable error classification. */
        ErrorCode code() const noexcept { return m_Code; }

    private:
        ErrorCode m_Code;
    };

    /** @brief Exception thrown when parsing fails. */
    class ParseError : public ArgumentError
    {
    public:
        explicit ParseError(const std::string &message,
                            ErrorCode code = ErrorCode::Parse)
            : ArgumentError(message, code) {}
    };

    /** @brief Exception thrown when the parser is used incorrectly. */
    class UsageError : public ArgumentError
    {
    public:
        explicit UsageError(const std::string &message)
            : ArgumentError(message, ErrorCode::Usage) {}
    };

    /** @brief Exception thrown when a required argument or value is missing. */
    class RequiredError : public ArgumentError
    {
    public:
        explicit RequiredError(const std::string &message,
                               ErrorCode code = ErrorCode::MissingRequired)
            : ArgumentError(message, code) {}
    };

    /** @brief Exception thrown when a dependency is missing. */
    class RequiredIfError : public ArgumentError
    {
    public:
        explicit RequiredIfError(const std::string &message)
            : ArgumentError(message, ErrorCode::DependencyMissing) {}
    };

    /** @brief Exception thrown when a subcommand token is not registered. */
    class UnknownSubcommandError : public ParseError
    {
    public:
        explicit UnknownSubcommandError(const std::string &token,
                                       const std::string &message)
            : ParseError(message, ErrorCode::UnknownSubcommand), m_Token(token) {}

        /** @brief Returns the unrecognized subcommand token. */
        const std::string &token() const noexcept { return m_Token; }

    private:
        std::string m_Token;
    };

    /**
     * @class HelpError
     * @brief Signal exception raised by the built-in help actions.
     */
    class HelpError : public ArgumentError
    {
    public:
        explicit HelpError(bool show_hidden = false)
            : ArgumentError("help requested", ErrorCode::HelpRequested),
              m_ShowHidden(show_hidden) {}

        /** @brief True when --help-all (hidden arguments included) was used. */
        bool show_hidden() const noexcept { return m_ShowHidden; }

    private:
        bool m_ShowHidden;
    };

    /** @brief Returns a stable, human-readable name for an error code. */
    inline const char *error_code_name(ErrorCode code) noexcept
    {
        switch (code)
        {
        case ErrorCode::None: return "none";
        case ErrorCode::Usage: return "usage";
        case ErrorCode::Parse: return "parse";
        case ErrorCode::UnknownOption: return "unknown-option";
        case ErrorCode::UnknownPositional: return "unknown-positional";
        case ErrorCode::UnknownSubcommand: return "unknown-subcommand";
        case ErrorCode::MissingValue: return "missing-value";
        case ErrorCode::MissingRequired: return "missing-required";
        case ErrorCode::InvalidChoice: return "invalid-choice";
        case ErrorCode::InvalidValue: return "invalid-value";
        case ErrorCode::MutuallyExclusive: return "mutually-exclusive";
        case ErrorCode::DependencyMissing: return "dependency-missing";
        case ErrorCode::InvalidRegistration: return "invalid-registration";
        case ErrorCode::HelpRequested: return "help-requested";
        case ErrorCode::Conversion: return "conversion";
        case ErrorCode::Io: return "io";
        }
        return "unknown";
    }

    //-----------------------------------------------------------------------------
    // [SECTION] Implementation details
    //-----------------------------------------------------------------------------

    namespace detail
    {

        template <typename T>
        struct is_optional : std::false_type
        {
        };

        template <typename T>
        struct is_optional<std::optional<T>> : std::true_type
        {
        };

        inline std::string strip_plus(std::string_view text)
        {
            if (text.size() > 1 && text.front() == '+')
            {
                text.remove_prefix(1);
            }
            return std::string(text);
        }

        template <typename T>
        bool parse_integral(std::string_view text, T &out)
        {
            if (text.empty())
            {
                return false;
            }

            if (text.front() == '+')
            {
                text.remove_prefix(1);
                if (text.empty())
                {
                    return false;
                }
            }

            if constexpr (std::is_signed<T>::value)
            {
                long long value = 0;
                const auto first = text.data();
                const auto last = text.data() + text.size();
                const auto result = std::from_chars(first, last, value);
                if (result.ec != std::errc() || result.ptr != last)
                {
                    return false;
                }
                if (value < static_cast<long long>(std::numeric_limits<T>::min()) ||
                    value > static_cast<long long>(std::numeric_limits<T>::max()))
                {
                    return false;
                }
                out = static_cast<T>(value);
                return true;
            }
            else
            {
                unsigned long long value = 0;
                const auto first = text.data();
                const auto last = text.data() + text.size();
                const auto result = std::from_chars(first, last, value);
                if (result.ec != std::errc() || result.ptr != last)
                {
                    return false;
                }
                if (value > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
                {
                    return false;
                }
                out = static_cast<T>(value);
                return true;
            }
        }

        template <typename T>
        bool parse_floating(std::string_view text, T &out)
        {
            if (text.empty())
            {
                return false;
            }

            if (text.front() == '+')
            {
                text.remove_prefix(1);
                if (text.empty())
                {
                    return false;
                }
            }

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
            T value{};
            const auto first = text.data();
            const auto last = text.data() + text.size();
            const auto result = std::from_chars(first, last, value);
            if (result.ec != std::errc() || result.ptr != last)
            {
                return false;
            }
            out = value;
            return true;
#else
            std::istringstream stream(std::string(text));
            stream.imbue(std::locale::classic());
            T value{};
            stream >> value;
            if (stream.fail() || !stream.eof())
            {
                return false;
            }
            out = value;
            return true;
#endif
        }

        template <typename T>
        bool parse_number(std::string_view text, T &out)
        {
            if constexpr (std::is_floating_point<T>::value)
            {
                return parse_floating(text, out);
            }
            else
            {
                return parse_integral(text, out);
            }
        }

        inline std::string to_lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        inline std::string to_upper(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::toupper(c)); });
            return text;
        }

        template <typename T>
        std::string to_string_value(const T &value)
        {
            using D = typename std::decay<T>::type;
            if constexpr (std::is_same<D, std::string>::value)
            {
                return value;
            }
            else if constexpr (std::is_same<D, const char *>::value ||
                               std::is_same<D, char *>::value)
            {
                return value == nullptr ? std::string() : std::string(value);
            }
            else if constexpr (std::is_same<D, bool>::value)
            {
                return value ? "true" : "false";
            }
            else if constexpr (std::is_same<D, char>::value)
            {
                return std::string(1, value);
            }
            else
            {
                std::ostringstream stream;
                stream.imbue(std::locale::classic());
                stream << value;
                return stream.str();
            }
        }

        inline bool is_negative_number_token(std::string_view token)
        {
            if (token.size() < 2 || token.front() != '-')
            {
                return false;
            }

            std::size_t index = 1;
            bool has_digit = false;

            while (index < token.size() &&
                   std::isdigit(static_cast<unsigned char>(token[index])))
            {
                has_digit = true;
                ++index;
            }

            if (index < token.size() && token[index] == '.')
            {
                ++index;
                while (index < token.size() &&
                       std::isdigit(static_cast<unsigned char>(token[index])))
                {
                    has_digit = true;
                    ++index;
                }
            }

            if (!has_digit)
            {
                return false;
            }

            if (index < token.size() && (token[index] == 'e' || token[index] == 'E'))
            {
                ++index;
                if (index < token.size() && (token[index] == '+' || token[index] == '-'))
                {
                    ++index;
                }
                bool has_exponent_digit = false;
                while (index < token.size() &&
                       std::isdigit(static_cast<unsigned char>(token[index])))
                {
                    has_exponent_digit = true;
                    ++index;
                }
                if (!has_exponent_digit)
                {
                    return false;
                }
            }

            return index == token.size();
        }

        inline std::size_t levenshtein_distance(std::string_view lhs,
                                                std::string_view rhs)
        {
            const std::size_t m = lhs.size();
            const std::size_t n = rhs.size();
            if (m == 0)
            {
                return n;
            }
            if (n == 0)
            {
                return m;
            }

            std::vector<std::size_t> costs(n + 1);
            for (std::size_t k = 0; k <= n; ++k)
            {
                costs[k] = k;
            }

            std::size_t i = 0;
            for (char c1 : lhs)
            {
                costs[0] = i + 1;
                std::size_t corner = i;
                std::size_t j = 0;
                for (char c2 : rhs)
                {
                    const std::size_t upper = costs[j + 1];
                    if (c1 == c2)
                    {
                        costs[j + 1] = corner;
                    }
                    else
                    {
                        const std::size_t t = corner < upper ? corner : upper;
                        costs[j + 1] = (t < costs[j] ? t : costs[j]) + 1;
                    }
                    corner = upper;
                    ++j;
                }
                ++i;
            }
            return costs[n];
        }

        inline bool has_ansi_escape(std::string_view text)
        {
            return text.find('\033') != std::string_view::npos;
        }

        inline std::size_t visible_length(std::string_view text)
        {
            std::size_t length = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '[')
                {
                    i += 2;
                    while (i < text.size())
                    {
                        const char c = text[i];
                        if (c >= '@' && c <= '~')
                        {
                            break;
                        }
                        ++i;
                    }
                    continue;
                }
                if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == ']')
                {
                    i += 2;
                    while (i + 1 < text.size() &&
                           !(text[i] == '\033' && text[i + 1] == '\\'))
                    {
                        ++i;
                    }
                    ++i;
                    continue;
                }
                ++length;
            }
            return length;
        }

    } // namespace detail

    //-----------------------------------------------------------------------------
    // [SECTION] Nargs
    //-----------------------------------------------------------------------------

    /**
     * @class Nargs
     * @brief Strong value type describing how many values an argument accepts.
     *
     * Using a named type keeps `.nargs(2)` unambiguous and makes call sites
     * self-documenting:
     *
     * @code
     * parser.add_argument("--pair").nargs(Nargs::exactly(2));
     * parser.add_argument("files").nargs(Nargs::zero_or_more());
     * @endcode
     */
    class Nargs
    {
    public:
        constexpr Nargs() : m_Min(1), m_Max(1) {}

        static constexpr Nargs exactly(std::size_t count)
        {
            return Nargs(count, count);
        }
        static constexpr Nargs range(std::size_t minimum, std::size_t maximum)
        {
            return Nargs(minimum, maximum < minimum ? minimum : maximum);
        }
        static constexpr Nargs optional() { return Nargs(0, 1); }
        static constexpr Nargs zero_or_more()
        {
            return Nargs(0, (std::numeric_limits<std::size_t>::max)());
        }
        static constexpr Nargs one_or_more()
        {
            return Nargs(1, (std::numeric_limits<std::size_t>::max)());
        }
        static constexpr Nargs none() { return Nargs(0, 0); }

        constexpr std::size_t min() const noexcept { return m_Min; }
        constexpr std::size_t max() const noexcept { return m_Max; }
        constexpr bool unbounded() const noexcept
        {
            return m_Max == (std::numeric_limits<std::size_t>::max)();
        }

        friend constexpr bool operator==(Nargs lhs, Nargs rhs) noexcept
        {
            return lhs.m_Min == rhs.m_Min && lhs.m_Max == rhs.m_Max;
        }
        friend constexpr bool operator!=(Nargs lhs, Nargs rhs) noexcept
        {
            return !(lhs == rhs);
        }

    private:
        constexpr Nargs(std::size_t minimum, std::size_t maximum)
            : m_Min(minimum), m_Max(maximum) {}

        std::size_t m_Min;
        std::size_t m_Max;
    };

    //-----------------------------------------------------------------------------
    // [SECTION] Built-in validators
    //-----------------------------------------------------------------------------

    /**
     * @struct RangeValidator
     * @brief Type-aware numeric range validator.
     */
    template <typename T>
    struct RangeValidator
    {
        T minimum;
        T maximum;

        bool operator()(const std::string &value) const
        {
            T parsed{};
            if (!detail::parse_number<T>(value, parsed))
            {
                return false;
            }
            return parsed >= minimum && parsed <= maximum;
        }
    };

    /** @brief Ready-made validators for common CLI values. */
    struct BuiltinValidators
    {
        /** @brief True when the path names an existing regular file. */
        static bool is_file(const std::string &path)
        {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error) && !error;
        }

        /** @brief True when the path names an existing directory. */
        static bool is_dir(const std::string &path)
        {
            std::error_code error;
            return std::filesystem::is_directory(path, error) && !error;
        }

        /** @brief True when the path exists (file, directory, or other). */
        static bool exists(const std::string &path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && !error;
        }

        /**
         * @brief Returns a type-aware inclusive range validator.
         * @details Integral ranges reject fractional input instead of
         * truncating it, and conversions are locale-independent.
         */
        template <typename T>
        static RangeValidator<T> in_range(T minimum, T maximum)
        {
            return RangeValidator<T>{minimum, maximum};
        }

        /** @brief Accepts only the listed strings. */
        static std::function<bool(const std::string &)> one_of(
            std::initializer_list<std::string> values)
        {
            std::vector<std::string> allowed(values);
            return [allowed](const std::string &value)
            {
                return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
            };
        }

        /** @brief Accepts strings with a non-zero length. */
        static bool not_empty(const std::string &value)
        {
            return !value.empty();
        }
    };

    //-----------------------------------------------------------------------------
    // [SECTION] HelpFormatter
    //-----------------------------------------------------------------------------

    /**
     * @class HelpFormatter
     * @brief Help renderer with themes, sections, tables, and multiple formats.
     */
    class HelpFormatter
    {
    public:
        /** @brief ANSI color and text-style codes. */
        enum class Color
        {
            Reset = 0,
            Bold = 1,
            Dim = 2,
            Italic = 3,
            Underline = 4,
            Blink = 5,
            Reverse = 7,
            Hidden = 8,
            Black = 30,
            Red = 31,
            Green = 32,
            Yellow = 33,
            Blue = 34,
            Magenta = 35,
            Cyan = 36,
            White = 37,
            Default = 39,
            BgBlack = 40,
            BgRed = 41,
            BgGreen = 42,
            BgYellow = 43,
            BgBlue = 44,
            BgMagenta = 45,
            BgCyan = 46,
            BgWhite = 47,
            BgDefault = 49
        };

        /** @brief Supported help output formats. */
        enum class Format
        {
            Plain,    ///< Plain text without colors.
            Colored,  ///< Text with ANSI colors.
            Markdown, ///< Markdown document.
            Compact   ///< Single-line rows without wrapping.
        };

        /** @brief Color scheme for help messages. */
        struct ColorTheme
        {
            bool enabled = true;
            Color usage_color = Color::Green;
            Color option_color = Color::Blue;
            Color warnings_color = Color::Red;
            Color required_color = Color::Red;
            Color heading_color = Color::Bold;
            Color section_color = Color::Cyan;
            Color notes_color = Color::Magenta;
            Color examples_color = Color::Green;
            Color metavar_color = Color::Yellow;
            Color description_color = Color::Default;

            static ColorTheme nord();
            static ColorTheme monokai();
            static ColorTheme dracula();
        };

        /** @brief Layout and wrapping configuration. */
        struct Alignment
        {
            std::size_t indent_size = 2;
            char indent_char = ' ';
            std::size_t option_width = 20;
            bool wrap_help_text = true;
            std::size_t max_help_position = 24;
            std::size_t min_description_width = 20;
        };

        /** @brief A logical block of help content. */
        class Section
        {
        public:
            /** @brief Section classification. */
            enum class Type
            {
                Notes,
                Custom,
                Examples,
                Warnings,
                Arguments,
            };

            Section(const std::string &title = "",
                    const std::string &description = "",
                    Type type = Type::Custom);

            Section &add_item(const std::string &option,
                              const std::string &metavar,
                              const std::string &help,
                              bool required = false);
            Section &add_text(const std::string &text);
            Section &add_example(const std::string &command,
                                 const std::string &description);
            bool visible();
            Section &visible(bool visible);
            const std::string &title() const;
            const std::string &description() const;
            Type type() const;
            bool visible() const;

        private:
            friend class HelpFormatter;

            struct Item
            {
                std::string option;
                std::string metavar;
                std::string help;
                bool required;
            };

            struct Example
            {
                std::string command;
                std::string description;
            };

            std::string m_Title;
            std::string m_Description;
            Type m_Type;
            bool m_Visible = true;
            std::vector<Item> m_Items;
            std::vector<std::string> m_Texts;
            std::vector<Example> m_Examples;
        };

        /** @brief Utility for rendering aligned tabular data. */
        class Table
        {
        public:
            void add_row(std::vector<std::string> columns);
            std::string format(const Alignment &align) const;

        private:
            std::vector<std::vector<std::string>> m_Rows;
        };

    public:
        /**
         * @brief Constructs a formatter.
         * @param width Output width. Zero selects the detected terminal width.
         * @param prog_name Program name used in usage text.
         */
        explicit HelpFormatter(std::size_t width = 0,
                               const std::string &prog_name = "");

        HelpFormatter &program(const std::string &name);
        HelpFormatter &usage(const std::string &usage);
        HelpFormatter &epilog(const std::string &epilog);
        HelpFormatter &description(const std::string &description);
        HelpFormatter &prefix_info(const std::string &info);
        HelpFormatter &suffix_info(const std::string &info);
        HelpFormatter &width(std::size_t width);
        HelpFormatter &format(Format format);
        HelpFormatter &alignment(const Alignment &align);
        HelpFormatter &use_color(bool enable);
        HelpFormatter &color_theme(const ColorTheme &theme);
        HelpFormatter &line_separator(const std::string &separator);
        HelpFormatter &line_separator(char sep = '-', int width = 80);
        HelpFormatter &sort_sections(bool sort);

        Section &add_section(const std::string &title,
                             const std::string &description = "",
                             Section::Type type = Section::Type::Custom);
        Section &get_section(const std::string &title,
                             const std::string &description = "",
                             Section::Type type = Section::Type::Custom);

        const std::string &program() const;
        std::size_t width() const;
        ColorTheme &color_theme();
        const ColorTheme &color_theme() const;
        Alignment &alignment();
        const Alignment &alignment() const;
        const std::string &usage() const;
        const std::string &epilog() const;
        const std::string &description() const;

        std::string format_help() const;
        std::string format_usage() const;
        std::string format_section(const Section &section) const;
        std::string wrap_text(const std::string &text,
                              std::size_t indent_level = 0) const;
        std::string colorize(const std::string &text, Color color) const;
        std::string hyperlink(const std::string &text,
                              const std::string &url) const;

        /** @brief Returns the printable length of text, ignoring ANSI escapes. */
        static std::size_t visible_length(const std::string &text);

        /** @brief Detects whether the standard output supports ANSI colors. */
        static bool supports_color();

    private:
        static std::size_t get_terminal_width();

        Color get_color_for_format(Color default_color) const;
        std::string format_colored(const std::string &text, Color color) const;
        std::string wrap_indented(const std::string &text,
                                  std::size_t first_indent,
                                  std::size_t subsequent_indent) const;
        std::string wrap_words(const std::vector<std::string> &words,
                               std::size_t first_indent,
                               std::size_t subsequent_indent) const;
        std::string format_item(const Section::Item &item) const;
        std::string format_example(const Section::Example &example) const;
        std::string format_help_markdown() const;
        std::string format_section_markdown(const Section &section) const;

    private:
        std::size_t m_Width;
        std::string m_Program;
        std::string m_Usage;
        std::string m_Epilog;
        std::string m_Description;
        std::string m_PrefixInfo;
        std::string m_SuffixInfo;
        std::string m_LineSeparator;
        bool m_SortSections = false;
        std::deque<Section> m_Sections;
        ColorTheme m_Theme;
        Alignment m_Alignment;
        Format m_Format;
    };

    //-----------------------------------------------------------------------------
    // [SECTION] ArgumentParser
    //-----------------------------------------------------------------------------

    /**
     * @class ArgumentParser
     * @brief Python-style command line parser for C++17.
     */
    class ArgumentParser
    {
    public:
        /**
         * @class Namespace
         * @brief Parsed values with typed and raw access.
         */
        class Namespace
        {
            friend class ArgumentParser;

        public:
            Namespace() = default;
            Namespace(const Namespace &other);
            Namespace &operator=(const Namespace &other);
            Namespace(Namespace &&) noexcept = default;
            Namespace &operator=(Namespace &&) noexcept = default;
            ~Namespace() = default;

            /** @brief True when the argument is registered in this namespace. */
            bool has(const std::string &name) const;
            /** @brief Alias for has(). */
            bool contains(const std::string &name) const { return has(name); }
            /** @brief Alias for has(); makes registration semantics explicit. */
            bool is_registered(const std::string &name) const { return has(name); }
            /** @brief True when the argument occurred on the command line. */
            bool provided(const std::string &name) const;
            /** @brief Raw stored values for an argument. */
            const std::vector<std::string> &raw_values(const std::string &name) const;
            /** @brief True when a subcommand was parsed. */
            bool has_subcommand() const;
            /** @brief Canonical name of the parsed subcommand. */
            const std::string &subcommand() const;
            /** @brief Namespace of the parsed subcommand. */
            const Namespace &subcommand_namespace() const;

            /** @brief Returns the first value converted to T. */
            template <typename T>
            T get(const std::string &name) const;
            /** @brief Returns all values converted to T. */
            template <typename T>
            std::vector<T> getlist(const std::string &name) const;
            /** @brief Returns a value when present and convertible, else nullopt. */
            template <typename T>
            std::optional<T> get_if(const std::string &name) const;
            /** @brief Returns a value or the supplied fallback when absent. */
            template <typename T>
            T get_or(const std::string &name, T fallback) const;

        private:
            template <typename T>
            static T convert(const std::string &value);

            std::unordered_map<std::string, std::vector<std::string>> m_Data;
            std::unordered_map<std::string, bool> m_Provided;
            std::string m_Subcommand;
            std::unique_ptr<Namespace> m_SubcommandNamespace;
        };

        /**
         * @class Argument
         * @brief Description of one positional or optional argument.
         */
        class Argument
        {
            friend class ArgumentParser;
            friend class ArgumentGroup;

        private:
            enum class Kind
            {
                Positional,
                Optional
            };

            enum class ActionType
            {
                Store,
                StoreTrue,
                StoreFalse,
                Append,
                Count,
                Help,
                HelpAll
            };

            struct Validator
            {
                std::function<bool(const std::string &)> callback;
                std::string message;
            };

        public:
            Argument &help(const std::string &text);
            Argument &hidden(bool value = true);
            Argument &required(bool value = true);
            Argument &metavar(const std::string &text);
            Argument &action(const std::string &name);

            /** @brief Sets an exact number of values. `.nargs(2)` is unambiguous. */
            template <typename Count,
                      typename = typename std::enable_if<
                          std::is_integral<Count>::value &&
                          !std::is_same<typename std::decay<Count>::type, char>::value &&
                          !std::is_same<typename std::decay<Count>::type, bool>::value>::type>
            Argument &nargs(Count count)
            {
                return nargs(Nargs::exactly(static_cast<std::size_t>(count)));
            }

            /** @brief Python-style pattern: '?', '*', or '+'. */
            Argument &nargs(char pattern);
            /** @brief Strong nargs description. */
            Argument &nargs(Nargs nargs);
            /** @brief Inclusive value-count range. */
            Argument &nargs(std::size_t minimum, std::size_t maximum)
            {
                return nargs(Nargs::range(minimum, maximum));
            }

            template <typename T>
            Argument &default_value(T value)
            {
                m_DefaultValues = {detail::to_string_value(value)};
                return *this;
            }

            Argument &default_values(std::initializer_list<std::string> values);

            template <typename T>
            Argument &implicit_value(T value)
            {
                m_ImplicitValue = detail::to_string_value(value);
                return *this;
            }

            Argument &env(const std::string &name);
            Argument &choices(std::initializer_list<std::string> values);

            /** @brief Adds a validator. Multiple calls compose (AND). */
            Argument &check(std::function<bool(const std::string &)> validator,
                            const std::string &message = "");

            Argument &depends_on(const std::string &name);
            Argument &conflicts_with(const std::string &name);
            Argument &requires_all(std::initializer_list<std::string> names);

            /** @brief Allows values that begin with '-' to be consumed. */
            Argument &allow_hyphen_values(bool value = true);

            const std::string &dest() const;
            const std::vector<std::string> &option_names() const { return m_OptionNames; }

        private:
            static std::string sanitize_dest(const std::string &name);
            bool takes_value() const;
            bool is_unbounded() const;
            void apply_nargs(Nargs nargs);

            Kind m_Kind{Kind::Positional};
            ActionType m_Action{ActionType::Store};

            std::vector<std::string> m_OptionNames;
            std::string m_Dest;
            std::string m_Help;
            std::string m_Metavar;

            Nargs m_Nargs{};

            bool m_Required{false};
            bool m_RequiredExplicit{false};
            bool m_AllowHyphenValues{false};

            std::vector<std::string> m_DefaultValues;
            std::optional<std::string> m_ImplicitValue;
            std::optional<std::string> m_EnvVar;
            std::vector<std::string> m_Choices;
            std::vector<Validator> m_Validators;
            std::optional<std::string> m_DependsOn;
            std::vector<std::string> m_ConflictsWith;
            std::vector<std::string> m_RequiresAll;
            bool m_Hidden{false};

            std::vector<std::string> m_Values;
            std::size_t m_Occurrences{0};
            bool m_DefaultApplied{false};
            bool m_EnvApplied{false};
        };

        /** @brief Group cardinality model. */
        enum class GroupCardinality
        {
            All,
            AtMostOne,
            ExactlyOne,
            AtLeastOne
        };

        /**
         * @class ArgumentGroup
         * @brief Group of arguments used for help output and validation.
         */
        class ArgumentGroup
        {
            friend class ArgumentParser;

        public:
            explicit ArgumentGroup(const std::string &title = "",
                                   const std::string &description = "")
                : m_Title(title), m_Description(description) {}

            ArgumentGroup &exclusive(bool value = true);
            ArgumentGroup &required(bool value = true);
            ArgumentGroup &cardinality(GroupCardinality value);
            GroupCardinality cardinality() const { return m_Cardinality; }
            const std::string &title() const { return m_Title; }

            template <typename... Names>
            Argument &add_argument(const Names &...names);

        private:
            std::string m_Title;
            std::string m_Description;
            std::vector<std::size_t> m_ArgumentIndices;
            GroupCardinality m_Cardinality{GroupCardinality::All};
            ArgumentParser *m_Parser{nullptr};
        };

        /** @brief Result of a non-throwing parse attempt. */
        struct ParseResult
        {
            const Namespace *namespace_ptr = nullptr;
            ErrorCode code = ErrorCode::None;
            std::string message;

            ParseResult() = default;
            ParseResult(const Namespace *ns, ErrorCode error_code, std::string text)
                : namespace_ptr(ns), code(error_code), message(std::move(text)) {}

            explicit operator bool() const noexcept { return namespace_ptr != nullptr; }
            const Namespace &operator*() const { return *namespace_ptr; }
            const Namespace *operator->() const { return namespace_ptr; }
            const Namespace &value() const
            {
                if (namespace_ptr == nullptr)
                {
                    throw ArgumentError(message, code);
                }
                return *namespace_ptr;
            }
            const std::string &error() const { return message; }
        };

        /** @brief Unknown tokens captured by parse_known_args(). */
        struct UnknownArguments
        {
            std::vector<std::string> options;
            std::vector<std::string> positionals;
        };

        /** @brief Supported completion shells. */
        enum class Shell
        {
            Bash,
            Zsh,
            Fish,
            PowerShell
        };

        using InteractiveHandler =
            std::function<std::optional<std::string>(const Argument &)>;

    public:
        explicit ArgumentParser(std::string program = "",
                                std::string description = "",
                                std::string epilog = "");

        ArgumentParser(const ArgumentParser &) = delete;
        ArgumentParser &operator=(const ArgumentParser &) = delete;
        ArgumentParser(ArgumentParser &&other) noexcept;
        ArgumentParser &operator=(ArgumentParser &&other) noexcept;
        ~ArgumentParser() = default;

        ArgumentGroup &add_argument_group(const std::string &title,
                                          const std::string &description = "");
        ArgumentGroup &add_mutually_exclusive_group(bool required = false);

        std::string &program();
        ArgumentParser &program(std::string program);
        std::string &usage();
        ArgumentParser &usage(std::string usage);
        std::string &epilog();
        ArgumentParser &epilog(std::string epilog);
        std::string &description();
        const std::string &description() const;
        ArgumentParser &description(std::string description);

        ArgumentParser &env_prefix(std::string prefix);

        /** @brief Enables the built-in std::cin prompt with a retry limit. */
        ArgumentParser &interactive(bool value = true);
        /** @brief Installs a custom prompt callback. */
        ArgumentParser &interactive(InteractiveHandler handler);

        HelpFormatter &formatter();
        const HelpFormatter &get_formatter() const;
        ArgumentParser &formatter(HelpFormatter formatter);

        /** @brief Builds help text. Nullopt uses the last help request flag. */
        std::string format_help(std::optional<bool> show_hidden = std::nullopt) const;
        std::string format_usage() const;

        template <typename... Names>
        Argument &add_argument(const Names &...names);

        ArgumentParser &add_subcommand(const std::string &name,
                                       const std::string &description = "");
        ArgumentParser &add_subcommand(std::initializer_list<std::string> names,
                                       const std::string &description = "");
        ArgumentParser &add_subcommand_alias(const std::string &alias,
                                             const std::string &canonical);
        ArgumentParser &require_subcommand(bool value = true);
        ArgumentParser &default_subcommand(std::string name);

        const Namespace &parse_args(int argc, char **argv);
        const Namespace &parse_known_args(int argc, char **argv);

        /** @brief Non-throwing parse wrapper. */
        ParseResult try_parse_args(int argc, char **argv);
        /** @brief Non-throwing permissive parse wrapper. */
        ParseResult try_parse_known_args(int argc, char **argv);

        const std::vector<std::string> &unknown_args() const;
        const std::vector<std::string> &unknown_options() const;
        const std::vector<std::string> &unknown_positionals() const;
        UnknownArguments unknown() const;

        void print_help(std::ostream &stream = std::cout) const;
        void print_usage(std::ostream &stream = std::cout) const;

        /** @brief Formats a parse error together with the usage line. */
        std::string format_error(const ArgumentError &error) const;

        std::string generate_completion_script(const std::string &shell) const;
        std::string generate_completion_script(Shell shell) const;

    private:
        struct PositionalToken
        {
            std::size_t index;
            std::string value;
        };

        struct UnknownEntry
        {
            std::size_t index;
            std::string text;
            bool positional;
        };

        static bool is_negative_number_token(const std::string &token);
        bool should_treat_as_optional(const std::string &token) const;
        bool is_registered_option_token(const std::string &token) const;
        bool is_value_boundary(const std::string &token, const Argument &arg) const;
        static std::string argument_display(const Argument &arg);
        static std::string usage_display(const Argument &arg);
        static std::string dependency_name(const Argument &arg);
        Argument *try_lookup_option(const std::string &name);
        const Argument *try_lookup_option(const std::string &name) const;
        std::string suggest_correction(const std::string &name) const;
        std::string suggest_subcommand(const std::string &name) const;
        Argument *find_argument(const std::string &name);
        const Argument *find_argument(const std::string &name) const;
        bool argument_supplied(const Argument &arg) const;
        bool argument_has_effective_value(const Argument &arg) const;

        void apply_action(Argument &arg, const std::vector<std::string> &values);
        std::vector<std::string> collect_option_values(Argument &arg,
                                                       int &index,
                                                       int argc,
                                                       char **argv,
                                                       std::optional<std::string> first);
        void consume_long_option(const std::string &token, int &index, int argc,
                                 char **argv, bool allow_unknown);
        void consume_short_cluster(const std::string &token, int &index, int argc,
                                   char **argv, bool allow_unknown);
        void assign_positionals(const std::vector<PositionalToken> &tokens,
                                bool allow_unknown);
        void finalize_and_validate();
        void build_namespace();
        void reset_parse_state();
        void record_unknown(std::size_t index, std::string text, bool positional);
        void rebuild_unknown();
        void merge_unknown(const ArgumentParser &child, std::size_t offset);
        std::string synthesize_usage() const;
        std::string subcommand_display(const std::string &canonical) const;
        ArgumentParser *find_subcommand(const std::string &token,
                                        std::string &canonical);
        void parse_impl(int argc, char **argv, bool allow_unknown);

        std::string m_Usage;
        std::string m_Epilog;
        std::string m_Program;
        std::string m_Description;

        bool m_AddHelp{true};
        bool m_RequireSubcommand{false};
        bool m_HelpShowHidden{false};
        std::string m_DefaultSubcommand;
        std::string m_EnvPrefix;

        std::deque<Argument> m_Arguments;
        std::deque<ArgumentGroup> m_Groups;
        std::vector<std::size_t> m_Positionals;
        std::unordered_map<std::string, std::size_t> m_OptionLookup;
        std::unordered_map<std::string, std::unique_ptr<ArgumentParser>> m_Subcommands;
        std::unordered_map<std::string, std::string> m_SubcommandAliases;
        std::vector<std::string> m_SubcommandOrder;
        std::vector<UnknownEntry> m_UnknownEntries;
        std::vector<std::string> m_UnknownArgs;
        std::vector<std::string> m_UnknownOptions;
        std::vector<std::string> m_UnknownPositionals;
        InteractiveHandler m_InteractiveHandler;

        Namespace m_Namespace;
        HelpFormatter m_HelpFormatter;
    };

    using Formatter = HelpFormatter;
    using Argparser = ArgumentParser;
    using Color = HelpFormatter::Color;
    using Section = HelpFormatter::Section;
    using Alignment = HelpFormatter::Alignment;
    using ColorTheme = HelpFormatter::ColorTheme;

    //-----------------------------------------------------------------------------
    // [SECTION] HelpFormatter definitions
    //-----------------------------------------------------------------------------

    inline HelpFormatter::ColorTheme HelpFormatter::ColorTheme::nord()
    {
        ColorTheme theme;
        theme.usage_color = Color::Cyan;
        theme.option_color = Color::Blue;
        theme.heading_color = Color::Bold;
        theme.section_color = Color::Cyan;
        theme.metavar_color = Color::Yellow;
        return theme;
    }

    inline HelpFormatter::ColorTheme HelpFormatter::ColorTheme::monokai()
    {
        ColorTheme theme;
        theme.usage_color = Color::Magenta;
        theme.option_color = Color::Green;
        theme.heading_color = Color::Bold;
        theme.section_color = Color::Magenta;
        theme.metavar_color = Color::Yellow;
        return theme;
    }

    inline HelpFormatter::ColorTheme HelpFormatter::ColorTheme::dracula()
    {
        ColorTheme theme;
        theme.usage_color = Color::Magenta;
        theme.option_color = Color::Cyan;
        theme.heading_color = Color::Bold;
        theme.section_color = Color::Magenta;
        theme.metavar_color = Color::Yellow;
        return theme;
    }

    inline void HelpFormatter::Table::add_row(std::vector<std::string> columns)
    {
        m_Rows.push_back(std::move(columns));
    }

    inline std::string HelpFormatter::Table::format(const Alignment &align) const
    {
        if (m_Rows.empty())
        {
            return "";
        }

        std::size_t columns = 0;
        for (const auto &row : m_Rows)
        {
            columns = (std::max)(columns, row.size());
        }

        std::vector<std::size_t> widths(columns, 0);
        for (const auto &row : m_Rows)
        {
            for (std::size_t i = 0; i < row.size(); ++i)
            {
                widths[i] = (std::max)(widths[i], visible_length(row[i]));
            }
        }

        std::ostringstream stream;
        const std::string indent(align.indent_size, align.indent_char);
        for (const auto &row : m_Rows)
        {
            stream << indent;
            for (std::size_t i = 0; i < row.size(); ++i)
            {
                stream << row[i];
                if (i + 1 < row.size())
                {
                    const std::size_t used = visible_length(row[i]);
                    const std::size_t padding =
                        widths[i] > used ? widths[i] - used + 2 : 2;
                    stream << std::string(padding, ' ');
                }
            }
            stream << "\n";
        }
        return stream.str();
    }

    inline HelpFormatter::HelpFormatter(std::size_t width,
                                        const std::string &prog_name)
        : m_Width(width == 0 ? get_terminal_width() : width),
          m_Program(prog_name),
          m_Format(supports_color() ? Format::Colored : Format::Plain)
    {
    }

    inline HelpFormatter &HelpFormatter::program(const std::string &name)
    {
        m_Program = name;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::usage(const std::string &usage)
    {
        m_Usage = usage;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::epilog(const std::string &epilog)
    {
        m_Epilog = epilog;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::description(const std::string &description)
    {
        m_Description = description;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::prefix_info(const std::string &info)
    {
        m_PrefixInfo = info;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::suffix_info(const std::string &info)
    {
        m_SuffixInfo = info;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::width(std::size_t width)
    {
        m_Width = width == 0 ? get_terminal_width() : width;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::format(Format format)
    {
        m_Format = format;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::alignment(const Alignment &align)
    {
        m_Alignment = align;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::use_color(bool enable)
    {
        m_Theme.enabled = enable;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::color_theme(const ColorTheme &theme)
    {
        m_Theme = theme;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::line_separator(const std::string &separator)
    {
        m_LineSeparator = separator;
        return *this;
    }

    inline HelpFormatter &HelpFormatter::line_separator(char sep, int width)
    {
        m_LineSeparator = std::string(width > 0 ? static_cast<std::size_t>(width) : 0, sep);
        return *this;
    }

    inline HelpFormatter &HelpFormatter::sort_sections(bool sort)
    {
        m_SortSections = sort;
        return *this;
    }

    inline HelpFormatter::Section &HelpFormatter::add_section(
        const std::string &title, const std::string &description, Section::Type type)
    {
        m_Sections.emplace_back(title, description, type);
        return m_Sections.back();
    }

    inline HelpFormatter::Section &HelpFormatter::get_section(
        const std::string &title, const std::string &description, Section::Type type)
    {
        for (auto &section : m_Sections)
        {
            if (section.title() == title)
            {
                return section;
            }
        }
        return add_section(title, description, type);
    }

    inline const std::string &HelpFormatter::program() const { return m_Program; }
    inline std::size_t HelpFormatter::width() const { return m_Width; }
    inline HelpFormatter::ColorTheme &HelpFormatter::color_theme() { return m_Theme; }
    inline const HelpFormatter::ColorTheme &HelpFormatter::color_theme() const { return m_Theme; }
    inline HelpFormatter::Alignment &HelpFormatter::alignment() { return m_Alignment; }
    inline const HelpFormatter::Alignment &HelpFormatter::alignment() const { return m_Alignment; }
    inline const std::string &HelpFormatter::usage() const { return m_Usage; }
    inline const std::string &HelpFormatter::epilog() const { return m_Epilog; }
    inline const std::string &HelpFormatter::description() const { return m_Description; }

    inline std::size_t HelpFormatter::visible_length(const std::string &text)
    {
        return detail::visible_length(text);
    }

    inline bool HelpFormatter::supports_color()
    {
        const char *no_color = std::getenv("NO_COLOR");
        if (no_color != nullptr)
        {
            return false;
        }
        const char *force = std::getenv("CLICOLOR_FORCE");
        if (force != nullptr && std::string(force) != "0")
        {
            return true;
        }
        const char *clicolor = std::getenv("CLICOLOR");
        if (clicolor != nullptr && std::string(clicolor) == "0")
        {
            return false;
        }
#if defined(_WIN32)
        return _isatty(_fileno(stdout)) != 0;
#else
        return isatty(STDOUT_FILENO) != 0;
#endif
    }

    inline std::size_t HelpFormatter::get_terminal_width()
    {
        static const std::size_t cached = []()
        {
            const char *columns = std::getenv("COLUMNS");
            if (columns != nullptr)
            {
                const int parsed = std::atoi(columns);
                if (parsed > 0)
                {
                    return static_cast<std::size_t>(parsed);
                }
            }
#if defined(_WIN32)
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
            {
                const int width = info.srWindow.Right - info.srWindow.Left + 1;
                if (width > 0)
                {
                    return static_cast<std::size_t>(width);
                }
            }
#else
            winsize size{};
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
            {
                return static_cast<std::size_t>(size.ws_col);
            }
#endif
            return static_cast<std::size_t>(80);
        }();

        return cached;
    }

    inline std::string HelpFormatter::format_help() const
    {
        if (m_Format == Format::Markdown)
        {
            return format_help_markdown();
        }

        std::ostringstream stream;
        if (!m_PrefixInfo.empty())
        {
            stream << m_PrefixInfo << "\n";
        }

        const std::string usage_text = m_Usage.empty() ? m_Program : m_Usage;
        if (!usage_text.empty())
        {
            stream << format_colored("usage: ", get_color_for_format(Color::Green))
                   << usage_text << "\n\n";
        }

        if (!m_Description.empty())
        {
            stream << wrap_text(m_Description, 0) << "\n\n";
        }

        if (!m_LineSeparator.empty())
        {
            stream << m_LineSeparator << "\n";
        }

        std::vector<const Section *> sections;
        sections.reserve(m_Sections.size());
        for (const auto &section : m_Sections)
        {
            if (section.visible())
            {
                sections.push_back(&section);
            }
        }
        if (m_SortSections)
        {
            std::stable_sort(sections.begin(), sections.end(),
                             [](const Section *lhs, const Section *rhs)
                             { return lhs->title() < rhs->title(); });
        }
        for (const Section *section : sections)
        {
            stream << format_section(*section);
        }

        if (!m_LineSeparator.empty() && !m_Epilog.empty())
        {
            stream << m_LineSeparator << "\n";
        }

        const auto ensure_blank_line = [&stream]()
        {
            const std::string current = stream.str();
            if (current.empty())
            {
                return;
            }
            if (current.size() >= 2 &&
                current[current.size() - 1] == '\n' &&
                current[current.size() - 2] == '\n')
            {
                return;
            }
            stream << (current.back() == '\n' ? "\n" : "\n\n");
        };

        if (!m_Epilog.empty())
        {
            ensure_blank_line();
            stream << wrap_text(m_Epilog, 0) << "\n";
        }
        if (!m_SuffixInfo.empty())
        {
            ensure_blank_line();
            stream << m_SuffixInfo << "\n";
        }
        return stream.str();
    }

    inline std::string HelpFormatter::format_help_markdown() const
    {
        std::ostringstream stream;
        if (!m_PrefixInfo.empty())
        {
            stream << m_PrefixInfo << "\n\n";
        }
        if (!m_Program.empty())
        {
            stream << "# " << m_Program << "\n\n";
        }
        if (!m_Description.empty())
        {
            stream << m_Description << "\n\n";
        }

        const std::string usage_text = m_Usage.empty() ? m_Program : m_Usage;
        if (!usage_text.empty())
        {
            stream << "## Usage\n\n```text\nusage: " << usage_text
                   << "\n```\n\n";
        }

        std::vector<const Section *> sections;
        for (const auto &section : m_Sections)
        {
            if (section.visible())
            {
                sections.push_back(&section);
            }
        }
        if (m_SortSections)
        {
            std::stable_sort(sections.begin(), sections.end(),
                             [](const Section *lhs, const Section *rhs)
                             { return lhs->title() < rhs->title(); });
        }
        for (const Section *section : sections)
        {
            stream << format_section_markdown(*section);
        }

        if (!m_Epilog.empty())
        {
            stream << m_Epilog << "\n\n";
        }
        if (!m_SuffixInfo.empty())
        {
            stream << m_SuffixInfo << "\n";
        }
        return stream.str();
    }

    inline std::string HelpFormatter::format_usage() const
    {
        std::ostringstream stream;
        stream << format_colored("usage: ", get_color_for_format(Color::Green));
        stream << (m_Usage.empty() ? m_Program : m_Usage) << "\n";
        return stream.str();
    }

    inline std::string HelpFormatter::format_section(const Section &section) const
    {
        std::ostringstream stream;
        if (!section.title().empty())
        {
            const Color heading = (m_Format == Format::Compact)
                                      ? Color::Default
                                      : m_Theme.heading_color;
            stream << format_colored(section.title() + ":", heading) << "\n";
        }
        if (!section.description().empty())
        {
            stream << wrap_text(section.description(), m_Alignment.indent_size)
                   << "\n\n";
        }
        for (const auto &item : section.m_Items)
        {
            stream << format_item(item) << "\n";
        }
        for (const auto &example : section.m_Examples)
        {
            stream << format_example(example) << "\n";
        }
        for (const auto &text : section.m_Texts)
        {
            stream << wrap_text(text, m_Alignment.indent_size) << "\n";
        }
        stream << "\n";
        return stream.str();
    }

    inline std::string HelpFormatter::format_section_markdown(
        const Section &section) const
    {
        std::ostringstream stream;
        if (!section.title().empty())
        {
            stream << "## " << section.title() << "\n\n";
        }
        if (!section.description().empty())
        {
            stream << section.description() << "\n\n";
        }
        for (const auto &item : section.m_Items)
        {
            stream << "- `" << item.option;
            if (!item.metavar.empty())
            {
                stream << " " << item.metavar;
            }
            stream << "`";
            if (item.required)
            {
                stream << " *(required)*";
            }
            if (!item.help.empty())
            {
                stream << " - " << item.help;
            }
            stream << "\n";
        }
        if (!section.m_Items.empty())
        {
            stream << "\n";
        }
        for (const auto &example : section.m_Examples)
        {
            stream << "```sh\n$ " << example.command << "\n```\n";
            if (!example.description.empty())
            {
                stream << "\n" << example.description << "\n";
            }
            stream << "\n";
        }
        for (const auto &text : section.m_Texts)
        {
            stream << text << "\n\n";
        }
        return stream.str();
    }

    inline std::string HelpFormatter::wrap_text(const std::string &text,
                                                std::size_t indent_level) const
    {
        if (!m_Alignment.wrap_help_text)
        {
            return text;
        }
        const std::size_t indent = indent_level * m_Alignment.indent_size;
        return wrap_indented(text, indent, indent);
    }

    inline std::string HelpFormatter::wrap_indented(const std::string &text,
                                                    std::size_t first_indent,
                                                    std::size_t subsequent_indent) const
    {
        if (text.empty())
        {
            return text;
        }

        std::string result;
        std::size_t position = 0;
        while (position <= text.size())
        {
            const std::size_t newline = text.find('\n', position);
            const std::string paragraph =
                text.substr(position, newline == std::string::npos
                                          ? std::string::npos
                                          : newline - position);
            if (!paragraph.empty())
            {
                std::istringstream words_stream(paragraph);
                std::vector<std::string> words;
                std::string word;
                while (words_stream >> word)
                {
                    words.push_back(word);
                }
                result += wrap_words(words, first_indent, subsequent_indent);
            }
            if (newline == std::string::npos)
            {
                break;
            }
            result += "\n";
            position = newline + 1;
        }
        return result;
    }

    inline std::string HelpFormatter::wrap_words(const std::vector<std::string> &words,
                                                 std::size_t first_indent,
                                                 std::size_t subsequent_indent) const
    {
        std::string result;
        std::size_t line_width = first_indent;
        bool line_started = false;

        for (const auto &word : words)
        {
            const std::size_t word_width = visible_length(word);
            if (!line_started)
            {
                result += std::string(first_indent, ' ');
                result += word;
                line_width = first_indent + word_width;
                line_started = true;
            }
            else if (line_width + 1 + word_width <= m_Width)
            {
                result += " ";
                result += word;
                line_width += 1 + word_width;
            }
            else
            {
                result += "\n";
                result += std::string(subsequent_indent, ' ');
                result += word;
                line_width = subsequent_indent + word_width;
            }
        }
        return result;
    }

    inline std::string HelpFormatter::colorize(const std::string &text,
                                               Color color) const
    {
        if (m_Format != Format::Colored || !m_Theme.enabled)
        {
            return text;
        }
        return format_colored(text, color);
    }

    inline std::string HelpFormatter::hyperlink(const std::string &text,
                                                const std::string &url) const
    {
        if (m_Format != Format::Colored || !m_Theme.enabled)
        {
            return text;
        }
        std::ostringstream stream;
        stream << "\033]8;;" << url << "\033\\" << text << "\033]8;;\033\\";
        return stream.str();
    }

    inline HelpFormatter::Color HelpFormatter::get_color_for_format(
        Color default_color) const
    {
        return (m_Format == Format::Colored || m_Format == Format::Compact) &&
                       m_Theme.enabled
                   ? default_color
                   : Color::Default;
    }

    inline std::string HelpFormatter::format_colored(const std::string &text,
                                                     Color color) const
    {
        if (m_Format != Format::Colored || !m_Theme.enabled ||
            color == Color::Default)
        {
            return text;
        }
        std::ostringstream stream;
        stream << "\033[" << static_cast<int>(color) << "m" << text << "\033[0m";
        return stream.str();
    }

    inline std::string HelpFormatter::format_item(const Section::Item &item) const
    {
        std::ostringstream stream;
        const std::string indent(m_Alignment.indent_size, m_Alignment.indent_char);

        std::string display = item.option;
        if (item.required)
        {
            display += format_colored(" [REQUIRED]", m_Theme.required_color);
        }

        stream << indent << format_colored(display, m_Theme.option_color);
        if (!item.metavar.empty())
        {
            stream << " " << format_colored(item.metavar, m_Theme.metavar_color);
        }

        if (m_Format == Format::Compact)
        {
            if (!item.help.empty())
            {
                stream << "  " << item.help;
            }
            return stream.str();
        }

        std::size_t current = m_Alignment.indent_size + visible_length(display);
        if (!item.metavar.empty())
        {
            current += 1 + visible_length(item.metavar);
        }

        const std::size_t minimum_help =
            current + 2;
        std::size_t help_start =
            (std::max)(minimum_help, m_Alignment.indent_size + m_Alignment.option_width);
        if (help_start > m_Alignment.max_help_position)
        {
            help_start = (std::max)(minimum_help, m_Alignment.max_help_position);
        }
        if (help_start + m_Alignment.min_description_width > m_Width)
        {
            help_start = minimum_help;
        }

        if (item.help.empty())
        {
            return stream.str();
        }

        if (help_start + m_Alignment.min_description_width > m_Width)
        {
            stream << "\n"
                   << wrap_indented(item.help, help_start, help_start);
            return stream.str();
        }

        if (current < help_start)
        {
            stream << std::string(help_start - current, ' ');
        }
        else
        {
            stream << "  ";
            help_start = current + 2;
        }
        stream << wrap_indented(item.help, help_start, help_start);
        return stream.str();
    }

    inline std::string HelpFormatter::format_example(
        const Section::Example &example) const
    {
        std::ostringstream stream;
        const std::size_t indent = m_Alignment.indent_size;
        stream << std::string(indent, m_Alignment.indent_char) << "  $ "
               << format_colored(example.command, m_Theme.examples_color);
        if (!example.description.empty())
        {
            stream << "\n"
                   << wrap_text(example.description, indent + 1);
        }
        return stream.str();
    }

    inline HelpFormatter::Section::Section(const std::string &title,
                                           const std::string &description,
                                           Type type)
        : m_Title(title), m_Description(description), m_Type(type)
    {
    }

    inline HelpFormatter::Section &HelpFormatter::Section::add_item(
        const std::string &option, const std::string &metavar,
        const std::string &help, bool required)
    {
        m_Items.push_back({option, metavar, help, required});
        return *this;
    }

    inline HelpFormatter::Section &HelpFormatter::Section::add_text(
        const std::string &text)
    {
        m_Texts.push_back(text);
        return *this;
    }

    inline HelpFormatter::Section &HelpFormatter::Section::add_example(
        const std::string &command, const std::string &description)
    {
        m_Examples.push_back({command, description});
        return *this;
    }

    inline bool HelpFormatter::Section::visible() { return m_Visible; }

    inline HelpFormatter::Section &HelpFormatter::Section::visible(bool visible)
    {
        m_Visible = visible;
        return *this;
    }

    inline const std::string &HelpFormatter::Section::title() const { return m_Title; }
    inline const std::string &HelpFormatter::Section::description() const { return m_Description; }
    inline HelpFormatter::Section::Type HelpFormatter::Section::type() const { return m_Type; }
    inline bool HelpFormatter::Section::visible() const { return m_Visible; }

    //-----------------------------------------------------------------------------
    // [SECTION] ArgumentParser definitions
    //-----------------------------------------------------------------------------

    inline ArgumentParser::ArgumentParser(std::string program,
                                          std::string description,
                                          std::string epilog)
        : m_Epilog(std::move(epilog)),
          m_Program(std::move(program)),
          m_Description(std::move(description)),
          m_HelpFormatter(0, m_Program)
    {
        m_HelpFormatter.program(m_Program)
            .description(m_Description)
            .epilog(m_Epilog);

        if (m_AddHelp)
        {
            add_argument("-h", "--help")
                .action("help")
                .default_value(false)
                .help("show this help message and exit");
            add_argument("--help-all")
                .action("help_all")
                .default_value(false)
                .help("show all arguments, including hidden ones");
        }
    }

    inline ArgumentParser::ArgumentParser(ArgumentParser &&other) noexcept
        : m_Usage(std::move(other.m_Usage)),
          m_Epilog(std::move(other.m_Epilog)),
          m_Program(std::move(other.m_Program)),
          m_Description(std::move(other.m_Description)),
          m_AddHelp(other.m_AddHelp),
          m_RequireSubcommand(other.m_RequireSubcommand),
          m_HelpShowHidden(other.m_HelpShowHidden),
          m_DefaultSubcommand(std::move(other.m_DefaultSubcommand)),
          m_EnvPrefix(std::move(other.m_EnvPrefix)),
          m_Arguments(std::move(other.m_Arguments)),
          m_Groups(std::move(other.m_Groups)),
          m_Positionals(std::move(other.m_Positionals)),
          m_OptionLookup(std::move(other.m_OptionLookup)),
          m_Subcommands(std::move(other.m_Subcommands)),
          m_SubcommandAliases(std::move(other.m_SubcommandAliases)),
          m_SubcommandOrder(std::move(other.m_SubcommandOrder)),
          m_UnknownEntries(std::move(other.m_UnknownEntries)),
          m_UnknownArgs(std::move(other.m_UnknownArgs)),
          m_UnknownOptions(std::move(other.m_UnknownOptions)),
          m_UnknownPositionals(std::move(other.m_UnknownPositionals)),
          m_InteractiveHandler(std::move(other.m_InteractiveHandler)),
          m_Namespace(std::move(other.m_Namespace)),
          m_HelpFormatter(std::move(other.m_HelpFormatter))
    {
        for (auto &group : m_Groups)
        {
            group.m_Parser = this;
        }
    }

    inline ArgumentParser &ArgumentParser::operator=(ArgumentParser &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        m_Usage = std::move(other.m_Usage);
        m_Epilog = std::move(other.m_Epilog);
        m_Program = std::move(other.m_Program);
        m_Description = std::move(other.m_Description);
        m_AddHelp = other.m_AddHelp;
        m_RequireSubcommand = other.m_RequireSubcommand;
        m_HelpShowHidden = other.m_HelpShowHidden;
        m_DefaultSubcommand = std::move(other.m_DefaultSubcommand);
        m_EnvPrefix = std::move(other.m_EnvPrefix);
        m_Arguments = std::move(other.m_Arguments);
        m_Groups = std::move(other.m_Groups);
        m_Positionals = std::move(other.m_Positionals);
        m_OptionLookup = std::move(other.m_OptionLookup);
        m_Subcommands = std::move(other.m_Subcommands);
        m_SubcommandAliases = std::move(other.m_SubcommandAliases);
        m_SubcommandOrder = std::move(other.m_SubcommandOrder);
        m_UnknownEntries = std::move(other.m_UnknownEntries);
        m_UnknownArgs = std::move(other.m_UnknownArgs);
        m_UnknownOptions = std::move(other.m_UnknownOptions);
        m_UnknownPositionals = std::move(other.m_UnknownPositionals);
        m_InteractiveHandler = std::move(other.m_InteractiveHandler);
        m_Namespace = std::move(other.m_Namespace);
        m_HelpFormatter = std::move(other.m_HelpFormatter);

        for (auto &group : m_Groups)
        {
            group.m_Parser = this;
        }
        return *this;
    }

    inline std::string &ArgumentParser::program() { return m_Program; }

    inline ArgumentParser &ArgumentParser::program(std::string program)
    {
        m_Program = std::move(program);
        return *this;
    }

    inline std::string &ArgumentParser::usage() { return m_Usage; }

    inline ArgumentParser &ArgumentParser::usage(std::string usage)
    {
        m_Usage = std::move(usage);
        return *this;
    }

    inline std::string &ArgumentParser::epilog() { return m_Epilog; }

    inline ArgumentParser &ArgumentParser::epilog(std::string epilog)
    {
        m_Epilog = std::move(epilog);
        return *this;
    }

    inline std::string &ArgumentParser::description() { return m_Description; }

    inline const std::string &ArgumentParser::description() const
    {
        return m_Description;
    }

    inline ArgumentParser &ArgumentParser::description(std::string description)
    {
        m_Description = std::move(description);
        return *this;
    }

    inline ArgumentParser &ArgumentParser::env_prefix(std::string prefix)
    {
        m_EnvPrefix = std::move(prefix);
        return *this;
    }

    inline ArgumentParser &ArgumentParser::interactive(bool value)
    {
        if (!value)
        {
            m_InteractiveHandler = nullptr;
            return *this;
        }

        m_InteractiveHandler = [](const Argument &argument) -> std::optional<std::string>
        {
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                std::cout << "Enter value for " << argument_display(argument);
                if (!argument.m_Help.empty())
                {
                    std::cout << " (" << argument.m_Help << ")";
                }
                std::cout << ": ";
                std::string line;
                if (!std::getline(std::cin, line))
                {
                    return std::nullopt;
                }
                if (!line.empty())
                {
                    return line;
                }
            }
            return std::nullopt;
        };
        return *this;
    }

    inline ArgumentParser &ArgumentParser::interactive(InteractiveHandler handler)
    {
        m_InteractiveHandler = std::move(handler);
        return *this;
    }

    inline HelpFormatter &ArgumentParser::formatter() { return m_HelpFormatter; }

    inline const HelpFormatter &ArgumentParser::get_formatter() const
    {
        return m_HelpFormatter;
    }

    inline ArgumentParser &ArgumentParser::formatter(HelpFormatter formatter)
    {
        m_HelpFormatter = std::move(formatter);
        return *this;
    }

    inline ArgumentParser::ArgumentGroup &ArgumentParser::add_argument_group(
        const std::string &title, const std::string &description)
    {
        m_Groups.emplace_back(title, description);
        m_Groups.back().m_Parser = this;
        return m_Groups.back();
    }

    inline ArgumentParser::ArgumentGroup &
    ArgumentParser::add_mutually_exclusive_group(bool required)
    {
        const std::string title =
            "option group " + std::to_string(m_Groups.size() + 1);
        auto &group = add_argument_group(title, "");
        group.cardinality(required ? GroupCardinality::ExactlyOne
                                   : GroupCardinality::AtMostOne);
        return group;
    }

    inline ArgumentParser::ArgumentGroup &ArgumentParser::ArgumentGroup::exclusive(
        bool value)
    {
        if (value)
        {
            m_Cardinality = m_Cardinality == GroupCardinality::AtLeastOne ||
                                    m_Cardinality == GroupCardinality::ExactlyOne
                                ? GroupCardinality::ExactlyOne
                                : GroupCardinality::AtMostOne;
        }
        else
        {
            m_Cardinality = m_Cardinality == GroupCardinality::ExactlyOne ||
                                    m_Cardinality == GroupCardinality::AtLeastOne
                                ? GroupCardinality::AtLeastOne
                                : GroupCardinality::All;
        }
        return *this;
    }

    inline ArgumentParser::ArgumentGroup &ArgumentParser::ArgumentGroup::required(
        bool value)
    {
        if (value)
        {
            m_Cardinality = m_Cardinality == GroupCardinality::AtMostOne ||
                                    m_Cardinality == GroupCardinality::ExactlyOne
                                ? GroupCardinality::ExactlyOne
                                : GroupCardinality::AtLeastOne;
        }
        else
        {
            m_Cardinality = m_Cardinality == GroupCardinality::ExactlyOne ||
                                    m_Cardinality == GroupCardinality::AtLeastOne
                                ? GroupCardinality::AtMostOne
                                : GroupCardinality::All;
        }
        return *this;
    }

    inline ArgumentParser::ArgumentGroup &ArgumentParser::ArgumentGroup::cardinality(
        GroupCardinality value)
    {
        m_Cardinality = value;
        return *this;
    }

    inline std::string ArgumentParser::synthesize_usage() const
    {
        std::vector<std::string> parts;
        if (!m_Program.empty())
        {
            parts.push_back(m_Program);
        }

        for (const auto &argument : m_Arguments)
        {
            if (argument.m_Kind != Argument::Kind::Optional || argument.m_Hidden)
            {
                continue;
            }

            const std::string display = usage_display(argument);
            std::string part = display;
            if (argument.takes_value())
            {
                std::string metavar = argument.m_Metavar;
                if (metavar.empty())
                {
                    if (!argument.m_Choices.empty())
                    {
                        metavar = "{";
                        for (std::size_t i = 0; i < argument.m_Choices.size(); ++i)
                        {
                            if (i != 0)
                            {
                                metavar += ",";
                            }
                            metavar += argument.m_Choices[i];
                        }
                        metavar += "}";
                    }
                    else
                    {
                        metavar = detail::to_upper(argument.m_Dest);
                    }
                }

                const std::size_t minimum = argument.m_Nargs.min();
                const std::size_t maximum = argument.m_Nargs.max();
                if (minimum == 0 && maximum == 1)
                {
                    part += " [" + metavar + "]";
                }
                else if (argument.is_unbounded())
                {
                    if (minimum == 0)
                    {
                        part += " [" + metavar + " ...]";
                    }
                    else
                    {
                        part += " " + metavar + " [" + metavar + " ...]";
                    }
                }
                else if (minimum == maximum)
                {
                    for (std::size_t i = 0; i < maximum; ++i)
                    {
                        part += " " + metavar;
                    }
                }
                else
                {
                    part += " " + metavar + " [" + metavar + " ...]";
                }
            }

            if (!argument.m_Required)
            {
                part = "[" + part + "]";
            }
            parts.push_back(part);
        }

        if (!m_Subcommands.empty())
        {
            std::ostringstream names;
            names << "{";
            for (std::size_t i = 0; i < m_SubcommandOrder.size(); ++i)
            {
                if (i != 0)
                {
                    names << ",";
                }
                names << m_SubcommandOrder[i];
            }
            names << "} ...";
            parts.push_back(m_RequireSubcommand ? names.str()
                                                : "[" + names.str() + "]");
        }

        for (const auto &argument : m_Arguments)
        {
            if (argument.m_Kind != Argument::Kind::Positional || argument.m_Hidden)
            {
                continue;
            }

            std::string name = argument.m_Metavar.empty()
                                   ? argument.m_Dest
                                   : argument.m_Metavar;
            const std::size_t minimum = argument.m_Nargs.min();
            const std::size_t maximum = argument.m_Nargs.max();

            std::string part;
            if (minimum == 0 && maximum == 1)
            {
                part = "[" + name + "]";
            }
            else if (argument.is_unbounded())
            {
                part = minimum == 0 ? "[" + name + " ...]"
                                    : name + " [" + name + " ...]";
            }
            else if (minimum == maximum)
            {
                for (std::size_t i = 0; i < maximum; ++i)
                {
                    if (i != 0)
                    {
                        part += " ";
                    }
                    part += name;
                }
            }
            else
            {
                part = name + " [" + name + " ...]";
            }

            const bool optional = !argument.m_Required ||
                                  !argument.m_DefaultValues.empty() ||
                                  argument.m_EnvVar.has_value();
            if (optional && !part.empty() && part.front() != '[')
            {
                part = "[" + part + "]";
            }
            parts.push_back(part);
        }

        std::ostringstream stream;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i != 0)
            {
                stream << " ";
            }
            stream << parts[i];
        }
        return stream.str();
    }

    inline std::string ArgumentParser::usage_display(const Argument &argument)
    {
        if (argument.m_Kind == Argument::Kind::Positional)
        {
            return argument.m_Metavar.empty() ? argument.m_Dest
                                              : argument.m_Metavar;
        }

        std::string short_name;
        std::string long_name;
        for (const auto &name : argument.m_OptionNames)
        {
            if (name.rfind("--", 0) == 0)
            {
                long_name = name;
            }
            else if (short_name.empty())
            {
                short_name = name;
            }
        }
        if (!long_name.empty())
        {
            return long_name;
        }
        return short_name.empty()
                   ? (argument.m_OptionNames.empty() ? argument.m_Dest
                                                     : argument.m_OptionNames.front())
                   : short_name;
    }

    inline std::string ArgumentParser::argument_display(const Argument &argument)
    {
        if (argument.m_Kind == Argument::Kind::Positional)
        {
            return argument.m_Dest;
        }

        std::string display;
        for (std::size_t i = 0; i < argument.m_OptionNames.size(); ++i)
        {
            if (i != 0)
            {
                display += ", ";
            }
            display += argument.m_OptionNames[i];
        }
        if (argument.takes_value())
        {
            const std::string metavar =
                argument.m_Metavar.empty()
                    ? detail::to_upper(argument.m_Dest)
                    : argument.m_Metavar;
            display += " " + metavar;
        }
        return display;
    }

    inline std::string ArgumentParser::subcommand_display(
        const std::string &canonical) const
    {
        std::string display = canonical;
        bool first = true;
        for (const auto &entry : m_SubcommandAliases)
        {
            if (entry.second == canonical)
            {
                if (first)
                {
                    display += " (";
                    first = false;
                }
                else
                {
                    display += ", ";
                }
                display += entry.first;
            }
        }
        if (!first)
        {
            display += ")";
        }
        return display;
    }

    inline std::string ArgumentParser::format_help(
        std::optional<bool> show_hidden) const
    {
        const bool include_hidden = show_hidden.value_or(m_HelpShowHidden);

        HelpFormatter formatter = m_HelpFormatter;
        formatter.program(m_Program)
            .description(m_Description)
            .epilog(m_Epilog)
            .usage(m_Usage.empty() ? synthesize_usage() : m_Usage);

        std::vector<bool> added(m_Arguments.size(), false);

        const auto add_item = [&](HelpFormatter::Section &section,
                                  const Argument &argument)
        {
            std::string option;
            if (argument.m_Kind == Argument::Kind::Optional)
            {
                for (std::size_t i = 0; i < argument.m_OptionNames.size(); ++i)
                {
                    if (i != 0)
                    {
                        option += ", ";
                    }
                    option += argument.m_OptionNames[i];
                }
            }
            else
            {
                option = argument.m_Dest;
            }

            std::string metavar;
            if (argument.m_Kind == Argument::Kind::Optional && argument.takes_value())
            {
                metavar = argument.m_Metavar;
                if (metavar.empty())
                {
                    if (!argument.m_Choices.empty())
                    {
                        metavar = "{";
                        for (std::size_t i = 0; i < argument.m_Choices.size(); ++i)
                        {
                            if (i != 0)
                            {
                                metavar += ",";
                            }
                            metavar += argument.m_Choices[i];
                        }
                        metavar += "}";
                    }
                    else
                    {
                        metavar = detail::to_upper(argument.m_Dest);
                    }
                }
            }

            section.add_item(option, metavar, argument.m_Help, argument.m_Required);
        };

        for (const auto &group : m_Groups)
        {
            if (group.m_ArgumentIndices.empty())
            {
                continue;
            }

            std::vector<std::size_t> visible;
            for (std::size_t index : group.m_ArgumentIndices)
            {
                const auto &argument = m_Arguments[index];
                if (include_hidden || !argument.m_Hidden)
                {
                    visible.push_back(index);
                }
            }
            if (visible.empty())
            {
                continue;
            }

            const std::string title = group.m_Title.empty() ? "arguments"
                                                            : group.m_Title;
            auto &section = formatter.add_section(
                title, group.m_Description, HelpFormatter::Section::Type::Arguments);
            for (std::size_t index : visible)
            {
                add_item(section, m_Arguments[index]);
                added[index] = true;
            }
        }

        bool has_optional = false;
        for (std::size_t i = 0; i < m_Arguments.size(); ++i)
        {
            if (m_Arguments[i].m_Kind == Argument::Kind::Optional &&
                (include_hidden || !m_Arguments[i].m_Hidden) && !added[i])
            {
                has_optional = true;
                break;
            }
        }
        if (has_optional)
        {
            auto &section = formatter.add_section(
                "optional arguments", "", HelpFormatter::Section::Type::Arguments);
            for (std::size_t i = 0; i < m_Arguments.size(); ++i)
            {
                if (m_Arguments[i].m_Kind == Argument::Kind::Optional &&
                    (include_hidden || !m_Arguments[i].m_Hidden) && !added[i])
                {
                    add_item(section, m_Arguments[i]);
                    added[i] = true;
                }
            }
        }

        bool has_positional = false;
        for (std::size_t i = 0; i < m_Arguments.size(); ++i)
        {
            if (m_Arguments[i].m_Kind == Argument::Kind::Positional &&
                (include_hidden || !m_Arguments[i].m_Hidden) && !added[i])
            {
                has_positional = true;
                break;
            }
        }
        if (has_positional)
        {
            auto &section = formatter.add_section(
                "positional arguments", "", HelpFormatter::Section::Type::Arguments);
            for (std::size_t i = 0; i < m_Arguments.size(); ++i)
            {
                if (m_Arguments[i].m_Kind == Argument::Kind::Positional &&
                    (include_hidden || !m_Arguments[i].m_Hidden) && !added[i])
                {
                    add_item(section, m_Arguments[i]);
                    added[i] = true;
                }
            }
        }

        if (!m_Subcommands.empty())
        {
            auto &section = formatter.add_section(
                "subcommands", "", HelpFormatter::Section::Type::Arguments);
            for (const auto &name : m_SubcommandOrder)
            {
                const auto iterator = m_Subcommands.find(name);
                if (iterator != m_Subcommands.end())
                {
                    section.add_item(subcommand_display(name), "",
                                     iterator->second->description(), false);
                }
            }
        }

        return formatter.format_help();
    }

    inline std::string ArgumentParser::format_usage() const
    {
        HelpFormatter formatter = m_HelpFormatter;
        formatter.program(m_Program)
            .usage(m_Usage.empty() ? synthesize_usage() : m_Usage);
        return formatter.format_usage();
    }

    inline void ArgumentParser::print_help(std::ostream &stream) const
    {
        stream << format_help();
    }

    inline void ArgumentParser::print_usage(std::ostream &stream) const
    {
        stream << format_usage();
    }

    inline std::string ArgumentParser::format_error(
        const ArgumentError &error) const
    {
        std::string output = "error: ";
        output += error.what();
        output += "\n";
        output += format_usage();
        return output;
    }

    template <typename... Names>
    inline ArgumentParser::Argument &ArgumentParser::add_argument(
        const Names &...names)
    {
        std::vector<std::string> values{names...};
        if (values.empty())
        {
            throw ArgumentError("add_argument requires at least one name",
                                ErrorCode::InvalidRegistration);
        }

        Argument argument;
        const bool optional =
            !values.front().empty() && values.front().front() == '-';

        if (optional)
        {
            argument.m_Kind = Argument::Kind::Optional;
            argument.m_OptionNames = values;
            for (const auto &name : values)
            {
                if (name.size() < 2 || name.front() != '-')
                {
                    throw ArgumentError(
                        "optional argument names must start with '-' and be non-empty: " + name,
                        ErrorCode::InvalidRegistration);
                }
                if (name == "--")
                {
                    throw ArgumentError("'--' is reserved and cannot be an option name",
                                        ErrorCode::InvalidRegistration);
                }
                if (name.find('=') != std::string::npos)
                {
                    throw ArgumentError("option names cannot contain '=': " + name,
                                        ErrorCode::InvalidRegistration);
                }
                if (name.rfind("--", 0) != 0 && name.size() != 2)
                {
                    throw ArgumentError(
                        "short option names must be exactly two characters: " + name +
                            " (use --" + name.substr(1) + " for a long option)",
                        ErrorCode::InvalidRegistration);
                }
            }

            std::string dest_candidate;
            for (const auto &name : values)
            {
                if (name.rfind("--", 0) == 0)
                {
                    dest_candidate = name.substr(2);
                }
            }
            if (dest_candidate.empty())
            {
                dest_candidate = values.front().substr(1);
            }
            argument.m_Dest = Argument::sanitize_dest(dest_candidate);
            if (argument.m_Dest.empty())
            {
                throw ArgumentError("option name produces an empty destination",
                                    ErrorCode::InvalidRegistration);
            }

            for (const auto &name : values)
            {
                if (m_OptionLookup.find(name) != m_OptionLookup.end())
                {
                    throw ArgumentError("duplicate option: " + name,
                                        ErrorCode::InvalidRegistration);
                }
            }
        }
        else
        {
            if (values.size() != 1)
            {
                throw ArgumentError(
                    "positional arguments only accept one name",
                    ErrorCode::InvalidRegistration);
            }
            if (values.front().empty())
            {
                throw ArgumentError("argument names cannot be empty",
                                    ErrorCode::InvalidRegistration);
            }
            argument.m_Kind = Argument::Kind::Positional;
            argument.m_Dest = values.front();
            argument.m_Required = true;
        }

        for (const auto &existing : m_Arguments)
        {
            if (existing.m_Dest == argument.m_Dest)
            {
                throw ArgumentError(
                    "duplicate destination '" + argument.m_Dest + "'",
                    ErrorCode::InvalidRegistration);
            }
        }

        m_Arguments.push_back(std::move(argument));
        const std::size_t index = m_Arguments.size() - 1;

        if (m_Arguments[index].m_Kind == Argument::Kind::Optional)
        {
            for (const auto &name : m_Arguments[index].m_OptionNames)
            {
                m_OptionLookup[name] = index;
            }
        }
        else
        {
            m_Positionals.push_back(index);
        }

        return m_Arguments[index];
    }

    inline ArgumentParser &ArgumentParser::add_subcommand(
        const std::string &name, const std::string &description)
    {
        if (name.empty() || name.front() == '-')
        {
            throw ArgumentError(
                "subcommand must be a non-empty token not starting with '-': " + name,
                ErrorCode::InvalidRegistration);
        }

        const auto alias = m_SubcommandAliases.find(name);
        if (alias != m_SubcommandAliases.end())
        {
            return *m_Subcommands.at(alias->second);
        }

        auto iterator = m_Subcommands.find(name);
        if (iterator == m_Subcommands.end())
        {
            auto parser = std::make_unique<ArgumentParser>(name, description, "");
            iterator = m_Subcommands.emplace(name, std::move(parser)).first;
            m_SubcommandOrder.push_back(name);
        }
        else
        {
            iterator->second->description(description);
        }
        return *iterator->second;
    }

    inline ArgumentParser &ArgumentParser::add_subcommand(
        std::initializer_list<std::string> names, const std::string &description)
    {
        if (names.size() == 0)
        {
            throw ArgumentError("add_subcommand requires at least one name",
                                ErrorCode::InvalidRegistration);
        }

        auto iterator = names.begin();
        const std::string canonical = *iterator;
        ArgumentParser &parser = add_subcommand(canonical, description);
        ++iterator;
        for (; iterator != names.end(); ++iterator)
        {
            add_subcommand_alias(*iterator, canonical);
        }
        return parser;
    }

    inline ArgumentParser &ArgumentParser::add_subcommand_alias(
        const std::string &alias, const std::string &canonical)
    {
        if (alias.empty() || alias.front() == '-')
        {
            throw ArgumentError(
                "subcommand alias must be a non-empty token not starting with '-': " + alias,
                ErrorCode::InvalidRegistration);
        }
        if (m_Subcommands.find(canonical) == m_Subcommands.end())
        {
            throw ArgumentError("unknown subcommand: " + canonical,
                                ErrorCode::InvalidRegistration);
        }
        if (m_Subcommands.find(alias) != m_Subcommands.end() ||
            m_SubcommandAliases.find(alias) != m_SubcommandAliases.end())
        {
            throw ArgumentError("duplicate subcommand or alias: " + alias,
                                ErrorCode::InvalidRegistration);
        }
        m_SubcommandAliases[alias] = canonical;
        return *m_Subcommands.at(canonical);
    }

    inline ArgumentParser &ArgumentParser::require_subcommand(bool value)
    {
        m_RequireSubcommand = value;
        return *this;
    }

    inline ArgumentParser &ArgumentParser::default_subcommand(std::string name)
    {
        m_DefaultSubcommand = std::move(name);
        return *this;
    }

    inline ArgumentParser *ArgumentParser::find_subcommand(
        const std::string &token, std::string &canonical)
    {
        auto iterator = m_Subcommands.find(token);
        if (iterator != m_Subcommands.end())
        {
            canonical = token;
            return iterator->second.get();
        }
        auto alias = m_SubcommandAliases.find(token);
        if (alias != m_SubcommandAliases.end())
        {
            canonical = alias->second;
            return m_Subcommands.at(alias->second).get();
        }
        return nullptr;
    }

    inline const ArgumentParser::Namespace &ArgumentParser::parse_args(
        int argc, char **argv)
    {
        parse_impl(argc, argv, false);
        return m_Namespace;
    }

    inline const ArgumentParser::Namespace &ArgumentParser::parse_known_args(
        int argc, char **argv)
    {
        parse_impl(argc, argv, true);
        return m_Namespace;
    }

    inline ArgumentParser::ParseResult ArgumentParser::try_parse_args(
        int argc, char **argv)
    {
        try
        {
            const Namespace &result = parse_args(argc, argv);
            return ParseResult(&result, ErrorCode::None, "");
        }
        catch (const HelpError &error)
        {
            m_HelpShowHidden = error.show_hidden();
            return ParseResult(nullptr, ErrorCode::HelpRequested, error.what());
        }
        catch (const ArgumentError &error)
        {
            return ParseResult(nullptr, error.code(), error.what());
        }
        catch (const std::exception &error)
        {
            return ParseResult(nullptr, ErrorCode::Io, error.what());
        }
    }

    inline ArgumentParser::ParseResult ArgumentParser::try_parse_known_args(
        int argc, char **argv)
    {
        try
        {
            const Namespace &result = parse_known_args(argc, argv);
            return ParseResult(&result, ErrorCode::None, "");
        }
        catch (const HelpError &error)
        {
            m_HelpShowHidden = error.show_hidden();
            return ParseResult(nullptr, ErrorCode::HelpRequested, error.what());
        }
        catch (const ArgumentError &error)
        {
            return ParseResult(nullptr, error.code(), error.what());
        }
        catch (const std::exception &error)
        {
            return ParseResult(nullptr, ErrorCode::Io, error.what());
        }
    }

    inline const std::vector<std::string> &ArgumentParser::unknown_args() const
    {
        return m_UnknownArgs;
    }

    inline const std::vector<std::string> &ArgumentParser::unknown_options() const
    {
        return m_UnknownOptions;
    }

    inline const std::vector<std::string> &ArgumentParser::unknown_positionals() const
    {
        return m_UnknownPositionals;
    }

    inline ArgumentParser::UnknownArguments ArgumentParser::unknown() const
    {
        return UnknownArguments{m_UnknownOptions, m_UnknownPositionals};
    }

    inline bool ArgumentParser::is_negative_number_token(const std::string &token)
    {
        return detail::is_negative_number_token(token);
    }

    inline bool ArgumentParser::should_treat_as_optional(
        const std::string &token) const
    {
        if (token.size() < 2 || token.front() != '-')
        {
            return false;
        }
        if (token == "-")
        {
            return false;
        }
        if (is_negative_number_token(token))
        {
            return false;
        }
        return true;
    }

    inline bool ArgumentParser::is_registered_option_token(
        const std::string &token) const
    {
        if (token.rfind("--", 0) == 0)
        {
            const std::size_t equals = token.find('=');
            const std::string name =
                equals == std::string::npos ? token : token.substr(0, equals);
            return m_OptionLookup.find(name) != m_OptionLookup.end();
        }
        if (token.size() >= 2 && token.front() == '-')
        {
            return m_OptionLookup.find(std::string("-") + token[1]) !=
                   m_OptionLookup.end();
        }
        return false;
    }

    inline bool ArgumentParser::is_value_boundary(const std::string &token,
                                                  const Argument &argument) const
    {
        if (token == "--")
        {
            return true;
        }
        if (argument.m_AllowHyphenValues)
        {
            return false;
        }
        if (!should_treat_as_optional(token))
        {
            return false;
        }
        // Any option-like token terminates value collection. Registered options
        // are the common case; unknown option-like tokens are also treated as a
        // boundary so that values are never silently stolen from the tail.
        return true;
    }

    inline ArgumentParser::Argument *ArgumentParser::try_lookup_option(
        const std::string &name)
    {
        const auto iterator = m_OptionLookup.find(name);
        if (iterator == m_OptionLookup.end())
        {
            return nullptr;
        }
        return &m_Arguments[iterator->second];
    }

    inline const ArgumentParser::Argument *ArgumentParser::try_lookup_option(
        const std::string &name) const
    {
        const auto iterator = m_OptionLookup.find(name);
        if (iterator == m_OptionLookup.end())
        {
            return nullptr;
        }
        return &m_Arguments[iterator->second];
    }

    inline ArgumentParser::Argument *ArgumentParser::find_argument(
        const std::string &name)
    {
        if (auto *option = try_lookup_option(name))
        {
            return option;
        }
        for (auto &argument : m_Arguments)
        {
            if (argument.m_Dest == name)
            {
                return &argument;
            }
        }

        std::string normalized = name;
        if (normalized.rfind("--", 0) == 0)
        {
            normalized.erase(0, 2);
        }
        else if (!normalized.empty() && normalized.front() == '-')
        {
            normalized.erase(0, 1);
        }
        normalized = Argument::sanitize_dest(normalized);
        for (auto &argument : m_Arguments)
        {
            if (argument.m_Dest == normalized)
            {
                return &argument;
            }
        }
        return nullptr;
    }

    inline const ArgumentParser::Argument *ArgumentParser::find_argument(
        const std::string &name) const
    {
        return const_cast<ArgumentParser *>(this)->find_argument(name);
    }

    inline std::string ArgumentParser::suggest_correction(
        const std::string &name) const
    {
        std::string best;
        std::size_t best_distance = 3;
        for (const auto &argument : m_Arguments)
        {
            for (const auto &option : argument.m_OptionNames)
            {
                const std::size_t distance =
                    detail::levenshtein_distance(name, option);
                if (distance < best_distance ||
                    (distance == best_distance && !best.empty() && option < best))
                {
                    best_distance = distance;
                    best = option;
                }
            }
        }
        return best;
    }

    inline std::string ArgumentParser::suggest_subcommand(
        const std::string &name) const
    {
        std::string best;
        std::size_t best_distance = 3;
        auto consider = [&](const std::string &candidate)
        {
            const std::size_t distance =
                detail::levenshtein_distance(name, candidate);
            if (distance < best_distance ||
                (distance == best_distance && !best.empty() && candidate < best))
            {
                best_distance = distance;
                best = candidate;
            }
        };
        for (const auto &subcommand : m_SubcommandOrder)
        {
            consider(subcommand);
        }
        for (const auto &alias : m_SubcommandAliases)
        {
            consider(alias.first);
        }
        return best;
    }

    inline void ArgumentParser::reset_parse_state()
    {
        m_UnknownEntries.clear();
        m_UnknownArgs.clear();
        m_UnknownOptions.clear();
        m_UnknownPositionals.clear();
        m_HelpShowHidden = false;
        m_Namespace.m_Subcommand.clear();
        m_Namespace.m_SubcommandNamespace.reset();
        for (auto &argument : m_Arguments)
        {
            argument.m_Values.clear();
            argument.m_Occurrences = 0;
            argument.m_DefaultApplied = false;
            argument.m_EnvApplied = false;
        }
    }

    inline void ArgumentParser::record_unknown(std::size_t index,
                                               std::string text,
                                               bool positional)
    {
        m_UnknownEntries.push_back({index, std::move(text), positional});
    }

    inline void ArgumentParser::rebuild_unknown()
    {
        std::stable_sort(m_UnknownEntries.begin(), m_UnknownEntries.end(),
                         [](const UnknownEntry &lhs, const UnknownEntry &rhs)
                         { return lhs.index < rhs.index; });
        m_UnknownArgs.clear();
        m_UnknownOptions.clear();
        m_UnknownPositionals.clear();
        for (const auto &entry : m_UnknownEntries)
        {
            m_UnknownArgs.push_back(entry.text);
            if (entry.positional)
            {
                m_UnknownPositionals.push_back(entry.text);
            }
            else
            {
                m_UnknownOptions.push_back(entry.text);
            }
        }
    }

    inline void ArgumentParser::merge_unknown(const ArgumentParser &child,
                                              std::size_t offset)
    {
        for (const auto &entry : child.m_UnknownEntries)
        {
            m_UnknownEntries.push_back(
                {entry.index + offset, entry.text, entry.positional});
        }
    }

    inline void ArgumentParser::apply_action(
        Argument &argument, const std::vector<std::string> &values)
    {
        ++argument.m_Occurrences;

        switch (argument.m_Action)
        {
        case Argument::ActionType::Help:
            m_HelpShowHidden = false;
            throw HelpError(false);
        case Argument::ActionType::HelpAll:
            m_HelpShowHidden = true;
            throw HelpError(true);
        case Argument::ActionType::StoreTrue:
            argument.m_Values = {"true"};
            return;
        case Argument::ActionType::StoreFalse:
            argument.m_Values = {"false"};
            return;
        case Argument::ActionType::Count:
            argument.m_Values = {std::to_string(argument.m_Occurrences)};
            return;
        case Argument::ActionType::Append:
            for (const auto &value : values)
            {
                argument.m_Values.push_back(value);
            }
            return;
        case Argument::ActionType::Store:
            argument.m_Values = values;
            return;
        }
    }

    inline std::vector<std::string> ArgumentParser::collect_option_values(
        Argument &argument, int &index, int argc, char **argv,
        std::optional<std::string> first)
    {
        std::vector<std::string> values;

        if (!argument.takes_value())
        {
            if (first.has_value())
            {
                throw ParseError(
                    "argument '" + usage_display(argument) +
                        "' does not take a value",
                    ErrorCode::InvalidValue);
            }
            return values;
        }

        if (first.has_value())
        {
            values.push_back(*first);
        }

        const std::size_t minimum = argument.m_Nargs.min();
        const std::size_t maximum = argument.m_Nargs.max();

        while (values.size() < minimum)
        {
            if (index + 1 >= argc)
            {
                throw RequiredError("argument requires a value: " +
                                        usage_display(argument),
                                    ErrorCode::MissingValue);
            }
            const std::string next = argv[index + 1];
            if (is_value_boundary(next, argument))
            {
                throw RequiredError("argument requires a value: " +
                                        usage_display(argument),
                                    ErrorCode::MissingValue);
            }
            ++index;
            values.push_back(next);
        }

        while (values.size() < maximum && index + 1 < argc)
        {
            const std::string next = argv[index + 1];
            if (is_value_boundary(next, argument))
            {
                break;
            }
            ++index;
            values.push_back(next);
        }

        if (values.empty() && argument.m_ImplicitValue.has_value())
        {
            values.push_back(*argument.m_ImplicitValue);
        }

        if (values.size() < minimum)
        {
            throw RequiredError("argument requires more values: " +
                                    usage_display(argument),
                                ErrorCode::MissingValue);
        }

        return values;
    }

    inline void ArgumentParser::consume_long_option(
        const std::string &token, int &index, int argc, char **argv,
        bool allow_unknown)
    {
        const std::size_t equals = token.find('=');
        const std::string name = equals == std::string::npos
                                     ? token
                                     : token.substr(0, equals);
        const std::optional<std::string> inline_value =
            equals == std::string::npos
                ? std::nullopt
                : std::optional<std::string>(token.substr(equals + 1));

        Argument *argument = try_lookup_option(name);
        if (argument == nullptr)
        {
            if (allow_unknown)
            {
                record_unknown(static_cast<std::size_t>(index), token, false);
                if (!inline_value.has_value() && m_Positionals.empty() &&
                    index + 1 < argc)
                {
                    const std::string next = argv[index + 1];
                    if (next != "--" && !should_treat_as_optional(next))
                    {
                        ++index;
                        record_unknown(static_cast<std::size_t>(index), next, false);
                    }
                }
                return;
            }

            std::string message = "unrecognized argument: " + name;
            const std::string correction = suggest_correction(name);
            if (!correction.empty())
            {
                message += ". Did you mean " + correction + "?";
            }
            throw ParseError(message, ErrorCode::UnknownOption);
        }

        const std::vector<std::string> values =
            collect_option_values(*argument, index, argc, argv, inline_value);
        apply_action(*argument, values);
    }

    inline void ArgumentParser::consume_short_cluster(
        const std::string &token, int &index, int argc, char **argv,
        bool allow_unknown)
    {
        for (std::size_t position = 1; position < token.size(); ++position)
        {
            const std::string name = std::string("-") + token[position];
            Argument *argument = try_lookup_option(name);
            if (argument == nullptr)
            {
                if (allow_unknown)
                {
                    record_unknown(static_cast<std::size_t>(index),
                                   "-" + token.substr(position), false);
                    return;
                }
                std::string message = "unrecognized argument: " + name;
                const std::string correction = suggest_correction(name);
                if (!correction.empty())
                {
                    message += ". Did you mean " + correction + "?";
                }
                throw ParseError(message, ErrorCode::UnknownOption);
            }

            if (!argument->takes_value())
            {
                if (position + 1 < token.size() && token[position + 1] == '=')
                {
                    throw ParseError(
                        "argument '" + name + "' does not take a value",
                        ErrorCode::InvalidValue);
                }
                apply_action(*argument, {});
                continue;
            }

            std::string remainder = token.substr(position + 1);
            if (!remainder.empty() && remainder.front() == '=')
            {
                remainder.erase(0, 1);
            }
            const std::optional<std::string> inline_value =
                remainder.empty() ? std::nullopt
                                  : std::optional<std::string>(remainder);
            const std::vector<std::string> values =
                collect_option_values(*argument, index, argc, argv, inline_value);
            apply_action(*argument, values);
            return;
        }
    }

    inline void ArgumentParser::assign_positionals(
        const std::vector<PositionalToken> &tokens, bool allow_unknown)
    {
        const auto effective_minimum = [](const Argument &argument)
        {
            if (!argument.m_Required || !argument.m_DefaultValues.empty() ||
                argument.m_EnvVar.has_value())
            {
                return std::size_t{0};
            }
            return argument.m_Nargs.min();
        };

        std::size_t cursor = 0;
        for (std::size_t p = 0; p < m_Positionals.size(); ++p)
        {
            Argument &argument = m_Arguments[m_Positionals[p]];
            const std::size_t remaining = tokens.size() - cursor;

            std::size_t reserve = 0;
            for (std::size_t j = p + 1; j < m_Positionals.size(); ++j)
            {
                reserve += effective_minimum(m_Arguments[m_Positionals[j]]);
            }

            const std::size_t minimum = effective_minimum(argument);
            if (remaining < reserve + minimum)
            {
                throw RequiredError("missing positional argument: " +
                                    argument_display(argument));
            }

            std::size_t maximum = remaining - reserve;
            if (!argument.is_unbounded())
            {
                maximum = (std::min)(maximum, argument.m_Nargs.max());
            }

            const std::size_t take = maximum;
            if (take < argument.m_Nargs.min() && minimum > 0)
            {
                throw RequiredError("missing positional argument: " +
                                    argument_display(argument));
            }

            for (std::size_t n = 0; n < take; ++n)
            {
                argument.m_Values.push_back(tokens[cursor + n].value);
            }
            if (take > 0)
            {
                argument.m_Occurrences = 1;
            }
            cursor += take;
        }

        if (cursor < tokens.size())
        {
            if (allow_unknown)
            {
                for (; cursor < tokens.size(); ++cursor)
                {
                    record_unknown(tokens[cursor].index, tokens[cursor].value, true);
                }
                return;
            }
            throw ParseError("unrecognized positional argument: " +
                                 tokens[cursor].value,
                             ErrorCode::UnknownPositional);
        }

        for (std::size_t index : m_Positionals)
        {
            const Argument &argument = m_Arguments[index];
            if (argument.m_Occurrences == 0 && argument.m_Required &&
                argument.m_DefaultValues.empty() &&
                !argument.m_EnvVar.has_value())
            {
                throw RequiredError("missing positional argument: " +
                                    argument_display(argument));
            }
        }
    }

    inline void ArgumentParser::parse_impl(int argc, char **argv,
                                           bool allow_unknown)
    {
        reset_parse_state();

        if (m_Program.empty() && argc > 0 && argv != nullptr &&
            argv[0] != nullptr)
        {
            m_Program = argv[0];
        }

        std::vector<PositionalToken> positional_tokens;
        int subcommand_index = -1;
        std::string subcommand_name;
        ArgumentParser *subparser = nullptr;

        int index = 1;
        while (index < argc)
        {
            const std::string token = argv[index];
            if (token == "--")
            {
                ++index;
                while (index < argc)
                {
                    positional_tokens.push_back(
                        {static_cast<std::size_t>(index), argv[index]});
                    ++index;
                }
                break;
            }

            if (should_treat_as_optional(token))
            {
                if (token.rfind("--", 0) == 0)
                {
                    consume_long_option(token, index, argc, argv, allow_unknown);
                }
                else
                {
                    consume_short_cluster(token, index, argc, argv, allow_unknown);
                }
                ++index;
                continue;
            }

            if (!m_Subcommands.empty())
            {
                std::string canonical;
                if (ArgumentParser *found = find_subcommand(token, canonical))
                {
                    subcommand_index = index;
                    subcommand_name = canonical;
                    subparser = found;
                    break;
                }

                if (m_Positionals.empty())
                {
                    // A default subcommand receives every token that is not a
                    // parent option or a registered subcommand, including
                    // positionals belonging to the child.
                    if (!m_DefaultSubcommand.empty())
                    {
                        std::string canonical;
                        ArgumentParser *fallback =
                            find_subcommand(m_DefaultSubcommand, canonical);
                        if (fallback == nullptr)
                        {
                            throw UsageError("unknown default subcommand: " +
                                             m_DefaultSubcommand);
                        }
                        subcommand_index = index - 1;
                        subcommand_name = canonical;
                        subparser = fallback;
                        break;
                    }

                    std::string message = "unknown subcommand: " + token;
                    const std::string correction = suggest_subcommand(token);
                    if (!correction.empty())
                    {
                        message += ". Did you mean " + correction + "?";
                    }
                    throw UnknownSubcommandError(token, message);
                }
            }

            positional_tokens.push_back(
                {static_cast<std::size_t>(index), token});
            ++index;
        }

        assign_positionals(positional_tokens, allow_unknown);
        rebuild_unknown();
        finalize_and_validate();
        build_namespace();

        const auto dispatch = [&](ArgumentParser &child,
                                  const std::string &canonical,
                                  int first_child_index)
        {
            child.m_Program = m_Program.empty()
                                  ? canonical
                                  : m_Program + " " + canonical;
            child.m_HelpFormatter.program(child.m_Program);

            std::vector<std::string> tokens;
            tokens.reserve(static_cast<std::size_t>(
                (argc - first_child_index) + 1));
            tokens.push_back(child.m_Program);
            for (int j = first_child_index; j < argc; ++j)
            {
                tokens.push_back(argv[j]);
            }

            std::vector<char *> pointers;
            pointers.reserve(tokens.size());
            for (auto &token : tokens)
            {
                pointers.push_back(const_cast<char *>(token.c_str()));
            }

            try
            {
                const Namespace &child_namespace =
                    allow_unknown
                        ? child.parse_known_args(
                              static_cast<int>(pointers.size()), pointers.data())
                        : child.parse_args(
                              static_cast<int>(pointers.size()), pointers.data());

                m_Namespace.m_Subcommand = canonical;
                m_Namespace.m_SubcommandNamespace =
                    std::make_unique<Namespace>(child_namespace);

                if (allow_unknown)
                {
                    const std::size_t offset =
                        first_child_index > 0
                            ? static_cast<std::size_t>(first_child_index - 1)
                            : 0;
                    merge_unknown(child, offset);
                    rebuild_unknown();
                }
            }
            catch (const HelpError &error)
            {
                m_HelpShowHidden = error.show_hidden();
                throw;
            }
        };

        if (subparser != nullptr)
        {
            dispatch(*subparser, subcommand_name, subcommand_index + 1);
            return;
        }

        if (!m_DefaultSubcommand.empty())
        {
            std::string canonical;
            ArgumentParser *fallback =
                find_subcommand(m_DefaultSubcommand, canonical);
            if (fallback == nullptr)
            {
                throw UsageError("unknown default subcommand: " +
                                 m_DefaultSubcommand);
            }
            dispatch(*fallback, canonical, argc);
            return;
        }

        if (m_RequireSubcommand)
        {
            std::string message = "a subcommand is required";
            if (!m_SubcommandOrder.empty())
            {
                message += " (choose from ";
                for (std::size_t i = 0; i < m_SubcommandOrder.size(); ++i)
                {
                    if (i != 0)
                    {
                        message += ", ";
                    }
                    message += m_SubcommandOrder[i];
                }
                message += ")";
            }
            throw RequiredError(message);
        }
    }

    inline bool ArgumentParser::argument_supplied(const Argument &argument) const
    {
        return argument.m_Occurrences > 0 || argument.m_DefaultApplied ||
               argument.m_EnvApplied;
    }

    inline bool ArgumentParser::argument_has_effective_value(
        const Argument &argument) const
    {
        return argument.m_Occurrences > 0 || !argument.m_Values.empty();
    }

    inline void ArgumentParser::finalize_and_validate()
    {
        // 1. Value precedence: CLI > environment > explicit default > action
        // default. Implicit values are only applied while consuming an option.
        for (auto &argument : m_Arguments)
        {
            if (argument.m_Occurrences != 0 || !argument.m_Values.empty())
            {
                continue;
            }

            if (argument.m_EnvVar.has_value())
            {
                const std::string variable = m_EnvPrefix + *argument.m_EnvVar;
                const char *value = std::getenv(variable.c_str());
                if (value != nullptr)
                {
                    argument.m_Values = {std::string(value)};
                    argument.m_EnvApplied = true;
                }
            }

            if (argument.m_Values.empty() && !argument.m_DefaultValues.empty())
            {
                argument.m_Values = argument.m_DefaultValues;
                argument.m_DefaultApplied = true;
            }

            if (argument.m_Values.empty())
            {
                switch (argument.m_Action)
                {
                case Argument::ActionType::StoreTrue:
                    argument.m_Values = {"false"};
                    break;
                case Argument::ActionType::StoreFalse:
                    argument.m_Values = {"true"};
                    break;
                case Argument::ActionType::Count:
                    argument.m_Values = {"0"};
                    break;
                default:
                    break;
                }
            }
        }

        // 2. Group cardinality. Groups count explicit command line
        // occurrences; defaults and environment values do not satisfy them.
        for (const auto &group : m_Groups)
        {
            std::vector<std::string> supplied;
            for (std::size_t index : group.m_ArgumentIndices)
            {
                if (m_Arguments[index].m_Occurrences > 0)
                {
                    supplied.push_back(argument_display(m_Arguments[index]));
                }
            }

            const std::string title = group.m_Title.empty() ? "arguments"
                                                            : group.m_Title;
            if (group.m_Cardinality == GroupCardinality::AtMostOne &&
                supplied.size() > 1)
            {
                std::string message =
                    "arguments in group '" + title + "' are mutually exclusive: ";
                for (std::size_t i = 0; i < supplied.size(); ++i)
                {
                    if (i != 0)
                    {
                        message += ", ";
                    }
                    message += supplied[i];
                }
                throw ArgumentError(message, ErrorCode::MutuallyExclusive);
            }
            if (group.m_Cardinality == GroupCardinality::ExactlyOne)
            {
                if (supplied.size() > 1)
                {
                    std::string message =
                        "exactly one argument from group '" + title +
                        "' may be used: ";
                    for (std::size_t i = 0; i < supplied.size(); ++i)
                    {
                        if (i != 0)
                        {
                            message += ", ";
                        }
                        message += supplied[i];
                    }
                    throw ArgumentError(message, ErrorCode::MutuallyExclusive);
                }
                if (supplied.empty())
                {
                    throw RequiredError("exactly one argument from group '" +
                                        title + "' is required");
                }
            }
            if (group.m_Cardinality == GroupCardinality::AtLeastOne &&
                supplied.empty())
            {
                throw RequiredError("at least one argument from group '" +
                                    title + "' is required");
            }
        }

        // 3. Required arguments. Default and environment values satisfy the
        // requirement; a configured default is a supported fallback.
        for (auto &argument : m_Arguments)
        {
            if (!argument.m_Required)
            {
                continue;
            }
            if (argument.m_Values.empty())
            {
                if (m_InteractiveHandler)
                {
                    const auto value = m_InteractiveHandler(argument);
                    if (value.has_value() && !value->empty())
                    {
                        argument.m_Values = {*value};
                        argument.m_Occurrences = 1;
                    }
                }
                if (argument.m_Values.empty())
                {
                    throw RequiredError("required argument missing: " +
                                        argument_display(argument));
                }
            }
        }

        // 4. Choices.
        for (const auto &argument : m_Arguments)
        {
            if (argument.m_Choices.empty())
            {
                continue;
            }
            for (const auto &value : argument.m_Values)
            {
                if (std::find(argument.m_Choices.begin(),
                              argument.m_Choices.end(),
                              value) == argument.m_Choices.end())
                {
                    std::string message =
                        "invalid choice for argument '" +
                        argument_display(argument) + "': " + value +
                        " (choose from ";
                    for (std::size_t i = 0; i < argument.m_Choices.size(); ++i)
                    {
                        if (i != 0)
                        {
                            message += ", ";
                        }
                        message += argument.m_Choices[i];
                    }
                    message += ")";
                    throw ArgumentError(message, ErrorCode::InvalidChoice);
                }
            }
        }

        // 5. Validators. Multiple validators compose with AND. Exceptions from
        // user callbacks are converted into ArgumentError diagnostics.
        for (const auto &argument : m_Arguments)
        {
            if (argument.m_Validators.empty())
            {
                continue;
            }
            for (const auto &value : argument.m_Values)
            {
                for (const auto &validator : argument.m_Validators)
                {
                    bool valid = false;
                    try
                    {
                        valid = validator.callback(value);
                    }
                    catch (const ArgumentError &)
                    {
                        throw;
                    }
                    catch (const std::exception &error)
                    {
                        throw ArgumentError(
                            "validator for '" + argument_display(argument) +
                                "' threw: " + error.what(),
                            ErrorCode::InvalidValue);
                    }
                    catch (...)
                    {
                        throw ArgumentError(
                            "validator for '" + argument_display(argument) +
                                "' threw an unknown exception",
                            ErrorCode::InvalidValue);
                    }

                    if (!valid)
                    {
                        const std::string message =
                            validator.message.empty()
                                ? "invalid value for argument '" +
                                      argument_display(argument) + "': " + value
                                : validator.message;
                        throw ArgumentError(message, ErrorCode::InvalidValue);
                    }
                }
            }
        }

        // 6. Dependencies, conflicts, and all-of requirements.
        for (const auto &argument : m_Arguments)
        {
            if (argument.m_DependsOn.has_value())
            {
                const Argument *dependency =
                    find_argument(*argument.m_DependsOn);
                if (dependency == nullptr)
                {
                    throw UsageError("unknown dependency '" +
                                     *argument.m_DependsOn +
                                     "' for argument '" +
                                     argument_display(argument) + "'");
                }
                if (argument_supplied(*dependency) &&
                    !argument_supplied(argument))
                {
                    throw RequiredIfError(
                        "argument '" + argument_display(argument) +
                        "' is required when '" +
                        argument_display(*dependency) + "' is present");
                }
            }

            if (!argument.m_RequiresAll.empty() &&
                argument_supplied(argument))
            {
                for (const auto &name : argument.m_RequiresAll)
                {
                    const Argument *dependency = find_argument(name);
                    if (dependency == nullptr)
                    {
                        throw UsageError("unknown dependency '" + name +
                                         "' for argument '" +
                                         argument_display(argument) + "'");
                    }
                    if (!argument_supplied(*dependency))
                    {
                        throw RequiredIfError(
                            "argument '" + argument_display(argument) +
                            "' requires '" + argument_display(*dependency) +
                            "'");
                    }
                }
            }

            if (argument_supplied(argument))
            {
                for (const auto &name : argument.m_ConflictsWith)
                {
                    const Argument *conflict = find_argument(name);
                    if (conflict == nullptr)
                    {
                        throw UsageError("unknown conflict '" + name +
                                         "' for argument '" +
                                         argument_display(argument) + "'");
                    }
                    if (argument_supplied(*conflict))
                    {
                        throw ArgumentError(
                            "argument '" + argument_display(argument) +
                                "' cannot be used together with '" +
                                argument_display(*conflict) + "'",
                            ErrorCode::MutuallyExclusive);
                    }
                }
            }
        }
    }

    inline void ArgumentParser::build_namespace()
    {
        m_Namespace.m_Data.clear();
        m_Namespace.m_Provided.clear();
        for (const auto &argument : m_Arguments)
        {
            m_Namespace.m_Data[argument.m_Dest] = argument.m_Values;
            m_Namespace.m_Provided[argument.m_Dest] =
                argument.m_Occurrences > 0;
        }
    }

    //-----------------------------------------------------------------------------
    // [SECTION] Shell completion
    //-----------------------------------------------------------------------------

    inline std::string ArgumentParser::generate_completion_script(
        Shell shell) const
    {
        std::string identifier;
        for (char c : m_Program)
        {
            identifier += std::isalnum(static_cast<unsigned char>(c)) || c == '_'
                              ? c
                              : '_';
        }
        if (identifier.empty())
        {
            identifier = "argparse";
        }

        std::vector<std::string> options;
        std::vector<std::string> subcommands;
        for (const auto &argument : m_Arguments)
        {
            if (argument.m_Kind != Argument::Kind::Optional || argument.m_Hidden)
            {
                continue;
            }
            for (const auto &name : argument.m_OptionNames)
            {
                options.push_back(name);
            }
        }
        for (const auto &name : m_SubcommandOrder)
        {
            subcommands.push_back(name);
        }

        const auto join = [](const std::vector<std::string> &values)
        {
            std::ostringstream stream;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i != 0)
                {
                    stream << " ";
                }
                stream << values[i];
            }
            return stream.str();
        };

        const auto escape_single = [](const std::string &text)
        {
            std::string result;
            for (char c : text)
            {
                if (c == '\'')
                {
                    result += "'\\''";
                }
                else
                {
                    result += c;
                }
            }
            return result;
        };

        std::ostringstream stream;
        switch (shell)
        {
        case Shell::Bash:
        {
            stream << "_" << identifier << "_completion() {\n";
            stream << "  local cur prev opts\n";
            stream << "  COMPREPLY=()\n";
            stream << "  cur=\"${COMP_WORDS[COMP_CWORD]}\"\n";
            stream << "  prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n";
            stream << "  opts=\"" << join(options) << "\"\n";
            stream << "  case \"$prev\" in\n";
            for (const auto &argument : m_Arguments)
            {
                if (argument.m_Kind != Argument::Kind::Optional ||
                    argument.m_Hidden || argument.m_Choices.empty() ||
                    !argument.takes_value())
                {
                    continue;
                }
                stream << "    ";
                for (std::size_t i = 0; i < argument.m_OptionNames.size(); ++i)
                {
                    if (i != 0)
                    {
                        stream << "|";
                    }
                    stream << argument.m_OptionNames[i];
                }
                stream << ") COMPREPLY=( $(compgen -W \""
                       << join(argument.m_Choices) << "\" -- \"$cur\") ); return 0 ;;\n";
            }
            stream << "  esac\n";
            stream << "  if [[ \"$cur\" == -* ]]; then\n";
            stream << "    COMPREPLY=( $(compgen -W \"$opts\" -- \"$cur\") )\n";
            stream << "    return 0\n";
            stream << "  fi\n";
            stream << "  COMPREPLY=( $(compgen -W \"" << join(subcommands)
                   << "\" -- \"$cur\") )\n";
            stream << "}\n";
            stream << "complete -F _" << identifier << "_completion "
                   << m_Program << "\n";
            break;
        }
        case Shell::Zsh:
        {
            stream << "#compdef " << m_Program << "\n\n";
            stream << "_" << identifier << "() {\n";
            stream << "  _arguments \\\n";
            for (const auto &argument : m_Arguments)
            {
                if (argument.m_Kind != Argument::Kind::Optional ||
                    argument.m_Hidden)
                {
                    continue;
                }
                const std::string help = escape_single(argument.m_Help);
                for (const auto &name : argument.m_OptionNames)
                {
                    stream << "    '" << name << "[" << help << "]";
                    if (argument.takes_value())
                    {
                        if (!argument.m_Choices.empty())
                        {
                            stream << ":" << argument.m_Dest << ":("
                                   << join(argument.m_Choices) << ")";
                        }
                        else
                        {
                            stream << ":" << argument.m_Dest << ":";
                        }
                    }
                    stream << "' \\\n";
                }
            }
            for (const auto &name : m_SubcommandOrder)
            {
                stream << "    '" << name << ":"
                       << escape_single(m_Subcommands.at(name)->description())
                       << "' \\\n";
            }
            stream << "    '*::argument:'\n";
            stream << "}\n\n";
            stream << "_" << identifier << " \"$@\"\n";
            break;
        }
        case Shell::Fish:
        {
            for (const auto &argument : m_Arguments)
            {
                if (argument.m_Kind != Argument::Kind::Optional ||
                    argument.m_Hidden)
                {
                    continue;
                }
                stream << "complete -c " << m_Program;
                for (const auto &name : argument.m_OptionNames)
                {
                    if (name.rfind("--", 0) == 0)
                    {
                        stream << " -l " << name.substr(2);
                    }
                    else
                    {
                        stream << " -s " << name.substr(1);
                    }
                }
                if (argument.takes_value())
                {
                    stream << " -r";
                }
                if (!argument.m_Choices.empty())
                {
                    stream << " -a \"" << join(argument.m_Choices) << "\"";
                }
                if (!argument.m_Help.empty())
                {
                    stream << " -d \"" << argument.m_Help << "\"";
                }
                stream << "\n";
            }
            if (!subcommands.empty())
            {
                stream << "complete -c " << m_Program << " -f -a \""
                       << join(subcommands) << "\"\n";
            }
            break;
        }
        case Shell::PowerShell:
        {
            stream << "Register-ArgumentCompleter -Native -CommandName '"
                   << m_Program << "' -ScriptBlock {\n";
            stream << "  param($wordToComplete, $commandAst, $cursorPosition)\n";
            stream << "  $options = @(";
            for (std::size_t i = 0; i < options.size(); ++i)
            {
                if (i != 0)
                {
                    stream << ", ";
                }
                stream << "'" << options[i] << "'";
            }
            stream << ")\n";
            stream << "  $subcommands = @(";
            for (std::size_t i = 0; i < subcommands.size(); ++i)
            {
                if (i != 0)
                {
                    stream << ", ";
                }
                stream << "'" << subcommands[i] << "'";
            }
            stream << ")\n";
            stream << "  ($options + $subcommands) | Where-Object { $_ -like \"$wordToComplete*\" } | ForEach-Object {\n";
            stream << "    [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_)\n";
            stream << "  }\n";
            stream << "}\n";
            break;
        }
        }
        return stream.str();
    }

    inline std::string ArgumentParser::generate_completion_script(
        const std::string &shell) const
    {
        const std::string lowered = detail::to_lower(shell);
        if (lowered == "bash")
        {
            return generate_completion_script(Shell::Bash);
        }
        if (lowered == "zsh")
        {
            return generate_completion_script(Shell::Zsh);
        }
        if (lowered == "fish")
        {
            return generate_completion_script(Shell::Fish);
        }
        if (lowered == "powershell" || lowered == "pwsh")
        {
            return generate_completion_script(Shell::PowerShell);
        }
        throw ArgumentError("unsupported completion shell: " + shell,
                            ErrorCode::Usage);
    }

    //-----------------------------------------------------------------------------
    // [SECTION] Namespace definitions
    //-----------------------------------------------------------------------------

    inline ArgumentParser::Namespace::Namespace(const Namespace &other)
        : m_Data(other.m_Data),
          m_Provided(other.m_Provided),
          m_Subcommand(other.m_Subcommand)
    {
        if (other.m_SubcommandNamespace)
        {
            m_SubcommandNamespace =
                std::make_unique<Namespace>(*other.m_SubcommandNamespace);
        }
    }

    inline ArgumentParser::Namespace &ArgumentParser::Namespace::operator=(
        const Namespace &other)
    {
        if (this == &other)
        {
            return *this;
        }
        m_Data = other.m_Data;
        m_Provided = other.m_Provided;
        m_Subcommand = other.m_Subcommand;
        if (other.m_SubcommandNamespace)
        {
            m_SubcommandNamespace =
                std::make_unique<Namespace>(*other.m_SubcommandNamespace);
        }
        else
        {
            m_SubcommandNamespace.reset();
        }
        return *this;
    }

    inline bool ArgumentParser::Namespace::has(const std::string &name) const
    {
        return m_Data.find(name) != m_Data.end();
    }

    inline bool ArgumentParser::Namespace::provided(const std::string &name) const
    {
        const auto iterator = m_Provided.find(name);
        return iterator != m_Provided.end() && iterator->second;
    }

    inline const std::vector<std::string> &
    ArgumentParser::Namespace::raw_values(const std::string &name) const
    {
        const auto iterator = m_Data.find(name);
        if (iterator == m_Data.end())
        {
            throw ParseError("unknown argument: " + name,
                             ErrorCode::UnknownPositional);
        }
        return iterator->second;
    }

    inline bool ArgumentParser::Namespace::has_subcommand() const
    {
        return !m_Subcommand.empty();
    }

    inline const std::string &ArgumentParser::Namespace::subcommand() const
    {
        return m_Subcommand;
    }

    inline const ArgumentParser::Namespace &
    ArgumentParser::Namespace::subcommand_namespace() const
    {
        if (!m_SubcommandNamespace)
        {
            throw ParseError("subcommand namespace not available",
                             ErrorCode::Usage);
        }
        return *m_SubcommandNamespace;
    }

    template <typename T>
    inline T ArgumentParser::Namespace::get(const std::string &name) const
    {
        const std::vector<std::string> &values = raw_values(name);
        if (values.empty())
        {
            throw ArgumentError("argument has no value: " + name,
                                ErrorCode::InvalidValue);
        }

        try
        {
            if constexpr (detail::is_optional<T>::value)
            {
                using U = typename T::value_type;
                return T{convert<U>(values.front())};
            }
            else
            {
                return convert<T>(values.front());
            }
        }
        catch (const ArgumentError &error)
        {
            if (error.code() == ErrorCode::Conversion)
            {
                throw ArgumentError("argument '" + name + "': " + error.what(),
                                    ErrorCode::Conversion);
            }
            throw;
        }
    }

    template <typename T>
    inline std::vector<T> ArgumentParser::Namespace::getlist(
        const std::string &name) const
    {
        const std::vector<std::string> &values = raw_values(name);
        std::vector<T> result;
        result.reserve(values.size());
        for (const auto &value : values)
        {
            try
            {
                result.push_back(convert<T>(value));
            }
            catch (const ArgumentError &error)
            {
                if (error.code() == ErrorCode::Conversion)
                {
                    throw ArgumentError(
                        "argument '" + name + "': " + error.what(),
                        ErrorCode::Conversion);
                }
                throw;
            }
        }
        return result;
    }

    template <typename T>
    inline std::optional<T> ArgumentParser::Namespace::get_if(
        const std::string &name) const
    {
        try
        {
            if constexpr (detail::is_optional<T>::value)
            {
                return get<T>(name);
            }
            else
            {
                const std::vector<std::string> &values = raw_values(name);
                if (values.empty())
                {
                    return std::nullopt;
                }
                return convert<T>(values.front());
            }
        }
        catch (const ArgumentError &)
        {
            return std::nullopt;
        }
        catch (const std::exception &)
        {
            return std::nullopt;
        }
    }

    template <typename T>
    inline T ArgumentParser::Namespace::get_or(const std::string &name,
                                               T fallback) const
    {
        const auto iterator = m_Data.find(name);
        if (iterator == m_Data.end() || iterator->second.empty())
        {
            return fallback;
        }
        const std::vector<std::string> &values = iterator->second;
        try
        {
            return convert<T>(values.front());
        }
        catch (const ArgumentError &error)
        {
            if (error.code() == ErrorCode::Conversion)
            {
                throw ArgumentError("argument '" + name + "': " + error.what(),
                                    ErrorCode::Conversion);
            }
            throw;
        }
    }

    template <typename T>
    inline T ArgumentParser::Namespace::convert(const std::string &value)
    {
        using D = typename std::decay<T>::type;

        if constexpr (std::is_same<D, std::string>::value)
        {
            return value;
        }
        else if constexpr (std::is_same<D, std::string_view>::value)
        {
            return std::string_view(value);
        }
        else if constexpr (std::is_same<D, const char *>::value)
        {
            return value.c_str();
        }
        else if constexpr (std::is_same<D, bool>::value)
        {
            const std::string lowered = detail::to_lower(value);
            if (lowered == "1" || lowered == "true" || lowered == "yes" ||
                lowered == "on")
            {
                return true;
            }
            if (lowered == "0" || lowered == "false" || lowered == "no" ||
                lowered == "off")
            {
                return false;
            }
            throw ArgumentError("cannot convert value '" + value + "' to bool",
                                ErrorCode::Conversion);
        }
        else if constexpr (std::is_same<D, char>::value)
        {
            if (value.size() != 1)
            {
                throw ArgumentError(
                    "cannot convert value '" + value +
                        "' to a character (expected exactly one byte)",
                    ErrorCode::Conversion);
            }
            return value.front();
        }
        else if constexpr (std::is_enum<D>::value)
        {
            using U = typename std::underlying_type<D>::type;
            return static_cast<D>(convert<U>(value));
        }
        else if constexpr (std::is_integral<D>::value)
        {
            D parsed{};
            if (!detail::parse_integral<D>(value, parsed))
            {
                throw ArgumentError(
                    "cannot convert value '" + value + "' to an integer",
                    ErrorCode::Conversion);
            }
            return parsed;
        }
        else if constexpr (std::is_floating_point<D>::value)
        {
            D parsed{};
            if (!detail::parse_floating<D>(value, parsed))
            {
                throw ArgumentError(
                    "cannot convert value '" + value + "' to a number",
                    ErrorCode::Conversion);
            }
            return parsed;
        }
        else
        {
            static_assert(!sizeof(D *), "Unsupported conversion type");
        }
    }

    //-----------------------------------------------------------------------------
    // [SECTION] Argument definitions
    //-----------------------------------------------------------------------------

    inline ArgumentParser::Argument &ArgumentParser::Argument::help(
        const std::string &text)
    {
        m_Help = text;
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::hidden(bool value)
    {
        m_Hidden = value;
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::required(bool value)
    {
        m_Required = value;
        m_RequiredExplicit = true;
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::metavar(
        const std::string &text)
    {
        m_Metavar = text;
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::action(
        const std::string &name)
    {
        if (name == "store")
        {
            m_Action = ActionType::Store;
            m_Nargs = Nargs::exactly(1);
        }
        else if (name == "store_true")
        {
            m_Action = ActionType::StoreTrue;
            m_Nargs = Nargs::none();
        }
        else if (name == "store_false")
        {
            m_Action = ActionType::StoreFalse;
            m_Nargs = Nargs::none();
        }
        else if (name == "append")
        {
            m_Action = ActionType::Append;
            m_Nargs = Nargs::exactly(1);
        }
        else if (name == "count")
        {
            m_Action = ActionType::Count;
            m_Nargs = Nargs::none();
        }
        else if (name == "help")
        {
            m_Action = ActionType::Help;
            m_Nargs = Nargs::none();
        }
        else if (name == "help_all")
        {
            m_Action = ActionType::HelpAll;
            m_Nargs = Nargs::none();
        }
        else
        {
            throw ArgumentError("unknown action: " + name,
                                ErrorCode::InvalidRegistration);
        }
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::nargs(char pattern)
    {
        if (pattern == '?')
        {
            return nargs(Nargs::optional());
        }
        if (pattern == '*')
        {
            return nargs(Nargs::zero_or_more());
        }
        if (pattern == '+')
        {
            return nargs(Nargs::one_or_more());
        }
        throw ArgumentError("unknown nargs pattern: " + std::string(1, pattern),
                            ErrorCode::InvalidRegistration);
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::nargs(Nargs nargs)
    {
        apply_nargs(nargs);
        return *this;
    }

    inline void ArgumentParser::Argument::apply_nargs(Nargs nargs)
    {
        m_Nargs = nargs;
        if (m_Kind == Kind::Positional && !m_RequiredExplicit)
        {
            m_Required = nargs.min() > 0;
        }
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::default_values(
        std::initializer_list<std::string> values)
    {
        m_DefaultValues.assign(values.begin(), values.end());
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::env(
        const std::string &name)
    {
        if (name.empty())
        {
            throw ArgumentError("environment variable name cannot be empty",
                                ErrorCode::InvalidRegistration);
        }
        m_EnvVar = name;
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::choices(
        std::initializer_list<std::string> values)
    {
        m_Choices.assign(values.begin(), values.end());
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::check(
        std::function<bool(const std::string &)> validator,
        const std::string &message)
    {
        if (!validator)
        {
            throw ArgumentError("validator must be callable",
                                ErrorCode::InvalidRegistration);
        }
        m_Validators.push_back({std::move(validator), message});
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::depends_on(
        const std::string &name)
    {
        if (name.empty())
        {
            throw ArgumentError("dependency name cannot be empty",
                                ErrorCode::InvalidRegistration);
        }
        m_DependsOn = name;
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::conflicts_with(
        const std::string &name)
    {
        if (name.empty())
        {
            throw ArgumentError("conflict name cannot be empty",
                                ErrorCode::InvalidRegistration);
        }
        m_ConflictsWith.push_back(name);
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::requires_all(
        std::initializer_list<std::string> names)
    {
        m_RequiresAll.assign(names.begin(), names.end());
        return *this;
    }

    inline ArgumentParser::Argument &ArgumentParser::Argument::allow_hyphen_values(
        bool value)
    {
        m_AllowHyphenValues = value;
        return *this;
    }

    inline const std::string &ArgumentParser::Argument::dest() const
    {
        return m_Dest;
    }

    inline std::string ArgumentParser::Argument::sanitize_dest(
        const std::string &name)
    {
        std::string result;
        result.reserve(name.size());
        for (char c : name)
        {
            result.push_back(c == '-' ? '_' : c);
        }
        return result;
    }

    inline bool ArgumentParser::Argument::takes_value() const
    {
        return m_Nargs.max() > 0;
    }

    inline bool ArgumentParser::Argument::is_unbounded() const
    {
        return m_Nargs.unbounded();
    }

    template <typename... Names>
    inline ArgumentParser::Argument &ArgumentParser::ArgumentGroup::add_argument(
        const Names &...names)
    {
        auto &argument = m_Parser->add_argument(names...);
        m_ArgumentIndices.push_back(m_Parser->m_Arguments.size() - 1);
        return argument;
    }

} // namespace argparse

#endif // DJOEZEKE_ARGPARSE_H

/**
 * LICENSE: MIT License
 *
 * Copyright (c) 2025 Sackey Ezekiel Etrue
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
