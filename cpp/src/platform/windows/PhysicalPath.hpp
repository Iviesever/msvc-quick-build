#pragma once

#include <algorithm>
#include <filesystem>
#include <string_view>
#include "mqb/core/WriteInventory.hpp"

namespace mqb::platform::windows::detail {
// This initial inventory boundary accepts ordinary absolute drive paths only.
// It never normalizes '..' across a junction or interprets device/stream syntax.
inline bool physical_path_supported(const std::filesystem::path& path, WriteExtent extent) {
    const auto drive = path.root_name().native();
    if (!path.is_absolute() || drive.size() != 2 || drive[1] != L':'
        || !((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z'))
        || path.native().find(L'\0') != std::wstring::npos
        || (extent != WriteExtent::file && extent != WriteExtent::directory_namespace)) return false;
    if (extent == WriteExtent::file && (path.filename().empty() || path.filename() == L".")) return false;
    for (const auto& component : path.relative_path()) {
        const auto& name = component.native();
        if (name.empty() || name == L".") continue;
        if (name == L".." || name.back() == L'.' || name.back() == L' ') return false;
        if (std::any_of(name.begin(), name.end(), [](wchar_t c) {
            return c < 32 || std::wstring_view{L"<>:\"/\\|?*"}.find(c) != std::wstring_view::npos;
        })) return false;
        auto stem = name.substr(0, name.find(L'.'));
        while (!stem.empty() && stem.back() == L' ') stem.pop_back();
        for (auto& c : stem) if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL"
            || stem == L"CONIN$" || stem == L"CONOUT$" || stem == L"CLOCK$") return false;
        if (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT"))
            && ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'\u00b9'
                || stem[3] == L'\u00b2' || stem[3] == L'\u00b3')) return false;
    }
    return true;
}
} // namespace mqb::platform::windows::detail
