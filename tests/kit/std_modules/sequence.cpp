// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module nom.kit_test.sequence;

import std;

namespace nom::kit_test
{
    std::vector<int> sorted_unique(std::span<const int> input)
    {
        std::vector<int> result(input.begin(), input.end());
        std::ranges::sort(result);
        const auto duplicates = std::ranges::unique(result);
        result.erase(duplicates.begin(), duplicates.end());
        return result;
    }
} // namespace nom::kit_test
