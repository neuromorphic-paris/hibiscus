#include "../third_party/hummingbird/third_party/pontella/source/pontella.hpp"
#include "../third_party/sepia/source/sepia.hpp"
#include <iomanip>
#include <sstream>

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "split creates one Event Stream file per clip, and removes frame events",
            "Syntax: ./split [options] /path/to/input.es /path/to/output_####.es",
            "There can be any number of # in the output, and they can be placed anywhere.",
            "This program does not create output directories.",
            "Available options:",
            "    -f, --force    overwrites the output file if they exist",
            "    -q, --quiet    do not output progress",
            "    -h, --help     shows this help message",
        },
        argc,
        argv,
        2,
        {},
        {{"force", {"f"}}, {"quiet", {"q"}}},
        [](pontella::command command) {
            std::string before_sharps;
            std::string after_sharps;
            std::size_t sharps = 0;
            {
                auto sharps_rbegin = command.arguments.back().rbegin();
                auto sharps_rend = command.arguments.back().rend();
                auto sharps_started = false;
                for (auto character_iterator = command.arguments.back().rbegin();
                     character_iterator != command.arguments.back().rend();
                     ++character_iterator) {
                    if (!sharps_started) {
                        if (*character_iterator == '#') {
                            sharps_rbegin = character_iterator;
                            sharps_started = true;
                        }
                    } else if (*character_iterator != '#') {
                        sharps_rend = character_iterator;
                        break;
                    }
                }
                if (sharps_rbegin == command.arguments.back().rend()) {
                    throw std::runtime_error("the input filename does not contain sharps");
                }
                before_sharps = std::string(sharps_rend, command.arguments.back().rend());
                std::reverse(before_sharps.begin(), before_sharps.end());
                after_sharps = std::string(command.arguments.back().rbegin(), sharps_rbegin);
                std::reverse(after_sharps.begin(), after_sharps.end());
                sharps = std::distance(sharps_rbegin, sharps_rend);
            }
            const auto force = command.flags.find("force") != command.flags.end();
            const auto quiet = command.flags.find("quiet") != command.flags.end();
            auto index = std::numeric_limits<std::size_t>::max();
            uint64_t begin_t = 0;
            std::unique_ptr<sepia::write<sepia::type::generic>> write;
            sepia::join_observable<sepia::type::generic>(
                sepia::filename_to_ifstream(command.arguments.front()), [&](sepia::generic_event generic_event) {
                    if (generic_event.bytes.size() == 0) {
                        throw std::runtime_error("empty event");
                    }
                    switch (generic_event.bytes.front()) {
                        case 's': {
                            if (index == std::numeric_limits<std::size_t>::max()) {
                                index = 0;
                            } else {
                                ++index;
                            }
                            std::stringstream stream;
                            stream << before_sharps << std::setfill('0') << std::setw(static_cast<int32_t>(sharps))
                                   << index << after_sharps;
                            auto filename = stream.str();
                            bool exists;
                            {
                                std::ifstream input(filename);
                                exists = input.good();
                            }
                            if (exists && !force) {
                                write.reset();
                                if (!quiet) {
                                    std::cout << stream.str() + " (skipped)\n";
                                    std::cout.flush();
                                }
                            } else {
                                write.reset(
                                    new sepia::write<sepia::type::generic>(sepia::filename_to_ofstream(stream.str())));
                                begin_t = generic_event.t;
                                if (!quiet) {
                                    std::cout << stream.str() + "\n";
                                    std::cout.flush();
                                }
                            }
                            break;
                        }
                        case 'a':
                        case 'b':
                        case 'c':
                        case 'l':
                        case 'r':
                        case 'w':
                            if (write) {
                                generic_event.t -= begin_t;
                                (*write)(generic_event);
                            }
                            break;
                        case 'f':
                            break;
                        default:
                            throw std::runtime_error("unexpected event type");
                    }
                });
        });
}
