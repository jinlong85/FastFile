#pragma once
#include <algorithm>
#include <vector>
#include <numeric>

namespace TabLayout {
// Preserve short content widths. Only long titles share the remaining room;
// when even all minimum widths do not fit the caller scrolls horizontally.
inline std::vector<int> Fit(const std::vector<int>& preferred, int minimum, int available) {
    if (std::accumulate(preferred.begin(), preferred.end(), 0) <= available) return preferred;
    std::vector<int> widths;
    int base = 0, maxWidth = minimum;
    for (int value : preferred) { widths.push_back((std::min)(value, minimum)); base += widths.back(); maxWidth = (std::max)(maxWidth, value); }
    if (base >= available) return widths;
    int low = minimum, high = maxWidth;
    while (low < high) {
        int cap = low + (high - low + 1) / 2, sum = 0;
        for (int value : preferred) sum += (std::min)(value, cap);
        if (sum <= available) low = cap; else high = cap - 1;
    }
    int sum = 0;
    for (size_t i = 0; i < widths.size(); ++i) { widths[i] = (std::min)(preferred[i], low); sum += widths[i]; }
    for (size_t i = 0; i < widths.size() && sum < available; ++i)
        if (widths[i] < preferred[i]) { ++widths[i]; ++sum; }
    return widths;
}
}
