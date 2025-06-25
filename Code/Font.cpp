#include "Font.h"
#include <cstring> // for memcpy
#include <cmath>   // for std::round, std::floor
#include <algorithm> // for std::min, std::max

namespace TextEdit {
    static constexpr int GLYPH_PADDING = 1; // 1-pixel padding around each glyph
    static constexpr int INITIAL_ATLAS_SIZE = 256; // Initial size for the font atlas texture
    static constexpr unsigned int DEFAULT_MAX_ATLAS_SIZE = 4096; // Default max atlas size

    std::shared_ptr<Font> Font::Create(const void* data, unsigned int bytes, float pixelHeight, float dpiScale) {
        if (!data || bytes == 0 || pixelHeight <= 0.0f || dpiScale <= 0.0f) {
            return nullptr;
        }
        auto font = std::make_shared<Font>(data, bytes, pixelHeight, dpiScale);
        if (!font->IsValid()) {
            return nullptr;
        }
        return font;
    }

    Font::Font(const void* ttfData, unsigned int bytes, float pixelHeight, float dpiScale)
        : mBaseFontLoaded(false), mBaseFontPixelHeight(pixelHeight), mDpiScale(dpiScale),
        mExtFontLoaded(false), mExtFontPixelHeight(pixelHeight), mExtDpiScale(dpiScale),
        mTextureID(0),
        mAtlasWidth(INITIAL_ATLAS_SIZE),
        mAtlasHeight(INITIAL_ATLAS_SIZE),
        mMaxAtlasSize(DEFAULT_MAX_ATLAS_SIZE),
        mNextX(0), mNextY(0), mRowHeight(0),
        mIsValid(false),
        mTabSize(4),
        mSpaceWidthPixels(0) {
        mAtlasPixels.resize(static_cast<size_t>(mAtlasWidth) * mAtlasHeight * 4, 0);
        glGenTextures(1, &mTextureID);
        mBaseFontLoaded = LoadTTF(ttfData, bytes, mBaseFont);
        if (mBaseFontLoaded) {
            LoadGlyphs(nullptr, 0, mBaseFontPixelHeight, mDpiScale);
        }
        else {
            mIsValid = false;
            if (mTextureID) {
                glDeleteTextures(1, &mTextureID);
                mTextureID = 0;
            }
        }
    }

    Font::~Font() {
        if (mTextureID) {
            glDeleteTextures(1, &mTextureID);
            mTextureID = 0;
        }
    }

    bool Font::IsValid() const {
        return mIsValid && mBaseFontLoaded;
    }

    void Font::LoadGlyphs(const void* ttfData, unsigned int bytes, float pixelHeight, float dpiScale) {
        if (ttfData && bytes > 0) {
            mBaseFontLoaded = LoadTTF(ttfData, bytes, mBaseFont);
            if (mBaseFontLoaded) {
                mBaseFontPixelHeight = pixelHeight;
                mDpiScale = dpiScale;
            }
            else {
                mIsValid = false;
                return;
            }
        }
        else if (!mBaseFontLoaded) {
            mIsValid = false;
            return;
        }
        else {
            mBaseFontPixelHeight = pixelHeight;
            mDpiScale = dpiScale;
        }

        mGlyphMap.clear();
        mAtlasPixels.assign(static_cast<size_t>(mAtlasWidth) * mAtlasHeight * 4, 0);
        mNextX = 0;
        mNextY = 0;
        mRowHeight = 0;

        for (char32_t c = 32; c <= 126; ++c) {
            BakeGlyphToAtlas(c, mBaseFont, mBaseFontPixelHeight, mDpiScale);
        }
        if (mGlyphMap.find(U' ') == mGlyphMap.end()) {
            BakeGlyphToAtlas(U' ', mBaseFont, mBaseFontPixelHeight, mDpiScale);
        }
        if (mGlyphMap.find(U'?') == mGlyphMap.end()) {
            BakeGlyphToAtlas(U'?', mBaseFont, mBaseFontPixelHeight, mDpiScale);
        }

        auto itSpace = mGlyphMap.find(U' ');
        if (itSpace != mGlyphMap.end() && itSpace->second.isValid) {
            mSpaceWidthPixels = static_cast<unsigned int>(std::round(itSpace->second.advance));
        }
        else {
            mSpaceWidthPixels = static_cast<unsigned int>(std::round(mBaseFontPixelHeight / 2.0f));
            GlyphInfo spaceGlyph;
            spaceGlyph.advance = mBaseFontPixelHeight / 2.0f;
            int spaceIdx = stbtt_FindGlyphIndex(&mBaseFont, ' ');
            if (spaceIdx != 0) {
                int adv, lsb;
                stbtt_GetGlyphHMetrics(&mBaseFont, spaceIdx, &adv, &lsb);
                spaceGlyph.advance = adv * GetScale(&mBaseFont, mBaseFontPixelHeight, mDpiScale) / mDpiScale;
                mSpaceWidthPixels = static_cast<unsigned int>(std::round(spaceGlyph.advance));
            }
        }

        if (!mGlyphMap.empty()) {
            UploadAtlasToGPU();
        }
        mIsValid = true;
    }

    void Font::LoadEmojis(const void* data, unsigned int bytes, float pixelHeight, float dpiScale) {
        if (data && bytes > 0) {
            mExtFontLoaded = LoadTTF(data, bytes, mExtFont);
            if (mExtFontLoaded) {
                mExtFontPixelHeight = pixelHeight;
                mExtDpiScale = dpiScale;
            }
        }
        else {
            mExtFontLoaded = false;
        }
    }

    void Font::SetTabNumSpaces(int numSpaces) {
        mTabSize = std::max(1, numSpaces);
    }

    unsigned int Font::GetTabNumSpaces() const {
        return static_cast<unsigned int>(mTabSize);
    }

    unsigned int Font::GetSpaceWidthPixels() const {
        return mSpaceWidthPixels;
    }

    unsigned int Font::GetTabWidthInPixels() const {
        return mSpaceWidthPixels * static_cast<unsigned int>(mTabSize);
    }

    float Font::GetScale(const stbtt_fontinfo* font, float pixelHeight, float dpiScale) const {
        if (!font || pixelHeight <= 0.0f || dpiScale <= 0.0f) return 0.0f;
        return stbtt_ScaleForPixelHeight(font, pixelHeight * dpiScale);
    }

    float Font::GetScaledAscent() const {
        if (!mBaseFontLoaded) return mBaseFontPixelHeight * 0.75f;
        int ascent_font_units;
        stbtt_GetFontVMetrics(&mBaseFont, &ascent_font_units, nullptr, nullptr);
        return static_cast<float>(ascent_font_units) * GetScale(&mBaseFont, mBaseFontPixelHeight, 1.0f);
    }

    float Font::GetScaledDescent() const {
        if (!mBaseFontLoaded) return mBaseFontPixelHeight * 0.25f;
        int descent_font_units;
        stbtt_GetFontVMetrics(&mBaseFont, nullptr, &descent_font_units, nullptr);
        return static_cast<float>(-descent_font_units) * GetScale(&mBaseFont, mBaseFontPixelHeight, 1.0f);
    }

    float Font::GetScaledLineGap() const {
        if (!mBaseFontLoaded) return 0.0f;
        int line_gap_font_units;
        stbtt_GetFontVMetrics(&mBaseFont, nullptr, nullptr, &line_gap_font_units);
        return static_cast<float>(line_gap_font_units) * GetScale(&mBaseFont, mBaseFontPixelHeight, 1.0f);
    }

    float Font::GetLineHeight() const {
        if (!mBaseFontLoaded) return mBaseFontPixelHeight * 1.2f;
        return GetScaledAscent() + GetScaledDescent() + GetScaledLineGap();
    }

    bool Font::BakeGlyph(char32_t codepoint) {
        if (mGlyphMap.count(codepoint)) {
            return mGlyphMap[codepoint].isValid;
        }

        bool baked = false;
        if (mBaseFontLoaded) {
            if (stbtt_FindGlyphIndex(&mBaseFont, static_cast<int>(codepoint)) != 0) {
                baked = BakeGlyphToAtlas(codepoint, mBaseFont, mBaseFontPixelHeight, mDpiScale);
            }
        }

        if (!baked && mExtFontLoaded) {
            if (stbtt_FindGlyphIndex(&mExtFont, static_cast<int>(codepoint)) != 0) {
                baked = BakeGlyphToAtlas(codepoint, mExtFont, mExtFontPixelHeight, mExtDpiScale);
            }
        }

        if (!baked) {
            GlyphInfo dummy = {};
            dummy.isValid = false;
            mGlyphMap[codepoint] = dummy;
            return false;
        }
        return true;
    }

    GlyphInfo Font::GetGlyph(char32_t codepoint) {
        auto it = mGlyphMap.find(codepoint);
        if (it == mGlyphMap.end()) {
            if (!BakeGlyph(codepoint)) {
                it = mGlyphMap.find(U'?');
                if (it == mGlyphMap.end()) {
                    if (!BakeGlyph(U'?')) {
                        GlyphInfo failedGlyph = {};
                        failedGlyph.isValid = false;
                        return failedGlyph;
                    }
                    it = mGlyphMap.find(U'?');
                }
            }
            else {
                it = mGlyphMap.find(codepoint);
            }
        }
        return (it != mGlyphMap.end()) ? it->second : GlyphInfo{};
    }

    unsigned int Font::GetAtlasTextureHandle() const {
        return mTextureID;
    }

    unsigned int Font::GetAtlasTextureWidth() const {
        return mAtlasWidth;
    }

    unsigned int Font::GetAtlasTextureHeight() const {
        return mAtlasHeight;
    }

    bool Font::AllocateSpaceForGlyph(int glyphW_unpadded, int glyphH_unpadded, int& outX_padded_block, int& outY_padded_block) {
        if (glyphW_unpadded < 0 || glyphH_unpadded < 0) {
            return false;
        }

        int actual_gw_to_allocate = glyphW_unpadded + (2 * GLYPH_PADDING);
        int actual_gh_to_allocate = glyphH_unpadded + (2 * GLYPH_PADDING);

        if (static_cast<unsigned int>(actual_gw_to_allocate) > mMaxAtlasSize ||
            static_cast<unsigned int>(actual_gh_to_allocate) > mMaxAtlasSize) {
            return false;
        }

        if (mNextX + actual_gw_to_allocate > static_cast<int>(mAtlasWidth)) {
            mNextX = 0;
            mNextY += mRowHeight;
            mRowHeight = 0;
        }

        if (mNextY + actual_gh_to_allocate > static_cast<int>(mAtlasHeight)) {
            if (!TryExpandAtlas(static_cast<unsigned int>(actual_gw_to_allocate),
                static_cast<unsigned int>(actual_gh_to_allocate))) {
                return false;
            }
            if (mNextX + actual_gw_to_allocate > static_cast<int>(mAtlasWidth)) {
                mNextX = 0;
                mNextY += mRowHeight;
                mRowHeight = 0;
            }
            if (mNextY + actual_gh_to_allocate > static_cast<int>(mAtlasHeight)) {
                return false;
            }
        }

        outX_padded_block = mNextX;
        outY_padded_block = mNextY;

        mNextX += actual_gw_to_allocate;
        if (actual_gh_to_allocate > mRowHeight) {
            mRowHeight = actual_gh_to_allocate;
        }
        return true;
    }

    bool Font::TryExpandAtlas(unsigned int neededGlyphW_padded, unsigned int neededGlyphH_padded) {
        unsigned int currentActualAtlasWidth = mAtlasWidth;
        unsigned int currentActualAtlasHeight = mAtlasHeight;

        unsigned int proposedWidth = mAtlasWidth;
        unsigned int proposedHeight = mAtlasHeight;

        while (true) {
            bool canFitInProposed = false;
            if (neededGlyphW_padded <= proposedWidth && (static_cast<unsigned int>(mNextY + mRowHeight) + neededGlyphH_padded) <= proposedHeight) {
                canFitInProposed = true;
            }
            else if ((static_cast<unsigned int>(mNextX) + neededGlyphW_padded) <= proposedWidth && (static_cast<unsigned int>(mNextY) + neededGlyphH_padded) <= proposedHeight) {
                canFitInProposed = true;
            }

            if (canFitInProposed) break;

            if (proposedWidth >= mMaxAtlasSize && proposedHeight >= mMaxAtlasSize) return false;

            unsigned int prevProposedWidth = proposedWidth;
            unsigned int prevProposedHeight = proposedHeight;

            if (proposedWidth == proposedHeight && proposedWidth < mMaxAtlasSize) {
                proposedWidth *= 2;
                proposedHeight *= 2;
            }
            else if (proposedWidth < proposedHeight && proposedWidth < mMaxAtlasSize) {
                proposedWidth *= 2;
            }
            else if (proposedHeight < proposedWidth && proposedHeight < mMaxAtlasSize) {
                proposedHeight *= 2;
            }
            else if (proposedWidth < mMaxAtlasSize) {
                proposedWidth *= 2;
            }
            else if (proposedHeight < mMaxAtlasSize) {
                proposedHeight *= 2;
            }
            else {
                return false;
            }

            proposedWidth = std::min(proposedWidth, mMaxAtlasSize);
            proposedHeight = std::min(proposedHeight, mMaxAtlasSize);

            if (proposedWidth == prevProposedWidth && proposedHeight == prevProposedHeight) {
                return false;
            }
        }

        if (proposedWidth == currentActualAtlasWidth && proposedHeight == currentActualAtlasHeight) {
            return true;
        }

        // Create a new atlas buffer
        std::vector<unsigned char> newPixels(proposedWidth * proposedHeight * 4, 0);
        mAtlasPixels = std::move(newPixels);
        mAtlasWidth = proposedWidth;
        mAtlasHeight = proposedHeight;

        // Reset packing state
        mNextX = 0;
        mNextY = 0;
        mRowHeight = 0;

        // Store the current glyph map to re-bake
        std::map<char32_t, GlyphInfo> oldGlyphMap = std::move(mGlyphMap);
        mGlyphMap.clear();

        // Re-bake all glyphs from the base font
        if (mBaseFontLoaded) {
            for (const auto& pair : oldGlyphMap) {
                char32_t codepoint = pair.first;
                // Only re-bake glyphs that were valid and came from the base font
                if (pair.second.isValid && stbtt_FindGlyphIndex(&mBaseFont, static_cast<int>(codepoint)) != 0) {
                    BakeGlyphToAtlas(codepoint, mBaseFont, mBaseFontPixelHeight, mDpiScale);
                }
            }
            // Ensure space and '?' are baked
            if (mGlyphMap.find(U' ') == mGlyphMap.end()) {
                BakeGlyphToAtlas(U' ', mBaseFont, mBaseFontPixelHeight, mDpiScale);
            }
            if (mGlyphMap.find(U'?') == mGlyphMap.end()) {
                BakeGlyphToAtlas(U'?', mBaseFont, mBaseFontPixelHeight, mDpiScale);
            }
            // Recalculate space width
            auto itSpace = mGlyphMap.find(U' ');
            if (itSpace != mGlyphMap.end() && itSpace->second.isValid) {
                mSpaceWidthPixels = static_cast<unsigned int>(std::round(itSpace->second.advance));
            }
            else {
                mSpaceWidthPixels = static_cast<unsigned int>(std::round(mBaseFontPixelHeight / 2.0f));
                GlyphInfo spaceGlyph;
                spaceGlyph.advance = mBaseFontPixelHeight / 2.0f;
                int spaceIdx = stbtt_FindGlyphIndex(&mBaseFont, ' ');
                if (spaceIdx != 0) {
                    int adv, lsb;
                    stbtt_GetGlyphHMetrics(&mBaseFont, spaceIdx, &adv, &lsb);
                    spaceGlyph.advance = adv * GetScale(&mBaseFont, mBaseFontPixelHeight, mDpiScale) / mDpiScale;
                    mSpaceWidthPixels = static_cast<unsigned int>(std::round(spaceGlyph.advance));
                }
            }
        }

        // Re-bake emoji glyphs from the extension font
        if (mExtFontLoaded) {
            for (const auto& pair : oldGlyphMap) {
                char32_t codepoint = pair.first;
                // Only re-bake glyphs that were valid and not in base font but in ext font
                if (pair.second.isValid &&
                    stbtt_FindGlyphIndex(&mBaseFont, static_cast<int>(codepoint)) == 0 &&
                    stbtt_FindGlyphIndex(&mExtFont, static_cast<int>(codepoint)) != 0) {
                    BakeGlyphToAtlas(codepoint, mExtFont, mExtFontPixelHeight, mExtDpiScale);
                }
            }
        }

        if (!mGlyphMap.empty()) {
            UploadAtlasToGPU();
        }

        return true;
    }

    void Font::UploadAtlasToGPU() {
        if (mTextureID == 0 || mAtlasPixels.empty() || mAtlasWidth == 0 || mAtlasHeight == 0) {
            if (mTextureID == 0 && mIsValid) glGenTextures(1, &mTextureID);
            else return;
        }

        glBindTexture(GL_TEXTURE_2D, mTextureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mAtlasWidth, mAtlasHeight, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, mAtlasPixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool Font::LoadTTF(const void* data, unsigned int bytes, stbtt_fontinfo& fontOut) {
        if (!data || bytes == 0) return false;
        int font_offset = stbtt_GetFontOffsetForIndex(static_cast<const unsigned char*>(data), 0);
        if (font_offset == -1) {
            font_offset = 0;
        }
        if (stbtt_InitFont(&fontOut, static_cast<const unsigned char*>(data), font_offset)) {
            return true;
        }
        return false;
    }

    bool Font::BakeGlyphToAtlas(char32_t codepoint, stbtt_fontinfo& font, float pixelHeight, float dpiScale) {
        float scale = GetScale(&font, pixelHeight, dpiScale);
        if (scale == 0) return false;

        int glyphIndex = stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint));
        if (glyphIndex == 0 && codepoint != 0) {
            return false;
        }

        GlyphInfo g = {};
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&font, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

        int gw_unpadded = x1 - x0;
        int gh_unpadded = y1 - y0;

        int advWidth_font_units, lsb_font_units;
        stbtt_GetGlyphHMetrics(&font, glyphIndex, &advWidth_font_units, &lsb_font_units);
        g.advance = (static_cast<float>(advWidth_font_units) * scale) / dpiScale;

        g.leftBearing = static_cast<float>(x0) / dpiScale;
        g.topBearing = static_cast<float>(y1) / dpiScale;
        g.width = static_cast<float>(gw_unpadded) / dpiScale;
        g.height = static_cast<float>(gh_unpadded) / dpiScale;

        if (gw_unpadded > 0 && gh_unpadded > 0) {
            int atlasX_padded_block, atlasY_padded_block;
            if (!AllocateSpaceForGlyph(gw_unpadded, gh_unpadded, atlasX_padded_block, atlasY_padded_block)) {
                g.isValid = false;
                mGlyphMap[codepoint] = g;
                return false;
            }

            std::vector<unsigned char> glyphBuffer(static_cast<size_t>(gw_unpadded) * gh_unpadded);
            stbtt_MakeGlyphBitmap(&font, glyphBuffer.data(),
                gw_unpadded, gh_unpadded, gw_unpadded,
                scale, scale, glyphIndex);

            for (int row = 0; row < gh_unpadded; ++row) {
                for (int col = 0; col < gw_unpadded; ++col) {
                    unsigned char val = glyphBuffer[static_cast<size_t>(row) * gw_unpadded + col];
                    int destXInAtlas = atlasX_padded_block + GLYPH_PADDING + col;
                    int destYInAtlas = atlasY_padded_block + GLYPH_PADDING + row;
                    size_t offset = (static_cast<size_t>(destYInAtlas) * mAtlasWidth + destXInAtlas) * 4;
                    if (offset + 3 < mAtlasPixels.size()) {
                        mAtlasPixels[offset + 0] = 255;
                        mAtlasPixels[offset + 1] = 255;
                        mAtlasPixels[offset + 2] = 255;
                        mAtlasPixels[offset + 3] = val;
                    }
                }
            }

            g.u0 = static_cast<float>(atlasX_padded_block + GLYPH_PADDING) / mAtlasWidth;
            g.v0 = static_cast<float>(atlasY_padded_block + GLYPH_PADDING) / mAtlasHeight;
            g.u1 = static_cast<float>(atlasX_padded_block + GLYPH_PADDING + gw_unpadded) / mAtlasWidth;
            g.v1 = static_cast<float>(atlasY_padded_block + GLYPH_PADDING + gh_unpadded) / mAtlasHeight;

            g.isValid = true;
            UploadAtlasToGPU();
        }
        else {
            g.isValid = true;
        }

        mGlyphMap[codepoint] = g;
        return g.isValid;
    }
}