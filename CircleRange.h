#pragma once

#include <algorithm>
#include <iterator>
#include <tuple>

class CircleRange {
public:
    struct Iterator {
        int cx, cy, r, y, x, x_max;
        int r2;

        Iterator(int cx, int cy, int r, bool end = false)
            : cx(cx), cy(cy), r(r), r2(r * r) {
            if (end) {
                y = r + 1; // past-end sentinel
                return;
            }
            y = -r;
            update_x_max();
            x = -x_max;
        }

        std::pair<int, int> operator*() const {
            return {cx + x, cy + y};
        }

        Iterator& operator++() {
            ++x;
            if (x > x_max) {
                ++y;
                if (y <= r) {
                    update_x_max();
                    x = -x_max;
                }
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return y != other.y;
        }

    private:
        void update_x_max() {
            int y2 = y * y;
            x_max = static_cast<int>(std::sqrt(r2 - y2));
        }
    };

    CircleRange(int cx, int cy, int r) : cx(cx), cy(cy), r(r) {}

    Iterator begin() const { return Iterator(cx, cy, r); }
    Iterator end() const { return Iterator(cx, cy, r, true); }

private:
    int cx, cy, r;
};

class CircleRangeEllipse {
public:
    struct Iterator {
        int cx, cy, rx, ry, y, x, x_max;

        Iterator(int cx, int cy, int rx, int ry, bool end = false)
            : cx(cx), cy(cy), rx(rx), ry(ry) {
            if (end) {
                y = ry + 1;
                return;
            }
            y = -ry;
            update_x_max();
            x = -x_max;
        }

        std::pair<int, int> operator*() const {
            return {cx + x, cy + y};
        }

        Iterator& operator++() {
            ++x;
            if (x > x_max) {
                ++y;
                if (y <= ry) {
                    update_x_max();
                    x = -x_max;
                }
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return y != other.y;
        }

    private:
        void update_x_max() {
            // Ellipse equation: (x/rx)^2 + (y/ry)^2 <= 1
            double y_norm = static_cast<double>(y) / ry;
            double x_norm_max = std::sqrt(std::max(0.0, 1.0 - y_norm * y_norm));
            x_max = static_cast<int>(std::floor(x_norm_max * rx));
        }
    };

    CircleRangeEllipse(int cx, int cy, int rx, int ry)
        : cx(cx), cy(cy), rx(rx), ry(ry) {}

    Iterator begin() const { return Iterator(cx, cy, rx, ry); }
    Iterator end() const { return Iterator(cx, cy, rx, ry, true); }

private:
    int cx, cy, rx, ry;
};

constexpr float solidAngleToAngularRadius(float omega) {
    return std::acos(1.0 - omega / (2.0 * M_PI));
}

CircleRangeEllipse equirectangularCircle(int cx, int cy, int imageWidth, int imageHeight, float solidAngle) {
    float angularRadius = solidAngleToAngularRadius(solidAngle);
    float v = cy / float(imageHeight);
    float theta = (v - 0.5) * M_PI;
    int rx = static_cast<int>(std::round(imageWidth  * (angularRadius / (2 * M_PI))));
    int ry = static_cast<int>(std::round(imageHeight * (angularRadius / M_PI) * std::cos(theta)));
    return CircleRangeEllipse(cx, cy, rx, ry);
};
