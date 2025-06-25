#include "Styles.h"

namespace TextEdit {
    Styles::Color TextEdit::Styles::GlobalTint = { 1.0f, 1.0f, 1.0f };
    float TextEdit::Styles::DisabledTint = 0.6f;



    // Core UI Colors
    Styles::Color TextEdit::Styles::BGColor = { 0.1f, 0.1f, 0.12f }; // Darker gray-blue background for depth
    Styles::Color TextEdit::Styles::TextColor = { 0.92f, 0.92f, 0.95f }; // Slightly cooler light gray for text
    Styles::Color TextEdit::Styles::SelectionColor = { 0.75f, 0.25f, 0.55f }; // Pinkish-purple for selections
    Styles::Color TextEdit::Styles::GutterColor = { 0.15f, 0.15f, 0.18f }; // Slightly lighter gutter for contrast
    Styles::Color TextEdit::Styles::CursorColor = { 0.95f, 0.45f, 0.75f }; // Bright pink cursor for visibility
    Styles::Color TextEdit::Styles::ScrollbarTrackColor = { 0.18f, 0.18f, 0.2f }; // Subtle dark track
    Styles::Color TextEdit::Styles::ScrollbarNibColor = { 0.35f, 0.35f, 0.38f }; // Medium gray nib
    Styles::Color TextEdit::Styles::ScrollbarNibHoverColor = { 0.85f, 0.45f, 0.65f }; // Pink on hover
    Styles::Color TextEdit::Styles::HighlighterButtonColor = { 0.25f, 0.25f, 0.28f }; // Darker button with subtle tone

    // Syntax Highlighting for C++/JavaScript
    Styles::Color TextEdit::Styles::TokenTypeNormal = { 0.92f, 0.92f, 0.95f }; // Cool light gray for normal text
    Styles::Color TextEdit::Styles::TokenTypeKeyword = { 0.45f, 0.65f, 0.95f }; // Soft blue for keywords
    Styles::Color TextEdit::Styles::TokenTypeString = { 0.55f, 0.85f, 0.55f }; // Bright green for strings
    Styles::Color TextEdit::Styles::TokenTypeNumber = { 0.95f, 0.55f, 0.35f }; // Orange for numbers
    Styles::Color TextEdit::Styles::TokenTypeComment = { 0.5f, 0.5f, 0.55f }; // Cool gray for comments
    Styles::Color TextEdit::Styles::TokenTypeFunction = { 0.35f, 0.75f, 0.85f }; // Cyan for functions
    Styles::Color TextEdit::Styles::TokenTypeOperator = { 0.95f, 0.45f, 0.45f }; // Red for operators
    Styles::Color TextEdit::Styles::TokenTypeIdentifier = { 0.92f, 0.92f, 0.95f }; // Matches normal text
    Styles::Color TextEdit::Styles::TokenTypeGrouping = { 0.95f, 0.85f, 0.35f }; // Yellow for brackets
    Styles::Color TextEdit::Styles::TokenTypePreprocessor = { 0.65f, 0.55f, 0.95f }; // Soft purple for preprocessor
    Styles::Color TextEdit::Styles::TokenTypeType = { 0.45f, 0.85f, 0.75f }; // Teal for types
    Styles::Color TextEdit::Styles::TokenTypeConstant = { 0.75f, 0.65f, 0.95f }; // Light purple for constants
    Styles::Color TextEdit::Styles::TokenTypeRegex = { 0.95f, 0.45f, 0.75f }; // Vibrant pink for regex
    Styles::Color TextEdit::Styles::TokenTypeTemplate = { 0.65f, 0.85f, 0.55f }; // Slightly brighter green for templates
    Styles::Color TextEdit::Styles::TokenTypeDecorator = { 0.95f, 0.75f, 0.35f }; // Gold for decorators
    Styles::Color TextEdit::Styles::TokenTypeLabel = { 0.85f, 0.75f, 0.65f }; // Tan for labels
    Styles::Color TextEdit::Styles::TokenTypeAttribute = { 0.55f, 0.75f, 0.95f }; // Light blue for attributes

    // File Menu Colors
    Styles::Color TextEdit::Styles::FileMenuBackgroundColor = { 0.12f, 0.12f, 0.14f }; // Darker menu background
    Styles::Color TextEdit::Styles::FileMenuTextColor = { 0.92f, 0.92f, 0.95f }; // Matches text color
    Styles::Color TextEdit::Styles::FileMenuHighlightColor = { 0.85f, 0.45f, 0.65f }; // Pink highlight for menu items
    Styles::Color TextEdit::Styles::FileMenuDropdownBackgroundColor = { 0.18f, 0.18f, 0.2f }; // Slightly lighter dropdown

    // Title bar colors
    Styles::Color TextEdit::Styles::TitleBarTextColor = { 0.92f, 0.92f, 0.95f }; // Same as text color
    Styles::Color TextEdit::Styles::WindowButtonColor = { 0.12f, 0.12f, 0.14f }; // Same as file menu background
    Styles::Color TextEdit::Styles::WindowButtonIconColor = { 0.92f, 0.92f, 0.95f }; // Same as text color
    Styles::Color TextEdit::Styles::WindowButtonHoverColor = { 0.25f, 0.25f, 0.28f }; // Lighter on hover
    Styles::Color TextEdit::Styles::WindowButtonCloseHoverColor = { 0.85f, 0.25f, 0.25f }; // Red on hover for close


    float TextEdit::Styles::SCROLLBAR_SIZE = 20.0f;
    float TextEdit::Styles::CURSOR_BLINK_RATE = 0.53f; // seconds
    float TextEdit::Styles::AUTOSCROLL_MARGIN = 30.0f;
    float TextEdit::Styles::AUTOSCROLL_SPEED_LINES_PER_SEC = 10.0f;

    float TextEdit::Styles::REGULAR_FONT_SIZE = 26;
    float TextEdit::Styles::MEDIUM_FONT_SIZE = 18;
    float TextEdit::Styles::GUTTER_RIGHT_PAD = 4;
    float TextEdit::Styles::SMALL_FONT_SIZE = 18;
    float TextEdit::Styles::HIGHLIGHTER_BUTTON_WIDTH = 53;
    float TextEdit::Styles::CONTEXT_MENU_WIDTH = 80;
    float TextEdit::Styles::FILE_MENU_HEIGHT = 28;
    float TextEdit::Styles::FILE_MENU_PADDING = 10;
    float TextEdit::Styles::FILE_MENU_DROPDOWN_HEIGHT = 24;
    float TextEdit::Styles::FILE_MENU_DROPDOWN_WIDTH = 75;
    float TextEdit::Styles::FILE_MENU_DIVIDER_HEIGHT = 7.0f; // Height of divider items
    float TextEdit::Styles::DPI = 1.0f;

    float TextEdit::Styles::TAB_BAR_HEIGHT = 30.0f;
    float TextEdit::Styles::TAB_BAR_DRAG_THRESHOLD = 5.0f;
    float TextEdit::Styles::TAB_WIDTH = 150.0f;

    // Title bar constants
    float TextEdit::Styles::WINDOW_BUTTON_WIDTH = 46.0f;

    std::unordered_map<TextEdit::TokenType, TextEdit::Styles::Color> TextEdit::Styles::style_map = {
        {TextEdit::TokenType::Normal, TextEdit::Styles::TokenTypeNormal},      // White
        {TextEdit::TokenType::Keyword, TextEdit::Styles::TokenTypeKeyword},    // Blue
        {TextEdit::TokenType::String, TextEdit::Styles::TokenTypeString},      // Green
        {TextEdit::TokenType::Number, TextEdit::Styles::TokenTypeNumber},      // Orange
        {TextEdit::TokenType::Comment, TextEdit::Styles::TokenTypeComment},    // Gray
        {TextEdit::TokenType::Operator, TextEdit::Styles::TokenTypeOperator},  // Cyan
        {TextEdit::TokenType::Grouping, TextEdit::Styles::TokenTypeGrouping},  // Red
        {TextEdit::TokenType::Identifier, TextEdit::Styles::TokenTypeIdentifier}, // White
        {TextEdit::TokenType::Preprocessor, TextEdit::Styles::TokenTypePreprocessor},
        {TextEdit::TokenType::Function, TextEdit::Styles::TokenTypeFunction},
        {TextEdit::TokenType::Type,     TextEdit::Styles::TokenTypeType},
        {TextEdit::TokenType::Constant, TextEdit::Styles::TokenTypeConstant},
        {TextEdit::TokenType::Regex,    TextEdit::Styles::TokenTypeRegex},
        {TextEdit::TokenType::Template, TextEdit::Styles::TokenTypeTemplate},
        {TextEdit::TokenType::Decorator, TextEdit::Styles::TokenTypeDecorator},
        {TextEdit::TokenType::Label,    TextEdit::Styles::TokenTypeLabel},
        {TextEdit::TokenType::Attribute, TextEdit::Styles::TokenTypeAttribute}
    };

    std::vector<SyntaxRule> TextEdit::Styles::syntax_rules = {
        // Multi-line comments (process before single-line to handle /* // */ correctly)
         {srell::u32regex(UR"(/\*[\s\S]*?\*/)"), TokenType::Comment},
        {srell::u32regex(UR"(/\*.*?\*/|/\*[^\n]*)"), TokenType::Comment}, // Multi-line comment (full or start)
       {srell::u32regex(UR"(//[^\n]*)"), TokenType::Comment}, // Single-line comment


       // C++ Preprocessor directives (must come early to avoid conflicts)
          {srell::u32regex(UR"(^\s*#\s*(include|define|undef|ifdef|ifndef|if|else|elif|endif|pragma|error|warning|line)\b.*)"), TokenType::Preprocessor},



          // Single-line comments
          {srell::u32regex(UR"(//[^\n]*)"), TokenType::Comment},

          // String literals (various types)
          // Standard strings
          {srell::u32regex(UR"("[^"\\]*(?:\\.[^"\\]*)*")"), TokenType::String},
          {srell::u32regex(UR"('[^'\\]*(?:\\.[^'\\]*)*')"), TokenType::String},

          // Raw strings (C++)
          {srell::u32regex(UR"(R"([^(]*)\([\s\S]*?\)\1")"), TokenType::String},

          // Template literals (JavaScript)
          {srell::u32regex(UR"(`[^`\\]*(?:\\.[^`\\]*)*`)"), TokenType::Template},

          // Regular expressions (JavaScript) - must come before division operator
          {srell::u32regex(UR"((?<=[=\(\[!&|;,\{:]\s*)/[^/\\\n\*](?:[^/\\\n]|\\.)*/[gimsuvy]*)"), TokenType::Regex},

          // Numbers (enhanced)
          // Hexadecimal
          {srell::u32regex(UR"(0[xX][0-9a-fA-F]+(?:[uU]?[lL]{0,2}|[lL]{0,2}[uU]?)?\b)"), TokenType::Number},
          // Binary (C++14)
          {srell::u32regex(UR"(0[bB][01]+(?:[uU]?[lL]{0,2}|[lL]{0,2}[uU]?)?\b)"), TokenType::Number},
          // Octal
          {srell::u32regex(UR"(0[0-7]+(?:[uU]?[lL]{0,2}|[lL]{0,2}[uU]?)?\b)"), TokenType::Number},
          // Floating point (with scientific notation)
          {srell::u32regex(UR"(\b\d+\.?\d*(?:[eE][+-]?\d+)?[fFlL]?\b)"), TokenType::Number},
          // Integer literals with separators (C++14)
          {srell::u32regex(UR"(\b\d+(?:'\d+)*(?:[uU]?[lL]{0,2}|[lL]{0,2}[uU]?)?\b)"), TokenType::Number},

          // C++ Attributes
          {srell::u32regex(UR"(\[\[[\w:]+(?:\([^)]*\))?\]\])"), TokenType::Attribute},

          // Keywords (expanded for both languages)
          {srell::u32regex(UR"(\b(?:if|else|for|while|do|return|class|struct|namespace|const|static|void|int|double|char|bool|switch|case|break|continue|template|typename|try|catch|finally|throw|new|delete|this|public|protected|private|virtual|override|final|explicit|inline|friend|using|typedef|enum|union|sizeof|alignof|decltype|nullptr|true|false|export|import|module|concept|requires|co_await|co_return|co_yield|constexpr|consteval|constinit|mutable|volatile|register|extern|auto|signed|unsigned|short|long|float|wchar_t|char8_t|char16_t|char32_t|asm|goto|default|operator|typeid|dynamic_cast|static_cast|const_cast|reinterpret_cast|thread_local|noexcept|alignas|static_assert|_Static_assert|_Thread_local|_Alignas|_Alignof|_Atomic|_Bool|_Complex|_Generic|_Imaginary|_Noreturn|restrict|function|var|let|async|await|yield|of|in|instanceof|typeof|with|debugger|extends|implements|interface|package|super|arguments|eval|Infinity|NaN|undefined|null|globalThis|constructor|prototype|get|set|from|as|satisfies)\b)"), TokenType::Keyword},

          // Built-in types (separate from keywords for different highlighting)
          {srell::u32regex(UR"(\b(?:int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|size_t|ptrdiff_t|intptr_t|uintptr_t|string|wstring|u8string|u16string|u32string|vector|map|set|list|array|unique_ptr|shared_ptr|weak_ptr|deque|queue|stack|pair|tuple|optional|variant|any|bitset|complex|valarray|span|string_view|function|promise|future|thread|mutex|condition_variable|atomic|duration|time_point|Number|String|Boolean|Object|Array|Function|Date|RegExp|Error|Promise|Map|Set|WeakMap|WeakSet|Symbol|BigInt|Int8Array|Uint8Array|Uint8ClampedArray|Int16Array|Uint16Array|Int32Array|Uint32Array|Float32Array|Float64Array|BigInt64Array|BigUint64Array|ArrayBuffer|SharedArrayBuffer|DataView|Proxy|Reflect)\b)"), TokenType::Type},

          // Built-in constants
          {srell::u32regex(UR"(\b(?:NULL|EOF|INFINITY|M_PI|M_E|__cplusplus|__LINE__|__FILE__|__DATE__|__TIME__|__FUNCTION__|__func__|CHAR_BIT|SCHAR_MIN|SCHAR_MAX|UCHAR_MAX|CHAR_MIN|CHAR_MAX|MB_LEN_MAX|SHRT_MIN|SHRT_MAX|USHRT_MAX|INT_MIN|INT_MAX|UINT_MAX|LONG_MIN|LONG_MAX|ULONG_MAX|LLONG_MIN|LLONG_MAX|ULLONG_MAX|FLT_MIN|FLT_MAX|DBL_MIN|DBL_MAX|LDBL_MIN|LDBL_MAX)\b)"), TokenType::Constant},

          // Decorators (JavaScript/TypeScript)
          {srell::u32regex(UR"(@\w+)"), TokenType::Decorator},

          // Labels
          {srell::u32regex(UR"(^\s*\w+\s*:(?!:))"), TokenType::Label},

          // Operators (comprehensive)
          {srell::u32regex(UR"(->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||::|\.\.\.|<=>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<=|>>=|\?\?|=>|\*\*|[+\-*/%=&|!<>^~?:.,;])"), TokenType::Operator},

          // Grouping symbols
          {srell::u32regex(UR"([\(\)\{\}\[\]])"), TokenType::Grouping},

          // Identifiers (must come after keywords)
          {srell::u32regex(UR"(\b[a-zA-Z_$][a-zA-Z0-9_$]*\b)"), TokenType::Identifier},

          // Whitespace
          {srell::u32regex(UR"(\s+)"), TokenType::Normal},

          // Any other character
          {srell::u32regex(UR"(.)"), TokenType::Normal}
    };
}

void TextEdit::Styles::ApplyDPI(float dpi) {
    DPI = dpi;
    SCROLLBAR_SIZE *= dpi;
    AUTOSCROLL_MARGIN *= dpi;
    REGULAR_FONT_SIZE *= dpi;
    SMALL_FONT_SIZE *= dpi;
    GUTTER_RIGHT_PAD *= dpi;
    HIGHLIGHTER_BUTTON_WIDTH *= dpi;
    CONTEXT_MENU_WIDTH *= dpi;
    FILE_MENU_HEIGHT *= dpi;
    FILE_MENU_PADDING *= dpi;
    MEDIUM_FONT_SIZE *= dpi;
    FILE_MENU_DROPDOWN_HEIGHT *= dpi;
    FILE_MENU_DROPDOWN_WIDTH *= dpi;
    FILE_MENU_DIVIDER_HEIGHT *= dpi;
    TAB_BAR_HEIGHT *= dpi;
    TAB_BAR_DRAG_THRESHOLD *= dpi;
    TAB_WIDTH *= dpi;
    WINDOW_BUTTON_WIDTH *= dpi;
}