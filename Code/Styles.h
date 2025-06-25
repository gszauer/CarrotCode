#pragma once

#include <vector>
#include "Document.h"

namespace TextEdit {
	class Styles {
	public:
		struct Color {
			float r, g, b;
		};

		static Color GlobalTint;
		static float DisabledTint;

		static Color BGColor;
		static Color TextColor;
		static Color SelectionColor;
		static Color GutterColor; // Like line numbers
		static Color CursorColor;
		static Color ScrollbarTrackColor;
		static Color ScrollbarNibColor;
		static Color ScrollbarNibHoverColor;
		static Color HighlighterButtonColor;

		static Color TokenTypeNormal;
		static Color TokenTypeKeyword;
		static Color TokenTypeString;
		static Color TokenTypeNumber;
		static Color TokenTypeComment;
		static Color TokenTypeFunction;
		static Color TokenTypeOperator;
		static Color TokenTypeIdentifier;
		static Color TokenTypeGrouping;
		static Color TokenTypePreprocessor;
		static Color TokenTypeType;
		static Color TokenTypeConstant;
		static Color TokenTypeRegex;
		static Color TokenTypeTemplate;
		static Color TokenTypeDecorator;
		static Color TokenTypeLabel;
		static Color TokenTypeAttribute;

		static Color FileMenuBackgroundColor;
		static Color FileMenuTextColor;
		static Color FileMenuHighlightColor;
		static Color FileMenuDropdownBackgroundColor;

		// Title bar colors
		static Color TitleBarTextColor;
		static Color WindowButtonColor;
		static Color WindowButtonIconColor;
		static Color WindowButtonHoverColor;
		static Color WindowButtonCloseHoverColor;

		static float CURSOR_BLINK_RATE; // seconds
		static float AUTOSCROLL_SPEED_LINES_PER_SEC;

		static float SCROLLBAR_SIZE;
		static float AUTOSCROLL_MARGIN;
		static float REGULAR_FONT_SIZE;
		static float MEDIUM_FONT_SIZE;
		static float SMALL_FONT_SIZE;
		static float GUTTER_RIGHT_PAD;
		static float HIGHLIGHTER_BUTTON_WIDTH;
		static float CONTEXT_MENU_WIDTH;
		static float FILE_MENU_HEIGHT;
		static float FILE_MENU_PADDING;
		static float FILE_MENU_DROPDOWN_HEIGHT;
		static float FILE_MENU_DROPDOWN_WIDTH;
		static float FILE_MENU_DIVIDER_HEIGHT;

		static float TAB_BAR_HEIGHT;
		static float TAB_BAR_DRAG_THRESHOLD;
		static float TAB_WIDTH;

		// Title bar constants
		static float WINDOW_BUTTON_WIDTH;

		static std::vector<SyntaxRule> syntax_rules;
		static std::unordered_map<TextEdit::TokenType, TextEdit::Styles::Color> style_map;

		static float DPI;
		static void ApplyDPI(float dpi);
	};
}