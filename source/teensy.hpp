#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <fcntl.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

/// hibiscus bundles tools to build a psychophysics platform on a Jetson TX1.
namespace hibiscus {
    /// teensy_event represents an event timestamped by the Teensy board.
    struct teensy_event {
        uint64_t t;
        uint8_t type;
    };

    /// tty represents a generic serial connection.
    class tty {
        public:
        tty(const std::string& filename, uint64_t baudrate, uint64_t timeout) :
            _filename(filename),
            _fileDescriptor(open(_filename.c_str(), O_RDWR | O_NOCTTY)) {
            if (_fileDescriptor < 0) {
                throw std::runtime_error(std::string("opening '") + _filename + "' failed");
            }
            termios options;
            if (tcgetattr(_fileDescriptor, &options) < 0) {
                throw std::logic_error("getting the terminal options failed");
            }
            cfmakeraw(&options);
            cfsetispeed(&options, baudrate);
            cfsetospeed(&options, baudrate);
            options.c_cc[VMIN] = 0;
            options.c_cc[VTIME] = timeout;
            tcsetattr(_fileDescriptor, TCSANOW, &options);
            if (tcsetattr(_fileDescriptor, TCSAFLUSH, &options) < 0) {
                throw std::logic_error("setting the terminal options failed");
            }
            tcflush(_fileDescriptor, TCIOFLUSH);
        }
        tty(const tty&) = delete;
        tty(tty&&) = default;
        tty& operator=(const tty&) = delete;
        tty& operator=(tty&&) = default;
        virtual ~tty() {}

        /// write sends data to the tty.
        virtual void write(const std::vector<uint8_t>& bytes) {
            if (::write(_fileDescriptor, bytes.data(), bytes.size()) != bytes.size()) {
                throw std::runtime_error("write error");
            }
            tcdrain(_fileDescriptor);
        }

        /// read loads a single byte from the tty.
        uint8_t read() {
            volatile uint8_t byte;
            const auto bytes_read = ::read(_fileDescriptor, const_cast<uint8_t*>(&byte), 1);
            if (bytes_read <= 0) {
                if (access(_filename.c_str(), F_OK) < 0) {
                    throw std::logic_error(std::string("'") + _filename + "' disconnected");
                }
                throw std::runtime_error("read timeout");
            }
            return byte;
        }

        protected:
        const std::string _filename;
        int32_t _fileDescriptor;
    };

    /// teensy manages the communication with the Teensy.
    class teensy {
        public:
        teensy() : _tty("/dev/ttyACM0", B9600, 20) {}
        teensy(const teensy&) = delete;
        teensy(teensy&&) = default;
        teensy& operator=(const teensy&) = delete;
        teensy& operator=(teensy&&) = default;
        virtual ~teensy() {}

        /// send sends a message to the teensy.
        virtual void send(uint8_t type) {
            write({type});
        }

        protected:
        /// write encodes a sends a message to the Teensy board.
        virtual void write(const std::vector<uint8_t>& message) {
            std::vector<uint8_t> encoded_message{0x00};
            for (auto byte : message) {
                switch (byte) {
                    case 0x00:
                        encoded_message.push_back(0xaa);
                        encoded_message.push_back(0xab);
                        break;
                    case 0xaa:
                        encoded_message.push_back(0xaa);
                        encoded_message.push_back(0xac);
                        break;
                    case 0xff:
                        encoded_message.push_back(0xaa);
                        encoded_message.push_back(0xad);
                        break;
                    default:
                        encoded_message.push_back(byte);
                }
            }
            encoded_message.push_back(0xff);
            _tty.write(encoded_message);
        }

        tty _tty;
    };

    /// specialized_teensy implements the communication with a Teensy board.
    template <typename Delegate, typename HandleException>
    class specialized_teensy : public teensy {
        public:
        specialized_teensy(Delegate delegate, HandleException handle_exception) :
            _delegate(std::forward<Delegate>(delegate)),
            _handle_exception(std::forward<HandleException>(handle_exception)),
            _running(true) {
            _delegate.handle_start(this, _tty);
            _read_loop = std::thread([this]() {
                try {
                    std::vector<uint8_t> message;
                    bool reading = false;
                    bool escaped = false;
                    while (_running.load(std::memory_order_acquire)) {
                        uint8_t byte;
                        try {
                            byte = _tty.read();
                        } catch (const std::runtime_error&) {
                            continue;
                        }
                        if (reading) {
                            if (escaped) {
                                escaped = false;
                                switch (byte) {
                                    case 0xab:
                                        message.push_back(0x00);
                                        break;
                                    case 0xac:
                                        message.push_back(0xaa);
                                        break;
                                    case 0xad:
                                        message.push_back(0xff);
                                        break;
                                    default:
                                        reading = false;
                                }
                            } else {
                                switch (byte) {
                                    case 0x00:
                                        message.clear();
                                        break;
                                    case 0xaa:
                                        escaped = true;
                                        break;
                                    case 0xff:
                                        reading = false;
                                        _delegate.handle_message(this, message);
                                        break;
                                    default:
                                        message.push_back(byte);
                                        break;
                                }
                            }
                        } else if (byte == 0x00) {
                            reading = true;
                            escaped = false;
                            message.clear();
                        }
                    }
                    _delegate.handle_stop(this, _tty);
                } catch (...) {
                    this->_handle_exception(std::current_exception());
                }
            });
        }
        specialized_teensy(const specialized_teensy&) = delete;
        specialized_teensy(specialized_teensy&&) = default;
        specialized_teensy& operator=(const specialized_teensy&) = delete;
        specialized_teensy& operator=(specialized_teensy&&) = default;
        virtual ~specialized_teensy() {
            _running.store(false, std::memory_order_release);
            _read_loop.join();
        }

        protected:
        Delegate _delegate;
        HandleException _handle_exception;
        std::atomic_bool _running;
        std::thread _read_loop;
    };

    /// teensy_record_delegate is a delegate for the record firmware.
    template <typename HandleEvent>
    class teensy_record_delegate {
        public:
        teensy_record_delegate(HandleEvent handle_event) :
            _handle_event(std::forward<HandleEvent>(handle_event)),
            _previous_teensy_t(0),
            _t_correction(0) {}
        teensy_record_delegate(const teensy_record_delegate&) = delete;
        teensy_record_delegate(teensy_record_delegate&&) = default;
        teensy_record_delegate& operator=(const teensy_record_delegate&) = delete;
        teensy_record_delegate& operator=(teensy_record_delegate&&) = default;
        virtual ~teensy_record_delegate() {}
        virtual void handle_start(teensy* parent, tty& parent_tty) {
            parent->send('r');
            {
                uint8_t state = 0;
                auto ready = false;
                while (!ready) {
                    uint8_t byte;
                    try {
                        byte = parent_tty.read();
                    } catch (const std::runtime_error&) {
                        continue;
                    }
                    switch (state) {
                        case 0:
                            if (byte == 0x00) {
                                state = 1;
                            }
                            break;
                        case 1:
                            if (byte == 'r') {
                                state = 2;
                            } else {
                                state = 0;
                            }
                            break;
                        case 2:
                            if (byte == 0xff) {
                                ready = true;
                            } else {
                                state = 0;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        virtual void handle_message(teensy*, const std::vector<uint8_t>& message) {
            if (message.size() == 5) {
                if (message[0] == 'f') {
                    const auto t = teensy_t_to_t(message_to_teensy_t(message));
                    for (auto& buffered_event : _buffered_events) {
                        if (!buffered_event.corrected) {
                            std::array<std::pair<uint64_t, uint64_t>, 3> differences_and_ts{
                                difference_and_t(t, buffered_event.event.t + _t_correction),
                                difference_and_t(
                                    t,
                                    buffered_event.event.t + _t_correction + std::numeric_limits<uint32_t>::max() + 1),
                                _t_correction > 0 ?
                                    difference_and_t(
                                        t,
                                        buffered_event.event.t + _t_correction
                                            - (std::numeric_limits<uint32_t>::max() + 1)) :
                                    std::pair<uint64_t, uint64_t>{std::numeric_limits<uint64_t>::max(), 0},
                            };
                            std::sort(differences_and_ts.begin(), differences_and_ts.end());
                            buffered_event.event.t = std::get<0>(differences_and_ts).second;
                            buffered_event.corrected = true;
                        }
                    }
                    std::sort(
                        _buffered_events.begin(),
                        _buffered_events.end(),
                        [](buffered_event first, buffered_event second) { return first.event.t < second.event.t; });
                    auto event_iterator = _buffered_events.begin();
                    for (; event_iterator != _buffered_events.end() && event_iterator->event.t < t; ++event_iterator) {
                        _handle_event(event_iterator->event);
                    }
                    _buffered_events.erase(_buffered_events.begin(), event_iterator);
                } else if (message[0] == 'd' || message[0] == 'e' || message[0] == 'l' || message[0] == 'r') {
                    _buffered_events.push_back({{message_to_teensy_t(message), message[0]}, false});
                } else if (message[0] == 'c') {
                    _handle_event(teensy_event{message_to_teensy_t(message), message[0]});
                } else {
                    _handle_event(teensy_event{teensy_t_to_t(message_to_teensy_t(message)), message[0]});
                }
            }
        }
        virtual void handle_stop(teensy*, tty&) {
            const auto t = static_cast<uint64_t>(_previous_teensy_t) + _t_correction;
            for (auto& buffered_event : _buffered_events) {
                if (!buffered_event.corrected) {
                    std::array<std::pair<uint64_t, uint64_t>, 3> differences_and_ts{
                        difference_and_t(t, buffered_event.event.t + _t_correction),
                        difference_and_t(
                            t, buffered_event.event.t + _t_correction + std::numeric_limits<uint32_t>::max() + 1),
                        _t_correction > 0 ? difference_and_t(
                            t, buffered_event.event.t + _t_correction - (std::numeric_limits<uint32_t>::max() + 1)) :
                                            std::pair<uint64_t, uint64_t>{std::numeric_limits<uint64_t>::max(), 0},
                    };
                    std::sort(differences_and_ts.begin(), differences_and_ts.end());
                    buffered_event.event.t = std::get<0>(differences_and_ts).second;
                    buffered_event.corrected = true;
                }
            }
            std::sort(
                _buffered_events.begin(), _buffered_events.end(), [](buffered_event first, buffered_event second) {
                    return first.event.t < second.event.t;
                });
            for (auto buffered_event : _buffered_events) {
                _handle_event(buffered_event.event);
            }
        }

        protected:
        /// buffered_event is used as part of the timestamp correction process.
        struct buffered_event {
            teensy_event event;
            bool corrected;
        };

        /// message_to_teensy_t interprets a message's bytes.
        static uint32_t message_to_teensy_t(const std::vector<uint8_t>& message) {
            return static_cast<uint32_t>(message[1]) | (static_cast<uint32_t>(message[2]) << 8)
                   | (static_cast<uint32_t>(message[3]) << 16) | (static_cast<uint32_t>(message[4]) << 24);
        }

        /// absolute_difference returns the absolute distance between two unsigned integers.
        static uint64_t absolute_difference(uint64_t first, uint64_t second) {
            if (first > second) {
                return first - second;
            }
            return second - first;
        }

        /// difference_and_t wraps a candidate t and a distance in a pair.
        static std::pair<uint64_t, uint64_t> difference_and_t(uint64_t t, uint64_t candidate_t) {
            return {absolute_difference(t, candidate_t), candidate_t};
        }

        /// teensy_t_to_t converts uint32 timestamps to uint64.
        uint64_t teensy_t_to_t(uint32_t teensy_t) {
            if (teensy_t < _previous_teensy_t) {
                _t_correction += static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
            }
            _previous_teensy_t = teensy_t;
            return static_cast<uint64_t>(teensy_t) + _t_correction;
        }

        HandleEvent _handle_event;
        uint32_t _previous_teensy_t;
        uint64_t _t_correction;
        std::vector<buffered_event> _buffered_events;
    };

    /// teensy_eventide_delegate is a delegate for the record firmware.
    template <typename HandleByte>
    class teensy_eventide_delegate {
        public:
        teensy_eventide_delegate(HandleByte handle_byte) : _handle_byte(std::forward<HandleByte>(handle_byte)) {}
        teensy_eventide_delegate(const teensy_eventide_delegate&) = delete;
        teensy_eventide_delegate(teensy_eventide_delegate&&) = default;
        teensy_eventide_delegate& operator=(const teensy_eventide_delegate&) = delete;
        teensy_eventide_delegate& operator=(teensy_eventide_delegate&&) = default;
        virtual ~teensy_eventide_delegate() {}
        virtual void handle_start(teensy*, tty&) {}
        virtual void handle_message(teensy*, const std::vector<uint8_t>& message) {
            if (message.size() == 1) {
                _handle_byte(message.front());
            }
        }
        virtual void handle_stop(teensy*, tty&) {}

        protected:
        HandleByte _handle_byte;
    };

    /// make_record_teensy creates a teensy from functors.
    template <typename HandleEvent, typename HandleException>
    std::unique_ptr<specialized_teensy<teensy_record_delegate<HandleEvent>, HandleException>>
    make_teensy_record(HandleEvent handle_event, HandleException handle_exception) {
        return std::unique_ptr<specialized_teensy<teensy_record_delegate<HandleEvent>, HandleException>>(
            new specialized_teensy<teensy_record_delegate<HandleEvent>, HandleException>(
                teensy_record_delegate<HandleEvent>(std::forward<HandleEvent>(handle_event)),
                std::forward<HandleException>(handle_exception)));
    }

    /// make_teensy_eventide is an interface to the teensy_eventide firmware.
    template <typename HandleByte, typename HandleException>
    std::unique_ptr<specialized_teensy<teensy_eventide_delegate<HandleByte>, HandleException>>
    make_teensy_eventide(HandleByte handle_byte, HandleException handle_exception) {
        return std::unique_ptr<specialized_teensy<teensy_eventide_delegate<HandleByte>, HandleException>>(
            new specialized_teensy<teensy_eventide_delegate<HandleByte>, HandleException>(
                teensy_eventide_delegate<HandleByte>(std::forward<HandleByte>(handle_byte)),
                std::forward<HandleException>(handle_exception)));
    }
}
