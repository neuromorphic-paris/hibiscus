#include "../third_party/hummingbird/source/display.hpp"
#include "../third_party/hummingbird/source/lightcrafter.hpp"
#include "../third_party/hummingbird/source/rotate.hpp"
#include "../third_party/hummingbird/third_party/pontella/source/pontella.hpp"
#include "../third_party/sepia/source/sepia.hpp"
#include "calibration.hpp"
#include "livetrack_data_observable.hpp"
#include <deque>

const std::array<std::array<uint8_t, 3>, 7> on_lookup{{
    {{0b11100000, 0b00000000, 0b00000000}},
    {{0b11111100, 0b00000000, 0b00000000}},
    {{0b11111111, 0b10000000, 0b00000000}},
    {{0b11111111, 0b11110000, 0b00000000}},
    {{0b11111111, 0b11111110, 0b00000000}},
    {{0b11111111, 0b11111111, 0b11000000}},
    {{0b11111111, 0b11111111, 0b11111000}},
}};

const std::array<std::array<uint8_t, 3>, 7> off_lookup{{
    {{0b11111111, 0b11111111, 0b11111000}},
    {{0b11111111, 0b11111111, 0b11000000}},
    {{0b11111111, 0b11111110, 0b00000000}},
    {{0b11111111, 0b11110000, 0b00000000}},
    {{0b11111111, 0b10000000, 0b00000000}},
    {{0b11111100, 0b00000000, 0b00000000}},
    {{0b11100000, 0b00000000, 0b00000000}},
}};

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "draw displays points matching the subject's gaze",
            "Syntax: ./draw [options] calibration.json",
            "Available options:",
            "    -i [ip], --ip [ip]                sets the LightCrafter IP address",
            "                                          defaults to 10.10.10.100",
            "    -h, --help                            shows this help message",
        },
        argc,
        argv,
        -1,
        {{"ip", {"i"}}},
        {},
        [](pontella::command command) {
            hibiscus::calibrations calibrations;
            {
                std::ifstream json_input(command.arguments.front());
                if (!json_input.good()) {
                    throw std::runtime_error(
                        std::string("'") + command.arguments.front() + "' could not be open for reading");
                }
                calibrations = hibiscus::json_to_calibrations(json_input);
            }
            hummingbird::lightcrafter::ip ip{10, 10, 10, 100};
            {
                const auto name_and_value = command.options.find("ip");
                if (name_and_value != command.options.end()) {
                    ip = hummingbird::lightcrafter::parse_ip(name_and_value->second);
                }
            }
            hummingbird::lightcrafter lightcrafter(ip);

            std::atomic_bool running(true);
            std::exception_ptr pipeline_exception;
            auto display = hummingbird::make_display(false, 608, 684, 0, 2, [&](hummingbird::display_event) {});
            display->start();
            std::atomic_flag accessing_points;
            std::deque<std::array<uint16_t, 2>> points;
            accessing_points.clear(std::memory_order_release);
            auto livetrack_data_observable = hibiscus::make_livetrack_data_observable(
                [&](hibiscus::livetrack_data livetrack_data) {
                    if (livetrack_data.left.has_pupil && livetrack_data.left.has_glint_1
                        && livetrack_data.right.has_pupil && livetrack_data.right.has_glint_1) {
                        const auto left_point = hibiscus::projection(
                            calibrations.left.matrix,
                            hibiscus::eye(
                                {static_cast<double>(livetrack_data.left.pupil_x) - livetrack_data.left.glint_1_x,
                                 static_cast<double>(livetrack_data.left.pupil_y) - livetrack_data.left.glint_1_y}));
                        const auto right_point = hibiscus::projection(
                            calibrations.right.matrix,
                            hibiscus::eye(
                                {static_cast<double>(livetrack_data.right.pupil_x) - livetrack_data.right.glint_1_x,
                                 static_cast<double>(livetrack_data.right.pupil_y) - livetrack_data.right.glint_1_y}));
                        const auto mean = hibiscus::product(hibiscus::sum<3>(left_point, right_point), 0.5);
                        if (std::get<0>(mean) > 0 && std::get<0>(mean) < 343 && std::get<1>(mean) > 0
                            && std::get<1>(mean) < 342) {
                            while (accessing_points.test_and_set(std::memory_order_acquire)) {
                            }
                            if (points.size() >= 250) {
                                points.pop_front();
                            }
                            points.push_back(
                                {static_cast<uint16_t>(std::get<0>(mean)), static_cast<uint16_t>(std::get<1>(mean))});
                            accessing_points.clear(std::memory_order_release);
                        }
                    }
                },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    running.store(false, std::memory_order_release);
                });
            livetrack_data_observable->start();
            std::vector<uint8_t> frame(343 * 342 * 3);
            std::vector<uint8_t> bytes(608 * 684 * 3);
            std::vector<std::array<uint16_t, 2>> render_points;
            auto next_render = std::chrono::high_resolution_clock::now();
            auto render_loop = std::thread([&]() {
                while (running.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_until(next_render);
                    while (accessing_points.test_and_set(std::memory_order_acquire)) {
                    }
                    render_points = std::vector<std::array<uint16_t, 2>>(points.begin(), points.end());
                    accessing_points.clear(std::memory_order_release);
                    std::fill(frame.begin(), frame.end(), 0);
                    for (uint16_t y = 0; y < 342; y += 341) {
                        for (uint16_t x = 0; x < 343; ++x) {
                            for (uint8_t channel = 0; channel < 3; ++channel) {
                                frame[(x + y * 343) * 3 + channel] = 0xff;
                            }
                        }
                    }
                    for (uint16_t y = 0; y < 342; ++y) {
                        for (uint16_t x = 0; x < 343; x += 342) {
                            for (uint8_t channel = 0; channel < 3; ++channel) {
                                frame[(x + y * 343) * 3 + channel] = 0xff;
                            }
                        }
                    }
                    if (render_points.size() == 250) {
                        for (uint8_t index = 0; index < 8; ++index) {
                            frame[(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3] =
                                on_lookup[index][2];
                            frame
                                [(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3
                                 + 1] = on_lookup[index][0];
                            frame
                                [(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3
                                 + 2] = on_lookup[index][1];
                        }
                        for (std::size_t index = 0; index < render_points.size() - 9; ++index) {
                            frame[(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3] =
                                0xff;
                            frame
                                [(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3
                                 + 1] = 0xff;
                            frame
                                [(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3
                                 + 2] = 0xff;
                        }
                        for (uint8_t offset = 0; offset < 7; ++offset) {
                            const auto index = render_points.size() - 8 + offset;
                            frame[(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3] =
                                off_lookup[index][2];
                            frame
                                [(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3
                                 + 1] = off_lookup[index][0];
                            frame
                                [(std::get<0>(render_points[index]) + std::get<1>(render_points[index]) * 343) * 3
                                 + 2] = off_lookup[index][1];
                        }
                    }
                    hummingbird::rotate(frame, bytes);
                    while (!display->push(bytes)) {
                    }
                    next_render += std::chrono::microseconds(16666);
                }
                display->close();
            });
            display->run(0);
            running.store(false, std::memory_order_release);
            render_loop.join();
            if (pipeline_exception) {
                std::rethrow_exception(pipeline_exception);
            }
        });
}
