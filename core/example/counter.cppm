// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

export module nom.example:counter;

import std;

export namespace nom::example
{
    class counter
    {
    public:
        void increment() noexcept;
        void reset() noexcept;
        [[nodiscard]] std::int64_t value() const noexcept;

    private:
        std::int64_t count_ = 0;
    };
} // namespace nom::example
