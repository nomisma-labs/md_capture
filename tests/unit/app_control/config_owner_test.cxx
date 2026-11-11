// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

// Catch2 must precede config_owner.h: GCC 16 rejects its standard headers after import std.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "../../../apps/okx/okx_md_capture/config_owner.h"

import std;
import nom.core;

namespace
{
    struct test_config
    {
        std::string name;

        std::expected<void, std::string> validate() const
        {
            if (name.empty())
                return std::unexpected("name must be nonempty");
            return {};
        }
    };

    template <typename Traits> struct construction_probe
    {
        static inline unsigned constructions{};

        construction_probe()
        {
            ++constructions;
        }
    };

    struct test_assembly : nom::assembly<test_assembly, nom::config_owner, construction_probe>
    {
        using config = test_config;
    };
} // namespace

TEST_CASE("Configuration validation runs before node construction")
{
    construction_probe<test_assembly>::constructions = 0;
    test_config config;
    SECTION("invalid config preserves the diagnostic and constructs no nodes")
    {
        CHECK_THROWS_WITH(nom::relay<test_assembly>{config}, "name must be nonempty");
        CHECK(construction_probe<test_assembly>::constructions == 0);
    }
    SECTION("valid config allows construction")
    {
        config.name = "capture";
        nom::relay<test_assembly> app(config);
        CHECK(app.ctx().cfg().name == "capture");
        CHECK(construction_probe<test_assembly>::constructions == 1);
    }
}
