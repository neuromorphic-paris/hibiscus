#pragma once

#include "../third_party/hidapi/hidapi.h"
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define HIBISCUS_PACK(declaration) __pragma(pack(push, 1)) declaration __pragma(pack(pop))
#else
#define HIBISCUS_PACK(declaration) declaration __attribute__((__packed__))
#endif

/// hibiscus bundles tools to build a psychophysics platform on a Jetson TX1.
namespace hibiscus {

    /// eye_livetrack_data bundles information returned by the LiveTrack for a
    /// single eye.
    HIBISCUS_PACK(struct eye_livetrack_data {
        uint32_t major_axis;
        uint32_t minor_axis;
        uint32_t pupil_x;
        uint32_t pupil_y;
        uint32_t glint_1_x;
        uint32_t glint_1_y;
        uint32_t glint_2_x;
        uint32_t glint_2_y;
        bool enabled;
        bool has_pupil;
        bool has_glint_1;
        bool has_glint_2;
    });

    /// livetrack_data bundles information returned by the LiveTrack.
    HIBISCUS_PACK(struct livetrack_data {
        uint64_t t;
        uint32_t io;
        eye_livetrack_data left;
        eye_livetrack_data right;
    });

    /// livetrack_data_observable handles the connection to a LiveTrack eye tracker.
    template <typename HandleLivetrackData, typename HandleException>
    class livetrack_data_observable {
        public:
        livetrack_data_observable(HandleLivetrackData handle_livetrack_data, HandleException handle_exception) :
            _running(true),
            _started(false),
            _handle_livetrack_data(std::forward<HandleLivetrackData>(handle_livetrack_data)),
            _handle_exception(std::forward<HandleException>(handle_exception)) {
            _device = hid_open(2145, 13367, nullptr);
            if (_device == nullptr) {
                throw std::runtime_error("connecting to the LiveTrack failed");
            }
            try {
                std::array<uint8_t, 64> buffer;
                write({102}, "stopping the acquisition");
                for (;;) {
                    const auto bytes_read = hid_read_timeout(_device, buffer.data(), buffer.size(), 20);
                    if (bytes_read < 0) {
                        throw std::runtime_error("reading from the LiveTrack failed");
                    } else if (bytes_read == 0) {
                        break;
                    }
                }
            } catch (const std::runtime_error& exception) {
                hid_close(_device);
                throw exception;
            }
            _loop = std::thread([&]() {
                try {
                    std::array<uint8_t, 64> buffer;
                    while (_running.load(std::memory_order_acquire)) {
                        if (_started.load(std::memory_order_acquire)) {
                            const auto bytes_read = hid_read_timeout(_device, buffer.data(), buffer.size(), 20);
                            if (bytes_read == 64) {
                                _handle_livetrack_data(livetrack_data{
                                    static_cast<uint64_t>(buffer[6]) | (static_cast<uint64_t>(buffer[7]) << 8)
                                        | (static_cast<uint64_t>(buffer[8]) << 16)
                                        | (static_cast<uint64_t>(buffer[9]) << 24)
                                        | (static_cast<uint64_t>(buffer[10]) << 32)
                                        | (static_cast<uint64_t>(buffer[11]) << 40)
                                        | (static_cast<uint64_t>(buffer[12]) << 48)
                                        | (static_cast<uint64_t>(buffer[13]) << 56),
                                    static_cast<uint32_t>(buffer[2]) | (static_cast<uint32_t>(buffer[3]) << 8)
                                        | (static_cast<uint32_t>(buffer[4]) << 16)
                                        | (static_cast<uint32_t>(buffer[5]) << 24),
                                    {
                                        static_cast<uint32_t>(buffer[15]) | (static_cast<uint32_t>(buffer[16]) << 8)
                                            | (static_cast<uint32_t>(buffer[17]) << 16),
                                        static_cast<uint32_t>(buffer[18]) | (static_cast<uint32_t>(buffer[19]) << 8)
                                            | (static_cast<uint32_t>(buffer[20]) << 16),
                                        static_cast<uint32_t>(buffer[21]) | (static_cast<uint32_t>(buffer[22]) << 8)
                                            | (static_cast<uint32_t>(buffer[23]) << 16),
                                        static_cast<uint32_t>(buffer[24]) | (static_cast<uint32_t>(buffer[25]) << 8)
                                            | (static_cast<uint32_t>(buffer[26]) << 16),
                                        static_cast<uint32_t>(buffer[27]) | (static_cast<uint32_t>(buffer[28]) << 8)
                                            | (static_cast<uint32_t>(buffer[29]) << 16),
                                        static_cast<uint32_t>(buffer[30]) | (static_cast<uint32_t>(buffer[31]) << 8)
                                            | (static_cast<uint32_t>(buffer[32]) << 16),
                                        static_cast<uint32_t>(buffer[33]) | (static_cast<uint32_t>(buffer[34]) << 8)
                                            | (static_cast<uint32_t>(buffer[35]) << 16),
                                        static_cast<uint32_t>(buffer[36]) | (static_cast<uint32_t>(buffer[37]) << 8)
                                            | (static_cast<uint32_t>(buffer[38]) << 16),
                                        (buffer[14] & 1) == 1,
                                        ((buffer[14] >> 1) & 1) == 1,
                                        ((buffer[14] >> 2) & 1) == 1,
                                        ((buffer[14] >> 3) & 1) == 1,
                                    },
                                    {
                                        static_cast<uint32_t>(buffer[40]) | (static_cast<uint32_t>(buffer[41]) << 8)
                                            | (static_cast<uint32_t>(buffer[42]) << 16),
                                        static_cast<uint32_t>(buffer[43]) | (static_cast<uint32_t>(buffer[44]) << 8)
                                            | (static_cast<uint32_t>(buffer[45]) << 16),
                                        static_cast<uint32_t>(buffer[46]) | (static_cast<uint32_t>(buffer[47]) << 8)
                                            | (static_cast<uint32_t>(buffer[48]) << 16),
                                        static_cast<uint32_t>(buffer[49]) | (static_cast<uint32_t>(buffer[50]) << 8)
                                            | (static_cast<uint32_t>(buffer[51]) << 16),
                                        static_cast<uint32_t>(buffer[52]) | (static_cast<uint32_t>(buffer[53]) << 8)
                                            | (static_cast<uint32_t>(buffer[54]) << 16),
                                        static_cast<uint32_t>(buffer[55]) | (static_cast<uint32_t>(buffer[56]) << 8)
                                            | (static_cast<uint32_t>(buffer[57]) << 16),
                                        static_cast<uint32_t>(buffer[58]) | (static_cast<uint32_t>(buffer[59]) << 8)
                                            | (static_cast<uint32_t>(buffer[60]) << 16),
                                        static_cast<uint32_t>(buffer[61]) | (static_cast<uint32_t>(buffer[62]) << 8)
                                            | (static_cast<uint32_t>(buffer[63]) << 16),
                                        (buffer[39] & 1) == 1,
                                        ((buffer[39] >> 1) & 1) == 1,
                                        ((buffer[39] >> 2) & 1) == 1,
                                        ((buffer[39] >> 3) & 1) == 1,
                                    },
                                });
                            } else if (bytes_read < 0) {
                                throw std::runtime_error("reading from the LiveTrack failed");
                            }

                        } else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                        }
                    }
                } catch (...) {
                    _handle_exception(std::current_exception());
                }
            });
        }
        livetrack_data_observable(const livetrack_data_observable&) = delete;
        livetrack_data_observable(livetrack_data_observable&&) = default;
        livetrack_data_observable& operator=(const livetrack_data_observable&) = delete;
        livetrack_data_observable& operator=(livetrack_data_observable&&) = default;
        virtual ~livetrack_data_observable() {
            try {
                write({102}, "stopping acquisition");
            } catch (const std::runtime_error&) {
            }
            hid_close(_device);
            _running.store(false, std::memory_order_release);
            _loop.join();
        }

        /// start enables data acquisition.
        virtual void start() {
            std::array<uint8_t, 64> buffer;
            write({106}, "starting the raw, high-resolution tracking");
            for (;;) {
                const auto bytes_read = hid_read_timeout(_device, buffer.data(), buffer.size(), 20);
                if (bytes_read < 0) {
                    throw std::runtime_error("reading from the LiveTrack failed");
                } else if (
                    bytes_read == 64
                    && (static_cast<uint64_t>(buffer[6]) | (static_cast<uint64_t>(buffer[7]) << 8)
                        | (static_cast<uint64_t>(buffer[8]) << 16) | (static_cast<uint64_t>(buffer[9]) << 24)
                        | (static_cast<uint64_t>(buffer[10]) << 32) | (static_cast<uint64_t>(buffer[11]) << 40)
                        | (static_cast<uint64_t>(buffer[12]) << 48) | (static_cast<uint64_t>(buffer[13]) << 56))
                           == 2000) {
                    break;
                }
            }
            _started.store(true, std::memory_order_release);
        }

        protected:
        /// write sends a message to the LiveTrack.
        virtual void write(const std::array<uint8_t, 64>& buffer, const std::string& name) {
            if (hid_write(_device, buffer.data(), buffer.size()) != buffer.size()) {
                throw std::runtime_error(name + " failed");
            }
        }

        std::atomic_bool _running;
        std::atomic_bool _started;
        std::thread _loop;
        hid_device* _device;
        HandleLivetrackData _handle_livetrack_data;
        HandleException _handle_exception;
    };

    /// make_livetrack_data_observable creates a livetrack_data_observable from
    /// functors.
    template <typename HandleLivetrackData, typename HandleException>
    std::unique_ptr<livetrack_data_observable<HandleLivetrackData, HandleException>>
    make_livetrack_data_observable(HandleLivetrackData handle_livetrack_data, HandleException handle_exception) {
        return std::unique_ptr<livetrack_data_observable<HandleLivetrackData, HandleException>>(
            new livetrack_data_observable<HandleLivetrackData, HandleException>(
                std::forward<HandleLivetrackData>(handle_livetrack_data),
                std::forward<HandleException>(handle_exception)));
    }
}
