#pragma once

#include "Renderer.h"
#include "application.h"
#include "Document.h"
#include <vector>
#include <string>
#include <functional>
#include "Styles.h"
#include <memory>

namespace TextEdit {

    class FileMenu {
    public:
        // Action to be performed when a menu item is clicked.
        using Action = std::function<void()>;

        // Represents a single item within a dropdown menu.
        struct MenuItem {
            std::u32string name;
            Action action;
            bool enabled;
        };

        // Represents a top-level menu option (e.g., "File", "Edit")
        // that contains a dropdown of MenuItems.
        struct MenuOption {
            std::u32string name;
            std::vector<MenuItem> items;
            bool isOpen;
            Rect rect; // Screen position of the top-level menu option
            float widthModifier;
        };

    private:
        std::shared_ptr<Renderer> mRenderer;
        std::shared_ptr<Font> mFont;

        // State
        std::vector<MenuOption> mMenuOptions;
        int mOpenMenuIndex; // Index of the currently open menu, -1 if none
        bool mIsMouseDown;

        // Input State
        MousePos mLastMousePos;

        // Constants
        static const std::u32string DIVIDER_STRING;

    public:
        FileMenu(std::shared_ptr<Renderer> renderer, std::shared_ptr<Font> font);
        ~FileMenu();

        // Add a top-level menu option with its dropdown items.
        void AddMenuOption(const std::u32string& name, const std::vector<MenuItem>& items, float widthMod);

        // Handle user input for the menu.
        void OnInput(const InputEvent& e);

        // Draw the menu bar and any open dropdown menus.
        float Display(float x, float y, float w, float h);

        // Gets the height of the main menu bar
        float GetMenuHeight() const;

        inline bool IsMenuVisible() const {
            return mOpenMenuIndex >= 0;
        }

        // Get the rectangle of the currently open menu dropdown (if any)
        // Returns an invalid rect (width/height = 0) if no menu is open
        Rect GetOpenMenuRect() const;

    private:
        void HandleMouseDown(const InputEvent& e);
        void HandleMouseUp(const InputEvent& e);
        void HandleMouseMove(const InputEvent& e);
        void CloseOpenMenu();

        // Helper functions
        bool IsDivider(const MenuItem& item) const;
        float GetItemHeight(const MenuItem& item) const;
        float GetTotalDropdownHeight(const MenuOption& menu) const;
        float GetItemY(const MenuOption& menu, size_t itemIndex) const;
    };

} // namespace TextEdit