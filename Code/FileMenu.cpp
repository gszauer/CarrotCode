#include "FileMenu.h"
#include "Styles.h"
#include "Font.h"

namespace TextEdit {

    const std::u32string FileMenu::DIVIDER_STRING = U"---";

    FileMenu::FileMenu(std::shared_ptr<Renderer> renderer, std::shared_ptr<Font> font)
        : mRenderer(renderer), mFont(font),
        mOpenMenuIndex(-1), mIsMouseDown(false) {

        mLastMousePos = { 0, 0 };
    }

    FileMenu::~FileMenu() {
    }

    void FileMenu::AddMenuOption(const std::u32string& name, const std::vector<MenuItem>& items, float widthMod) {
        MenuOption option;
        option.name = name;
        option.items = items;
        option.isOpen = false;
        option.widthModifier = widthMod;
        mMenuOptions.push_back(option);
    }

    void FileMenu::OnInput(const InputEvent& e) {
        // Update mouse position on any mouse event for hover tracking
        if (e.type == InputEvent::Type::MOUSE_MOVE || e.type == InputEvent::Type::MOUSE_DOWN || e.type == InputEvent::Type::MOUSE_UP) {
            mLastMousePos = { e.mouse.x, e.mouse.y };
        }

        switch (e.type) {
        case InputEvent::Type::MOUSE_DOWN:
            HandleMouseDown(e);
            break;
        case InputEvent::Type::MOUSE_UP:
            HandleMouseUp(e);
            break;
        case InputEvent::Type::MOUSE_MOVE:
            HandleMouseMove(e);
            break;
        default:
            // Other input types are not handled by the menu
            break;
        }
    }

    float FileMenu::Display(float x, float y, float w, float h) {
        mRenderer->SetFont(mFont);
        mRenderer->SetClip(x, y, w, h);

        float currentX = x;
        for (size_t i = 0; i < mMenuOptions.size(); ++i) {
            mRenderer->SetFont(mFont);
            float textWidth = mRenderer->DrawText(mMenuOptions[i].name, 0, 0, TextEdit::Styles::FileMenuTextColor.r, TextEdit::Styles::FileMenuTextColor.g, TextEdit::Styles::FileMenuTextColor.b); // Measure text
            mMenuOptions[i].rect = Rect(currentX, y, textWidth + 2 * TextEdit::Styles::FILE_MENU_PADDING, TextEdit::Styles::FILE_MENU_HEIGHT);
            currentX += mMenuOptions[i].rect.width;
        }
        float result = currentX;
        currentX = x;


        // Draw menu bar background
        mRenderer->DrawRect(x, y, w, TextEdit::Styles::FILE_MENU_HEIGHT, TextEdit::Styles::FileMenuBackgroundColor.r, TextEdit::Styles::FileMenuBackgroundColor.g, TextEdit::Styles::FileMenuBackgroundColor.b);

        // Draw top-level menu options
        for (size_t i = 0; i < mMenuOptions.size(); ++i) {
            mRenderer->SetFont(mFont);

            mRenderer->DrawText(mMenuOptions[i].name, currentX + TextEdit::Styles::FILE_MENU_PADDING, y + (TextEdit::Styles::FILE_MENU_HEIGHT - mFont->GetLineHeight()) / 2.0f, TextEdit::Styles::FileMenuTextColor.r, TextEdit::Styles::FileMenuTextColor.g, TextEdit::Styles::FileMenuTextColor.b);
            currentX += mMenuOptions[i].rect.width;

            // Draw dropdown if this menu is open
            if (mOpenMenuIndex != -1 && (size_t)mOpenMenuIndex == i) {
                Rect menuOptionRect = mMenuOptions[i].rect;
                float dropdownY = menuOptionRect.y + menuOptionRect.height;
                float dropdownHeight = GetTotalDropdownHeight(mMenuOptions[i]);

                // Dropdown background
                float wMod = mMenuOptions[mOpenMenuIndex].widthModifier;
                mRenderer->DrawRect(menuOptionRect.x, dropdownY, TextEdit::Styles::FILE_MENU_DROPDOWN_WIDTH * wMod, dropdownHeight, TextEdit::Styles::FileMenuDropdownBackgroundColor.r, TextEdit::Styles::FileMenuDropdownBackgroundColor.g, TextEdit::Styles::FileMenuDropdownBackgroundColor.b);

                // Draw dropdown items
                for (size_t j = 0; j < mMenuOptions[i].items.size(); ++j) {
                    float itemY = GetItemY(mMenuOptions[i], j);
                    float itemHeight = GetItemHeight(mMenuOptions[i].items[j]);
                    Rect itemRect(menuOptionRect.x, itemY, TextEdit::Styles::FILE_MENU_DROPDOWN_WIDTH * wMod, itemHeight);

                    if (IsDivider(mMenuOptions[i].items[j])) {
                        // Draw divider line
                        float lineY = itemY + itemHeight / 2.0f;
                        if (TextEdit::Styles::DPI > 1.0f) {
                            lineY -= TextEdit::Styles::DPI * 0.5f;
                        }
                        float lineMargin = TextEdit::Styles::FILE_MENU_PADDING;
                        mRenderer->DrawRect(itemRect.x + lineMargin, lineY, itemRect.width - 2 * lineMargin, std::max(1.0f, TextEdit::Styles::DPI),
                            TextEdit::Styles::FileMenuTextColor.r * 0.5f, 
                            TextEdit::Styles::FileMenuTextColor.g * 0.5f, 
                            TextEdit::Styles::FileMenuTextColor.b * 0.5f);
                    }
                    else {
                        // Highlight on hover
                        if (itemRect.Contains((float)mLastMousePos.x, (float)mLastMousePos.y)) {
                            mRenderer->DrawRect(itemRect.x, itemRect.y, itemRect.width, itemRect.height, TextEdit::Styles::FileMenuHighlightColor.r, TextEdit::Styles::FileMenuHighlightColor.g, TextEdit::Styles::FileMenuHighlightColor.b);
                        }

                        mRenderer->DrawText(mMenuOptions[i].items[j].name, itemRect.x + TextEdit::Styles::FILE_MENU_PADDING, itemY + (TextEdit::Styles::FILE_MENU_DROPDOWN_HEIGHT - mFont->GetLineHeight()) / 2.0f, TextEdit::Styles::FileMenuTextColor.r, TextEdit::Styles::FileMenuTextColor.g, TextEdit::Styles::FileMenuTextColor.b);
                    }
                }
            }
        }
        mRenderer->ClearClip();

        return result;
    }

    float FileMenu::GetMenuHeight() const {
        return TextEdit::Styles::FILE_MENU_HEIGHT;
    }

    void FileMenu::HandleMouseDown(const InputEvent& e) {
        mIsMouseDown = true;
        int previouslyOpenMenu = mOpenMenuIndex;

        // If we click outside an open menu, close it.
        bool clickInsideMenu = false;
        if (mOpenMenuIndex != -1) {
            const auto& openMenu = mMenuOptions[mOpenMenuIndex];
            float wMod = mMenuOptions[mOpenMenuIndex].widthModifier;
            float dropdownHeight = GetTotalDropdownHeight(openMenu);
            Rect dropdownRect(openMenu.rect.x, openMenu.rect.y + openMenu.rect.height, TextEdit::Styles::FILE_MENU_DROPDOWN_WIDTH * wMod, dropdownHeight);
            if (dropdownRect.Contains((float)e.mouse.x, (float)e.mouse.y)) {
                clickInsideMenu = true;
            }
        }

        // Check if a top-level menu option was clicked
        for (size_t i = 0; i < mMenuOptions.size(); ++i) {
            if (mMenuOptions[i].rect.Contains((float)e.mouse.x, (float)e.mouse.y)) {
                clickInsideMenu = true;
                if ((int)i == previouslyOpenMenu) {
                    // Clicked the same menu header again, so close it.
                    CloseOpenMenu();
                }
                else {
                    // Clicked a new menu header, so open it.
                    mOpenMenuIndex = (int)i;
                }
                return;
            }
        }

        if (!clickInsideMenu) {
            CloseOpenMenu();
        }
    }

    void FileMenu::HandleMouseUp(const InputEvent& e) {
        if (mOpenMenuIndex != -1) {
            const auto& openMenu = mMenuOptions[mOpenMenuIndex];
            Rect menuOptionRect = openMenu.rect;
            float dropdownY = menuOptionRect.y + menuOptionRect.height;

            // Check if a dropdown item was clicked
            for (size_t i = 0; i < openMenu.items.size(); ++i) {
                // Skip dividers
                if (IsDivider(openMenu.items[i])) {
                    continue;
                }

                float itemY = GetItemY(openMenu, i);
                float itemHeight = GetItemHeight(openMenu.items[i]);
                float wMod = mMenuOptions[mOpenMenuIndex].widthModifier;
                Rect itemRect(menuOptionRect.x, itemY, TextEdit::Styles::FILE_MENU_DROPDOWN_WIDTH * wMod, itemHeight);

                if (itemRect.Contains((float)e.mouse.x, (float)e.mouse.y)) {
                    if (openMenu.items[i].action) {
                        openMenu.items[i].action();
                    }
                    CloseOpenMenu();
                    mIsMouseDown = false;
                    return;
                }
            }
        }
        mIsMouseDown = false;
    }

    void FileMenu::HandleMouseMove(const InputEvent& e) {
        // If a menu is open, switch to the menu under the cursor when hovering over a different top-level menu
        if (mOpenMenuIndex != -1) {
            for (size_t i = 0; i < mMenuOptions.size(); ++i) {
                if (mMenuOptions[i].rect.Contains((float)e.mouse.x, (float)e.mouse.y)) {
                    if ((int)i != mOpenMenuIndex) {
                        mOpenMenuIndex = (int)i;
                    }
                    break; // Found the menu under the cursor
                }
            }
        }
    }

    void FileMenu::CloseOpenMenu() {
        if (mOpenMenuIndex != -1) {
            mMenuOptions[mOpenMenuIndex].isOpen = false;
            mOpenMenuIndex = -1;
        }
    }

    Rect FileMenu::GetOpenMenuRect() const {
        if (mOpenMenuIndex == -1) {
            return Rect(0, 0, 0, 0); // Invalid rect - no menu open
        }

        const auto& openMenu = mMenuOptions[mOpenMenuIndex];
        float dropdownY = openMenu.rect.y + openMenu.rect.height;
        float dropdownHeight = GetTotalDropdownHeight(openMenu);
        float dropdownWidth = TextEdit::Styles::FILE_MENU_DROPDOWN_WIDTH * openMenu.widthModifier;

        return Rect(openMenu.rect.x, dropdownY, dropdownWidth, dropdownHeight);
    }

    bool FileMenu::IsDivider(const MenuItem& item) const {
        return item.name == DIVIDER_STRING;
    }

    float FileMenu::GetItemHeight(const MenuItem& item) const {
        return IsDivider(item) ? TextEdit::Styles::FILE_MENU_DIVIDER_HEIGHT : TextEdit::Styles::FILE_MENU_DROPDOWN_HEIGHT;
    }

    float FileMenu::GetTotalDropdownHeight(const MenuOption& menu) const {
        float totalHeight = 0.0f;
        for (const auto& item : menu.items) {
            totalHeight += GetItemHeight(item);
        }
        return totalHeight;
    }

    float FileMenu::GetItemY(const MenuOption& menu, size_t itemIndex) const {
        float y = menu.rect.y + menu.rect.height;
        for (size_t i = 0; i < itemIndex; ++i) {
            y += GetItemHeight(menu.items[i]);
        }
        return y;
    }

} // namespace TextEdit