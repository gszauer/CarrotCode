#include "DocumentContainer.h"
#include "Styles.h"
#include <algorithm>
#include <cmath>

namespace TextEdit {

    bool DocumentContainer::sDraggingTab = false;
    DocumentContainer::Tab* DocumentContainer::sDraggedTab = nullptr;
    DocumentContainer* DocumentContainer::sDragSource = nullptr;
    MousePos DocumentContainer::sDragStartPos = { 0, 0 };
    MousePos DocumentContainer::sDragCurrentPos = { 0, 0 };
    bool DocumentContainer::sDragStarted = false;
    std::shared_ptr<DocumentContainer> DocumentContainer::sActiveContainer = nullptr;
    int DocumentContainer::sActiveTabIndex = -1;

    DocumentContainer::DocumentContainer(std::shared_ptr<Renderer> renderer,
        std::shared_ptr<Font> font,
        std::shared_ptr<Font> smallFont,
        ContainerType type)
        : mType(type)
        , mActiveTabIndex(-1)
        , mShowTabBar(true)
        , mSplitPosition(0.5f)
        , mRenderer(renderer)
        , mFont(font)
        , mSmallFont(smallFont)
        , mIsDraggingSplitter(false)
        , mSplitterDragOffset(0.0f)
        , mTabScrollOffset(0.0f)
        , mMaxTabScrollOffset(0.0f)
        , mIsHoveringLeftArrow(false)
        , mIsHoveringRightArrow(false)
        , mTabBarContentWidth(0.0f)
        , mVisibleTabBarWidth(0.0f) {

        if (mType == ContainerType::HORIZONTAL_SPLIT || mType == ContainerType::VERTICAL_SPLIT) {
            mLeftOrTop = std::make_shared<DocumentContainer>(renderer, font, smallFont, ContainerType::TABBED);
            mRightOrBottom = std::make_shared<DocumentContainer>(renderer, font, smallFont, ContainerType::TABBED);
            mLeftOrTop->SetParent(weak_from_this());
            mRightOrBottom->SetParent(weak_from_this());
        }
    }

    DocumentContainer::~DocumentContainer() {
        if (sActiveContainer.get() == this) {
            sActiveContainer = nullptr;
            sActiveTabIndex = -1;
        }
    }

    void DocumentContainer::AddDocument(std::shared_ptr<Document> doc) {
        if (mType != ContainerType::TABBED) {
            std::shared_ptr<DocumentContainer> target = FindLargestTabbedContainer();
            if (target != nullptr) {
                target->AddDocument(doc);
            }
            return;
        }

        Tab newTab;
        newTab.document = doc;
        newTab.view = std::make_shared<DocumentView>(mRenderer, doc, mFont, mSmallFont);
        newTab.markedForClose = false;

        mTabs.push_back(std::move(newTab));
        mActiveTabIndex = static_cast<int>(mTabs.size()) - 1;

        SetAsActive();
        ScrollTabsToShowTab(mActiveTabIndex);
    }

    void DocumentContainer::CloseMenus() {
        if (mType == ContainerType::TABBED) {
            for (auto& tab : mTabs) {
                if (tab.view && !tab.markedForClose) {
                    tab.view->CloseMenus();
                }
            }
        }
        else {
            if (mLeftOrTop) mLeftOrTop->CloseMenus();
            if (mRightOrBottom) mRightOrBottom->CloseMenus();
        }
    }

    void DocumentContainer::RemoveDocument(std::shared_ptr<Document> doc, bool isMoving) {
        if (mType != ContainerType::TABBED) {
            if (mLeftOrTop) mLeftOrTop->RemoveDocument(doc, isMoving);
            if (mRightOrBottom) mRightOrBottom->RemoveDocument(doc, isMoving);
            return;
        }

        auto it = std::find_if(mTabs.begin(), mTabs.end(),
            [&doc](const Tab& tab) { return tab.document == doc && !tab.markedForClose; });

        if (it != mTabs.end()) {
            it->markedForClose = true;
            it->isBeingMoved = isMoving;
        }
    }

    void DocumentContainer::CloseTab(int index) {
        if (index >= 0 && index < static_cast<int>(mTabs.size()) && !mTabs[index].markedForClose) {
            mTabs[index].markedForClose = true;
        }
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::FindFirstTabbedContainerWithTabs() {
        if (IsRoot()) {
            return FindFirstTabbedContainerWithTabsRecursive();
        }

        DocumentContainer* root = this;
        while (auto parent = root->GetParent()) {
            root = parent.get();
        }
        return root->FindFirstTabbedContainerWithTabsRecursive();
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::FindFirstTabbedContainerWithTabsRecursive() {
        if (mType == ContainerType::TABBED) {
            int validTabs = 0;
            for (const auto& tab : mTabs) {
                if (!tab.markedForClose) validTabs++;
            }
            if (validTabs > 0) {
                return shared_from_this();
            }
        }

        if (mType != ContainerType::TABBED) {
            std::shared_ptr<DocumentContainer> result = nullptr;
            if (mLeftOrTop) {
                result = mLeftOrTop->FindFirstTabbedContainerWithTabsRecursive();
                if (result) return result;
            }
            if (mRightOrBottom) {
                result = mRightOrBottom->FindFirstTabbedContainerWithTabsRecursive();
                if (result) return result;
            }
        }

        return nullptr;
    }

    void DocumentContainer::SetActiveTab(int index) {
        if (mType == ContainerType::TABBED && index >= 0 && index < static_cast<int>(mTabs.size()) && !mTabs[index].markedForClose) {
            mActiveTabIndex = index;
            SetAsActive();
            ScrollTabsToShowTab(index);
        }
    }

    void DocumentContainer::SetAsActive() {
        if (mType == ContainerType::TABBED && mActiveTabIndex >= 0 && mActiveTabIndex < static_cast<int>(mTabs.size()) && !mTabs[mActiveTabIndex].markedForClose) {
            sActiveContainer = shared_from_this();
            sActiveTabIndex = mActiveTabIndex;
        }
    }

    bool DocumentContainer::IsActive() const {
        return sActiveContainer.get() == this && mActiveTabIndex >= 0 && mActiveTabIndex == sActiveTabIndex;
    }

    void DocumentContainer::SetParent(std::weak_ptr<DocumentContainer> parent) {
        mParent = parent;
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::GetParent() const {
        return mParent.lock();
    }

    bool DocumentContainer::IsEmpty() const {
        if (mType == ContainerType::TABBED) {
            for (const auto& tab : mTabs) {
                if (!tab.markedForClose) return false;
            }
            return true;
        }
        return (!mLeftOrTop || mLeftOrTop->IsEmpty()) &&
            (!mRightOrBottom || mRightOrBottom->IsEmpty());
    }

    bool DocumentContainer::IsRoot() const {
        return mParent.expired();
    }

    void DocumentContainer::Display(float x, float y, float w, float h) {
        mBounds = { x, y, w, h };

        if (sDraggingTab && sDragStarted) {
            TextEdit::Styles::GlobalTint.r = TextEdit::Styles::DisabledTint;
            TextEdit::Styles::GlobalTint.g = TextEdit::Styles::DisabledTint;
            TextEdit::Styles::GlobalTint.b = TextEdit::Styles::DisabledTint;
        }

        if (mType == ContainerType::TABBED) {
            float contentX = x, contentY = y, contentW = w, contentH = h;

            bool hasVisibleTabs = false;
            for (const auto& tab : mTabs) {
                if (!tab.markedForClose) {
                    hasVisibleTabs = true;
                    break;
                }
            }

            if (hasVisibleTabs && mShowTabBar) {
                DrawTabBar(x, y, w, TextEdit::Styles::TAB_BAR_HEIGHT);
                contentY += TextEdit::Styles::TAB_BAR_HEIGHT;
                contentH -= TextEdit::Styles::TAB_BAR_HEIGHT;
            }
            else if (!hasVisibleTabs && mShowTabBar) {
                DrawTabBar(x, y, w, TextEdit::Styles::TAB_BAR_HEIGHT);
                contentY += TextEdit::Styles::TAB_BAR_HEIGHT;
                contentH -= TextEdit::Styles::TAB_BAR_HEIGHT;
            }

            if (mActiveTabIndex >= 0 && mActiveTabIndex < static_cast<int>(mTabs.size()) && !mTabs[mActiveTabIndex].markedForClose) {
                mTabs[mActiveTabIndex].view->Display(contentX, contentY, contentW, contentH);
            }

            if (sDraggingTab && sDragStarted) {
                if (sDraggingTab && sDragStarted) {
                    TextEdit::Styles::GlobalTint.r = 1.0f;
                    TextEdit::Styles::GlobalTint.g = 1.0f;
                    TextEdit::Styles::GlobalTint.b = 1.0f;
                }

                UpdateDockingWidgets(contentX, contentY, contentW, contentH);
                DrawDockingWidgets(contentX, contentY, contentW, contentH);


                if (sDraggingTab && sDragStarted) {
                    TextEdit::Styles::GlobalTint.r = TextEdit::Styles::DisabledTint;
                    TextEdit::Styles::GlobalTint.g = TextEdit::Styles::DisabledTint;
                    TextEdit::Styles::GlobalTint.b = TextEdit::Styles::DisabledTint;
                }
            }
        }
        else if (mType == ContainerType::HORIZONTAL_SPLIT) {
            float leftWidth = w * mSplitPosition;
            if (mLeftOrTop) mLeftOrTop->Display(x, y, leftWidth - 2, h);
            if (mRightOrBottom) mRightOrBottom->Display(x + leftWidth + 2, y, w - leftWidth - 2, h);

            bool hoverSplitter = IsOnSplitter(static_cast<float>(sDragCurrentPos.x), static_cast<float>(sDragCurrentPos.y));
            auto& splitterColor = (hoverSplitter || mIsDraggingSplitter) ? Styles::ScrollbarNibHoverColor : Styles::ScrollbarTrackColor;
            mRenderer->DrawRect(x + leftWidth - 2, y, 4, h, splitterColor.r, splitterColor.g, splitterColor.b);
        }
        else if (mType == ContainerType::VERTICAL_SPLIT) {
            float topHeight = h * mSplitPosition;
            if (mLeftOrTop) mLeftOrTop->Display(x, y, w, topHeight - 2);
            if (mRightOrBottom) mRightOrBottom->Display(x, y + topHeight + 2, w, h - topHeight - 2);

            bool hoverSplitter = IsOnSplitter(static_cast<float>(sDragCurrentPos.x), static_cast<float>(sDragCurrentPos.y));
            auto& splitterColor = (hoverSplitter || mIsDraggingSplitter) ? Styles::ScrollbarNibHoverColor : Styles::ScrollbarTrackColor;
            mRenderer->DrawRect(x, y + topHeight - 2, w, 4, splitterColor.r, splitterColor.g, splitterColor.b);
        }

        if (sDraggingTab && sDragStarted) {
            TextEdit::Styles::GlobalTint.r = 1.0f;
            TextEdit::Styles::GlobalTint.g = 1.0f;
            TextEdit::Styles::GlobalTint.b = 1.0f;
        }

        ProcessMarkedForClose();
    }

    void DocumentContainer::Update(float deltaTime) {
        if (mType == ContainerType::TABBED) {
            if (mActiveTabIndex >= 0 && mActiveTabIndex < static_cast<int>(mTabs.size()) && !mTabs[mActiveTabIndex].markedForClose) {
                mTabs[mActiveTabIndex].view->Update(deltaTime);
            }
        }
        else {
            if (mLeftOrTop) mLeftOrTop->Update(deltaTime);
            if (mRightOrBottom) mRightOrBottom->Update(deltaTime);
        }
    }

    void DocumentContainer::OnInput(const InputEvent& e) {
        if (e.type == InputEvent::Type::MOUSE_MOVE) {
            sDragCurrentPos = { e.mouse.x, e.mouse.y };
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
        case InputEvent::Type::MOUSE_WHEEL:
            HandleMouseWheel(e);
            break;
        default:
            if (sActiveContainer && sActiveContainer->mType == ContainerType::TABBED &&
                sActiveTabIndex >= 0 && sActiveTabIndex < static_cast<int>(sActiveContainer->mTabs.size())) {
                sActiveContainer->mTabs[sActiveTabIndex].view->OnInput(e);
            }
            break;
        }
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::FindLargestTabbedContainer() {
        if (mType == ContainerType::TABBED) {
            return shared_from_this();
        }

        std::shared_ptr<DocumentContainer> leftLargest = mLeftOrTop ? mLeftOrTop->FindLargestTabbedContainer() : nullptr;
        std::shared_ptr<DocumentContainer> rightLargest = mRightOrBottom ? mRightOrBottom->FindLargestTabbedContainer() : nullptr;

        if (!leftLargest) return rightLargest;
        if (!rightLargest) return leftLargest;

        float leftArea = leftLargest->mBounds.width * leftLargest->mBounds.height;
        float rightArea = rightLargest->mBounds.width * rightLargest->mBounds.height;

        return (leftArea >= rightArea) ? leftLargest : rightLargest;
    }

    void DocumentContainer::SplitHorizontal(std::shared_ptr<DocumentContainer> newContainer, bool putNewOnRight) {
        auto oldContent = std::make_shared<DocumentContainer>(mRenderer, mFont, mSmallFont, mType);
        oldContent->mTabs = std::move(mTabs);
        oldContent->mActiveTabIndex = mActiveTabIndex;
        oldContent->mLeftOrTop = std::move(mLeftOrTop);
        oldContent->mRightOrBottom = std::move(mRightOrBottom);
        oldContent->SetParent(weak_from_this());

        if (sActiveContainer.get() == this) {
            sActiveContainer = oldContent;
        }

        mType = ContainerType::HORIZONTAL_SPLIT;
        mTabs.clear();
        mActiveTabIndex = -1;

        if (putNewOnRight) {
            mLeftOrTop = oldContent;
            mRightOrBottom = newContainer;
        }
        else {
            mLeftOrTop = newContainer;
            mRightOrBottom = oldContent;
        }

        mLeftOrTop->SetParent(weak_from_this());
        mRightOrBottom->SetParent(weak_from_this());
    }

    void DocumentContainer::SplitVertical(std::shared_ptr<DocumentContainer> newContainer, bool putNewOnBottom) {
        auto oldContent = std::make_shared<DocumentContainer>(mRenderer, mFont, mSmallFont, mType);
        oldContent->mTabs = std::move(mTabs);
        oldContent->mActiveTabIndex = mActiveTabIndex;
        oldContent->mLeftOrTop = std::move(mLeftOrTop);
        oldContent->mRightOrBottom = std::move(mRightOrBottom);
        oldContent->SetParent(weak_from_this());

        if (sActiveContainer.get() == this) {
            sActiveContainer = oldContent;
        }

        mType = ContainerType::VERTICAL_SPLIT;
        mTabs.clear();
        mActiveTabIndex = -1;

        if (putNewOnBottom) {
            mLeftOrTop = oldContent;
            mRightOrBottom = newContainer;
        }
        else {
            mLeftOrTop = newContainer;
            mRightOrBottom = oldContent;
        }

        mLeftOrTop->SetParent(weak_from_this());
        mRightOrBottom->SetParent(weak_from_this());
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::FindFirstTabbedContainer() {
        if (mType == ContainerType::TABBED) {
            int validTabs = 0;
            for (const auto& tab : mTabs) {
                if (!tab.markedForClose) validTabs++;
            }
            if (validTabs > 0) {
                return shared_from_this();
            }
        }

        if (mType != ContainerType::TABBED) {
            std::shared_ptr<DocumentContainer> result = nullptr;
            if (mLeftOrTop) {
                result = mLeftOrTop->FindFirstTabbedContainer();
                if (result) return result;
            }
            if (mRightOrBottom) {
                result = mRightOrBottom->FindFirstTabbedContainer();
                if (result) return result;
            }
        }

        return nullptr;
    }

    void DocumentContainer::RemoveEmptyContainers() {
        if (IsRoot()) {
            if (mType != ContainerType::TABBED) {
                if (mLeftOrTop) mLeftOrTop->RemoveEmptyContainers();
                if (mRightOrBottom) mRightOrBottom->RemoveEmptyContainers();

                bool leftEmpty = !mLeftOrTop || mLeftOrTop->IsEmpty();
                bool rightEmpty = !mRightOrBottom || mRightOrBottom->IsEmpty();

                if (leftEmpty && !rightEmpty) {
                    ReplaceWith(mRightOrBottom);
                }
                else if (!leftEmpty && rightEmpty) {
                    ReplaceWith(mLeftOrTop);
                }
                else if (leftEmpty && rightEmpty) {
                    mType = ContainerType::TABBED;
                    mTabs.clear();
                    mActiveTabIndex = -1;
                    mLeftOrTop = nullptr;
                    mRightOrBottom = nullptr;
                }
            }

            if (!sActiveContainer || sActiveTabIndex < 0) {
                std::shared_ptr<DocumentContainer> firstTabbed = FindFirstTabbedContainer();
                if (firstTabbed != nullptr) {
                    firstTabbed->SetAsActive();
                }
            }
            return;
        }

        if (mType != ContainerType::TABBED) {
            if (mLeftOrTop) mLeftOrTop->RemoveEmptyContainers();
            if (mRightOrBottom) mRightOrBottom->RemoveEmptyContainers();

            bool leftEmpty = !mLeftOrTop || mLeftOrTop->IsEmpty();
            bool rightEmpty = !mRightOrBottom || mRightOrBottom->IsEmpty();

            if (leftEmpty && rightEmpty) {
                return;
            }

            if (leftEmpty || rightEmpty) {
                auto nonEmpty = leftEmpty ? mRightOrBottom : mLeftOrTop;
                if (nonEmpty) {
                    ReplaceWith(nonEmpty);
                }
            }
        }
    }

    void DocumentContainer::ProcessMarkedForClose() {
        if (mType == ContainerType::TABBED) {
            bool anyRemoved = false;
            bool wasActive = false;
            int oldActiveIndex = mActiveTabIndex;

            mTabs.erase(std::remove_if(mTabs.begin(), mTabs.end(),
                [&](const Tab& tab) {
                    if (tab.markedForClose) {
                        int index = static_cast<int>(&tab - &mTabs[0]);
                        if (sActiveContainer.get() == this && mActiveTabIndex == index) {
                            wasActive = true;
                        }
                        anyRemoved = true;

                        tab.view->GetTarget()->SaveIfNeededOnClose();

                        return true;
                    }
                    return false;
                }), mTabs.end());

            if (anyRemoved) {
                if (mActiveTabIndex >= static_cast<int>(mTabs.size())) {
                    mActiveTabIndex = static_cast<int>(mTabs.size()) - 1;
                }

                if (wasActive) {
                    if (mActiveTabIndex >= 0 && !mTabs.empty()) {
                        SetAsActive();
                    }
                    else {
                        sActiveContainer = nullptr;
                        sActiveTabIndex = -1;

                        std::shared_ptr<DocumentContainer> newActive = FindFirstTabbedContainerWithTabs();
                        if (newActive && !newActive->mTabs.empty()) {
                            newActive->mActiveTabIndex = 0;
                            newActive->SetAsActive();
                        }
                    }
                }

                if (IsEmpty() && !IsRoot()) {
                    if (auto parent = GetParent()) {
                        parent->RemoveEmptyContainers();
                    }
                }
            }
        }
        else {
            if (mLeftOrTop) mLeftOrTop->ProcessMarkedForClose();
            if (mRightOrBottom) mRightOrBottom->ProcessMarkedForClose();

            if (!IsRoot() && IsEmpty()) {
                if (auto parent = GetParent()) {
                    parent->RemoveEmptyContainers();
                }
            }
        }
    }

    void DocumentContainer::StartTabDrag(Tab* tab, DocumentContainer* source, int mouseX, int mouseY) {
        sDraggingTab = true;
        sDraggedTab = tab;
        sDragSource = source;
        sDragStartPos = { mouseX, mouseY };
        sDragCurrentPos = { mouseX, mouseY };
        sDragStarted = false;
    }

    DocumentContainer::Tab* DocumentContainer::GetDraggedTab() {
        return sDraggedTab;
    }

    bool DocumentContainer::IsTabDragging() {
        return sDraggingTab && sDragStarted;
    }

    void DocumentContainer::EndTabDrag() {
        sDraggingTab = false;
        sDraggedTab = nullptr;
        sDragSource = nullptr;
        sDragStarted = false;
    }

    DocumentContainer::ContainerType DocumentContainer::GetType() const {
        return mType;
    }

    const std::vector<DocumentContainer::Tab>& DocumentContainer::GetTabs() const {
        return mTabs;
    }

    float DocumentContainer::GetContentArea(float& outX, float& outY, float& outW, float& outH) const {
        outX = mBounds.x;
        outY = mBounds.y;
        outW = mBounds.width;
        outH = mBounds.height;

        if (mType == ContainerType::TABBED && mShowTabBar) {
            bool hasVisibleTabs = false;
            for (const auto& tab : mTabs) {
                if (!tab.markedForClose) {
                    hasVisibleTabs = true;
                    break;
                }
            }
            if (hasVisibleTabs || mTabs.empty()) {
                outY += TextEdit::Styles::TAB_BAR_HEIGHT;
                outH -= TextEdit::Styles::TAB_BAR_HEIGHT;
            }
        }

        return outH;
    }

    void DocumentContainer::CloseMenusOnRoot(DocumentContainer* keepOpen) {
        auto parent = mParent.lock();
        while (parent != nullptr) {
            auto parent_parent = parent->mParent.lock();
            if (parent_parent == nullptr) {
                break;
            }
            parent = parent_parent;
        }

        if (parent != nullptr) {
            parent->CloseMenus();
        }
        else {
            if (keepOpen != 0 && keepOpen == this) {
                return;
            }
            CloseMenus();
        }
    }

    void DocumentContainer::HandleMouseDown(const InputEvent& e) {
        float mx = static_cast<float>(e.mouse.x);
        float my = static_cast<float>(e.mouse.y);

        if (!mBounds.Contains(mx, my)) {
            return;
        }

        auto parent = mParent.lock();

        if (IsOnSplitter(mx, my)) {
            mIsDraggingSplitter = true;
            if (mType == ContainerType::HORIZONTAL_SPLIT) {
                mSplitterDragOffset = mx - (mBounds.x + mBounds.width * mSplitPosition);
            }
            else {
                mSplitterDragOffset = my - (mBounds.y + mBounds.height * mSplitPosition);
            }
            CloseMenusOnRoot();
            return;
        }

        if (mType != ContainerType::TABBED) {
            if (mLeftOrTop) mLeftOrTop->HandleMouseDown(e);
            if (mRightOrBottom) mRightOrBottom->HandleMouseDown(e);
            return;
        }

        if (mType == ContainerType::TABBED && mShowTabBar) {
            if (my >= mBounds.y && my <= mBounds.y + TextEdit::Styles::TAB_BAR_HEIGHT) {
                CloseMenusOnRoot();

                if (mLeftArrowRect.width > 0 && mLeftArrowRect.Contains(mx, my)) {
                    ScrollTabsLeft();
                    return;
                }
                if (mRightArrowRect.width > 0 && mRightArrowRect.Contains(mx, my)) {
                    ScrollTabsRight();
                    return;
                }

                int closeButtonIndex = GetCloseButtonAtPosition(mx, my);
                if (closeButtonIndex >= 0) {
                    CloseTab(closeButtonIndex);
                    return;
                }

                int tabIndex = GetTabAtPosition(mx, my);
                if (tabIndex >= 0) {
                    SetActiveTab(tabIndex);
                    StartTabDrag(&mTabs[tabIndex], this, e.mouse.x, e.mouse.y);
                    return;
                }

                static unsigned long lastClickTime = 0;
                static float lastClickX = 0;
                static float lastClickY = 0;
                static DocumentContainer* lastClickContainer = nullptr;
                const unsigned long DOUBLE_CLICK_TIME = 500;
                const float DOUBLE_CLICK_DISTANCE = 5.0f * TextEdit::Styles::DPI;

                unsigned long currentTime = e.time;
                float dx = mx - lastClickX;
                float dy = my - lastClickY;
                float distance = std::sqrt(dx * dx + dy * dy);

                if (currentTime - lastClickTime < DOUBLE_CLICK_TIME &&
                    distance < DOUBLE_CLICK_DISTANCE &&
                    lastClickContainer == this) {
                    std::shared_ptr<TextEdit::Document> document = TextEdit::Document::Create();

                    Tab newTab;
                    newTab.document = document;
                    newTab.view = std::make_shared<DocumentView>(mRenderer, document, mFont, mSmallFont);
                    newTab.markedForClose = false;
                    mTabs.push_back(std::move(newTab));
                    mActiveTabIndex = static_cast<int>(mTabs.size()) - 1;
                    SetAsActive();
                    ScrollTabsToShowTab(mActiveTabIndex);

                    lastClickTime = 0;
                    lastClickContainer = nullptr;
                }
                else {
                    lastClickTime = currentTime;
                    lastClickX = mx;
                    lastClickY = my;
                    lastClickContainer = this;
                }

                return;
            }
        }

        if (mType == ContainerType::TABBED && mActiveTabIndex >= 0 && mActiveTabIndex < static_cast<int>(mTabs.size()) && !mTabs[mActiveTabIndex].markedForClose) {
            float contentX, contentY, contentW, contentH;
            GetContentArea(contentX, contentY, contentW, contentH);
            if (mx >= contentX && mx < contentX + contentW &&
                my >= contentY && my < contentY + contentH) {
                SetAsActive();
                CloseMenusOnRoot(this);
                mTabs[mActiveTabIndex].view->OnInput(e);
                return;
            }
        }
    }

    void DocumentContainer::HandleMouseUp(const InputEvent& e) {
        mIsDraggingSplitter = false;

        if (mType != ContainerType::TABBED) {
            if (mLeftOrTop) mLeftOrTop->HandleMouseUp(e);
            if (mRightOrBottom) mRightOrBottom->HandleMouseUp(e);
        }

        if (sDraggingTab && sDragStarted) {
            float mx = static_cast<float>(e.mouse.x);
            float my = static_cast<float>(e.mouse.y);

            std::shared_ptr<DocumentContainer> dropTarget = FindDropTarget(mx, my);

            if (dropTarget) {
                DockingWidget* widget = dropTarget->GetDockingWidgetAtPosition(mx, my);
                if (widget) {
                    dropTarget->HandleTabDrop(widget->position);
                }
            }
        }

        EndTabDrag();

        if (sActiveContainer.get() == this && mType == ContainerType::TABBED &&
            sActiveTabIndex >= 0 && sActiveTabIndex < static_cast<int>(mTabs.size()) && !mTabs[sActiveTabIndex].markedForClose) {
            mTabs[sActiveTabIndex].view->OnInput(e);
        }
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::FindDropTarget(float x, float y) {
        DocumentContainer* root = this;
        while (auto parent = root->GetParent()) {
            root = parent.get();
        }

        return root->FindDropTargetRecursive(x, y);
    }

    std::shared_ptr<DocumentContainer> DocumentContainer::FindDropTargetRecursive(float x, float y) {
        if (!mBounds.Contains(x, y)) {
            return nullptr;
        }

        if (mType == ContainerType::TABBED) {
            float contentX, contentY, contentW, contentH;
            GetContentArea(contentX, contentY, contentW, contentH);
            if (x >= contentX && x < contentX + contentW &&
                y >= contentY && y < contentY + contentH) {
                return shared_from_this();
            }
            return nullptr;
        }

        std::shared_ptr<DocumentContainer> result = nullptr;
        if (mLeftOrTop) {
            result = mLeftOrTop->FindDropTargetRecursive(x, y);
            if (result) return result;
        }
        if (mRightOrBottom) {
            result = mRightOrBottom->FindDropTargetRecursive(x, y);
            if (result) return result;
        }

        return nullptr;
    }

    void DocumentContainer::HandleMouseMove(const InputEvent& e) {
        float mx = static_cast<float>(e.mouse.x);
        float my = static_cast<float>(e.mouse.y);

        if (sDraggingTab && !sDragStarted) {
            float dx = mx - static_cast<float>(sDragStartPos.x);
            float dy = my - static_cast<float>(sDragStartPos.y);
            float distance = std::sqrt(dx * dx + dy * dy);

            if (distance > TextEdit::Styles::TAB_BAR_DRAG_THRESHOLD) {
                sDragStarted = true;
            }
        }

        if (mIsDraggingSplitter) {
            if (mType == ContainerType::HORIZONTAL_SPLIT) {
                UpdateSplitPosition(mx - mSplitterDragOffset, false);
            }
            else if (mType == ContainerType::VERTICAL_SPLIT) {
                UpdateSplitPosition(my - mSplitterDragOffset, true);
            }
            return;
        }

        if (!sDraggingTab || !sDragStarted) {
            if (sActiveContainer && sActiveContainer->mType == ContainerType::TABBED &&
                sActiveTabIndex >= 0 && sActiveTabIndex < static_cast<int>(sActiveContainer->mTabs.size()) && !sActiveContainer->mTabs[sActiveTabIndex].markedForClose) {
                sActiveContainer->mTabs[sActiveTabIndex].view->OnInput(e);
            }
        }

        if (mType != ContainerType::TABBED) {
            if (mLeftOrTop) mLeftOrTop->HandleMouseMove(e);
            if (mRightOrBottom) mRightOrBottom->HandleMouseMove(e);
        }
    }

    void DocumentContainer::HandleMouseWheel(const InputEvent& e) {
        float mx = static_cast<float>(e.mouse.x);
        float my = static_cast<float>(e.mouse.y);

        if (mType == ContainerType::TABBED && mShowTabBar) {
            if (mx >= mBounds.x && mx <= mBounds.x + mBounds.width &&
                my >= mBounds.y && my <= mBounds.y + TextEdit::Styles::TAB_BAR_HEIGHT) {

                float scrollAmount = -e.mouse.delta / 120.0f * TextEdit::Styles::TAB_WIDTH;
                mTabScrollOffset += scrollAmount;
                ClampTabScroll();
                return;
            }
        }

        if (mType == ContainerType::TABBED && mActiveTabIndex >= 0 &&
            mActiveTabIndex < static_cast<int>(mTabs.size()) && !mTabs[mActiveTabIndex].markedForClose) {
            mTabs[mActiveTabIndex].view->OnInput(e);
        }
        else {
            if (mLeftOrTop) mLeftOrTop->OnInput(e);
            if (mRightOrBottom) mRightOrBottom->OnInput(e);
        }
    }

    void DocumentContainer::DrawTabBar(float x, float y, float w, float h) {
        mRenderer->DrawRect(x, y, w, h,
            Styles::GutterColor.r, Styles::GutterColor.g, Styles::GutterColor.b);

        mRenderer->SetFont(mSmallFont);

        const float arrowButtonWidth = 30.0f * TextEdit::Styles::DPI;
        const float arrowAreaWidth = arrowButtonWidth * 2 + 10.0f * TextEdit::Styles::DPI;

        mVisibleTabBarWidth = w - arrowAreaWidth - 10.0f * TextEdit::Styles::DPI;
        float tabAreaX = x + 5.0f * TextEdit::Styles::DPI;

        mRenderer->SetClip(tabAreaX, y, mVisibleTabBarWidth, h);

        mTabBarContentWidth = 0.0f;
        int visibleTabCount = 0;
        for (const auto& tab : mTabs) {
            if (!tab.markedForClose) {
                mTabBarContentWidth += TextEdit::Styles::TAB_WIDTH;
                visibleTabCount++;
            }
        }

        UpdateTabScrollLimits();

        float tabX = tabAreaX - mTabScrollOffset;

        for (int i = 0; i < static_cast<int>(mTabs.size()); ++i) {
            Tab& tab = mTabs[i];
            if (tab.markedForClose) continue;

            if (tabX + TextEdit::Styles::TAB_WIDTH >= tabAreaX && tabX < tabAreaX + mVisibleTabBarWidth) {
                if (i == mActiveTabIndex && IsActive()) {
                    mRenderer->DrawRect(tabX, y, TextEdit::Styles::TAB_WIDTH, h,
                        Styles::SelectionColor.r, Styles::SelectionColor.g, Styles::SelectionColor.b);
                }
                else if (i == mActiveTabIndex) {
                    mRenderer->DrawRect(tabX, y, TextEdit::Styles::TAB_WIDTH, h,
                        Styles::ScrollbarNibColor.r, Styles::ScrollbarNibColor.g, Styles::ScrollbarNibColor.b);
                }

                float textX = tabX + 5.0f * Styles::DPI;
                float textY = y + (h - mSmallFont->GetLineHeight()) / 2.0f;
                float maxTextWidth = TextEdit::Styles::TAB_WIDTH - 45.0f * Styles::DPI;

                mRenderer->SetClip(std::max(tabAreaX, tabX), y,
                    std::min(maxTextWidth + 10.0f * Styles::DPI, tabAreaX + mVisibleTabBarWidth - tabX), h);
                mRenderer->DrawText(tab.GetTitle(), textX, textY,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                mRenderer->SetClip(tabAreaX, y, mVisibleTabBarWidth, h);

                if (tab.document->IsDirty()) {
                    mRenderer->DrawText(U"*", textX + maxTextWidth + 10.0f * Styles::DPI, textY,
                        Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                }

                mRenderer->SetFont(mSmallFont);
                float closeSize = 14.0f * Styles::DPI;
                float closeX = tabX + TextEdit::Styles::TAB_WIDTH - closeSize - 5.0f * Styles::DPI;
                float closeY = y + (h - closeSize) / 2.0f + 1.0f * Styles::DPI;
                tab.closeButtonRect = { closeX, closeY, closeSize, closeSize };

                bool hoverClose = tab.closeButtonRect.Contains(static_cast<float>(sDragCurrentPos.x),
                    static_cast<float>(sDragCurrentPos.y));
                if (hoverClose) {
                    mRenderer->DrawRect(closeX - 2 * Styles::DPI, closeY - 2 * Styles::DPI,
                        closeSize + 4 * Styles::DPI, closeSize + 4 * Styles::DPI,
                        Styles::ScrollbarNibHoverColor.r, Styles::ScrollbarNibHoverColor.g,
                        Styles::ScrollbarNibHoverColor.b);
                }

                mRenderer->SetLayoutScale(0.6f);
                mRenderer->DrawText(U"❌", closeX, closeY,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                mRenderer->SetLayoutScale(1.0f);
            }

            if (i < static_cast<int>(mTabs.size()) - 1 &&
                tabX + TextEdit::Styles::TAB_WIDTH >= tabAreaX &&
                tabX + TextEdit::Styles::TAB_WIDTH < tabAreaX + mVisibleTabBarWidth) {
                mRenderer->DrawRect(tabX + TextEdit::Styles::TAB_WIDTH, y + 5 * Styles::DPI,
                    1 * Styles::DPI, h - 10 * Styles::DPI,
                    Styles::ScrollbarTrackColor.r, Styles::ScrollbarTrackColor.g,
                    Styles::ScrollbarTrackColor.b);
            }

            tabX += TextEdit::Styles::TAB_WIDTH;
        }

        mRenderer->ClearClip();

        if (mTabBarContentWidth > mVisibleTabBarWidth) {
            float arrowY = y + (h - mSmallFont->GetLineHeight()) / 2.0f;
            float arrowAreaX = x + w - arrowAreaWidth;

            mLeftArrowRect = { arrowAreaX, y, arrowButtonWidth, h };
            bool canScrollLeft = mTabScrollOffset > 0;
            mIsHoveringLeftArrow = mLeftArrowRect.Contains(static_cast<float>(sDragCurrentPos.x),
                static_cast<float>(sDragCurrentPos.y));

            if (canScrollLeft) {
                auto& leftArrowColor = mIsHoveringLeftArrow ? Styles::ScrollbarNibHoverColor : Styles::ScrollbarNibColor;
                mRenderer->DrawRect(mLeftArrowRect.x, mLeftArrowRect.y, mLeftArrowRect.width, mLeftArrowRect.height,
                    leftArrowColor.r, leftArrowColor.g, leftArrowColor.b);
            }

            mRenderer->DrawText(U"⬅️", mLeftArrowRect.x + 5.0f * Styles::DPI, arrowY,
                canScrollLeft ? Styles::TextColor.r : Styles::TokenTypeComment.r,
                canScrollLeft ? Styles::TextColor.g : Styles::TokenTypeComment.g,
                canScrollLeft ? Styles::TextColor.b : Styles::TokenTypeComment.b);

            mRightArrowRect = { arrowAreaX + arrowButtonWidth + 5.0f * Styles::DPI, y, arrowButtonWidth, h };
            bool canScrollRight = mTabScrollOffset < mMaxTabScrollOffset;
            mIsHoveringRightArrow = mRightArrowRect.Contains(static_cast<float>(sDragCurrentPos.x),
                static_cast<float>(sDragCurrentPos.y));

            if (canScrollRight) {
                auto& rightArrowColor = mIsHoveringRightArrow ? Styles::ScrollbarNibHoverColor : Styles::ScrollbarNibColor;
                mRenderer->DrawRect(mRightArrowRect.x, mRightArrowRect.y, mRightArrowRect.width, mRightArrowRect.height,
                    rightArrowColor.r, rightArrowColor.g, rightArrowColor.b);
            }

            mRenderer->DrawText(U"➡️", mRightArrowRect.x + 5.0f * Styles::DPI, arrowY,
                canScrollRight ? Styles::TextColor.r : Styles::TokenTypeComment.r,
                canScrollRight ? Styles::TextColor.g : Styles::TokenTypeComment.g,
                canScrollRight ? Styles::TextColor.b : Styles::TokenTypeComment.b);
        }
        else {
            mLeftArrowRect = {};
            mRightArrowRect = {};
        }
    }

    std::shared_ptr<DocumentView> DocumentContainer::GetActiveDocumentView() {
        if (sActiveContainer && sActiveContainer->mType == ContainerType::TABBED &&
            sActiveTabIndex >= 0 && sActiveTabIndex < static_cast<int>(sActiveContainer->mTabs.size()) && !sActiveContainer->mTabs[sActiveTabIndex].markedForClose) {
            return sActiveContainer->mTabs[sActiveTabIndex].view;
        }

        return nullptr;
    }

    void DocumentContainer::CloseActiveDocumentView() {
        if (!IsRoot()) {
            if (auto parent = GetParent()) {
                DocumentContainer* root = parent.get();
                while (auto rootParent = root->GetParent()) {
                    root = rootParent.get();
                }
                root->CloseActiveDocumentView();
            }
            return;
        }

        if (sActiveContainer && sActiveContainer->mType == ContainerType::TABBED &&
            sActiveTabIndex >= 0 && sActiveTabIndex < static_cast<int>(sActiveContainer->mTabs.size())) {

            sActiveContainer->mTabs[sActiveTabIndex].markedForClose = true;

            sActiveContainer->ProcessMarkedForClose();

            RemoveEmptyContainers();
        }
    }

    void DocumentContainer::SaveAll() {
        if (!IsRoot()) {
            if (auto parent = GetParent()) {
                DocumentContainer* root = parent.get();
                while (auto rootParent = root->GetParent()) {
                    root = rootParent.get();
                }
                root->SaveAll();
            }
            return;
        }

        SaveAllRecursive();
    }

    void DocumentContainer::SaveAllRecursive() {
        if (mType == ContainerType::TABBED) {
            for (auto& tab : mTabs) {
                if (!tab.markedForClose && tab.document) {
                    if (tab.document->IsDirty()) {
                        tab.document->Save();
                    }
                }
            }
        }
        else {
            if (mLeftOrTop) {
                mLeftOrTop->SaveAllRecursive();
            }
            if (mRightOrBottom) {
                mRightOrBottom->SaveAllRecursive();
            }
        }
    }

    void DocumentContainer::CloseAll() {
        if (!IsRoot()) {
            if (auto parent = GetParent()) {
                DocumentContainer* root = parent.get();
                while (auto rootParent = root->GetParent()) {
                    root = rootParent.get();
                }
                root->CloseAll();
            }
            return;
        }

        CloseAllRecursive();

        ProcessMarkedForCloseRecursive();

        RemoveEmptyContainers();

        if (IsEmpty()) {
            mType = ContainerType::TABBED;
            mTabs.clear();
            mActiveTabIndex = -1;
            mLeftOrTop = nullptr;
            mRightOrBottom = nullptr;
            sActiveContainer = nullptr;
            sActiveTabIndex = -1;
        }
    }

    void DocumentContainer::CloseAllRecursive() {
        if (mType == ContainerType::TABBED) {
            for (auto& tab : mTabs) {
                if (!tab.markedForClose) {
                    tab.markedForClose = true;
                }
            }
        }
        else {
            if (mLeftOrTop) {
                mLeftOrTop->CloseAllRecursive();
            }
            if (mRightOrBottom) {
                mRightOrBottom->CloseAllRecursive();
            }
        }
    }

    void DocumentContainer::ProcessMarkedForCloseRecursive() {
        if (mType == ContainerType::TABBED) {
            ProcessMarkedForClose();
        }
        else {
            if (mLeftOrTop) {
                mLeftOrTop->ProcessMarkedForCloseRecursive();
            }
            if (mRightOrBottom) {
                mRightOrBottom->ProcessMarkedForCloseRecursive();
            }
        }
    }

    void DocumentContainer::DrawDockingWidgets(float x, float y, float w, float h) {
        float mx = static_cast<float>(sDragCurrentPos.x);
        float my = static_cast<float>(sDragCurrentPos.y);

        if (mx >= x && mx < x + w && my >= y && my < y + h) {
            for (auto& widget : mDockingWidgets) {
                widget.isHovered = widget.rect.Contains(mx, my);
            }
        }


        float widgetAreaSize = 200.0f * Styles::DPI;
        float centerX = x + w / 2.0f;
        float centerY = y + h / 2.0f;
        float areaX = centerX - widgetAreaSize / 2.0f;
        float areaY = centerY - widgetAreaSize / 2.0f;

        mRenderer->DrawRect(areaX, areaY, widgetAreaSize, widgetAreaSize, 0.1f, 0.1f, 0.1f);

        for (const auto& widget : mDockingWidgets) {
            if (widget.isHovered) {
                mRenderer->DrawRect(widget.rect.x - 2 * Styles::DPI, widget.rect.y - 2 * Styles::DPI,
                    widget.rect.width + 4 * Styles::DPI, widget.rect.height + 4 * Styles::DPI,
                    0.2f, 0.4f, 0.8f);
            }

            auto& bgColor = widget.isHovered ? Styles::SelectionColor : Styles::ScrollbarNibColor;

            mRenderer->DrawRect(widget.rect.x, widget.rect.y, widget.rect.width, widget.rect.height,
                bgColor.r, bgColor.g, bgColor.b);

            float iconCenterX = widget.rect.x + widget.rect.width / 2.0f;
            float iconCenterY = widget.rect.y + widget.rect.height / 2.0f;
            float iconSize = 20.0f * Styles::DPI;

#if 1
            switch (widget.position) {
            case DockingWidget::CENTER:
                mRenderer->DrawRect(iconCenterX - iconSize / 2, iconCenterY - iconSize / 2, iconSize, iconSize,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                break;
            case DockingWidget::LEFT:
                mRenderer->DrawRect(widget.rect.x + 5, iconCenterY - iconSize / 2, 4, iconSize,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                break;
            case DockingWidget::RIGHT:
                mRenderer->DrawRect(widget.rect.x + widget.rect.width - 9, iconCenterY - iconSize / 2, 4, iconSize,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                break;
            case DockingWidget::TOP:
                mRenderer->DrawRect(iconCenterX - iconSize / 2, widget.rect.y + 5, iconSize, 4,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                break;
            case DockingWidget::BOTTOM:
                mRenderer->DrawRect(iconCenterX - iconSize / 2, widget.rect.y + widget.rect.height - 9, iconSize, 4,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
                break;
            }
#endif
        }
    }

    void DocumentContainer::UpdateDockingWidgets(float x, float y, float w, float h) {
        mDockingWidgets.clear();

        float widgetSize = 40.0f * Styles::DPI;
        float centerX = x + w / 2.0f;
        float centerY = y + h / 2.0f;
        float spacing = widgetSize + 10.0f * Styles::DPI;

        mDockingWidgets.push_back({ {centerX - widgetSize / 2, centerY - widgetSize / 2, widgetSize, widgetSize}, DockingWidget::CENTER, false });
        mDockingWidgets.push_back({ {centerX - widgetSize / 2, centerY - spacing - widgetSize / 2, widgetSize, widgetSize}, DockingWidget::TOP, false });
        mDockingWidgets.push_back({ {centerX - widgetSize / 2, centerY + spacing - widgetSize / 2, widgetSize, widgetSize}, DockingWidget::BOTTOM, false });
        mDockingWidgets.push_back({ {centerX - spacing - widgetSize / 2, centerY - widgetSize / 2, widgetSize, widgetSize}, DockingWidget::LEFT, false });
        mDockingWidgets.push_back({ {centerX + spacing - widgetSize / 2, centerY - widgetSize / 2, widgetSize, widgetSize}, DockingWidget::RIGHT, false });
    }

    int DocumentContainer::GetTabAtPosition(float x, float y) {
        if (y < mBounds.y || y > mBounds.y + TextEdit::Styles::TAB_BAR_HEIGHT) {
            return -1;
        }

        float tabAreaX = mBounds.x + 5.0f * Styles::DPI;
        float relativeX = x - tabAreaX + mTabScrollOffset;
        float tabX = 0.0f;

        for (int i = 0; i < static_cast<int>(mTabs.size()); ++i) {
            if (mTabs[i].markedForClose) continue;
            if (relativeX >= tabX && relativeX < tabX + TextEdit::Styles::TAB_WIDTH) {
                return i;
            }
            tabX += TextEdit::Styles::TAB_WIDTH;
        }

        return -1;
    }

    int DocumentContainer::GetCloseButtonAtPosition(float x, float y) {
        for (int i = 0; i < static_cast<int>(mTabs.size()); ++i) {
            if (mTabs[i].markedForClose) continue;
            if (mTabs[i].closeButtonRect.Contains(x, y)) {
                return i;
            }
        }
        return -1;
    }

    DocumentContainer::DockingWidget* DocumentContainer::GetDockingWidgetAtPosition(float x, float y) {
        for (auto& widget : mDockingWidgets) {
            if (widget.rect.Contains(x, y)) {
                return &widget;
            }
        }
        return nullptr;
    }

    void DocumentContainer::HandleTabDrop(DockingWidget::Position position) {
        if (!sDraggedTab || !sDragSource) return;

        if (position == DockingWidget::CENTER && sDragSource == this) {
            bool isSameTab = false;
            for (int i = 0; i < static_cast<int>(mTabs.size()); ++i) {
                if (&mTabs[i] == sDraggedTab && !mTabs[i].markedForClose) {
                    isSameTab = true;
                    break;
                }
            }
            if (isSameTab) {
                return;
            }
        }

        Tab tabCopy = *sDraggedTab;
        tabCopy.markedForClose = false;
        std::shared_ptr<Document> draggedDoc = sDraggedTab->document;
        DocumentContainer* sourceContainer = sDragSource;

        sourceContainer->MoveTabOut(draggedDoc);

        switch (position) {
        case DockingWidget::CENTER:
            if (mType == ContainerType::TABBED) {
                mTabs.push_back(tabCopy);
                mActiveTabIndex = static_cast<int>(mTabs.size()) - 1;
                SetAsActive();
                ScrollTabsToShowTab(mActiveTabIndex);
            }
            break;

        case DockingWidget::LEFT:
        {
            auto newContainer = std::make_shared<DocumentContainer>(mRenderer, mFont, mSmallFont, ContainerType::TABBED);
            newContainer->mTabs.push_back(tabCopy);
            newContainer->mActiveTabIndex = 0;
            SplitHorizontal(newContainer, false);
            newContainer->SetAsActive();
            break;
        }

        case DockingWidget::RIGHT:
        {
            auto newContainer = std::make_shared<DocumentContainer>(mRenderer, mFont, mSmallFont, ContainerType::TABBED);
            newContainer->mTabs.push_back(tabCopy);
            newContainer->mActiveTabIndex = 0;
            SplitHorizontal(newContainer, true);
            newContainer->SetAsActive();
            break;
        }

        case DockingWidget::TOP:
        {
            auto newContainer = std::make_shared<DocumentContainer>(mRenderer, mFont, mSmallFont, ContainerType::TABBED);
            newContainer->mTabs.push_back(tabCopy);
            newContainer->mActiveTabIndex = 0;
            SplitVertical(newContainer, false);
            newContainer->SetAsActive();
            break;
        }

        case DockingWidget::BOTTOM:
        {
            auto newContainer = std::make_shared<DocumentContainer>(mRenderer, mFont, mSmallFont, ContainerType::TABBED);
            newContainer->mTabs.push_back(tabCopy);
            newContainer->mActiveTabIndex = 0;
            SplitVertical(newContainer, true);
            newContainer->SetAsActive();
            break;
        }
        }

        if (!sourceContainer->IsRoot()) {
            if (auto parent = sourceContainer->GetParent()) {
                parent->RemoveEmptyContainers();
            }
        }
        else {
            sourceContainer->RemoveEmptyContainers();
        }

        if (!sActiveContainer || sActiveTabIndex < 0) {
            std::shared_ptr<DocumentContainer> firstTabbed = sourceContainer->IsRoot() ?
                sourceContainer->FindFirstTabbedContainer() :
                FindFirstTabbedContainer();
            if (firstTabbed != nullptr) {
                firstTabbed->SetAsActive();
            }
        }
    }

    void DocumentContainer::UpdateSplitPosition(float mousePos, bool isVertical) {
        if (isVertical) {
            float relativePos = (mousePos - mBounds.y) / mBounds.height;
            mSplitPosition = std::max(0.1f, std::min(0.9f, relativePos));
        }
        else {
            float relativePos = (mousePos - mBounds.x) / mBounds.width;
            mSplitPosition = std::max(0.1f, std::min(0.9f, relativePos));
        }
    }

    bool DocumentContainer::IsOnSplitter(float x, float y) const {
        const float splitterThickness = 4.0f;

        if (mType == ContainerType::HORIZONTAL_SPLIT) {
            float splitX = mBounds.x + mBounds.width * mSplitPosition;
            return x >= splitX - splitterThickness / 2 && x <= splitX + splitterThickness / 2 &&
                y >= mBounds.y && y <= mBounds.y + mBounds.height;
        }
        else if (mType == ContainerType::VERTICAL_SPLIT) {
            float splitY = mBounds.y + mBounds.height * mSplitPosition;
            return x >= mBounds.x && x <= mBounds.x + mBounds.width &&
                y >= splitY - splitterThickness / 2 && y <= splitY + splitterThickness / 2;
        }

        return false;
    }

    void DocumentContainer::ReplaceWith(std::shared_ptr<DocumentContainer> newContainer) {
        if (sActiveContainer.get() == this) {
            sActiveContainer = newContainer;
        }

        mType = newContainer->mType;
        mTabs = std::move(newContainer->mTabs);
        mActiveTabIndex = newContainer->mActiveTabIndex;
        mLeftOrTop = std::move(newContainer->mLeftOrTop);
        mRightOrBottom = std::move(newContainer->mRightOrBottom);
        mSplitPosition = newContainer->mSplitPosition;

        if (mLeftOrTop) mLeftOrTop->SetParent(weak_from_this());
        if (mRightOrBottom) mRightOrBottom->SetParent(weak_from_this());
    }

    std::vector<std::shared_ptr<Document>> DocumentContainer::GetAllOpenDocuments() {
        std::vector<std::shared_ptr<Document>> documents;

        DocumentContainer* root = this;
        while (auto parent = root->GetParent()) {
            root = parent.get();
        }

        root->CollectAllOpenDocumentsRecursive(documents);

        return documents;
    }

    void DocumentContainer::CollectAllOpenDocumentsRecursive(std::vector<std::shared_ptr<Document>>& documents) {
        if (mType == ContainerType::TABBED) {
            for (const auto& tab : mTabs) {
                if (!tab.markedForClose && tab.document) {
                    documents.push_back(tab.document);
                }
            }
        }
        else {
            if (mLeftOrTop) {
                mLeftOrTop->CollectAllOpenDocumentsRecursive(documents);
            }
            if (mRightOrBottom) {
                mRightOrBottom->CollectAllOpenDocumentsRecursive(documents);
            }
        }
    }

    void DocumentContainer::MoveTabOut(std::shared_ptr<Document> doc) {
        if (mType != ContainerType::TABBED) {
            if (mLeftOrTop) mLeftOrTop->MoveTabOut(doc);
            if (mRightOrBottom) mRightOrBottom->MoveTabOut(doc);
            return;
        }

        auto it = std::find_if(mTabs.begin(), mTabs.end(),
            [&doc](const Tab& tab) { return tab.document == doc && !tab.markedForClose; });

        if (it != mTabs.end()) {
            int removedIndex = static_cast<int>(it - mTabs.begin());
            mTabs.erase(it);

            if (mActiveTabIndex >= removedIndex) {
                mActiveTabIndex--;
                if (mActiveTabIndex < 0 && !mTabs.empty()) {
                    mActiveTabIndex = 0;
                }
            }

            if (this == sActiveContainer.get()) {
                if (mTabs.empty() || mActiveTabIndex < 0) {
                    sActiveContainer = nullptr;
                    sActiveTabIndex = -1;
                }
                else {
                    sActiveTabIndex = mActiveTabIndex;
                }
            }
        }
    }

    void DocumentContainer::UpdateTabScrollLimits() {
        mMaxTabScrollOffset = std::max(0.0f, mTabBarContentWidth - mVisibleTabBarWidth);
        ClampTabScroll();
    }

    void DocumentContainer::ClampTabScroll() {
        mTabScrollOffset = std::max(0.0f, std::min(mTabScrollOffset, mMaxTabScrollOffset));
    }

    void DocumentContainer::ScrollTabsLeft() {
        mTabScrollOffset -= TextEdit::Styles::TAB_WIDTH;
        ClampTabScroll();
    }

    void DocumentContainer::ScrollTabsRight() {
        mTabScrollOffset += TextEdit::Styles::TAB_WIDTH;
        ClampTabScroll();
    }

    void DocumentContainer::ScrollTabsToShowTab(int tabIndex) {
        if (tabIndex < 0 || tabIndex >= static_cast<int>(mTabs.size())) return;

        float tabPos = 0.0f;
        int visibleIndex = 0;
        for (int i = 0; i < tabIndex; ++i) {
            if (!mTabs[i].markedForClose) {
                tabPos += TextEdit::Styles::TAB_WIDTH;
                visibleIndex++;
            }
        }

        if (tabPos < mTabScrollOffset) {
            mTabScrollOffset = tabPos;
        }
        else if (tabPos + TextEdit::Styles::TAB_WIDTH > mTabScrollOffset + mVisibleTabBarWidth) {
            mTabScrollOffset = tabPos + TextEdit::Styles::TAB_WIDTH - mVisibleTabBarWidth;
        }

        ClampTabScroll();
    }
}