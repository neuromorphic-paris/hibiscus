#include "teensy.hpp"
#include "terminal.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        std::exception_ptr pipeline_exception;
        std::atomic_bool running(true);
        auto terminal = hibiscus::make_terminal(
            [&](int32_t pressed_character) {
                if (std::isspace(pressed_character)) {
                    running.store(false, std::memory_order_release);
                }
            },
            [&](std::exception_ptr exception) {
                pipeline_exception = exception;
                running.store(false, std::memory_order_release);
            });
        std::array<std::pair<std::string, int32_t>, 7> chunks_and_attributes{{
            {"l: ", A_NORMAL},
            {"0", A_NORMAL},
            {"\nr: ", A_NORMAL},
            {"0", A_NORMAL},
            {"\n\npress ", A_NORMAL},
            {"return", A_BOLD},
            {" to quit", A_NORMAL},
        }};
        terminal->set_chunks_and_attributes(chunks_and_attributes.begin(), chunks_and_attributes.end());
        auto teensy = hibiscus::make_teensy_record(
            [&](hibiscus::teensy_event teensy_event) {
                switch (teensy_event.type) {
                    case 'l':
                        std::get<1>(chunks_and_attributes).first = std::to_string(teensy_event.t);
                        break;
                    case 'r':
                        std::get<3>(chunks_and_attributes).first = std::to_string(teensy_event.t);
                        break;
                    default:
                        break;
                }
                terminal->set_chunks_and_attributes(chunks_and_attributes.begin(), chunks_and_attributes.end());
            },
            [&](std::exception_ptr exception) {
                pipeline_exception = exception;
                running.store(false, std::memory_order_release);
            });
        while (running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (pipeline_exception) {
            std::rethrow_exception(pipeline_exception);
        }
    } catch (const std::runtime_error& exception) {
        std::cout << exception.what() << std::endl;
    }
}
