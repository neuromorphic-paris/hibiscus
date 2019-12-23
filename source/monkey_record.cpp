#include "../third_party/hummingbird/source/decoder.hpp"
#include "../third_party/hummingbird/source/display.hpp"
#include "../third_party/hummingbird/source/interleave.hpp"
#include "../third_party/hummingbird/source/lightcrafter.hpp"
#include "../third_party/hummingbird/source/rotate.hpp"
#include "../third_party/hummingbird/third_party/pontella/source/pontella.hpp"
#include "../third_party/sepia/source/sepia.hpp"
#include "teensy.hpp"

/// draw_pattern draws the given binary pattern to the frame, centered at the
/// given position. The frame must be 343 * 342 * 3 bytes long.
inline void draw_pattern(
    std::vector<uint8_t>& frame,
    const uint16_t center_x,
    const uint16_t center_y,
    const std::vector<bool>& pattern,
    const uint16_t pattern_width,
    const std::array<uint8_t, 3> color) {
    for (uint16_t y = 0; y < pattern.size() / pattern_width; ++y) {
        for (uint16_t x = 0; x < pattern_width; ++x) {
            const auto pixel_x = static_cast<int32_t>(center_x) + x - pattern_width / 2;
            const auto pixel_y = static_cast<int32_t>(center_y) + y - pattern.size() / pattern_width / 2;
            if (pattern[x + y * pattern_width] && pixel_x >= 0 && pixel_x < 343 && pixel_y >= 0 && pixel_y < 342) {
                for (uint8_t channel = 0; channel < 3; ++channel) {
                    frame[(pixel_x + pixel_y * 343) * 3 + channel] = color[channel];
                }
            }
        }
    }
}

/// draw_rectangle draws a rectangle in the given frame.
/// The frame must be 343 * 342 * 3 bytes long.
inline void draw_rectangle(std::vector<uint8_t>& frame, const uint16_t center_x, const uint16_t center_y) {
    draw_pattern(frame, center_x, center_y, std::vector<bool>(25, true), 5, {0xff, 0xff, 0xff});
}

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "monkey_record plays a list of stimuli, with phases piloted by an external trigger",
            "Syntax: ./monkey_record [options] /path/to/first/clip.mp4 [/path/to/second/clip.mp4...] output.es",
            "Available options:",
            "    -f, --force                       overwrites the output file if "
            "it exists",
            "    -b [frames], --buffer [frames]    sets the number of frames "
            "buffered",
            "                                          defaults to 64",
            "                                          the smaller the buffer, "
            "the smaller the delay between clips",
            "                                                however, small "
            "buffers increase the risk",
            "                                                to miss frames",
            "    -i [ip], --ip [ip]                sets the LightCrafter IP "
            "address",
            "                                          defaults to 10.10.10.100",
            "    -e, --fake-events                 send fake button pushes periodically",
            "    -h, --help                            shows this help message",
        },
        argc,
        argv,
        -1,
        {{"buffer", {"b"}}, {"ip", {"i"}}},
        {{"force", {"f"}}},
        [](pontella::command command) {
            if (command.arguments.size() < 2) {
                throw std::runtime_error(
                    "at least two arguments are required (a clip input and the Event Stream output)");
            }
            for (auto filename_iterator = std::next(command.arguments.begin());
                 filename_iterator != std::prev(command.arguments.end());
                 ++filename_iterator) {
                std::ifstream input_file(*filename_iterator);
                if (!input_file.good()) {
                    throw std::runtime_error(std::string("'") + *filename_iterator + "' could not be open for reading");
                }
            }
            {
                std::ifstream input(command.arguments.back());
                if (input.good() && command.flags.find("force") == command.flags.end()) {
                    throw std::runtime_error(
                        std::string("'") + command.arguments.back() + "' already exists (use --force to overwrite it)");
                }
            }
            auto write = sepia::write<sepia::type::generic>(sepia::filename_to_ofstream(command.arguments.back()));
            std::size_t fifo_size = 64;
            {
                const auto name_and_value = command.options.find("buffer");
                if (name_and_value != command.options.end()) {
                    fifo_size = std::stoull(name_and_value->second);
                }
            }
            hummingbird::lightcrafter::ip ip{10, 10, 10, 100};
            {
                const auto name_and_value = command.options.find("ip");
                if (name_and_value != command.options.end()) {
                    ip = hummingbird::lightcrafter::parse_ip(name_and_value->second);
                }
            }
            hummingbird::lightcrafter lightcrafter(ip);

            // write handler
            const auto reference_t = std::chrono::system_clock::now();
            auto now = [=]() {
                return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::system_clock::now() - reference_t)
                                                 .count());
            };
            std::atomic_flag accessing_write;
            accessing_write.clear(std::memory_order_release);
            auto write_message = [&](const std::string& message) {
                std::cout << message + "\n";
                std::cout.flush();
                std::vector<uint8_t> bytes(message.size());
                std::copy(message.begin(), message.end(), bytes.begin());
                while (accessing_write.test_and_set(std::memory_order_acquire)) {
                }
                write(sepia::generic_event{now(), bytes});
                accessing_write.clear(std::memory_order_release);
            };
            write_message(
                std::string("reference t: ")
                + std::to_string(static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(reference_t.time_since_epoch()).count())));

            // common loop management
            std::atomic_bool running(true);
            std::exception_ptr pipeline_exception;

            // teensy observable
            std::atomic<uint8_t> byte;
            std::atomic_flag expecting_byte;
            expecting_byte.test_and_set(std::memory_order_release);
            auto teensy = hibiscus::make_teensy_eventide(
                [&](uint8_t local_byte) {
                    byte.store(local_byte, std::memory_order_release);
                    expecting_byte.clear(std::memory_order_release);
                },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    running.store(false, std::memory_order_release);
                });

            // display observable
            auto display =
                hummingbird::make_display(false, 608, 684, 0, fifo_size, [&](hummingbird::display_event display_event) {
                    if (display_event.empty_fifo) {
                        write_message("warning: empty fifo");
                    } else if (
                        display_event.loop_duration > 0
                        && (display_event.loop_duration < 6000 || display_event.loop_duration > 28000)) {
                        write_message(
                            std::string("throttling (loop duration: ") + std::to_string(display_event.loop_duration)
                            + " microseconds)");
                    }
                });

            // decoder observable
            std::size_t frame_id = 0;
            auto started = false;
            std::vector<uint8_t> bytes(608 * 684 * 3);
            std::fill(bytes.begin(), bytes.end(), 0);
            auto decoder = hummingbird::make_decoder([&](const Glib::RefPtr<Gst::Buffer>& buffer) {
                hummingbird::interleave(buffer, bytes);
                while (running.load(std::memory_order_acquire)) {
                    if (display->push(bytes, frame_id)) {
                        ++frame_id;
                        break;
                    } else {
                        if (!started) {
                            started = true;
                            display->start();
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                }
            });

            // play loop
            std::vector<uint8_t> frame(343 * 342 * 3, 0);
            std::atomic_bool wait_for_empty_fifo(true);
            std::thread play_loop([&]() {
                try {
                    std::size_t clip_index = 0;
                    std::fill(bytes.begin(), bytes.end(), 0);
                    display->pause_and_clear(bytes, &wait_for_empty_fifo);
                    while (running.load(std::memory_order_acquire)) {
                        while (expecting_byte.test_and_set(std::memory_order_acquire)
                               && running.load(std::memory_order_acquire)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        }
                        if (!running.load(std::memory_order_acquire)) {
                            break;
                        }
                        const auto local_byte = byte.load(std::memory_order_acquire);

                        // @DEBUG {
                        std::cout << std::string("byte: ") + std::to_string(local_byte) + "\n";
                        std::cout.flush();
                        // }

                        if (local_byte == 0b00000100) {
                            if (clip_index > command.arguments.size() - 2) {
                                write_message(
                                    std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                    + ", clip index overflow");
                                break;
                            }
                            write_message(
                                std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte)) + ", clip "
                                + std::to_string(clip_index - 1) + ", " + command.arguments[clip_index]);
                            decoder->read(command.arguments[clip_index]);
                            std::fill(bytes.begin(), bytes.end(), 0);
                            display->pause_and_clear(bytes, &wait_for_empty_fifo);
                            started = false;
                            ++clip_index;
                        } else {
                            uint16_t x = 0;
                            uint16_t y = 0;
                            auto valid = true;
                            switch (local_byte) {
                                case 0b00001000: // fixation
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", fixation");
                                    x = 171;
                                    y = 171;
                                    break;
                                case 0b01010000: // calibration top left
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 2");
                                    x = 34;
                                    y = 34;
                                    break;
                                case 0b01100000: // calibration top center
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 3");
                                    x = 171;
                                    y = 34;
                                    break;
                                case 0b01110000: // calibration top right
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 4");
                                    x = 308;
                                    y = 34;
                                    break;
                                case 0b10010000: // calibration center left
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 1");
                                    x = 34;
                                    y = 171;
                                    break;
                                case 0b10100000: // calibration center center
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 5");
                                    x = 171;
                                    y = 171;
                                    break;
                                case 0b10110000: // calibration center right
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 5");
                                    x = 308;
                                    y = 171;
                                    break;
                                case 0b11010000: // calibration bottom left
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 5");
                                    x = 34;
                                    y = 308;
                                    break;
                                case 0b11100000: // calibration bottom center
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 5");
                                    x = 171;
                                    y = 308;
                                    break;
                                case 0b11110000: // calibration bottom right
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", calibration 5");
                                    x = 308;
                                    y = 308;
                                    break;
                                default:
                                    valid = false;
                                    write_message(
                                        std::string("code: ") + std::to_string(static_cast<uint16_t>(local_byte))
                                        + ", unknown");
                                    break;
                            }

                            // @DEBUG {
                            std::cout << std::to_string(x) + ", " + std::to_string(y) + ": "
                                             + (valid ? "valid" : "unknown") + "\n";
                            std::cout.flush();
                            // }

                            if (valid) {
                                std::fill(frame.begin(), frame.end(), 0);
                                draw_rectangle(frame, x, y);
                                std::fill(bytes.begin(), bytes.end(), 0);
                                hummingbird::rotate(frame, bytes);
                                display->pause_and_clear(bytes, &wait_for_empty_fifo);
                            }
                        }
                    }
                } catch (...) {
                    pipeline_exception = std::current_exception();
                }
                display->close();
            });

            // start the display
            display->run(60);
            running.store(false, std::memory_order_release);
            decoder->stop();
            play_loop.join();
            if (pipeline_exception) {
                std::rethrow_exception(pipeline_exception);
            }
        });
}
