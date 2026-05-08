// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <windows.h>

namespace ClickTerminal
{
    struct LayoutSlot
    {
        uint32_t     Row{ 0 };
        uint32_t     Col{ 0 };
        std::wstring ProjectId; // empty = default terminal
    };

    struct Layout
    {
        std::wstring            Id;         // "layout-{uuid}"
        std::wstring            Name;
        uint32_t                Rows{ 1 };  // 1-3
        uint32_t                Cols{ 1 };  // 1-3
        std::vector<LayoutSlot> Slots;      // sparse; missing (r,c) = default terminal
        std::wstring            CreatedAt;
        std::wstring            LastUsedAt;
    };

    class LayoutManager
    {
    public:
        explicit LayoutManager(std::wstring configPath = L"");

        LayoutManager(const LayoutManager&)            = delete;
        LayoutManager& operator=(const LayoutManager&) = delete;
        LayoutManager(LayoutManager&&)                 = default;

        bool    LoadLayouts();
        bool    SaveLayouts();

        Layout  AddLayout(Layout layout);
        bool    UpdateLayout(const Layout& layout);
        bool    RemoveLayout(const std::wstring& id);

        std::optional<Layout>   GetLayoutById(const std::wstring& id) const;
        std::vector<Layout>     GetAllLayouts() const;
        bool                    TouchLayout(const std::wstring& id);

        std::function<void()> OnLayoutsChanged;

    private:
        std::wstring         _configPath;
        std::vector<Layout>  _layouts;
        bool                 _loaded{ false };

        bool        DeserializeFromJson(const std::string& json);
        std::string SerializeToJson() const;
        bool        WriteFileAtomic(const std::wstring& path, const std::string& content);

        static std::wstring GenerateLayoutId();
        static std::wstring GetDefaultConfigPath(); // layouts.json beside clickterminal.json
    };

} // namespace ClickTerminal
