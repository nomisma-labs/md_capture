// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

export module nom.kit_test.sequence;

import std;

export namespace nom::kit_test
{
    // Standard types cross the module boundary; the consumer owns its input.
    std::vector<int> sorted_unique(std::span<const int> input);

    // A template must remain usable when instantiated in the importing translation unit.
    template <typename Value> std::optional<Value> first_or_empty(std::span<const Value> input)
    {
        if (input.empty())
        {
            return std::nullopt;
        }
        return input.front();
    }
} // namespace nom::kit_test
