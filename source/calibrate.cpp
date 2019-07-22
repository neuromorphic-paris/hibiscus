#include "../third_party/CppNumericalSolvers/include/cppoptlib/problem.h"
#include "../third_party/CppNumericalSolvers/include/cppoptlib/solver/neldermeadsolver.h"
#include "../third_party/hummingbird/source/display.hpp"
#include "../third_party/hummingbird/source/lightcrafter.hpp"
#include "../third_party/hummingbird/source/rotate.hpp"
#include "../third_party/hummingbird/third_party/pontella/source/pontella.hpp"
#include "calibration.hpp"
#include "livetrack_data_observable.hpp"
#include "livetrack_video_observable.hpp"
#include "terminal.hpp"
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/SVD>
#include <fstream>
#include <random>

/// calibration_optimization optimization the eye tracker calibration matrix.
class calibration_optimization : public cppoptlib::Problem<double> {
    public:
    calibration_optimization(
        const std::vector<std::array<double, 3>>& source,
        const std::vector<std::array<double, 3>>& target) :
        _source(source),
        _target(target) {
        if (_source.size() != _target.size()) {
            throw std::logic_error("source and targt must have the same size");
        }
    }
    virtual double value(const Eigen::Matrix<double, Eigen::Dynamic, 1>& normalized_vector) {
        const auto errors = target_errors(normalized_vector);
        return *std::max_element(errors.begin(), errors.end());
    }

    /// target_errors returns the target error for each point.
    virtual std::vector<double> target_errors(const Eigen::Matrix<double, Eigen::Dynamic, 1>& normalized_vector) {
        std::array<double, 16> matrix;
        for (std::size_t index = 0; index < matrix.size(); ++index) {
            matrix[index] = normalized_vector(index);
        }
        std::vector<double> result(_source.size());
        std::transform(
            _source.begin(),
            _source.end(),
            _target.begin(),
            result.begin(),
            [=](std::array<double, 3> source_point, std::array<double, 3> target_point) {
                return hibiscus::norm(hibiscus::difference(hibiscus::projection(matrix, source_point), target_point));
            });
        return result;
    }

    protected:
    const std::vector<std::array<double, 3>>& _source;
    const std::vector<std::array<double, 3>>& _target;
};

/// estimate_calibration calculates the eye tracker calibration matrix.
template <typename SourceIterator, typename TargetIterator>
inline hibiscus::calibration
estimate_calibration(SourceIterator source_begin, SourceIterator source_end, TargetIterator target_begin) {
    hibiscus::calibration result;
    const auto size = static_cast<std::size_t>(std::distance(source_begin, source_end));
    std::vector<std::array<double, 3>> source(size);
    std::transform(source_begin, source_end, source.begin(), hibiscus::eye);
    std::vector<std::array<double, 3>> target(size);
    std::transform(target_begin, std::next(target_begin, size), target.begin(), [](std::array<double, 2> point) {
        return std::array<double, 3>{std::get<0>(point), std::get<1>(point), 0};
    });
    const auto source_mean = hibiscus::mean<3>(source.begin(), source.end());
    const auto target_mean = hibiscus::mean<3>(target.begin(), target.end());
    std::transform(source.begin(), source.end(), source.begin(), [source_mean](std::array<double, 3> point) {
        return hibiscus::difference(point, source_mean);
    });
    std::transform(target.begin(), target.end(), target.begin(), [target_mean](std::array<double, 3> point) {
        return hibiscus::difference(point, target_mean);
    });
    const auto source_scale =
        std::accumulate(source.begin(), source.end(), 0.0, [size](double accumulator, std::array<double, 3> point) {
            return accumulator + hibiscus::norm(point) / size;
        });
    const auto target_scale =
        std::accumulate(target.begin(), target.end(), 0.0, [size](double accumulator, std::array<double, 3> point) {
            return accumulator + hibiscus::norm(point) / size;
        });
    std::transform(source.begin(), source.end(), source.begin(), [source_scale](std::array<double, 3> point) {
        return hibiscus::product(point, 1.0 / source_scale);
    });
    std::transform(target.begin(), target.end(), target.begin(), [target_scale](std::array<double, 3> point) {
        return hibiscus::product(point, 1.0 / target_scale);
    });
    Eigen::MatrixXd a;
    a.setZero(3 * size, 16);
    for (std::size_t index = 0; index < size; ++index) {
        a(index * 3, 0) = -std::get<0>(source[index]);
        a(index * 3, 1) = -std::get<1>(source[index]);
        a(index * 3, 2) = -std::get<2>(source[index]);
        a(index * 3, 3) = -1;
        a(index * 3, 12) = std::get<0>(source[index]) * std::get<0>(target[index]);
        a(index * 3, 13) = std::get<1>(source[index]) * std::get<0>(target[index]);
        a(index * 3, 13) = std::get<2>(source[index]) * std::get<0>(target[index]);
        a(index * 3, 15) = std::get<0>(target[index]);
        a(index * 3 + 1, 4) = -std::get<0>(source[index]);
        a(index * 3 + 1, 5) = -std::get<1>(source[index]);
        a(index * 3 + 1, 6) = -std::get<2>(source[index]);
        a(index * 3 + 1, 7) = -1;
        a(index * 3 + 1, 12) = std::get<0>(source[index]) * std::get<1>(target[index]);
        a(index * 3 + 1, 13) = std::get<1>(source[index]) * std::get<1>(target[index]);
        a(index * 3 + 1, 14) = std::get<2>(source[index]) * std::get<1>(target[index]);
        a(index * 3 + 1, 15) = std::get<1>(target[index]);
        a(index * 3 + 2, 8) = -std::get<0>(source[index]);
        a(index * 3 + 2, 9) = -std::get<1>(source[index]);
        a(index * 3 + 2, 10) = -std::get<2>(source[index]);
        a(index * 3 + 2, 11) = -1;
        a(index * 3 + 2, 12) = std::get<0>(source[index]) * std::get<2>(target[index]);
        a(index * 3 + 2, 13) = std::get<1>(source[index]) * std::get<2>(target[index]);
        a(index * 3 + 2, 14) = std::get<2>(source[index]) * std::get<2>(target[index]);
        a(index * 3 + 2, 15) = std::get<2>(target[index]);
    }
    Eigen::JacobiSVD<decltype(a)> svd(a, Eigen::ComputeThinV);
    Eigen::Matrix<double, Eigen::Dynamic, 1> normalized_vector(svd.matrixV().col(15));
    {
        calibration_optimization heuristic(source, target);
        cppoptlib::NelderMeadSolver<calibration_optimization> solver;
        solver.minimize(heuristic, normalized_vector);
        auto errors = heuristic.target_errors(normalized_vector);
        std::transform(errors.begin(), errors.end(), errors.begin(), [target_scale](double error) {
            return error * target_scale;
        });
        result.points_and_errors.reserve(size);
        auto target = target_begin;
        for (const auto error : errors) {
            result.points_and_errors.emplace_back(*target, error);
            ++target;
        }
    }
    Eigen::Map<Eigen::Matrix<double, 4, 4, Eigen::RowMajor>> normalized_matrix(normalized_vector.data());
    Eigen::Matrix4d source_normalize_transform;
    source_normalize_transform << (Eigen::Matrix3d::Identity() / source_scale),
        (-(Eigen::Matrix<double, 3, 1>() << std::get<0>(source_mean),
           std::get<1>(source_mean),
           std::get<2>(source_mean))
              .finished()
         / source_scale),
        Eigen::Matrix<double, 1, 3>::Zero(), Eigen::Matrix<double, 1, 1>::Ones();
    Eigen::Matrix4d target_normalize_transform;
    target_normalize_transform << (Eigen::Matrix3d::Identity() / target_scale),
        (-(Eigen::Matrix<double, 3, 1>() << std::get<0>(target_mean), std::get<1>(target_mean), 0).finished()
         / target_scale),
        Eigen::Matrix<double, 1, 3>::Zero(), Eigen::Matrix<double, 1, 1>::Ones();
    Eigen::Map<Eigen::Matrix<double, 4, 4, Eigen::RowMajor>> matrix_wrapper(result.matrix.data());
    matrix_wrapper = target_normalize_transform.inverse() * normalized_matrix * source_normalize_transform;
    return result;
}

/// gaze_map draws a gaze map from source points and a calibration matrix.
template <typename Iterator>
inline std::vector<uint8_t> gaze_map(
    const std::array<double, 16> matrix,
    Iterator begin,
    Iterator end,
    const std::array<uint8_t, 3> color,
    const uint16_t radius,
    const double cutoff) {
    std::vector<double> pattern((radius * 2 + 1) * (radius * 2 + 1));
    const auto spread = std::log(cutoff) / (2 * std::pow(radius, 2));
    for (uint16_t y = 0; y < radius * 2 + 1; ++y) {
        for (uint16_t x = 0; x < radius * 2 + 1; ++x) {
            pattern[x + y * (2 * radius + 1)] = std::exp(spread * (std::pow(radius - x, 2) + std::pow(radius - y, 2)));
        }
    }
    std::vector<double> gazes(343 * 342, 0.0);
    for (auto iterator = begin; iterator != end; ++iterator) {
        const auto projected_point = hibiscus::projection(matrix, hibiscus::eye(*iterator));
        for (uint16_t y = 0; y < radius * 2 + 1; ++y) {
            for (uint16_t x = 0; x < radius * 2 + 1; ++x) {
                const auto pixel_x = static_cast<int32_t>(std::round(std::get<0>(projected_point))) + x - radius;
                const auto pixel_y = static_cast<int32_t>(std::round(std::get<1>(projected_point))) + y - radius;
                if (pixel_x >= 0 && pixel_x < 343 && pixel_y >= 0 && pixel_y < 342) {
                    gazes[pixel_x + pixel_y * 343] += pattern[x + y * (2 * radius + 1)];
                }
            }
        }
    }
    const auto gazes_maximum = *std::max_element(gazes.begin(), gazes.end());
    std::vector<uint8_t> result(343 * 342 * 3, 0);
    for (uint16_t y = 0; y < 342; ++y) {
        for (uint16_t x = 0; x < 343; ++x) {
            for (uint8_t channel = 0; channel < 3; ++channel) {
                result[(x + y * 343) * 3 + channel] =
                    static_cast<uint8_t>(std::round(gazes[x + y * 343] / gazes_maximum * color[channel]));
            }
        }
    }
    return result;
}

/// draw_pattern draws the given binary pattern to the frame, centered at the
/// given position. The frame must be 343 *342 * 3 bytes long.
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

/// estimate_calibration_and_gaze_map estimates the calibration and represents the measurements.
template <typename TargetIterator, typename IndexToGazesIterator>
std::pair<hibiscus::calibration, std::vector<uint8_t>> estimate_calibration_and_gaze_map(
    TargetIterator target_begin,
    TargetIterator target_end,
    IndexToGazesIterator index_to_gazes_begin,
    const std::array<uint8_t, 3> color,
    const std::vector<std::array<double, 2>>& extra_gazes) {
    std::pair<hibiscus::calibration, std::vector<uint8_t>> calibration_and_gaze_map;
    const auto size = static_cast<std::size_t>(std::distance(target_begin, target_end));
    std::vector<std::array<double, 2>> measurements;
    measurements.reserve(size);
    {
        auto index_to_gazes_iterator = index_to_gazes_begin;
        for (auto target_iterator = target_begin; target_iterator != target_end; ++target_iterator) {
            if (index_to_gazes_iterator->empty()) {
                measurements.push_back(*target_iterator);
            } else {
                measurements.push_back(
                    hibiscus::median<2>(index_to_gazes_iterator->begin(), index_to_gazes_iterator->end()));
            }
            ++index_to_gazes_iterator;
        }
    }
    calibration_and_gaze_map.first = estimate_calibration(measurements.begin(), measurements.end(), target_begin);
    std::vector<std::array<double, 2>> gazes;
    {
        auto index_to_gazes_iterator = index_to_gazes_begin;
        for (auto target_iterator = target_begin; target_iterator != target_end; ++target_iterator) {
            gazes.insert(gazes.end(), index_to_gazes_iterator->begin(), index_to_gazes_iterator->end());
            ++index_to_gazes_iterator;
        }
    }
    gazes.insert(gazes.end(), extra_gazes.begin(), extra_gazes.end());
    calibration_and_gaze_map.second =
        gaze_map(calibration_and_gaze_map.first.matrix, gazes.begin(), gazes.end(), color, 10, 0.05);
    for (const auto point : measurements) {
        const auto projected_point = hibiscus::projection(calibration_and_gaze_map.first.matrix, hibiscus::eye(point));
        draw_pattern(
            calibration_and_gaze_map.second,
            static_cast<uint16_t>(std::round(std::get<0>(projected_point))),
            static_cast<uint16_t>(std::round(std::get<1>(projected_point))),
            {
                false, false, true,  false, false, false, false, true,  false, false, true,  true,  true,
                true,  true,  false, false, true,  false, false, false, false, true,  false, false,
            },
            5,
            {255, 255, 0});
    }
    for (auto target_iterator = target_begin; target_iterator != target_end; ++target_iterator) {
        draw_pattern(
            calibration_and_gaze_map.second,
            static_cast<uint16_t>(std::get<0>(*target_iterator)),
            static_cast<uint16_t>(std::get<1>(*target_iterator)),
            {
                false,
                true,
                false,
                true,
                true,
                true,
                false,
                true,
                false,
            },
            3,
            {255, 255, 255});
    }
    return calibration_and_gaze_map;
}

/// downsample converts a 1280 x 240 RGB frame to a 576 x 108 RGB frame.
inline void downsample(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    uint16_t x_offset = 0;
    uint16_t x_input = 0;
    std::array<uint8_t, 4> x_areas{9, 9, 2, 1};
    uint8_t x_range = 3;
    uint16_t y_offset = 0;
    uint16_t y_input = 0;
    std::array<uint8_t, 4> y_areas{9, 9, 2, 1};
    uint8_t y_range = 3;
    for (uint16_t y = 0; y < 108; ++y) {
        for (uint16_t x = 0; x < 576; ++x) {
            const auto x_reference = x_offset + x_input;
            const auto y_reference = y_offset + y_input;
            for (uint8_t channel = 0; channel < 3; ++channel) {
                uint32_t sum = 0;
                for (uint8_t y_index = 0; y_index < y_range; ++y_index) {
                    for (uint8_t x_index = 0; x_index < x_range; ++x_index) {
                        sum += input[((x_reference + x_index) + (y_reference + y_index) * 1280) * 3 + channel]
                               * x_areas[x_index] * y_areas[y_index];
                    }
                }
                output[(x + y * 576) * 3 + channel] = static_cast<uint8_t>(sum / 400);
            }
            switch (x_input) {
                case 0:
                    x_input = 2;
                    x_areas[0] = 7;
                    x_areas[1] = 9;
                    x_areas[2] = 4;
                    break;
                case 2:
                    x_input = 4;
                    x_areas[0] = 5;
                    x_areas[1] = 9;
                    x_areas[2] = 6;
                    break;
                case 4:
                    x_input = 6;
                    x_areas[0] = 3;
                    x_areas[1] = 9;
                    x_areas[2] = 8;
                    break;
                case 6:
                    x_input = 8;
                    x_areas[0] = 1;
                    x_areas[1] = 9;
                    x_areas[2] = 9;
                    x_range = 4;
                    break;
                case 8:
                    x_input = 11;
                    x_areas[0] = 8;
                    x_areas[1] = 9;
                    x_areas[2] = 3;
                    x_range = 3;
                    break;
                case 11:
                    x_input = 13;
                    x_areas[0] = 6;
                    x_areas[1] = 9;
                    x_areas[2] = 5;
                    break;
                case 13:
                    x_input = 15;
                    x_areas[0] = 4;
                    x_areas[1] = 9;
                    x_areas[2] = 7;
                    break;
                case 15:
                    x_input = 17;
                    x_areas[0] = 2;
                    x_areas[1] = 9;
                    x_areas[2] = 9;
                    break;
                case 17:
                    x_input = 0;
                    x_areas[0] = 9;
                    x_areas[1] = 9;
                    x_areas[2] = 2;
                    x_offset += 20;
                    break;
                default:
                    break;
            }
        }
        x_offset = 0;
        switch (y_input) {
            case 0:
                y_input = 2;
                y_areas[0] = 7;
                y_areas[1] = 9;
                y_areas[2] = 4;
                break;
            case 2:
                y_input = 4;
                y_areas[0] = 5;
                y_areas[1] = 9;
                y_areas[2] = 6;
                break;
            case 4:
                y_input = 6;
                y_areas[0] = 3;
                y_areas[1] = 9;
                y_areas[2] = 8;
                break;
            case 6:
                y_input = 8;
                y_areas[0] = 1;
                y_areas[1] = 9;
                y_areas[2] = 9;
                y_range = 4;
                break;
            case 8:
                y_input = 11;
                y_areas[0] = 8;
                y_areas[1] = 9;
                y_areas[2] = 3;
                y_range = 3;
                break;
            case 11:
                y_input = 13;
                y_areas[0] = 6;
                y_areas[1] = 9;
                y_areas[2] = 5;
                break;
            case 13:
                y_input = 15;
                y_areas[0] = 4;
                y_areas[1] = 9;
                y_areas[2] = 7;
                break;
            case 15:
                y_input = 17;
                y_areas[0] = 2;
                y_areas[1] = 9;
                y_areas[2] = 9;
                break;
            case 17:
                y_input = 0;
                y_areas[0] = 9;
                y_areas[1] = 9;
                y_areas[2] = 2;
                y_offset += 20;
                break;
            default:
                break;
        }
    }
}

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

/// phase defines the app phase, and is used for threads synchronization.
enum class phase {
    display,
    flush,
    acquisition,
    idle,
};

int main(int argc, char* argv[]) {
    return pontella::main(
        {
            "calibrate estimates the parameters of the Livetrack to DMD 4 x 4 "
            "calibration matrix",
            "Syntax: ./calibrate [options] output.json [dump.csv]",
            "Available options:",
            "    -p parameters.json, --parameters parameters.json    sets the "
            "calibration parameters",
            "        default file content:",
            "            {",
            "                \"before_fixation_duration\": 1200,",
            "                \"fixation_duration\": 1100,",
            "                \"after_fixation_duration\": 200,",
            "                \"points\": [",
            "                    [ 34,  34],",
            "                    [171,  34],",
            "                    [308,  34],",
            "                    [ 34, 171],",
            "                    [171, 171],",
            "                    [308, 171],",
            "                    [ 34, 308],",
            "                    [171, 308],",
            "                    [308, 308]",
            "                ],",
            "                \"pattern\": [",
            "                    \"   #   \",",
            "                    \"   #   \",",
            "                    \"   #   \",",
            "                    \"#######\",",
            "                    \"   #   \",",
            "                    \"   #   \",",
            "                    \"   #   \",",
            "                ]",
            "            }",
            "        the durations are expressed in milliseconds",
            "        there must be at least four points (results are more "
            "accurate with more points)",
            "        the pattern must have an odd number of rows and columns,",
            "        and must contain only '#' and ' ' characters (representing "
            "on and off pixels, respectively)",
            "    -i [ip], --ip [ip]                                  sets the "
            "LightCrafter IP address",
            "                                                            "
            "defaults to 10.10.10.100",
            "    -f, --force                                         overwrites "
            "the output file if it exists",
            "    -h, --help                                          shows this "
            "help message",
        },
        argc,
        argv,
        -1,
        {{"parameters", {"p"}}, {"ip", {"i"}}},
        {{"force", {"f"}}},
        [](pontella::command command) {
            if (command.arguments.size() != 1 && command.arguments.size() != 2) {
                throw std::runtime_error("One or two arguments are expected");
            }
            std::ofstream dump;
            if (command.arguments.size() == 2) {
                dump.open(command.arguments.back());
                if (!dump.good()) {
                    throw std::runtime_error(
                        std::string("'" + command.arguments.back() + "' could not be open for writing"));
                }
                dump << "t/-1,"
                     << "left_pupil_x/point_x,"
                     << "left_pupil_y/point_y,"
                     << "left_glint_x/phase,"
                     << "left_glint_y/-1,"
                     << "right_pupil_x/-1,"
                     << "right_pupil_y/-1,"
                     << "right_glint_x/-1,"
                     << "right_glint_y/-1\r\n";
            }
            {
                bool exists;
                {
                    std::ifstream input(command.arguments[0]);
                    exists = input.good();
                }
                if (exists && command.flags.find("force") == command.flags.end()) {
                    throw std::runtime_error(
                        std::string("'") + command.arguments[0] + "' already exists (use --force to overwrite it)");
                }
                {
                    std::ofstream output(command.arguments[0], std::ofstream::app);
                    if (!output.good()) {
                        throw std::runtime_error(
                            std::string("'") + command.arguments[0] + "' could not be open for writing");
                    }
                }
                if (!exists) {
                    std::remove(command.arguments[0].c_str());
                }
            }
            std::chrono::milliseconds before_fixation_duration(1200);
            std::chrono::milliseconds fixation_duration(1100);
            std::chrono::milliseconds after_fixation_duration(200);
            std::vector<std::array<double, 2>> points{
                {34, 34},
                {171, 34},
                {308, 34},
                {34, 171},
                {171, 171},
                {308, 171},
                {34, 308},
                {171, 308},
                {308, 308},
            };
            std::vector<bool> pattern{
                false, false, false, true,  false, false, false, false, false, false, true,  false, false,
                false, false, false, false, true,  false, false, false, true,  true,  true,  true,  true,
                true,  true,  false, false, false, true,  false, false, false, false, false, false, true,
                false, false, false, false, false, false, true,  false, false, false,
            };
            uint16_t pattern_width = 7;
            {
                const auto name_and_value = command.options.find("parameters");
                if (name_and_value != command.options.end()) {
                    std::ifstream json_input(name_and_value->second);
                    if (!json_input.good()) {
                        throw std::runtime_error(
                            std::string("'") + name_and_value->second + "' could not be open for reading");
                    }
                    nlohmann::json json;
                    try {
                        json_input >> json;
                    } catch (const nlohmann::detail::parse_error& exception) {
                        throw std::runtime_error(exception.what());
                    }
                    if (!json.is_object()) {
                        throw std::runtime_error(
                            std::string("'") + name_and_value->second + "' must contain a JSON object");
                    }
                    for (auto json_iterator = json.begin(); json_iterator != json.end(); ++json_iterator) {
                        if (json_iterator.key() == "before_fixation_duration") {
                            if (!json_iterator.value().is_number()) {
                                throw std::runtime_error("the key 'fixation_duration' must "
                                                         "be associated with a number");
                            }
                            const double raw_before_fixation_duration = json_iterator.value();
                            if (raw_before_fixation_duration < 0) {
                                throw std::runtime_error("'before_fixation_duration' must be a postive number");
                            }
                            if (static_cast<uint64_t>(raw_before_fixation_duration) != raw_before_fixation_duration) {
                                throw std::runtime_error("'before_fixation_duration' must be an integer");
                            }
                            before_fixation_duration =
                                std::chrono::milliseconds(static_cast<uint64_t>(raw_before_fixation_duration));
                        } else if (json_iterator.key() == "fixation_duration") {
                            if (!json_iterator.value().is_number()) {
                                throw std::runtime_error("the key 'fixation_duration' must "
                                                         "be associated with a number");
                            }
                            const double raw_fixation_duration = json_iterator.value();
                            if (raw_fixation_duration < 0) {
                                throw std::runtime_error("'fixation_duration' must be a postive number");
                            }
                            if (static_cast<uint64_t>(raw_fixation_duration) != raw_fixation_duration) {
                                throw std::runtime_error("'fixation_duration' must be an integer");
                            }
                            fixation_duration = std::chrono::milliseconds(static_cast<uint64_t>(raw_fixation_duration));
                        } else if (json_iterator.key() == "after_fixation_duration") {
                            if (!json_iterator.value().is_number()) {
                                throw std::runtime_error("the key 'fixation_duration' must "
                                                         "be associated with a number");
                            }
                            const double raw_after_fixation_duration = json_iterator.value();
                            if (raw_after_fixation_duration < 0) {
                                throw std::runtime_error("'after_fixation_duration' must be a postive number");
                            }
                            if (static_cast<uint64_t>(raw_after_fixation_duration) != raw_after_fixation_duration) {
                                throw std::runtime_error("'after_fixation_duration' must be an integer");
                            }
                            after_fixation_duration =
                                std::chrono::milliseconds(static_cast<uint64_t>(raw_after_fixation_duration));
                        } else if (json_iterator.key() == "points") {
                            if (!json_iterator.value().is_array()) {
                                throw std::runtime_error("the key 'points' must be associated with an array");
                            }
                            points.clear();
                            points.reserve(json_iterator.value().size());
                            for (const auto& point : json_iterator.value()) {
                                if (!point.is_array() || point.size() != 2) {
                                    throw std::runtime_error("the elements of the 'points' array must be "
                                                             "two-elements arrays");
                                }
                                points.push_back({point[0], point[1]});
                            }
                        } else if (json_iterator.key() == "pattern") {
                            if (!json_iterator.value().is_array()) {
                                throw std::runtime_error("the key 'pattern' must be associated with an array");
                            }
                            const uint16_t pattern_height = json_iterator.value().size();
                            if (pattern_height % 2 == 0) {
                                throw std::runtime_error("the 'pattern' array must have an "
                                                         "odd number of elements");
                            }
                            pattern.clear();
                            if (!json_iterator.value().empty()) {
                                auto first = true;
                                for (const auto& line : json_iterator.value()) {
                                    if (!line.is_string()) {
                                        throw std::runtime_error("the elements of the 'pattern' "
                                                                 "array must be strings");
                                    }
                                    if (first) {
                                        first = false;
                                        pattern_width = line.size();
                                        if (pattern_width % 2 == 0) {
                                            throw std::runtime_error(
                                                "the elements of the 'pattern' array must have odd "
                                                "lengths");
                                        }
                                        pattern.reserve(json_iterator.value().size() * pattern_width);
                                    } else {
                                        if (line.size() != pattern_width) {
                                            throw std::runtime_error(
                                                "all the elements of the 'pattern' array must have "
                                                "the same length");
                                        }
                                    }
                                    std::string characters = line;
                                    for (auto character : characters) {
                                        if (character == '#') {
                                            pattern.push_back(true);
                                        } else if (character == ' ') {
                                            pattern.push_back(false);
                                        } else {
                                            throw std::runtime_error("the elements of the 'pattern' array must contain "
                                                                     "only '#' and ' ' characters");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            hummingbird::lightcrafter::ip ip{10, 10, 10, 100};
            {
                const auto name_and_value = command.options.find("ip");
                if (name_and_value != command.options.end()) {
                    ip = hummingbird::lightcrafter::parse_ip(name_and_value->second);
                }
            }
            hummingbird::lightcrafter lightcrafter(ip, hummingbird::lightcrafter::default_settings());
            std::exception_ptr pipeline_exception;
            auto display = hummingbird::make_display(false, 608, 684, 0, 64, [](hummingbird::display_event) {});
            display->start();
            std::size_t current_point_index = 0;
            std::atomic<int32_t> character(0);
            auto terminal = hibiscus::make_terminal(
                [&](int32_t pressed_character) { character.store(pressed_character, std::memory_order_release); },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    display->close();
                });
            std::atomic<phase> app_phase(phase::display);
            std::atomic_flag accessing_phase;
            std::vector<std::vector<std::array<double, 2>>> point_index_to_left_gazes(points.size());
            std::vector<std::vector<std::array<double, 2>>> point_index_to_right_gazes(points.size());
            std::vector<std::pair<std::string, int32_t>> chunks_and_attributes{
                {"t: ", A_NORMAL},
                {"0", A_NORMAL},
                {"\nio: ", A_NORMAL},
                {"0x00000000", A_NORMAL},
                {"\n\nleft eye", A_BOLD},
                {"\nenabled: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\nhas pupil: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\nhas glint 1: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\nhas glint 2: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\naxis (major, minor): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\npupil (x, y): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\nglint 1 (x, y): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\nglint 2 (x, y): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\n\nright eye", A_BOLD},
                {"\nenabled: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\nhas pupil: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\nhas glint 1: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\nhas glint 2: ", A_NORMAL},
                {"false", COLOR_PAIR(1)},
                {"\naxis (major, minor): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\npupil (x, y): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\nglint 1 (x, y): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\nglint 2 (x, y): ", A_NORMAL},
                {"(0, 0)", A_NORMAL},
                {"\n\npress ", A_NORMAL},
                {"return", A_BOLD},
                {" to start the calibration", A_NORMAL},
            };
            accessing_phase.clear(std::memory_order_release);
            hibiscus::livetrack_data previous_livetrack_data;
            auto livetrack_data_observable = hibiscus::make_livetrack_data_observable(
                [&](hibiscus::livetrack_data livetrack_data) {
                    while (accessing_phase.test_and_set(std::memory_order_acquire)) {
                    }
                    if (dump.is_open()) {
                        dump << livetrack_data.t << "," << livetrack_data.left.pupil_x << ","
                             << livetrack_data.left.pupil_y << "," << livetrack_data.left.glint_1_x << ","
                             << livetrack_data.left.glint_1_y << "," << livetrack_data.right.pupil_x << ","
                             << livetrack_data.right.pupil_y << "," << livetrack_data.right.glint_1_x << ","
                             << livetrack_data.right.glint_1_y << "\r\n";
                    }
                    switch (app_phase) {
                        case phase::display: {
                            chunks_and_attributes[1].first = std::to_string(livetrack_data.t);
                            if (livetrack_data.io != previous_livetrack_data.io) {
                                std::stringstream stream;
                                stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << livetrack_data.io;
                                chunks_and_attributes[3].first = stream.str();
                            }
                            if (livetrack_data.left.enabled != previous_livetrack_data.left.enabled) {
                                chunks_and_attributes[6].first = (livetrack_data.left.enabled ? "true" : "false");
                                chunks_and_attributes[6].second =
                                    (livetrack_data.left.enabled ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.left.has_pupil != previous_livetrack_data.left.has_pupil) {
                                chunks_and_attributes[8].first = (livetrack_data.left.has_pupil ? "true" : "false");
                                chunks_and_attributes[8].second =
                                    (livetrack_data.left.has_pupil ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.left.has_glint_1 != previous_livetrack_data.left.has_glint_1) {
                                chunks_and_attributes[10].first = (livetrack_data.left.has_glint_1 ? "true" : "false");
                                chunks_and_attributes[10].second =
                                    (livetrack_data.left.has_glint_1 ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.left.has_glint_2 != previous_livetrack_data.left.has_glint_2) {
                                chunks_and_attributes[12].first = (livetrack_data.left.has_glint_2 ? "true" : "false");
                                chunks_and_attributes[12].second =
                                    (livetrack_data.left.has_glint_2 ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.left.major_axis != previous_livetrack_data.left.major_axis
                                || livetrack_data.left.minor_axis != previous_livetrack_data.left.minor_axis) {
                                chunks_and_attributes[14].first =
                                    std::string("(") + std::to_string(livetrack_data.left.major_axis) + ", "
                                    + std::to_string(livetrack_data.left.minor_axis) + ")";
                            }
                            if (livetrack_data.left.pupil_x != previous_livetrack_data.left.pupil_x
                                || livetrack_data.left.pupil_y != previous_livetrack_data.left.pupil_y) {
                                chunks_and_attributes[16].first = std::string("(")
                                                                  + std::to_string(livetrack_data.left.pupil_x) + ", "
                                                                  + std::to_string(livetrack_data.left.pupil_y) + ")";
                            }
                            if (livetrack_data.left.glint_1_x != previous_livetrack_data.left.glint_1_x
                                || livetrack_data.left.glint_1_y != previous_livetrack_data.left.glint_1_y) {
                                chunks_and_attributes[18].first = std::string("(")
                                                                  + std::to_string(livetrack_data.left.glint_1_x) + ", "
                                                                  + std::to_string(livetrack_data.left.glint_1_y) + ")";
                            }
                            if (livetrack_data.left.glint_2_x != previous_livetrack_data.left.glint_2_x
                                || livetrack_data.left.glint_2_y != previous_livetrack_data.left.glint_2_y) {
                                chunks_and_attributes[20].first = std::string("(")
                                                                  + std::to_string(livetrack_data.left.glint_2_x) + ", "
                                                                  + std::to_string(livetrack_data.left.glint_2_y) + ")";
                            }
                            if (livetrack_data.right.enabled != previous_livetrack_data.right.enabled) {
                                chunks_and_attributes[23].first = (livetrack_data.right.enabled ? "true" : "false");
                                chunks_and_attributes[23].second =
                                    (livetrack_data.right.enabled ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.right.has_pupil != previous_livetrack_data.right.has_pupil) {
                                chunks_and_attributes[25].first = (livetrack_data.right.has_pupil ? "true" : "false");
                                chunks_and_attributes[25].second =
                                    (livetrack_data.right.has_pupil ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.right.has_glint_1 != previous_livetrack_data.right.has_glint_1) {
                                chunks_and_attributes[27].first = (livetrack_data.right.has_glint_1 ? "true" : "false");
                                chunks_and_attributes[27].second =
                                    (livetrack_data.right.has_glint_1 ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.right.has_glint_2 != previous_livetrack_data.right.has_glint_2) {
                                chunks_and_attributes[29].first = (livetrack_data.right.has_glint_2 ? "true" : "false");
                                chunks_and_attributes[29].second =
                                    (livetrack_data.right.has_glint_2 ? COLOR_PAIR(2) : COLOR_PAIR(1));
                            }
                            if (livetrack_data.right.major_axis != previous_livetrack_data.right.major_axis
                                || livetrack_data.right.minor_axis != previous_livetrack_data.right.minor_axis) {
                                chunks_and_attributes[31].first =
                                    std::string("(") + std::to_string(livetrack_data.right.major_axis) + ", "
                                    + std::to_string(livetrack_data.right.minor_axis) + ")";
                            }
                            if (livetrack_data.right.pupil_x != previous_livetrack_data.right.pupil_x
                                || livetrack_data.right.pupil_y != previous_livetrack_data.right.pupil_y) {
                                chunks_and_attributes[33].first = std::string("(")
                                                                  + std::to_string(livetrack_data.right.pupil_x) + ", "
                                                                  + std::to_string(livetrack_data.right.pupil_y) + ")";
                            }
                            if (livetrack_data.right.glint_1_x != previous_livetrack_data.right.glint_1_x
                                || livetrack_data.right.glint_1_y != previous_livetrack_data.right.glint_1_y) {
                                chunks_and_attributes[35].first =
                                    std::string("(") + std::to_string(livetrack_data.right.glint_1_x) + ", "
                                    + std::to_string(livetrack_data.right.glint_1_y) + ")";
                            }
                            if (livetrack_data.right.glint_2_x != previous_livetrack_data.right.glint_2_x
                                || livetrack_data.right.glint_2_y != previous_livetrack_data.right.glint_2_y) {
                                chunks_and_attributes[37].first =
                                    std::string("(") + std::to_string(livetrack_data.right.glint_2_x) + ", "
                                    + std::to_string(livetrack_data.right.glint_2_y) + ")";
                            }
                            terminal->set_chunks_and_attributes(
                                chunks_and_attributes.begin(), chunks_and_attributes.end());
                            previous_livetrack_data = livetrack_data;
                            break;
                        }
                        case phase::acquisition: {
                            if (livetrack_data.left.has_pupil && livetrack_data.left.has_glint_1) {
                                point_index_to_left_gazes[current_point_index].push_back(
                                    {static_cast<double>(livetrack_data.left.pupil_x) - livetrack_data.left.glint_1_x,
                                     static_cast<double>(livetrack_data.left.pupil_y) - livetrack_data.left.glint_1_y});
                            }
                            if (livetrack_data.right.has_pupil && livetrack_data.right.has_glint_1) {
                                point_index_to_right_gazes[current_point_index].push_back(
                                    {static_cast<double>(livetrack_data.right.pupil_x) - livetrack_data.right.glint_1_x,
                                     static_cast<double>(livetrack_data.right.pupil_y)
                                         - livetrack_data.right.glint_1_y});
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    accessing_phase.clear(std::memory_order_release);
                },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    display->close();
                });
            livetrack_data_observable->start();
            std::vector<uint8_t> downsampled_bytes(576 * 108 * 3);
            std::vector<uint8_t> bytes(608 * 684 * 3);
            auto livetrack_video_observable = hibiscus::make_livetrack_video_observable(
                "/dev/video0",
                [&](const std::vector<uint8_t>& livetrack_bytes) {
                    while (accessing_phase.test_and_set(std::memory_order_acquire)) {
                    }
                    switch (app_phase) {
                        case phase::display: {
                            std::fill(bytes.begin(), bytes.end(), 0);
                            downsample(livetrack_bytes, downsampled_bytes);
                            rotate(downsampled_bytes, bytes);
                            display->push(bytes);
                            break;
                        }
                        case phase::flush: {
                            std::fill(bytes.begin(), bytes.end(), 0);
                            display->pause_and_clear(bytes);
                            app_phase = phase::idle;
                            break;
                        }
                        default:
                            break;
                    }
                    accessing_phase.clear(std::memory_order_release);
                },
                [&](std::exception_ptr exception) {
                    pipeline_exception = exception;
                    display->close();
                });
            std::atomic_bool running(true);
            std::thread play_loop([&]() {
                auto sleep_for_while_running = [&](const std::chrono::milliseconds& duration) {
                    for (const auto end = std::chrono::high_resolution_clock::now() + duration;
                         std::chrono::high_resolution_clock::now() < end;) {
                        if (!running.load(std::memory_order_acquire)) {
                            return false;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    return true;
                };
                std::vector<uint8_t> frame(343 * 342 * 3);
                try {
                    character.store(0, std::memory_order_release);
                    while (running.load(std::memory_order_acquire)) {
                        while (accessing_phase.test_and_set(std::memory_order_acquire)) {
                        }
                        if (app_phase == phase::display) {
                            if (std::isspace(character.fetch_and(0, std::memory_order_acq_rel))) {
                                app_phase = phase::flush;
                            }
                        } else if (app_phase == phase::idle) {
                            display->start();
                            accessing_phase.clear(std::memory_order_release);
                            livetrack_video_observable.reset();
                            break;
                        }
                        accessing_phase.clear(std::memory_order_release);
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    }
                    std::random_device random_device;
                    std::mt19937 generator(random_device());
                    std::vector<std::pair<hibiscus::calibration, std::vector<uint8_t>>> left_calibrations_and_gaze_maps;
                    std::vector<std::pair<hibiscus::calibration, std::vector<uint8_t>>>
                        right_calibrations_and_gaze_maps;
                    while (running.load(std::memory_order_acquire)) {
                        lightcrafter.load_settings(hummingbird::lightcrafter::high_framerate_settings());
                        std::vector<std::size_t> points_indices;
                        {
                            points_indices.reserve(points.size());
                            chunks_and_attributes.clear();
                            chunks_and_attributes.reserve(points.size() * 2);
                            std::size_t maximum_width = 0;
                            while (points_indices.size() < points.size()) {
                                points_indices.push_back(points_indices.size());
                                chunks_and_attributes.emplace_back(
                                    std::string("(")
                                        + std::to_string(
                                            static_cast<uint16_t>(std::get<0>(points[points_indices.back()])))
                                        + ", "
                                        + std::to_string(
                                            static_cast<uint16_t>(std::get<1>(points[points_indices.back()])))
                                        + ") ",
                                    A_NORMAL);
                                if (chunks_and_attributes.back().first.size() > maximum_width) {
                                    maximum_width = chunks_and_attributes.back().first.size();
                                }
                                chunks_and_attributes.emplace_back("\n", A_NORMAL);
                            }
                            for (std::size_t index = 0; index < points.size(); ++index) {
                                chunks_and_attributes[index * 2].first +=
                                    std::string(maximum_width - chunks_and_attributes[index * 2].first.size(), ' ');
                            }
                            std::shuffle(points_indices.begin(), points_indices.end(), generator);
                        }
                        for (const auto point_index : points_indices) {
                            if (!running.load(std::memory_order_acquire)) {
                                break;
                            }
                            chunks_and_attributes[2 * point_index + 1].first = "acquiring\n";
                            chunks_and_attributes[2 * point_index + 1].second = COLOR_PAIR(3);
                            terminal->set_chunks_and_attributes(
                                chunks_and_attributes.begin(), chunks_and_attributes.end());
                            const auto point = points[point_index];
                            std::fill(bytes.begin(), bytes.end(), 0);
                            std::fill(frame.begin(), frame.end(), 0);
                            draw_pattern(
                                frame,
                                static_cast<uint16_t>(std::get<0>(point)),
                                static_cast<uint16_t>(std::get<1>(point)),
                                pattern,
                                pattern_width,
                                {255, 255, 255});
                            hummingbird::rotate(frame, bytes);
                            display->push(bytes);
                            if (dump.is_open()) {
                                while (accessing_phase.test_and_set(std::memory_order_acquire)) {
                                }
                                dump << "-1," << std::get<0>(point) << "," << std::get<1>(point)
                                     << ",0,-1,-1,-1,-1,-1\r\n";
                                accessing_phase.clear(std::memory_order_release);
                            }
                            if (!sleep_for_while_running(before_fixation_duration)) {
                                break;
                            }
                            while (accessing_phase.test_and_set(std::memory_order_acquire)) {
                            }
                            if (dump.is_open()) {
                                dump << "-1," << std::get<0>(point) << "," << std::get<1>(point)
                                     << ",1,-1,-1,-1,-1,-1\r\n";
                            }
                            point_index_to_left_gazes[point_index].clear();
                            point_index_to_right_gazes[point_index].clear();
                            current_point_index = point_index;
                            app_phase = phase::acquisition;
                            accessing_phase.clear(std::memory_order_release);
                            if (!sleep_for_while_running(fixation_duration)) {
                                break;
                            }
                            while (accessing_phase.test_and_set(std::memory_order_acquire)) {
                            }
                            if (dump.is_open()) {
                                dump << "-1," << std::get<0>(point) << "," << std::get<1>(point)
                                     << ",2,-1,-1,-1,-1,-1\r\n";
                            }
                            app_phase = phase::idle;
                            accessing_phase.clear(std::memory_order_release);
                            if (!sleep_for_while_running(after_fixation_duration)) {
                                break;
                            }
                            chunks_and_attributes[2 * point_index + 1].first = "done\n";
                            chunks_and_attributes[2 * point_index + 1].second = COLOR_PAIR(2);
                            terminal->set_chunks_and_attributes(
                                chunks_and_attributes.begin(), chunks_and_attributes.end());
                        }
                        std::fill(bytes.begin(), bytes.end(), 0);
                        display->push(bytes);
                        lightcrafter.load_settings(hummingbird::lightcrafter::default_settings());
                        left_calibrations_and_gaze_maps.push_back(estimate_calibration_and_gaze_map(
                            points.begin(), points.end(), point_index_to_left_gazes.begin(), {255, 0, 0}, {}));
                        left_calibrations_and_gaze_maps.push_back(estimate_calibration_and_gaze_map(
                            hibiscus::make_skip_iterator(points.begin(), 0),
                            hibiscus::make_skip_iterator(points.end()),
                            hibiscus::make_skip_iterator(point_index_to_left_gazes.begin(), 0),
                            {255, 0, 0},
                            point_index_to_left_gazes[0]));
                        for (std::size_t index = 1; index < 9; ++index) {
                            auto calibration_and_gaze_map = estimate_calibration_and_gaze_map(
                                hibiscus::make_skip_iterator(points.begin(), index),
                                hibiscus::make_skip_iterator(points.end()),
                                hibiscus::make_skip_iterator(point_index_to_left_gazes.begin(), index),
                                {255, 0, 0},
                                point_index_to_left_gazes[index]);
                            if (hibiscus::maximum_error(calibration_and_gaze_map.first)
                                < hibiscus::maximum_error(left_calibrations_and_gaze_maps.back().first)) {
                                left_calibrations_and_gaze_maps.back() = std::move(calibration_and_gaze_map);
                            }
                        }
                        right_calibrations_and_gaze_maps.push_back(estimate_calibration_and_gaze_map(
                            points.begin(), points.end(), point_index_to_right_gazes.begin(), {0, 0, 255}, {}));
                        right_calibrations_and_gaze_maps.push_back(estimate_calibration_and_gaze_map(
                            hibiscus::make_skip_iterator(points.begin(), 0),
                            hibiscus::make_skip_iterator(points.end()),
                            hibiscus::make_skip_iterator(point_index_to_right_gazes.begin(), 0),
                            {0, 0, 255},
                            point_index_to_right_gazes[0]));
                        for (std::size_t index = 1; index < 9; ++index) {
                            auto calibration_and_gaze_map = estimate_calibration_and_gaze_map(
                                hibiscus::make_skip_iterator(points.begin(), index),
                                hibiscus::make_skip_iterator(points.end(), index),
                                hibiscus::make_skip_iterator(point_index_to_right_gazes.begin(), index),
                                {0, 0, 255},
                                point_index_to_right_gazes[index]);
                            if (hibiscus::maximum_error(calibration_and_gaze_map.first)
                                < hibiscus::maximum_error(right_calibrations_and_gaze_maps.back().first)) {
                                right_calibrations_and_gaze_maps.back() = std::move(calibration_and_gaze_map);
                            }
                        }
                        {
                            std::fill(bytes.begin(), bytes.end(), 0);
                            hummingbird::rotate(left_calibrations_and_gaze_maps[0].second, bytes);
                            display->push(bytes);
                            std::size_t active_line = 0;
                            std::size_t selected_left = left_calibrations_and_gaze_maps.size();
                            std::size_t selected_right = right_calibrations_and_gaze_maps.size();
                            chunks_and_attributes.clear();
                            chunks_and_attributes.reserve(
                                (left_calibrations_and_gaze_maps.size() + right_calibrations_and_gaze_maps.size()) * 2
                                + 3);
                            for (std::size_t index = 0; index < left_calibrations_and_gaze_maps.size(); ++index) {
                                std::stringstream stream;
                                stream << "left, trial " << index / 2 + 1 << ", "
                                       << left_calibrations_and_gaze_maps[index].first.points_and_errors.size()
                                       << " points (worst: " << std::fixed << std::setprecision(3)
                                       << hibiscus::maximum_error(left_calibrations_and_gaze_maps[index].first)
                                       << ", average: " << std::fixed << std::setprecision(3)
                                       << hibiscus::mean_error(left_calibrations_and_gaze_maps[index].first) << ") ";
                                chunks_and_attributes.emplace_back(stream.str(), index == 0 ? A_REVERSE : A_NORMAL);
                                chunks_and_attributes.emplace_back("[ ]\n", index == 0 ? A_REVERSE : A_NORMAL);
                            }
                            for (std::size_t index = 0; index < right_calibrations_and_gaze_maps.size(); ++index) {
                                std::stringstream stream;
                                stream << "right, trial " << index / 2 + 1 << ", "
                                       << right_calibrations_and_gaze_maps[index].first.points_and_errors.size()
                                       << " points (worst: " << std::fixed << std::setprecision(3)
                                       << hibiscus::maximum_error(right_calibrations_and_gaze_maps[index].first)
                                       << ", average: " << std::fixed << std::setprecision(3)
                                       << hibiscus::mean_error(right_calibrations_and_gaze_maps[index].first) << ") ";
                                chunks_and_attributes.emplace_back(stream.str(), A_NORMAL);
                                chunks_and_attributes.emplace_back("[ ]\n", A_NORMAL);
                            }
                            chunks_and_attributes.emplace_back("\n", A_NORMAL);
                            chunks_and_attributes.emplace_back("perform another calibration\n", A_NORMAL);
                            chunks_and_attributes.emplace_back("save the calibration and quit\n", A_DIM);
                            terminal->set_chunks_and_attributes(
                                chunks_and_attributes.begin(), chunks_and_attributes.end());
                            auto set_attribute = [&](std::size_t line, int32_t attribute) {
                                if (line < left_calibrations_and_gaze_maps.size()
                                               + right_calibrations_and_gaze_maps.size()) {
                                    chunks_and_attributes[line * 2].second = attribute;
                                    chunks_and_attributes[line * 2 + 1].second = attribute;
                                } else {
                                    chunks_and_attributes
                                        [line + left_calibrations_and_gaze_maps.size()
                                         + right_calibrations_and_gaze_maps.size() + 1]
                                            .second = attribute;
                                }
                            };
                            while (running.load(std::memory_order_acquire)) {
                                const auto new_character = character.fetch_and(0, std::memory_order_acq_rel);
                                if (std::isspace(new_character)) {
                                    if (active_line < left_calibrations_and_gaze_maps.size()) {
                                        if (selected_left == active_line) {
                                            selected_left = left_calibrations_and_gaze_maps.size();
                                            chunks_and_attributes[active_line * 2 + 1].first = "[ ]\n";
                                            if (selected_right < right_calibrations_and_gaze_maps.size()) {
                                                chunks_and_attributes
                                                    [(left_calibrations_and_gaze_maps.size()
                                                      + right_calibrations_and_gaze_maps.size())
                                                         * 2
                                                     + 2]
                                                        .second = A_DIM;
                                            }
                                        } else {
                                            if (selected_left < left_calibrations_and_gaze_maps.size()) {
                                                chunks_and_attributes[selected_left * 2 + 1].first = "[ ]\n";
                                            } else if (selected_right < right_calibrations_and_gaze_maps.size()) {
                                                chunks_and_attributes
                                                    [(left_calibrations_and_gaze_maps.size()
                                                      + right_calibrations_and_gaze_maps.size())
                                                         * 2
                                                     + 2]
                                                        .second = A_NORMAL;
                                            }
                                            selected_left = active_line;
                                            chunks_and_attributes[selected_left * 2 + 1].first = "[x]\n";
                                        }
                                        terminal->set_chunks_and_attributes(
                                            chunks_and_attributes.begin(), chunks_and_attributes.end());
                                    } else if (
                                        active_line < left_calibrations_and_gaze_maps.size()
                                                          + right_calibrations_and_gaze_maps.size()) {
                                        if (selected_right == active_line - left_calibrations_and_gaze_maps.size()) {
                                            selected_right = right_calibrations_and_gaze_maps.size();
                                            chunks_and_attributes[active_line * 2 + 1].first = "[ ]\n";
                                            if (selected_left < left_calibrations_and_gaze_maps.size()) {
                                                chunks_and_attributes
                                                    [(left_calibrations_and_gaze_maps.size()
                                                      + right_calibrations_and_gaze_maps.size())
                                                         * 2
                                                     + 2]
                                                        .second = A_DIM;
                                            }
                                        } else {
                                            if (selected_right < right_calibrations_and_gaze_maps.size()) {
                                                chunks_and_attributes
                                                    [(selected_right + left_calibrations_and_gaze_maps.size()) * 2 + 1]
                                                        .first = "[ ]\n";
                                            } else if (selected_left < right_calibrations_and_gaze_maps.size()) {
                                                chunks_and_attributes
                                                    [(left_calibrations_and_gaze_maps.size()
                                                      + right_calibrations_and_gaze_maps.size())
                                                         * 2
                                                     + 2]
                                                        .second = A_NORMAL;
                                            }
                                            selected_right = active_line - left_calibrations_and_gaze_maps.size();
                                            chunks_and_attributes[active_line * 2 + 1].first = "[x]\n";
                                        }
                                        terminal->set_chunks_and_attributes(
                                            chunks_and_attributes.begin(), chunks_and_attributes.end());
                                    } else if (
                                        active_line
                                        == left_calibrations_and_gaze_maps.size()
                                               + right_calibrations_and_gaze_maps.size()) {
                                        break;
                                    } else {
                                        {
                                            std::ofstream output(command.arguments[0]);
                                            hibiscus::calibrations_to_json(
                                                {left_calibrations_and_gaze_maps[selected_left].first,
                                                 right_calibrations_and_gaze_maps[selected_right].first},
                                                output);
                                        }
                                        display->close();
                                        running.store(false, std::memory_order_release);
                                        break;
                                    }
                                } else if (new_character == KEY_UP || new_character == KEY_DOWN) {
                                    const auto previous_active_line = active_line;
                                    if (new_character == KEY_UP) {
                                        if (active_line > 0) {
                                            --active_line;
                                        }
                                    } else {
                                        if (active_line < left_calibrations_and_gaze_maps.size()
                                                              + right_calibrations_and_gaze_maps.size()) {
                                            ++active_line;
                                        } else if (
                                            active_line
                                                == left_calibrations_and_gaze_maps.size()
                                                       + right_calibrations_and_gaze_maps.size()
                                            && selected_left < left_calibrations_and_gaze_maps.size()
                                            && selected_right < left_calibrations_and_gaze_maps.size()) {
                                            ++active_line;
                                        }
                                    }
                                    if (previous_active_line != active_line) {
                                        set_attribute(previous_active_line, A_NORMAL);
                                        set_attribute(active_line, A_REVERSE);
                                        if (active_line < left_calibrations_and_gaze_maps.size()) {
                                            std::fill(bytes.begin(), bytes.end(), 0);
                                            hummingbird::rotate(
                                                left_calibrations_and_gaze_maps[active_line].second, bytes);
                                            display->push(bytes);
                                        } else if (
                                            active_line < left_calibrations_and_gaze_maps.size()
                                                              + right_calibrations_and_gaze_maps.size()) {
                                            std::fill(bytes.begin(), bytes.end(), 0);
                                            hummingbird::rotate(
                                                right_calibrations_and_gaze_maps
                                                    [active_line - left_calibrations_and_gaze_maps.size()]
                                                        .second,
                                                bytes);
                                            display->push(bytes);
                                        } else {
                                            std::fill(bytes.begin(), bytes.end(), 0);
                                            display->push(bytes);
                                        }
                                        terminal->set_chunks_and_attributes(
                                            chunks_and_attributes.begin(), chunks_and_attributes.end());
                                    }
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            }
                        }
                    }
                } catch (...) {
                    pipeline_exception = std::current_exception();
                }
                display->close();
            });
            display->run();
            running.store(false, std::memory_order_release);
            play_loop.join();
            if (pipeline_exception) {
                std::rethrow_exception(pipeline_exception);
            }
        });
}
