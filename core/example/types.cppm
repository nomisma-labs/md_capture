// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

export module nom.example:types;

import std;

export namespace nom::example
{
    // Importers instantiate this alias using the standard-library module types.
    template <std::size_t Index, typename... Types> using type_at_t = std::tuple_element_t<Index, std::tuple<Types...>>;
} // namespace nom::example
