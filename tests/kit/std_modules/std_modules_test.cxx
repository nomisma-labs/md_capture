// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

import std;

import nom.kit_test.sequence;

TEST_CASE("std module types and algorithms cross the library boundary", "[std_modules]")
{
    const std::vector<int> input{4, -1, 4, 2, -1};
    CHECK(nom::kit_test::sorted_unique(input) == std::vector<int>{-1, 2, 4});
    CHECK(input == std::vector<int>{4, -1, 4, 2, -1});
    CHECK(nom::kit_test::sorted_unique({}).empty());
}

TEST_CASE("std module templates instantiate in the consumer", "[std_modules]")
{
    const std::array<std::string, 2> input{"first", "second"};
    const auto first = nom::kit_test::first_or_empty<std::string>(input);
    REQUIRE(first.has_value());
    CHECK(*first == "first");
    CHECK_FALSE(nom::kit_test::first_or_empty<std::string>({}).has_value());
}
