#pragma once

#include <memory>
#include <ncursesw/ncurses.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

/// hibiscus bundles tools to build a psychophysics platform on a Jetson TX1.
namespace hibiscus {
    /// terminal is a wrapper for ncurses.
    template <typename HandleKeypress, typename HandleException>
    class terminal {
        public:
        terminal(HandleKeypress handle_keypress, HandleException handle_exception) :
            _handle_keypress(std::forward<HandleKeypress>(handle_keypress)),
            _handle_exception(std::forward<HandleException>(handle_exception)),
            _running(true),
            _update_required(false) {
            _accessing_chunks_and_attributes.clear(std::memory_order_release);
            initscr();
            cbreak();
            noecho();
            keypad(stdscr, true);
            curs_set(0);
            start_color();
            use_default_colors();
            init_pair(0, COLOR_BLACK, -1);
            init_pair(1, COLOR_RED, -1);
            init_pair(2, COLOR_GREEN, -1);
            init_pair(3, COLOR_YELLOW, -1);
            init_pair(4, COLOR_BLUE, -1);
            init_pair(5, COLOR_MAGENTA, -1);
            init_pair(6, COLOR_CYAN, -1);
            init_pair(7, COLOR_WHITE, -1);
            timeout(20);
            _loop = std::thread([this]() {
                while (_running.load(std::memory_order_acquire)) {
                    while (_accessing_chunks_and_attributes.test_and_set(std::memory_order_acquire)) {
                    }
                    if (_update_required) {
                        clear();
                        move(0, 0);
                        for (const auto& chunk_and_attribute : _chunks_and_attributes) {
                            attron(chunk_and_attribute.second);
                            addstr(chunk_and_attribute.first.data());
                            attroff(chunk_and_attribute.second);
                        }
                        refresh();
                        _update_required = false;
                    }
                    _accessing_chunks_and_attributes.clear(std::memory_order_release);
                    const auto character = getch();
                    if (character != -1) {
                        _handle_keypress(character);
                    }
                }
            });
        }
        terminal(const terminal&) = delete;
        terminal(terminal&&) = default;
        terminal& operator=(const terminal&) = delete;
        terminal& operator=(terminal&&) = default;
        virtual ~terminal() {
            _running.store(false, std::memory_order_release);
            _loop.join();
            endwin();
        }

        /// set_chunks_and_attributes shows text on the terminal.
        template <typename Iterator>
        void set_chunks_and_attributes(Iterator begin, Iterator end) {
            while (_accessing_chunks_and_attributes.test_and_set(std::memory_order_acquire)) {
            }
            _update_required = true;
            _chunks_and_attributes.assign(begin, end);
            _accessing_chunks_and_attributes.clear(std::memory_order_release);
        }
        virtual void
        set_chunks_and_attributes(std::initializer_list<std::pair<std::string, int32_t>> chunks_and_attributes) {
            set_chunks_and_attributes(chunks_and_attributes.begin(), chunks_and_attributes.end());
        }

        protected:
        HandleKeypress _handle_keypress;
        HandleException _handle_exception;
        std::atomic_bool _running;
        std::thread _loop;
        std::atomic_flag _accessing_chunks_and_attributes;
        bool _update_required;
        std::vector<std::pair<std::string, int32_t>> _chunks_and_attributes;
    };

    /// make_terminal creates a terminal from functors.
    template <typename HandleKeypress, typename HandleException>
    std::unique_ptr<terminal<HandleKeypress, HandleException>>
    make_terminal(HandleKeypress handle_keypress, HandleException handle_exception) {
        return std::unique_ptr<terminal<HandleKeypress, HandleException>>(new terminal<HandleKeypress, HandleException>(
            std::forward<HandleKeypress>(handle_keypress), std::forward<HandleException>(handle_exception)));
    }
}
