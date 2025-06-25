#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <cstdint> // For char32_t and other integer types

#ifdef _WIN32
#include "glad.h"
#endif // _WIN32

#include "stb_truetype.h"

namespace TextEdit {
    struct Rect; // Forward declaration if used by Font, though not directly visible here
    class Renderer; // Forward declaration for friend class

    struct GlyphInfo {
        float u0;          // Texture coordinate (left)
        float v0;          // Texture coordinate (top)
        float u1;          // Texture coordinate (right)
        float v1;          // Texture coordinate (bottom)
        float advance;       // Horizontal advance for the pen
        float leftBearing;   // Horizontal offset from pen origin (on baseline) to left of glyph bitmap. (Scaled x0 from stbtt_GetGlyphBitmapBox)
        float topBearing;    // Vertical offset from baseline to top of glyph bitmap (Y-positive upwards, font space). (Scaled y1 from stbtt_GetGlyphBitmapBox)
        float width;         // Pixel width of the glyph bitmap
        float height;        // Pixel height of the glyph bitmap
        bool isValid;

        GlyphInfo() : u0(0), v0(0), u1(0), v1(0), advance(0), leftBearing(0), topBearing(0), width(0), height(0), isValid(false) {
        }
    };

    class Font {
    public:
        friend class Renderer; // Renderer might need access to private members for optimization or specific details

    protected:
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;
        Font(Font&&) = delete;
        Font& operator=(Font&&) = delete;

    public:
        static std::shared_ptr<Font> Create(const void* data, unsigned int bytes, float pixelHeight, float dpiScale);

        Font(const void* ttfData, unsigned int bytes, float pixelHeight, float dpiScale);
        virtual ~Font();

        bool IsValid() const;

        // Load glyphs for the base font, replacing existing ones.
        // If ttfData is null, it re-bakes with current font data and new pixelHeight.
        void LoadGlyphs(const void* ttfData, unsigned int bytes, float pixelHeight, float dpiScale);

        // Load an extension font (e.g., for emojis or additional character sets).
        // Glyphs from this font are baked on demand if not found in the base font.
        void LoadEmojis(const void* data, unsigned int bytes, float pixelHeight, float dpiScale);

        void SetTabNumSpaces(int numSpaces);
        unsigned int GetTabNumSpaces() const;      // Added getter
        unsigned int GetTabWidthInPixels() const;  // Calculates based on space width and tab num spaces
        unsigned int GetSpaceWidthPixels() const;  // Added getter

        bool BakeGlyph(char32_t codepoint); // Ensures a glyph is baked into the atlas if possible
        GlyphInfo GetGlyph(char32_t codepoint); // Returns glyph info, baking it if necessary

        unsigned int GetAtlasTextureHandle() const;
        unsigned int GetAtlasTextureWidth() const;
        unsigned int GetAtlasTextureHeight() const;

        float GetScaledAscent() const;     // Pixel distance from baseline to top of Ascent line
        float GetScaledDescent() const;    // Pixel distance from baseline to bottom of Descent line (positive value)
        float GetScaledLineGap() const;    // Pixel spacing between lines
        float GetLineHeight() const;       // Total height for a line (Ascent + Descent + LineGap)

    private:
        stbtt_fontinfo mBaseFont;
        bool mBaseFontLoaded;
        float mBaseFontPixelHeight; // Pixel height the base font was initially scaled to or last re-scaled with LoadGlyphs
        float mDpiScale;            // DPI scale for oversampling

        stbtt_fontinfo mExtFont;      // Extension font (e.g., for emojis)
        bool mExtFontLoaded;
        float mExtFontPixelHeight;    // Pixel height for the extension font
        float mExtDpiScale;           // DPI scale for the extension font

        GLuint mTextureID;
        unsigned int mAtlasWidth;
        unsigned int mAtlasHeight;
        unsigned int mMaxAtlasSize; // Maximum dimensions for the atlas texture
        std::vector<unsigned char> mAtlasPixels; // CPU-side copy of atlas pixels (RGBA)

        // Atlas packing state
        int mNextX;
        int mNextY;
        int mRowHeight; // Height of the current row being packed in the atlas

        std::map<char32_t, GlyphInfo> mGlyphMap; // Cache for baked glyphs
        bool mIsValid; // Overall validity of the Font object (e.g., base font loaded successfully)

        int mTabSize; // Number of spaces for a tab character
        unsigned int mSpaceWidthPixels; // Cached width of a space character in pixels

        // Internal helper methods
        bool AllocateSpaceForGlyph(int glyphW, int glyphH, int& outX, int& outY);
        bool TryExpandAtlas(unsigned int neededGlyphW, unsigned int neededGlyphH);
        void UploadAtlasToGPU();
        float GetScale(const stbtt_fontinfo* font, float pixelHeight, float dpiScale) const;
        bool LoadTTF(const void* data, unsigned int bytes, stbtt_fontinfo& fontOut); // Helper to init stbtt_fontinfo
        bool BakeGlyphToAtlas(char32_t codepoint, stbtt_fontinfo& font, float pixelHeight, float dpiScale); // Bakes using a specific font_info
    };
}