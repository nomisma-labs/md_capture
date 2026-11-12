// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <base/util/force_inline.h>

export module nom.services:config_owner;

import std;

export namespace nom
{
    template <typename Traits> struct config_owner
    {
        using config = typename Traits::config;

        static_assert(
            requires(const config& value) {
                { value.validate() } -> std::same_as<std::expected<void, std::string>>;
            }, "The assembly config must provide validate() const returning std::expected<void, std::string>");

        class context_mixin
        {
        public:
            // Copy and validate configuration before the assembly constructs its nodes.
            explicit context_mixin(const config& value) : config_(value)
            {
                if (const auto result = std::as_const(config_).validate(); !result)
                    throw std::invalid_argument(result.error());
            }

            NOM_FORCE_INLINE const config& cfg() const noexcept
            {
                return config_;
            }

        private:
            config config_{};
        };
    };
} // namespace nom
