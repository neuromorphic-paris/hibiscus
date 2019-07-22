#pragma once

#include <atomic>
#include <fcntl.h>
#include <jpeglib.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <thread>
#include <vector>

/// hibiscus bundles tools to build a psychophysics platform on a Jetson TX1.
namespace hibiscus {
    /// livetrack_video_observable retrieves frames from a LiveTrack.
    template <typename HandleFrame, typename HandleException>
    class livetrack_video_observable {
        public:
        livetrack_video_observable(
            const std::string& source,
            HandleFrame handle_frame,
            HandleException handle_exception) :
            _handle_frame(std::forward<HandleFrame>(handle_frame)),
            _handle_exception(std::forward<HandleException>(handle_exception)),
            _running(true) {
            _file_descriptor = v4l2_open(source.c_str(), O_RDWR);
            if (_file_descriptor < 0) {
                throw std::runtime_error(std::string("opening '") + source + "' failed");
            }
            {
                v4l2_fmtdesc format_description;
                format_description.index = 0;
                format_description.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (v4l2_ioctl(_file_descriptor, VIDIOC_ENUM_FMT, &format_description) < 0) {
                    v4l2_close(_file_descriptor);
                    throw std::runtime_error("retrieving the LiveTrack format description failed");
                }
                v4l2_frmsizeenum frame_size;
                frame_size.index = 0;
                frame_size.pixel_format = format_description.pixelformat;
                if (v4l2_ioctl(_file_descriptor, VIDIOC_ENUM_FRAMESIZES, &frame_size) < 0) {
                    v4l2_close(_file_descriptor);
                    throw std::runtime_error("retrieving the LiveTrack frame size failed");
                }
                if (frame_size.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                    v4l2_close(_file_descriptor);
                    throw std::runtime_error("unsupported LiveTrack frame type");
                }

                if (frame_size.discrete.width != 1280 || frame_size.discrete.height != 280) {
                    v4l2_close(_file_descriptor);
                    throw std::runtime_error("unexpected LiveTrack frame size");
                }
                _bytes.resize(frame_size.discrete.width * frame_size.discrete.height * 3);
                _encoded_bytes.resize(_bytes.size());
            }
            _loop = std::thread([this]() {
                jpeg_decompress_struct decompress_information;
                jpeg_create_decompress(&decompress_information);
                jpeg_error_mgr error_message;
                decompress_information.err = jpeg_std_error(&error_message);
                error_message.error_exit = [](j_common_ptr information) {
                    char message[JMSG_LENGTH_MAX];
                    (*(information->err->format_message))(information, message);
                    throw std::runtime_error(message);
                };
                decompress_information.do_fancy_upsampling = false;
                try {
                    pollfd poll_file_descriptor;
                    poll_file_descriptor.fd = _file_descriptor;
                    poll_file_descriptor.events = POLLIN;
                    while (_running.load(std::memory_order_acquire)) {
                        const auto poll_result = poll(&poll_file_descriptor, 1, 25);
                        if (poll_result < 0) {
                            throw std::runtime_error("poll LiveTrack failed");
                        }
                        if (poll_result > 0) {
                            const auto read_bytes =
                                v4l2_read(_file_descriptor, _encoded_bytes.data(), _encoded_bytes.size());
                            jpeg_mem_src(&decompress_information, _encoded_bytes.data(), read_bytes);
                            jpeg_read_header(&decompress_information, 1);
                            jpeg_start_decompress(&decompress_information);
                            auto output = _bytes.data();
                            while (decompress_information.output_scanline < decompress_information.image_height) {
                                const auto lines_read = jpeg_read_scanlines(
                                    &decompress_information, reinterpret_cast<JSAMPARRAY>(&output), 1);
                                output += lines_read * decompress_information.image_width
                                          * decompress_information.num_components;
                            }
                            jpeg_finish_decompress(&decompress_information);
                            _handle_frame(_bytes);
                        }
                    }
                } catch (...) {
                    _handle_exception(std::current_exception());
                }
                jpeg_destroy_decompress(&decompress_information);
            });
        }
        livetrack_video_observable(const livetrack_video_observable&) = delete;
        livetrack_video_observable(livetrack_video_observable&&) = default;
        livetrack_video_observable& operator=(const livetrack_video_observable&) = delete;
        livetrack_video_observable& operator=(livetrack_video_observable&&) = default;
        virtual ~livetrack_video_observable() {
            _running.store(false, std::memory_order_release);
            _loop.join();
            v4l2_close(_file_descriptor);
        }

        protected:
        HandleFrame _handle_frame;
        HandleException _handle_exception;
        std::atomic_bool _running;
        std::thread _loop;
        int32_t _file_descriptor;
        std::vector<uint8_t> _bytes;
        std::vector<uint8_t> _encoded_bytes;
    };

    /// make_livetrack_video_observable creates a livetrack_video_observable from
    /// functors.
    template <typename HandleFrame, typename HandleException>
    std::unique_ptr<livetrack_video_observable<HandleFrame, HandleException>> make_livetrack_video_observable(
        const std::string& source,
        HandleFrame handle_frame,
        HandleException handle_exception) {
        return std::unique_ptr<livetrack_video_observable<HandleFrame, HandleException>>(
            new livetrack_video_observable<HandleFrame, HandleException>(
                source, std::forward<HandleFrame>(handle_frame), std::forward<HandleException>(handle_exception)));
    }
}
