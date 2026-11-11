// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

// Pass the full warning option as a string literal, e.g. "-Wmaybe-uninitialized".
// Each BEGIN must have a matching END; regions may be nested.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#define NOM_GCC16_DETAIL_PRAGMA(directive) _Pragma(#directive)
#define NOM_GCC16_IGNORE_BEGIN(warning_name)                                                                           \
    _Pragma("GCC diagnostic push") NOM_GCC16_DETAIL_PRAGMA(GCC diagnostic ignored warning_name)
#define NOM_GCC16_IGNORE_END _Pragma("GCC diagnostic pop")
#else
#define NOM_GCC16_IGNORE_BEGIN(warning_name)
#define NOM_GCC16_IGNORE_END
#endif

// GCC 16 can misdiagnose access to the already-initialized context from a sibling
// node's constructor. Limit suppression to the wrapped code.
#define NOM_GCC16_IGNORE_MAYBE_UNINITIALIZED(...)                                                                      \
    NOM_GCC16_IGNORE_BEGIN("-Wmaybe-uninitialized") __VA_ARGS__ NOM_GCC16_IGNORE_END
