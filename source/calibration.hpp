#pragma once

#include "../third_party/json.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

/// hibiscus bundles tools to build a psychophysics platform on a Jetson TX1.
namespace hibiscus {
    /// calibration stores a calibration matrix and errors.
    struct calibration {
        std::array<double, 16> matrix;
        std::vector<std::pair<std::array<double, 2>, double>> points_and_errors;
    };

    /// calibrations stores both eyes' calibrations.
    struct calibrations {
        calibration left;
        calibration right;
    };

    /// maximum_error returns the maximum error in a calibration.
    double maximum_error(const calibration& estimated_calibration) {
        return std::accumulate(
            estimated_calibration.points_and_errors.begin(),
            estimated_calibration.points_and_errors.end(),
            0.0,
            [](double error, std::pair<std::array<double, 2>, double> point_and_error) {
                return std::max(error, point_and_error.second);
            });
    }

    /// mean_error returns the mean error in a calibration.
    double mean_error(const calibration& estimated_calibration) {
        const auto size = estimated_calibration.points_and_errors.size();
        return std::accumulate(
            estimated_calibration.points_and_errors.begin(),
            estimated_calibration.points_and_errors.end(),
            0.0,
            [=](double error, std::pair<std::array<double, 2>, double> point_and_error) {
                return error + point_and_error.second / size;
            });
    }

    /// product returns the product of a point and a scalar.
    template <std::size_t dimension>
    static std::array<double, dimension> product(std::array<double, dimension> point, const double scalar) {
        std::transform(point.begin(), point.end(), point.begin(), [=](double value) { return scalar * value; });
        return point;
    }

    /// sum computes the sum of two points.
    template <std::size_t dimension>
    static std::array<double, dimension>
    sum(std::array<double, dimension> first_point, const std::array<double, dimension> second_point) {
        std::transform(
            first_point.begin(), first_point.end(), second_point.begin(), first_point.begin(), std::plus<double>{});
        return first_point;
    }

    /// difference computes the difference between two points.
    template <std::size_t dimension>
    static std::array<double, dimension>
    difference(std::array<double, dimension> first_point, const std::array<double, dimension> second_point) {
        std::transform(
            first_point.begin(), first_point.end(), second_point.begin(), first_point.begin(), std::minus<double>{});
        return first_point;
    }

    /// join maps the given range to a string and joins the result with the given separator.
    template <typename Iterator, typename Separator, typename UnaryOperation>
    void join(std::ostream& output, Iterator begin, Iterator end, Separator separator, UnaryOperation unary_operation) {
        for (; begin != end; ++begin) {
            output << unary_operation(*begin);
            if (std::next(begin) != end) {
                output << separator;
            }
        }
    }
    template <typename Iterator, typename Separator>
    void join(std::ostream& output, Iterator begin, Iterator end, Separator separator) {
        join(output, begin, end, separator, [](typename Iterator::reference value) { return value; });
    }

    /// projection calculates a geometric transformation using homogeneous
    /// coordinates.
    static std::array<double, 3> projection(const std::array<double, 16> matrix, const std::array<double, 3> point) {
        const auto w = std::get<12>(matrix) * std::get<0>(point) + std::get<13>(matrix) * std::get<1>(point)
                       + std::get<14>(matrix) * std::get<2>(point) + std::get<15>(matrix);
        return {(std::get<0>(matrix) * std::get<0>(point) + std::get<1>(matrix) * std::get<1>(point)
                 + std::get<2>(matrix) * std::get<2>(point) + std::get<3>(matrix))
                    / w,
                (std::get<4>(matrix) * std::get<0>(point) + std::get<5>(matrix) * std::get<1>(point)
                 + std::get<6>(matrix) * std::get<2>(point) + std::get<7>(matrix))
                    / w,
                (std::get<8>(matrix) * std::get<0>(point) + std::get<9>(matrix) * std::get<1>(point)
                 + std::get<10>(matrix) * std::get<2>(point) + std::get<11>(matrix))
                    / w};
    }

    /// norm computes a point norm.
    template <std::size_t dimension>
    static double norm(const std::array<double, dimension> point) {
        return std::sqrt(std::accumulate(point.begin(), point.end(), 0.0, [](double accumulator, double value) {
            return accumulator + std::pow(value, 2);
        }));
    }

    /// eye calculates eye surface coordinates from camera coordinates.
    static std::array<double, 3> eye(std::array<double, 2> point) {
        return {
            std::get<0>(point), std::get<1>(point), 100 * 8192 - std::hypot(std::get<0>(point), std::get<1>(point))};
    }

    /// mean retrieves the mean point from a range.
    template <std::size_t dimension, typename Iterator>
    static std::array<double, dimension> mean(Iterator begin, Iterator end) {
        const auto scalar = 1.0 / std::distance(begin, end);
        return std::accumulate(
            begin,
            end,
            std::array<double, dimension>(),
            [scalar](std::array<double, dimension> accumulator, std::array<double, dimension> point) {
                return sum(accumulator, product(point, scalar));
            });
    }

    /// skip_iterator wraps an iterator but skips an element.
    template <typename Iterator>
    class skip_iterator {
        public:
        using iterator_category = std::input_iterator_tag;
        using value_type = typename std::iterator_traits<Iterator>::value_type;
        using difference_type = typename std::iterator_traits<Iterator>::difference_type;
        using pointer = typename std::iterator_traits<Iterator>::pointer;
        using reference = typename std::iterator_traits<Iterator>::reference;
        skip_iterator(Iterator iterator, std::size_t skip_index) :
            _iterator(iterator),
            _skip_index(skip_index),
            _index(0) {
            if (_skip_index == 0) {
                ++_iterator;
            }
        }
        skip_iterator(const skip_iterator&) = default;
        skip_iterator(skip_iterator&&) = default;
        skip_iterator& operator=(const skip_iterator&) = default;
        skip_iterator& operator=(skip_iterator&&) = default;
        virtual ~skip_iterator() {}
        skip_iterator& operator++() {
            ++_index;
            if (_index == _skip_index) {
                ++_iterator;
            }
            ++_iterator;
            return *this;
        }
        bool operator==(skip_iterator other) const {
            return _iterator == other->_iterator;
        }
        bool operator!=(skip_iterator other) const {
            return _iterator != other._iterator;
        }
        reference operator*() const {
            return *_iterator;
        }
        pointer operator->() const {
            return &(*_iterator);
        }

        private:
        Iterator _iterator;
        const std::size_t _skip_index;
        std::size_t _index;
    };
    template <typename Iterator>
    skip_iterator<Iterator> make_skip_iterator(Iterator iterator, std::size_t skip_index = -1) {
        return skip_iterator<Iterator>(iterator, skip_index);
    }

    /// median retrieves the median point from a range.
    /// The median is retrieved for each coordinate, hence the result may be a
    /// non-existing point.
    template <std::size_t dimension, typename Iterator>
    static std::array<double, dimension> median(Iterator begin, Iterator end) {
        std::array<double, dimension> result;
        const auto size = static_cast<std::size_t>(std::distance(begin, end));
        const auto is_even = (size % 2 == 0);
        std::vector<double> values(size);
        for (std::size_t index = 0; index < dimension; ++index) {
            std::transform(
                begin, end, values.begin(), [=](std::array<double, dimension> point) { return point[index]; });
            auto target = std::next(values.begin(), size / 2);
            std::nth_element(values.begin(), target, values.end());
            result[index] = is_even ? (*std::max_element(values.begin(), target) + *target) / 2 : *target;
        }
        return result;
    }

    /// calibrations_to_json writes left and right calibrations to a stream in JSON format.
    static void calibrations_to_json(const calibrations& calibrations_to_write, std::ostream& output) {
        std::array<std::size_t, 2> left_points_widths{0, 0};
        for (auto point_and_error : calibrations_to_write.left.points_and_errors) {
            for (uint8_t index = 0; index < 2; ++index) {
                std::stringstream stream;
                stream << static_cast<uint16_t>(point_and_error.first[index]);
                left_points_widths[index] = std::max(left_points_widths[index], stream.str().size());
            }
        }
        std::array<std::size_t, 2> right_points_widths{0, 0};
        for (auto point_and_error : calibrations_to_write.right.points_and_errors) {
            for (uint8_t index = 0; index < 2; ++index) {
                std::stringstream stream;
                stream << static_cast<uint16_t>(point_and_error.first[index]);
                right_points_widths[index] = std::max(right_points_widths[index], stream.str().size());
            }
        }
        const auto left_errors_width = std::accumulate(
            calibrations_to_write.left.points_and_errors.begin(),
            calibrations_to_write.left.points_and_errors.end(),
            static_cast<std::size_t>(0),
            [](std::size_t accumulator, std::pair<std::array<double, 2>, double> point_and_error) {
                std::stringstream stream;
                stream << point_and_error.second;
                return std::max(accumulator, stream.str().size());
            });
        const auto right_errors_width = std::accumulate(
            calibrations_to_write.right.points_and_errors.begin(),
            calibrations_to_write.right.points_and_errors.end(),
            static_cast<std::size_t>(0),
            [](std::size_t accumulator, std::pair<std::array<double, 2>, double> point_and_error) {
                std::stringstream stream;
                stream << point_and_error.second;
                return std::max(accumulator, stream.str().size());
            });
        std::array<std::size_t, 4> left_columns_widths;
        std::array<std::size_t, 4> right_columns_widths;
        for (uint8_t column = 0; column < 4; ++column) {
            left_columns_widths[column] = 0;
            right_columns_widths[column] = 0;
            for (uint8_t row = 0; row < 4; ++row) {
                std::stringstream left_stream;
                std::stringstream right_stream;
                left_stream << calibrations_to_write.left.matrix[column + row * 4];
                right_stream << calibrations_to_write.right.matrix[column + row * 4];
                left_columns_widths[column] = std::max(left_columns_widths[column], left_stream.str().size());
                right_columns_widths[column] = std::max(right_columns_widths[column], right_stream.str().size());
            }
        }
        output << "{\n    \"left\": {\n        \"matrix\": [\n";
        for (uint8_t row = 0; row < 4; ++row) {
            output << "            ";
            for (uint8_t column = 0; column < 4; ++column) {
                output << std::setw(static_cast<int32_t>(left_columns_widths[column]))
                       << calibrations_to_write.left.matrix[column + row * 4];
                if (column < 3) {
                    output << ", ";
                }
            }
            if (row < 3) {
                output << ",";
            }
            output << "\n";
        }
        output << "        ],\n        \"points\": [\n";
        join(
            output,
            calibrations_to_write.left.points_and_errors.begin(),
            calibrations_to_write.left.points_and_errors.end(),
            "],\n",
            [=](const std::pair<std::array<double, 2>, double>& point_and_error) {
                std::stringstream stream;
                stream << "            [" << std::setw(static_cast<int32_t>(std::get<0>(left_points_widths)))
                       << std::get<0>(point_and_error.first) << ", "
                       << std::setw(static_cast<int32_t>(std::get<1>(left_points_widths)))
                       << std::get<1>(point_and_error.first);
                return std::string(stream.str());
            });
        output << "]\n        ],\n        \"errors\": [\n";
        join(
            output,
            calibrations_to_write.left.points_and_errors.begin(),
            calibrations_to_write.left.points_and_errors.end(),
            ",\n",
            [=](const std::pair<std::array<double, 2>, double>& point_and_error) {
                std::stringstream stream;
                stream << "            " << std::setw(static_cast<int32_t>(left_errors_width))
                       << point_and_error.second;
                return std::string(stream.str());
            });
        output << "\n        ]\n    },\n    \"right\": {\n        \"matrix\": [\n";
        for (uint8_t row = 0; row < 4; ++row) {
            output << "            ";
            for (uint8_t column = 0; column < 4; ++column) {
                output << std::setw(static_cast<int32_t>(right_columns_widths[column]))
                       << calibrations_to_write.right.matrix[column + row * 4];
                if (column < 3) {
                    output << ", ";
                }
            }
            if (row < 3) {
                output << ",";
            }
            output << "\n";
        }
        output << "        ],\n        \"points\": [\n";
        join(
            output,
            calibrations_to_write.right.points_and_errors.begin(),
            calibrations_to_write.right.points_and_errors.end(),
            "],\n",
            [=](const std::pair<std::array<double, 2>, double>& point_and_error) {
                std::stringstream stream;
                stream << "            [" << std::setw(static_cast<int32_t>(std::get<0>(right_points_widths)))
                       << std::get<0>(point_and_error.first) << ", "
                       << std::setw(static_cast<int32_t>(std::get<1>(right_points_widths)))
                       << std::get<1>(point_and_error.first);
                return std::string(stream.str());
            });
        output << "]\n        ],\n        \"errors\": [\n";
        join(
            output,
            calibrations_to_write.right.points_and_errors.begin(),
            calibrations_to_write.right.points_and_errors.end(),
            ",\n",
            [=](const std::pair<std::array<double, 2>, double>& point_and_error) {
                std::stringstream stream;
                stream << "            " << std::setw(static_cast<int32_t>(right_errors_width))
                       << point_and_error.second;
                return std::string(stream.str());
            });
        output << "\n        ]\n    }\n}\n";
    }

    /// json_to_calibrations parses and validates left and right calibrations from a stream.
    static calibrations json_to_calibrations(std::istream& input) {
        calibrations result;
        nlohmann::json json;
        try {
            input >> json;
        } catch (const nlohmann::detail::parse_error& exception) {
            throw std::runtime_error(exception.what());
        }
        if (!json.is_object()) {
            throw std::runtime_error("the root element must be a JSON object");
        }
        std::array<bool, 8> fields_found;
        std::vector<std::array<double, 2>> left_points;
        std::vector<double> left_errors;
        std::vector<std::array<double, 2>> right_points;
        std::vector<double> right_errors;
        for (auto json_iterator = json.begin(); json_iterator != json.end(); ++json_iterator) {
            if (json_iterator.key() == "left") {
                if (!json_iterator.value().is_object()) {
                    throw std::runtime_error("the key 'left' must be associated with an object");
                }
                for (auto json_subiterator = json_iterator.value().begin();
                     json_subiterator != json_iterator.value().end();
                     ++json_subiterator) {
                    if (json_subiterator.key() == "matrix") {
                        if (!json_subiterator.value().is_array()) {
                            throw std::runtime_error("the key 'matrix' of 'left' must be associated with an array");
                        }
                        if (json_subiterator.value().size() != 16) {
                            throw std::runtime_error("'matrix' of 'left' must have 16 elements");
                        }
                        for (auto json_subsubiterator = json_subiterator.value().begin();
                             json_subsubiterator != json_subiterator.value().end();
                             ++json_subsubiterator) {
                            if (!json_subsubiterator.value().is_number()) {
                                throw std::runtime_error("the elements of 'matrix' of 'left' must be numbers");
                            }
                            result.left.matrix[std::distance(json_subiterator.value().begin(), json_subsubiterator)] =
                                json_subsubiterator.value();
                        }
                        std::get<1>(fields_found) = true;
                    } else if (json_subiterator.key() == "points") {
                        if (!json_subiterator.value().is_array()) {
                            throw std::runtime_error("the key 'points' must be associated with an array");
                        }
                        for (auto json_subsubiterator = json_subiterator.value().begin();
                             json_subsubiterator != json_subiterator.value().end();
                             ++json_subsubiterator) {
                            if (!json_subsubiterator.value().is_array() || json_subsubiterator.value().size() != 2) {
                                throw std::runtime_error(
                                    "the elements of 'points' of 'left' must be two-elements arrays");
                            }
                            left_points.push_back({});
                            for (auto json_subsubsubiterator = json_subsubiterator.value().begin();
                                 json_subsubsubiterator != json_subsubiterator.value().end();
                                 ++json_subsubsubiterator) {
                                if (!json_subsubsubiterator.value().is_number()) {
                                    throw std::runtime_error(
                                        "the elements of each element of 'points' of 'left' must be numbers");
                                }
                                left_points.back()[std::distance(
                                    json_subsubiterator.value().begin(), json_subsubsubiterator)] =
                                    json_subsubsubiterator.value();
                            }
                        }
                        std::get<2>(fields_found) = true;
                    } else if (json_subiterator.key() == "errors") {
                        if (!json_subiterator.value().is_array()) {
                            throw std::runtime_error("the key 'errors' of 'left' must be associated with an array");
                        }
                        for (auto json_subsubiterator = json_subiterator.value().begin();
                             json_subsubiterator != json_subiterator.value().end();
                             ++json_subsubiterator) {
                            if (!json_subsubiterator.value().is_number()) {
                                throw std::runtime_error("the elements of 'errors' of 'left' must be numbers");
                            }
                            left_errors.push_back(0);
                            left_errors.back() = json_subsubiterator.value();
                        }
                        std::get<3>(fields_found) = true;
                    }
                }
                std::get<0>(fields_found) = true;
            } else if (json_iterator.key() == "right") {
                if (!json_iterator.value().is_object()) {
                    throw std::runtime_error("the key 'right' must be associated with an object");
                }
                for (auto json_subiterator = json_iterator.value().begin();
                     json_subiterator != json_iterator.value().end();
                     ++json_subiterator) {
                    if (json_subiterator.key() == "matrix") {
                        if (!json_subiterator.value().is_array()) {
                            throw std::runtime_error("the key 'matrix' of 'right' must be associated with an array");
                        }
                        if (json_subiterator.value().size() != 16) {
                            throw std::runtime_error("'matrix' of 'right' must have 16 elements");
                        }
                        for (auto json_subsubiterator = json_subiterator.value().begin();
                             json_subsubiterator != json_subiterator.value().end();
                             ++json_subsubiterator) {
                            if (!json_subsubiterator.value().is_number()) {
                                throw std::runtime_error("the elements of 'matrix' of 'right' must be numbers");
                            }
                            result.right.matrix[std::distance(json_subiterator.value().begin(), json_subsubiterator)] =
                                json_subsubiterator.value();
                        }
                        std::get<5>(fields_found) = true;
                    } else if (json_subiterator.key() == "points") {
                        if (!json_subiterator.value().is_array()) {
                            throw std::runtime_error("the key 'points' must be associated with an array");
                        }
                        for (auto json_subsubiterator = json_subiterator.value().begin();
                             json_subsubiterator != json_subiterator.value().end();
                             ++json_subsubiterator) {
                            if (!json_subsubiterator.value().is_array() || json_subsubiterator.value().size() != 2) {
                                throw std::runtime_error(
                                    "the elements of 'points' of 'right' must be two-elements arrays");
                            }
                            right_points.push_back({});
                            for (auto json_subsubsubiterator = json_subsubiterator.value().begin();
                                 json_subsubsubiterator != json_subsubiterator.value().end();
                                 ++json_subsubsubiterator) {
                                if (!json_subsubsubiterator.value().is_number()) {
                                    throw std::runtime_error(
                                        "the elements of each element of 'points' of 'right' must be numbers");
                                }
                                right_points.back()[std::distance(
                                    json_subsubiterator.value().begin(), json_subsubsubiterator)] =
                                    json_subsubsubiterator.value();
                            }
                        }
                        std::get<6>(fields_found) = true;
                    } else if (json_subiterator.key() == "errors") {
                        if (!json_subiterator.value().is_array()) {
                            throw std::runtime_error("the key 'errors' of 'right' must be associated with an array");
                        }
                        for (auto json_subsubiterator = json_subiterator.value().begin();
                             json_subsubiterator != json_subiterator.value().end();
                             ++json_subsubiterator) {
                            if (!json_subsubiterator.value().is_number()) {
                                throw std::runtime_error("the elements of 'errors' of 'right' must be numbers");
                            }
                            right_errors.push_back(0);
                            right_errors.back() = json_subsubiterator.value();
                        }
                        std::get<7>(fields_found) = true;
                    }
                }
                std::get<4>(fields_found) = true;
            }
        }
        if (!std::get<0>(fields_found)) {
            throw std::runtime_error("the root object must have a 'left' key");
        }
        if (!std::get<1>(fields_found)) {
            throw std::runtime_error("'left' must have a 'matrix' key");
        }
        if (!std::get<2>(fields_found)) {
            throw std::runtime_error("'left' must have a 'points' key");
        }
        if (!std::get<3>(fields_found)) {
            throw std::runtime_error("'left' must have a 'errors' key");
        }
        if (!std::get<4>(fields_found)) {
            throw std::runtime_error("the root object must have a 'right' key");
        }
        if (!std::get<5>(fields_found)) {
            throw std::runtime_error("'right' must have a 'matrix' key");
        }
        if (!std::get<6>(fields_found)) {
            throw std::runtime_error("'right' must have a 'points' key");
        }
        if (!std::get<7>(fields_found)) {
            throw std::runtime_error("'right' must have a 'errors' key");
        }
        if (left_points.size() != left_errors.size()) {
            throw std::runtime_error("'points' and 'errors' of 'left' must have the same number of elements");
        }
        result.left.points_and_errors.reserve(left_points.size());
        for (std::size_t index = 0; index < left_points.size(); ++index) {
            result.left.points_and_errors.emplace_back(left_points[index], left_errors[index]);
        }
        if (right_points.size() != right_errors.size()) {
            throw std::runtime_error("'points' and 'errors' of 'left' must have the same number of elements");
        }
        result.right.points_and_errors.reserve(right_points.size());
        for (std::size_t index = 0; index < right_points.size(); ++index) {
            result.right.points_and_errors.emplace_back(right_points[index], right_errors[index]);
        }
        return result;
    }
}
