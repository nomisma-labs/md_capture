// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

export module nom.example:join;

import std;

export namespace nom::example
{
    // Templates live in module interfaces so importers can instantiate them.
    template <typename... Parts> std::string join([[maybe_unused]] std::string_view separator, const Parts&... parts)
    {
        std::string result;
        std::string_view current_separator;
        ((result += current_separator, result += std::format("{}", parts), current_separator = separator), ...);
        return result;
    }
} // namespace nom::example
