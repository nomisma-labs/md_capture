// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// Import standard declarations used directly, as well as the library module.

import std;

import nom.example;

int main()
{
    static_assert(std::is_same_v<nom::example::type_at_t<1, int, std::string>, std::string>);
    nom::example::counter clicks;
    clicks.increment();
    const nom::example::settings defaults;
    const auto restored = nom::example::parse_settings(nom::example::serialize_settings(defaults));
    const bool ok = restored && restored->name == defaults.name && restored->repeat == defaults.repeat &&
                    nom::example::join("-", defaults.name, clicks.value()) == "world-1";
    return ok ? 0 : 1;
}
