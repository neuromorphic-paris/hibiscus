#include "../third_party/hummingbird/source/decoder.hpp"
#include "../third_party/hummingbird/source/display.hpp"
#include "../third_party/hummingbird/source/interleave.hpp"
#include "../third_party/hummingbird/source/lightcrafter.hpp"
#include "../third_party/hummingbird/third_party/pontella/source/pontella.hpp"
#include "../third_party/sepia/source/sepia.hpp"
#include "../third_party/tarsier/source/merge.hpp"
#include "calibration.hpp"
#include "livetrack_data_observable.hpp"
#include "teensy.hpp"

/// dmd_state determines which action to take on DMD events.
enum class dmd_state {
    idle,
    check,
    write,
};

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "record plays a list of stimuli and records precise timings",
            "Syntax: ./record [options] calibration.json /path/to/first/clip.mp4 "
            "[/path/to/second/clip.mp4...] output.es",
            "Available options:",
            "    -f, --force                       overwrites the output file if "
            "it exists",
            "    -d, --duration                    sets the inhibition duration "
            "in microseconds",
            "                                          defaults to 500000",
            "                                          button pushes during this "
            "duration after a clip start",
            "                                          are not accounted for",
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
        {{"duration", {"d"}}, {"buffer", {"b"}}, {"ip", {"i"}}},
        {{"force", {"f"}}, {"fake-events", {"e"}}},
        [](pontella::command command) {
            if (command.arguments.size() < 3) {
                throw std::runtime_error("at least three arguments are required (a calibration file input, a clip "
                                         "input and the Event Stream output)");
            }
            hibiscus::calibrations calibrations;
            {
                std::ifstream json_input(command.arguments.front());
                if (!json_input.good()) {
                    throw std::runtime_error(
                        std::string("'") + command.arguments.front() + "' could not be open for reading");
                }
                calibrations = hibiscus::json_to_calibrations(json_input);
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
            {
                std::ofstream output(command.arguments.back(), std::ofstream::app);
                if (!output.good()) {
                    throw std::runtime_error(
                        std::string("'") + command.arguments.back() + "' could not be open for writing");
                }
            }
            uint64_t inhibition_duration = 500000;
            {
                const auto name_and_value = command.options.find("duration");
                if (name_and_value != command.options.end()) {
                    inhibition_duration = std::stoull(name_and_value->second);
                }
            }
            std::size_t fifo_size = 64;
            {
                const auto name_and_value = command.options.find("buffer");
                if (name_and_value != command.options.end()) {
                    fifo_size = std::stoull(name_and_value->second);
                }
            }
            const auto fake_events = command.flags.find("fake-events") != command.flags.end();
            hummingbird::lightcrafter::ip ip{10, 10, 10, 100};
            {
                const auto name_and_value = command.options.find("ip");
                if (name_and_value != command.options.end()) {
                    ip = hummingbird::lightcrafter::parse_ip(name_and_value->second);
                }
            }
            hummingbird::lightcrafter lightcrafter(ip);
            std::unique_ptr<hibiscus::teensy> teensy;

            // merge event handler
            auto merge = tarsier::make_merge<2, sepia::generic_event>(
                1 << 20,
                std::chrono::milliseconds(20),
                sepia::write<sepia::type::generic>(sepia::filename_to_ofstream(command.arguments.back())));
            auto warn = [&](uint8_t channel, uint64_t t, const std::string& message) {
                std::cout << std::string("    \033[33mwarning: ") + message + "\033[0m\n";
                std::cout.flush();
                std::vector<uint8_t> bytes(message.size() + 1);
                bytes[0] = 'w';
                std::copy(message.begin(), message.end(), std::next(bytes.begin()));
                if (channel == 0) {
                    merge->push<0>(sepia::generic_event{t, bytes});
                } else {
                    merge->push<1>(sepia::generic_event{t, bytes});
                }
            };

            // display observable
            std::atomic<uint64_t> display_event_as_uint64(std::numeric_limits<uint64_t>::max());
            sepia::fifo<std::string> display_warnings(1 << 16);
            auto display =
                hummingbird::make_display(false, 608, 684, 0, fifo_size, [&](hummingbird::display_event display_event) {
                    display_event_as_uint64.store(
                        static_cast<uint64_t>(display_event.tick)
                            | (static_cast<uint64_t>(display_event.has_id ? 1 : 0) << 32)
                            | (static_cast<uint64_t>(display_event.id & 0x7fffffff) << 33),
                        std::memory_order_release);
                    if (display_event.empty_fifo) {
                        if (!display_warnings.push("empty fifo")) {
                            throw std::runtime_error("display_warnings fifo overflow");
                        }
                    } else if (
                        display_event.loop_duration > 0
                        && (display_event.loop_duration < 6000 || display_event.loop_duration > 28000)) {
                        if (!display_warnings.push(
                                std::string("throttling (loop duration: ") + std::to_string(display_event.loop_duration)
                                + " microseconds)")) {
                            throw std::runtime_error("display_warnings fifo overflow");
                        }
                    }
                });

            // common loop management
            std::atomic_bool running(true);
            std::exception_ptr pipeline_exception;
            std::atomic_bool wait_for_empty_fifo(true);
            std::atomic_bool stopping(false);

            // decoder observable
            std::size_t frame_id = 0;
            auto started = false;
            std::vector<uint8_t> bytes;
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

            // teensy observable
            uint64_t previous_teensy_t = 0;
            auto display_tick_correction = 0ll;
            sepia::fifo<std::string> livetrack_warnings(1 << 16);
            std::atomic<uint32_t> livetrack_left_samples(0);
            std::atomic<uint32_t> livetrack_right_samples(0);
            std::string warning;
            std::atomic<hibiscus::teensy_event> ab_event;
            auto c_teensy_tick_offset = std::numeric_limits<int64_t>::max();
            auto c_tick = 0ll;
            auto c_will_stop = false;
            auto c_previous_id = 0u;
            auto c_new_clip = false;
            auto c_stopping_acknowledged = false;
            auto c_ticks_inconsistency = false;
            auto d_tick = std::numeric_limits<int64_t>::max();
            auto d_recording = false;
            auto d_tick_to_index = 0ll;
            auto d_clip_start_t = std::numeric_limits<uint64_t>::max();
            auto d_clip_index = 0ll;
            std::atomic_bool d_stopping_acknowledged(false);
            uint8_t e_index = std::numeric_limits<uint8_t>::max();
            auto e_recording = false;
            auto lr_inhibited = true;
            auto write_frame_event = [&](uint64_t t) {
                const auto frame_index = static_cast<uint32_t>((d_tick - d_tick_to_index) * 24 + e_index);
                merge->push<0>(sepia::generic_event{t,
                                                    {
                                                        'f',
                                                        static_cast<uint8_t>(frame_index & 0xff),
                                                        static_cast<uint8_t>((frame_index >> 8) & 0xff),
                                                        static_cast<uint8_t>((frame_index >> 16) & 0xff),
                                                        static_cast<uint8_t>((frame_index >> 24) & 0xff),
                                                    }});
            };
            teensy = hibiscus::make_teensy_record(
                [&](hibiscus::teensy_event teensy_event) {
                    if (display_warnings.pull(warning)) {
                        warn(0, previous_teensy_t, warning);
                    }
                    if (livetrack_warnings.pull(warning)) {
                        warn(0, previous_teensy_t, warning);
                    }
                    switch (teensy_event.type) {
                        case 'a':
                        case 'b':
                            ab_event.store(teensy_event, std::memory_order_release);
                            break;
                        case 'c': { // for 'c' events, teensy_event.t is the tick, not the timestamp
                            const auto display_event = display_event_as_uint64.load(std::memory_order_acquire);
                            if (display_event == std::numeric_limits<uint64_t>::max()) {
                                break;
                            }
                            const auto tick =
                                static_cast<int64_t>(display_event & 0xffffffff) + display_tick_correction;
                            const auto has_id = ((display_event >> 32) & 1) == 1;
                            const auto id = static_cast<uint32_t>(display_event >> 33);
                            if (c_teensy_tick_offset == std::numeric_limits<int64_t>::max()) {
                                c_teensy_tick_offset = static_cast<int64_t>(teensy_event.t) - tick;
                            } else if (tick != static_cast<int64_t>(teensy_event.t) - c_teensy_tick_offset) {
                                if (!c_stopping_acknowledged) {
                                    if (c_ticks_inconsistency) {
                                        warn(
                                            0,
                                            previous_teensy_t,
                                            std::string("display and teensy ticks are not equal (")
                                                + std::to_string(tick) + " and "
                                                + std::to_string(teensy_event.t - c_teensy_tick_offset) + ")");
                                        display_tick_correction +=
                                            static_cast<int64_t>(teensy_event.t) - c_teensy_tick_offset - tick;
                                    } else {
                                        c_ticks_inconsistency = true;
                                    }
                                }
                            } else {
                                c_ticks_inconsistency = false;
                            }
                            if (teensy_event.t - c_teensy_tick_offset != c_tick) {
                                warn(
                                    0,
                                    previous_teensy_t,
                                    std::string("teensy and c ticks are not equal (")
                                        + std::to_string(teensy_event.t - c_teensy_tick_offset) + " and "
                                        + std::to_string(c_tick) + ")");
                            }
                            ++c_tick;
                            if (d_tick == std::numeric_limits<int64_t>::max()) {
                                d_tick = 0;
                            }
                            if (has_id) {
                                if (d_recording) {
                                    if (id < c_previous_id) {
                                        c_new_clip = true;
                                    }
                                } else {
                                    if (id >= 1) {
                                        c_new_clip = true;
                                        d_recording = true;
                                    }
                                }
                            } else {
                                if (d_recording) {
                                    if (c_will_stop) {
                                        c_will_stop = false;
                                        d_recording = false;
                                        if (stopping.load(std::memory_order_acquire)) {
                                            c_stopping_acknowledged = true;
                                        }
                                    } else {
                                        c_will_stop = true;
                                    }
                                }
                            }
                            c_previous_id = id;
                            break;
                        }
                        case 'd':
                            if (d_tick != std::numeric_limits<int64_t>::max()) {
                                ++d_tick;
                                if (c_tick != d_tick) {
                                    warn(
                                        0,
                                        teensy_event.t,
                                        std::string("c and d ticks are not equal (") + std::to_string(c_tick) + " and "
                                            + std::to_string(d_tick) + ")");
                                }
                                if (e_index != std::numeric_limits<uint8_t>::max() && e_index != 24) {
                                    warn(0, teensy_event.t, "unexpected 'd' event");
                                }
                                if (c_new_clip) {
                                    const auto left_samples = livetrack_left_samples.load(std::memory_order_acquire);
                                    const auto right_samples = livetrack_right_samples.load(std::memory_order_acquire);
                                    if (d_clip_start_t != std::numeric_limits<uint64_t>::max()) {
                                        const auto ratio = 1e6 / (teensy_event.t - d_clip_start_t);
                                        std::cout << std::string("    livetrack samples per second: \033[31m")
                                                         + std::to_string(static_cast<uint32_t>(ratio * left_samples))
                                                         + " left\033[0m and \033[32m"
                                                         + std::to_string(static_cast<uint32_t>(ratio * right_samples))
                                                         + " right\033[0m\n";
                                    }
                                    livetrack_left_samples.fetch_sub(left_samples, std::memory_order_release);
                                    livetrack_right_samples.fetch_sub(right_samples, std::memory_order_release);
                                    c_new_clip = false;
                                    lr_inhibited = false;
                                    d_clip_start_t = teensy_event.t;
                                    merge->push<0>(
                                        sepia::generic_event{teensy_event.t,
                                                             {
                                                                 's',
                                                                 static_cast<uint8_t>(d_clip_index & 0xff),
                                                                 static_cast<uint8_t>((d_clip_index >> 8) & 0xff),
                                                                 static_cast<uint8_t>((d_clip_index >> 16) & 0xff),
                                                                 static_cast<uint8_t>((d_clip_index >> 24) & 0xff),
                                                             }});
                                    std::cout << (d_clip_index < command.arguments.size() - 2 ?
                                                      std::string("clip: ") + command.arguments[d_clip_index + 1] + " ("
                                                          + std::to_string(d_clip_index + 1) + " / "
                                                          + std::to_string(command.arguments.size() - 2) + ")" :
                                                      std::string("clip index overflow"))
                                                     + "\n";
                                    std::cout.flush();
                                    ++d_clip_index;
                                    d_tick_to_index = d_tick;
                                }
                                if (d_recording) {
                                    e_index = 0;
                                    write_frame_event(teensy_event.t);
                                } else {
                                    lr_inhibited = true;
                                }
                                if (c_stopping_acknowledged) {
                                    d_stopping_acknowledged.store(true, std::memory_order_release);
                                }
                                e_recording = d_recording;
                                e_index = 1;
                            }
                            previous_teensy_t = teensy_event.t;
                            break;
                        case 'e':
                            if (e_index != std::numeric_limits<uint8_t>::max()) {
                                if (e_index >= 24) {
                                    warn(0, teensy_event.t, "unexpected 'e' event");
                                }
                                if (e_recording) {
                                    write_frame_event(teensy_event.t);
                                }
                                ++e_index;
                            }
                            previous_teensy_t = teensy_event.t;
                            break;
                        case 'l':
                        case 'r':
                            if (!lr_inhibited && teensy_event.t - d_clip_start_t > inhibition_duration
                                && wait_for_empty_fifo.load(std::memory_order_acquire)) {
                                wait_for_empty_fifo.store(false, std::memory_order_release);
                                decoder->stop();
                                lr_inhibited = true;
                                merge->push<0>(sepia::generic_event{teensy_event.t, {teensy_event.type}});
                                if (teensy_event.type == 'l') {
                                    std::cout << "    button: \033[31mleft\033[0m\n";
                                } else {
                                    std::cout << "    button: \033[32mright\033[0m\n";
                                }
                                std::cout.flush();
                            }
                            previous_teensy_t = teensy_event.t;
                            break;
                        default:
                            break;
                    }
                },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    wait_for_empty_fifo.store(false, std::memory_order_release);
                    running.store(false, std::memory_order_release);
                });

            // livetrack observable
            std::atomic_bool livetrack_ready(false);
            auto is_livetrack_high = false;
            std::vector<hibiscus::livetrack_data> livetrack_data_events;
            livetrack_data_events.reserve(1 << 16);
            std::size_t past_the_edge_index = 0;
            uint64_t livetrack_previous_reference_t = 0;
            uint64_t livetrack_previous_t = 0;
            std::atomic_bool livetrack_stopping_acknowledged(false);
            auto livetrack_data_observable = hibiscus::make_livetrack_data_observable(
                [&](hibiscus::livetrack_data livetrack_data) {
                    livetrack_data.t -= 1000; // statistical estimator for the actual timestamp
                    livetrack_data_events.push_back(livetrack_data);
                    if (is_livetrack_high != (((livetrack_data.io >> 22) & 1) == 1)) {
                        if (fake_events && std::rand() < RAND_MAX / 100) {
                            teensy->send('f');
                        }
                        past_the_edge_index = livetrack_data_events.size();
                        is_livetrack_high = !is_livetrack_high;
                    }
                    if (past_the_edge_index != 0) {
                        auto teensy_event = ab_event.load(std::memory_order_acquire);
                        if (teensy_event.type != 0) {
                            if ((teensy_event.type == 'a' && is_livetrack_high)
                                || (teensy_event.type == 'b' && !is_livetrack_high)) {
                                if (livetrack_ready.load(std::memory_order_acquire)) {
                                    const auto slope =
                                        static_cast<double>(teensy_event.t - livetrack_previous_reference_t)
                                        / static_cast<double>(
                                            livetrack_data_events[past_the_edge_index - 1].t - livetrack_previous_t);
                                    const auto intercept =
                                        livetrack_previous_reference_t - slope * livetrack_previous_t;
                                    for (std::size_t index = 0; index < past_the_edge_index; ++index) {
                                        auto livetrack_data = livetrack_data_events[index];
                                        const auto t = static_cast<uint64_t>(slope * livetrack_data.t + intercept);
                                        if (livetrack_data.left.has_pupil && livetrack_data.left.has_glint_1) {
                                            const auto point = hibiscus::projection(
                                                calibrations.left.matrix,
                                                hibiscus::eye({static_cast<double>(livetrack_data.left.pupil_x)
                                                                   - livetrack_data.left.glint_1_x,
                                                               static_cast<double>(livetrack_data.left.pupil_y)
                                                                   - livetrack_data.left.glint_1_y}));
                                            const uint64_t x = *reinterpret_cast<const uint64_t*>(&std::get<0>(point));
                                            const uint64_t y = *reinterpret_cast<const uint64_t*>(&std::get<1>(point));
                                            merge->push<1>(sepia::generic_event{
                                                t,
                                                {'a',
                                                 static_cast<uint8_t>(x & 0xff),
                                                 static_cast<uint8_t>((x >> 8) & 0xff),
                                                 static_cast<uint8_t>((x >> 16) & 0xff),
                                                 static_cast<uint8_t>((x >> 24) & 0xff),
                                                 static_cast<uint8_t>((x >> 32) & 0xff),
                                                 static_cast<uint8_t>((x >> 40) & 0xff),
                                                 static_cast<uint8_t>((x >> 48) & 0xff),
                                                 static_cast<uint8_t>((x >> 56) & 0xff),
                                                 static_cast<uint8_t>(y & 0xff),
                                                 static_cast<uint8_t>((y >> 8) & 0xff),
                                                 static_cast<uint8_t>((y >> 16) & 0xff),
                                                 static_cast<uint8_t>((y >> 24) & 0xff),
                                                 static_cast<uint8_t>((y >> 32) & 0xff),
                                                 static_cast<uint8_t>((y >> 40) & 0xff),
                                                 static_cast<uint8_t>((y >> 48) & 0xff),
                                                 static_cast<uint8_t>((y >> 56) & 0xff),
                                                 static_cast<uint8_t>(livetrack_data.left.major_axis & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.left.major_axis >> 8) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.left.major_axis >> 16) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.left.major_axis >> 24) & 0xff),
                                                 static_cast<uint8_t>(livetrack_data.left.minor_axis & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.left.minor_axis >> 8) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.left.minor_axis >> 16) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.left.minor_axis >> 24) & 0xff)}});
                                            livetrack_left_samples.fetch_add(1, std::memory_order_release);
                                        }
                                        if (livetrack_data.right.has_pupil && livetrack_data.right.has_glint_1) {
                                            const auto point = hibiscus::projection(
                                                calibrations.right.matrix,
                                                hibiscus::eye({static_cast<double>(livetrack_data.right.pupil_x)
                                                                   - livetrack_data.right.glint_1_x,
                                                               static_cast<double>(livetrack_data.right.pupil_y)
                                                                   - livetrack_data.right.glint_1_y}));
                                            const uint64_t x = *reinterpret_cast<const uint64_t*>(&std::get<0>(point));
                                            const uint64_t y = *reinterpret_cast<const uint64_t*>(&std::get<1>(point));
                                            merge->push<1>(sepia::generic_event{
                                                t,
                                                {'b',
                                                 static_cast<uint8_t>(x & 0xff),
                                                 static_cast<uint8_t>((x >> 8) & 0xff),
                                                 static_cast<uint8_t>((x >> 16) & 0xff),
                                                 static_cast<uint8_t>((x >> 24) & 0xff),
                                                 static_cast<uint8_t>((x >> 32) & 0xff),
                                                 static_cast<uint8_t>((x >> 40) & 0xff),
                                                 static_cast<uint8_t>((x >> 48) & 0xff),
                                                 static_cast<uint8_t>((x >> 56) & 0xff),
                                                 static_cast<uint8_t>(y & 0xff),
                                                 static_cast<uint8_t>((y >> 8) & 0xff),
                                                 static_cast<uint8_t>((y >> 16) & 0xff),
                                                 static_cast<uint8_t>((y >> 24) & 0xff),
                                                 static_cast<uint8_t>((y >> 32) & 0xff),
                                                 static_cast<uint8_t>((y >> 40) & 0xff),
                                                 static_cast<uint8_t>((y >> 48) & 0xff),
                                                 static_cast<uint8_t>((y >> 56) & 0xff),
                                                 static_cast<uint8_t>(livetrack_data.right.major_axis & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.right.major_axis >> 8) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.right.major_axis >> 16) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.right.major_axis >> 24) & 0xff),
                                                 static_cast<uint8_t>(livetrack_data.right.minor_axis & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.right.minor_axis >> 8) & 0xff),
                                                 static_cast<uint8_t>((livetrack_data.right.minor_axis >> 16) & 0xff),
                                                 static_cast<uint8_t>(
                                                     (livetrack_data.right.minor_axis >> 24) & 0xff)}});
                                            livetrack_right_samples.fetch_add(1, std::memory_order_release);
                                        }
                                    }
                                } else {
                                    livetrack_ready.store(true, std::memory_order_release);
                                }
                                livetrack_previous_reference_t = teensy_event.t;
                                livetrack_previous_t = livetrack_data_events[past_the_edge_index - 1].t;
                                merge->push<1>(sepia::generic_event{
                                    teensy_event.t,
                                    {'c',
                                     static_cast<uint8_t>(livetrack_previous_reference_t & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 8) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 16) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 24) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 32) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 40) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 48) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_reference_t >> 56) & 0xff),
                                     static_cast<uint8_t>(livetrack_previous_t & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 8) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 16) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 24) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 32) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 40) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 48) & 0xff),
                                     static_cast<uint8_t>((livetrack_previous_t >> 56) & 0xff)}});
                                livetrack_data_events.erase(
                                    livetrack_data_events.begin(),
                                    std::next(livetrack_data_events.begin(), past_the_edge_index));
                                past_the_edge_index = 0;
                                teensy->send(teensy_event.type == 'a' ? 'b' : 'a');
                                if (stopping.load(std::memory_order_acquire)) {
                                    livetrack_stopping_acknowledged.store(true, std::memory_order_release);
                                }
                            } else {
                                if (!livetrack_warnings.push("livetrack edge type and teensy event mismatch")) {
                                    throw std::runtime_error("livetrack_warnings fifo overflow");
                                }
                            }
                        }
                    }
                },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    running.store(false, std::memory_order_release);
                    wait_for_empty_fifo.store(false, std::memory_order_release);
                });

            // play loop
            std::thread play_loop([&]() {
                try {
                    std::size_t clip_index = 1;
                    while (running.load(std::memory_order_acquire)) {
                        wait_for_empty_fifo.store(true, std::memory_order_release);
                        decoder->read(command.arguments[clip_index]);
                        if (!running.load(std::memory_order_acquire)) {
                            break;
                        }
                        std::fill(bytes.begin(), bytes.end(), 0);
                        display->pause_and_clear(bytes, &wait_for_empty_fifo);
                        wait_for_empty_fifo.store(false, std::memory_order_release);
                        if (clip_index >= command.arguments.size() - 2) {
                            stopping.store(true, std::memory_order_release);
                            while (!livetrack_stopping_acknowledged.load(std::memory_order_acquire)
                                   || !d_stopping_acknowledged.load(std::memory_order_acquire)) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            }
                            break;
                        } else {
                            ++clip_index;
                        }
                    }
                } catch (...) {
                    pipeline_exception = std::current_exception();
                }
                display->close();
            });

            // synchronize the livetrack
            livetrack_data_observable->start();
            teensy->send('a');
            while (!livetrack_ready.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            // start the display
            display->run(60);
            running.store(false, std::memory_order_release);
            decoder->stop();
            play_loop.join();
            if (pipeline_exception) {
                std::rethrow_exception(pipeline_exception);
            }
            livetrack_data_observable.reset();
            teensy.reset();
        });
}
