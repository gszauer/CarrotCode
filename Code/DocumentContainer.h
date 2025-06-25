#pragma once

#include <memory>
#include <vector>
#include <string>
#include "Document.h"
#include "DocumentView.h"
#include "Renderer.h"
#include "Font.h"
#include "application.h"

namespace TextEdit {
    class DocumentContainer : public std::enable_shared_from_this<DocumentContainer> {
    public:
        enum class ContainerType {
            TABBED,
            HORIZONTAL_SPLIT,
            VERTICAL_SPLIT
        };

        struct Tab {
            std::shared_ptr<Document> document;
            std::shared_ptr<DocumentView> view;
            Rect closeButtonRect;
            bool markedForClose;
            bool isBeingMoved;

            inline const std::u32string& GetTitle() {
                return document->GetName();
            }
        };

        struct DockingWidget {
            Rect rect;
            enum Position {
                CENTER,
                TOP,
                BOTTOM,
                LEFT,
                RIGHT
            } position;
            bool isHovered;
        };

    private:
        ContainerType mType;

        std::vector<Tab> mTabs;
        int mActiveTabIndex;
        bool mShowTabBar;

        std::shared_ptr<DocumentContainer> mLeftOrTop;
        std::shared_ptr<DocumentContainer> mRightOrBottom;
        float mSplitPosition;

        std::weak_ptr<DocumentContainer> mParent;
        std::shared_ptr<Renderer> mRenderer;
        std::shared_ptr<Font> mFont;
        std::shared_ptr<Font> mSmallFont;
        Rect mBounds;

        static std::shared_ptr<DocumentContainer> sActiveContainer;
        static int sActiveTabIndex;

        static bool sDraggingTab;
        static Tab* sDraggedTab;
        static DocumentContainer* sDragSource;
        static MousePos sDragStartPos;
        static MousePos sDragCurrentPos;
        static bool sDragStarted;

        std::vector<DockingWidget> mDockingWidgets;

        bool mIsDraggingSplitter;
        float mSplitterDragOffset;

        // Tab scrolling members
        float mTabScrollOffset;
        float mMaxTabScrollOffset;
        Rect mLeftArrowRect;
        Rect mRightArrowRect;
        bool mIsHoveringLeftArrow;
        bool mIsHoveringRightArrow;
        float mTabBarContentWidth;
        float mVisibleTabBarWidth;

    public:
        DocumentContainer(std::shared_ptr<Renderer> renderer,
            std::shared_ptr<Font> font,
            std::shared_ptr<Font> smallFont,
            ContainerType type = ContainerType::TABBED);
        virtual ~DocumentContainer();

        void AddDocument(std::shared_ptr<Document> doc);
        void RemoveDocument(std::shared_ptr<Document> doc, bool isMoving);
        void CloseTab(int index);
        void SetActiveTab(int index);

        void SetParent(std::weak_ptr<DocumentContainer> parent);
        std::shared_ptr<DocumentContainer> GetParent() const;
        bool IsEmpty() const;
        bool IsRoot() const;

        void Display(float x, float y, float w, float h);
        void Update(float deltaTime);
        void OnInput(const InputEvent& e);
        void CloseMenus();

        std::shared_ptr<DocumentContainer> FindLargestTabbedContainer();
        std::shared_ptr<DocumentContainer> FindDropTarget(float x, float y);
        std::shared_ptr<DocumentContainer> FindFirstTabbedContainer();
        void SetAsActive();
        bool IsActive() const;

        void SplitHorizontal(std::shared_ptr<DocumentContainer> newContainer, bool putNewOnRight);
        void SplitVertical(std::shared_ptr<DocumentContainer> newContainer, bool putNewOnBottom);

        void RemoveEmptyContainers();
        void ProcessMarkedForClose();

        static bool IsTabDragging();

        ContainerType GetType() const;
        const std::vector<Tab>& GetTabs() const;
        float GetContentArea(float& outX, float& outY, float& outW, float& outH) const;

        std::shared_ptr<DocumentView> GetActiveDocumentView();
        void CloseActiveDocumentView();
        void CloseAll();
        void SaveAll();
        std::vector<std::shared_ptr<Document>> GetAllOpenDocuments();
    private:
        static void StartTabDrag(Tab* tab, DocumentContainer* source, int mouseX, int mouseY);
        static void EndTabDrag();
        static Tab* GetDraggedTab();
        std::shared_ptr<DocumentContainer> FindDropTargetRecursive(float x, float y);

        void HandleMouseDown(const InputEvent& e);
        void HandleMouseUp(const InputEvent& e);
        void HandleMouseMove(const InputEvent& e);
        void HandleMouseWheel(const InputEvent& e);

        void DrawTabBar(float x, float y, float w, float h);
        void DrawDockingWidgets(float x, float y, float w, float h);
        void UpdateDockingWidgets(float x, float y, float w, float h);

        int GetTabAtPosition(float x, float y);
        int GetCloseButtonAtPosition(float x, float y);
        DockingWidget* GetDockingWidgetAtPosition(float x, float y);

        void HandleTabDrop(DockingWidget::Position position);

        void UpdateSplitPosition(float mousePos, bool isVertical);
        bool IsOnSplitter(float x, float y) const;

        void ReplaceWith(std::shared_ptr<DocumentContainer> newContainer);

        void CloseMenusOnRoot(DocumentContainer* keepOpen = nullptr);

        std::shared_ptr<DocumentContainer> FindFirstTabbedContainerWithTabs();
        std::shared_ptr<DocumentContainer> FindFirstTabbedContainerWithTabsRecursive();
        void CloseAllRecursive();
        void ProcessMarkedForCloseRecursive();
        void SaveAllRecursive();
        void CollectAllOpenDocumentsRecursive(std::vector<std::shared_ptr<Document>>& documents);
        void MoveTabOut(std::shared_ptr<Document> doc);

        // New helper methods for tab scrolling
        void UpdateTabScrollLimits();
        void ScrollTabsLeft();
        void ScrollTabsRight();
        void ScrollTabsToShowTab(int tabIndex);
        void ClampTabScroll();
    };
}