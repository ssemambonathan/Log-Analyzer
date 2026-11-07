#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "cli/cli.hpp"
#include "engine/engine.hpp"
#include "export/exporter.hpp"
#include "report/report.hpp"

namespace {

bool stdout_is_terminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

void enable_ansi_on_windows() {
#if defined(_WIN32)
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) != 0) {
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

bool color_enabled(hpla::ColorMode mode, bool writing_to_file) {
    if (mode == hpla::ColorMode::Never || writing_to_file) {
        return false;
    }
    if (mode == hpla::ColorMode::Always) {
        return true;
    }
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    if (const char* term = std::getenv("TERM"); term != nullptr && std::string(term) == "dumb") {
        return false;
    }
    return stdout_is_terminal();
}

}  // namespace

int main(int argc, char** argv) {
    const hpla::CliParseResult parsed = hpla::parse_cli(argc, argv);
    if (!parsed.ok) {
        std::cerr << "hpla: " << parsed.error << "\nTry 'hpla --help' for usage.\n";
        return 2;
    }

    const hpla::CliOptions& options = parsed.options;
    if (options.show_help) {
        std::cout << hpla::usage_text();
        return 0;
    }
    if (options.show_version) {
        std::cout << "hpla " << hpla::kVersion << '\n';
        return 0;
    }

    hpla::install_interrupt_handler();

    try {
        const std::vector<hpla::FileInfo> files = hpla::stat_input_files(options.files);

        hpla::EngineOptions engine;
        engine.threads = options.threads;
        engine.buffer_size = options.buffer_size;
        engine.sequential = options.sequential;
        engine.use_mmap = options.use_mmap;
        engine.top_n = options.top_n;
        engine.max_distinct = options.max_distinct;

        const hpla::AnalysisResult result =
            hpla::LogAnalyzer::run(files, options.filter, engine);

        const bool use_color = color_enabled(options.color, options.output_path.has_value());
        if (use_color) {
            enable_ansi_on_windows();
        }

        const auto write_output = [&](std::ostream& out) {
            switch (options.format) {
                case hpla::OutputFormat::Text:
                    hpla::write_text_report(out, result, files, options.filter, use_color);
                    break;
                case hpla::OutputFormat::Json:
                    hpla::write_json_report(out, result, files, options.filter, engine,
                                            options.include_records);
                    break;
                case hpla::OutputFormat::Csv:
                    hpla::write_csv_report(out, result, files, options.filter, engine,
                                           options.include_records);
                    break;
            }
        };

        if (options.output_path.has_value()) {
            std::ofstream out(*options.output_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                throw std::runtime_error("cannot open output file '" + *options.output_path + "'");
            }
            write_output(out);
            out.flush();
            if (!out) {
                throw std::runtime_error("failed to write output file '" + *options.output_path + "'");
            }
            std::cerr << "Report written to " << *options.output_path << '\n';
        } else {
            write_output(std::cout);
            std::cout.flush();
            if (!std::cout) {
                std::cerr << "hpla: failed to write to standard output\n";
                return 1;
            }
        }

        return result.performance.cancelled ? 130 : 0;
    } catch (const std::exception& error) {
        std::cerr << "hpla: " << error.what() << '\n';
        return 1;
    }
}
