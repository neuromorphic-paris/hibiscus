#include "../third_party/hummingbird/source/display.hpp"
#include "../third_party/hummingbird/source/lightcrafter.hpp"
#include "../third_party/hummingbird/source/rotate.hpp"

/// rotate converts a 576 x 108 RGB frame to a 608 x 684 RGB frame.
inline void rotate(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    for (uint16_t y = 0; y < 108; ++y) {
        for (uint16_t x = 0; x < 576; ++x) {
            for (uint8_t channel = 0; channel < 3; ++channel) {
                output[(133 + (x + y + 1) / 2 + (575 - x + y) * 608) * 3 + channel] =
                    input[(x + y * 576) * 3 + channel];
            }
        }
    }
}

int main(int argc, char* argv[]) {
    hummingbird::lightcrafter lightcrafter({10, 10, 10, 100});
    auto display = hummingbird::make_display(false, 608, 684, 0, 64, [](hummingbird::display_event) {});
    std::atomic_bool running(true);
    std::vector<uint8_t> frame(576 * 108 * 3);
    std::vector<uint8_t> bytes(608 * 684 * 3);
    auto play_loop = std::thread([&]() {
        while (running.load(std::memory_order_acquire)) {
            std::fill(bytes.begin(), bytes.end(), 0);
            for (uint16_t x = 0; x < 576; ++x) {
                frame[x * 3] = 0xff;
                frame[x * 3 + 1] = 0xff;
                frame[x * 3 + 2] = 0xff;
                frame[(x + 107 * 576) * 3] = 0xff;
                frame[(x + 107 * 576) * 3 + 1] = 0xff;
                frame[(x + 107 * 576) * 3 + 2] = 0xff;
            }
            for (uint16_t y = 0; y < 108; ++y) {
                frame[(y * 576) * 3] = 0xff;
                frame[(y * 576) * 3 + 1] = 0xff;
                frame[(y * 576) * 3 + 2] = 0xff;
                frame[(575 + y * 576) * 3] = 0xff;
                frame[(575 + y * 576) * 3 + 1] = 0xff;
                frame[(575 + y * 576) * 3 + 2] = 0xff;
            }
            for (uint16_t y = 0; y < 108; ++y) {
                frame[(y + y * 576) * 3] = 0xff;
                frame[(y + y * 576) * 3 + 1] = 0xff;
                frame[(y + y * 576) * 3 + 2] = 0xff;
            }
            rotate(frame, bytes);
            display->push(bytes);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    display->start();
    display->run();
    running.store(false, std::memory_order_release);
    play_loop.join();
}
