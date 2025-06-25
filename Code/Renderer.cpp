#include "Renderer.h"
#include "Font.h" // Make sure Font.h is included
#include "Styles.h"
#include <cmath>
#include <cstdio> // For printf
#include <cstring> // For offsetof if not using C++11 std::offsetof



// External font data (definitions would be in a .c or .cpp file linked in)
// For the purpose of this example, assume they are declared elsewhere if used by Create().
// unsigned char Roboto[] = { /* ... */ };
// unsigned int Roboto_Size = sizeof(Roboto);
// unsigned char NotoEmoji[] = { /* ... */ };
// unsigned int NotoEmoji_Size = sizeof(NotoEmoji);


namespace TextEdit {
    static const char* gVertexShader = R"GLSL(#version 300 es
        layout(location = 0) in vec2 inPosition;
        layout(location = 1) in vec3 inColor;
        layout(location = 2) in float inTexFlag;
        layout(location = 3) in vec2 inUV;

        uniform vec2 uViewportSize;
        
        out vec3 fragColor;
        out float texFlag;
        out vec2 uvCoord;

        void main() {
            fragColor = inColor;
            texFlag = inTexFlag;
            uvCoord = inUV;
            vec2 ndcPos = vec2(
                (inPosition.x / uViewportSize.x) * 2.0 - 1.0,
                1.0 - (inPosition.y / uViewportSize.y) * 2.0 // Y flipped to correspond to top-left origin
            );
            gl_Position = vec4(ndcPos, 0.0, 1.0);
        }
    )GLSL";

    static const char* gFragmentShader = R"GLSL(#version 300 es
		precision mediump float;
        in vec3 fragColor;
        in float texFlag;
        in vec2 uvCoord;
        out vec4 outColor;

        uniform sampler2D uTexture;

        void main()
        {
            if(texFlag > 0.5) { // Using a threshold for texFlag
                vec4 sampleColor = texture(uTexture, uvCoord);
                // Font atlas stores white characters with alpha.
                // Modulate fragColor (desired text color) with sampleColor.rgb (should be white, effectively 1.0)
                // and use sampleColor.a as the final alpha.
                outColor = vec4(fragColor.rgb * sampleColor.rgb, sampleColor.a);
            } else {
                outColor = vec4(fragColor, 1.0); // For solid rects, alpha is 1.0
            }
        }
    )GLSL";

    static GLuint CompileShader(GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint status = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == 0) {
            char buffer[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, buffer);
            printf("Shader Compile Error (%s): %s\n", (type == GL_VERTEX_SHADER ? "VS" : "FS"), buffer);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    static GLuint CreateProgram(const char* vsSrc, const char* fsSrc) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
        if (!vs) return 0;
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!fs) {
            glDeleteShader(vs);
            return 0;
        }
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs); // Can delete after linking
        glDeleteShader(fs); // Can delete after linking

        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked) {
            char buffer[1024];
            glGetProgramInfoLog(prog, 1024, nullptr, buffer);
            printf("Program Link Error: %s\n", buffer);
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    }


    std::shared_ptr<Renderer> Renderer::Create(float dpi, std::shared_ptr<Font> defaultFont) {
        auto r = std::make_shared<Renderer>();
        if (!r->InitGLResources()) { // Check if InitGLResources failed
            // Handle initialization failure, perhaps by returning nullptr or logging
            printf("Renderer: Failed to initialize GL resources.\n");
            return nullptr; 
        }

        if (defaultFont != nullptr) {
            r->mDefaultFont = defaultFont;
        }
        else {
            std::shared_ptr<Font> defaultBase = Font::Create(Roboto, Roboto_Size, 18.0f, dpi);
            defaultBase->LoadEmojis(NotoEmoji, NotoEmoji_Size, 18.0f, dpi);
            r->mDefaultFont = defaultBase;
        }
        return r;
    }

    Renderer::Renderer()
        : mClipRect(0, 0, 0, 0),
        mBoundFont(nullptr),
        mDefaultFont(nullptr),
        mProgram(0),
        mVBO(0),
        mVAO(0),
        mViewportX(0),
        mViewportY(0),
        mViewportWidth(0),
        mViewportHeight(0),
        mLayoutScale(1.0f) {
    }

    Renderer::~Renderer() {
        CleanGLResources();
    }

    void Renderer::StartFrame(unsigned int viewport_x, unsigned int viewport_y,
        unsigned int viewport_width, unsigned int viewport_height) {
        mViewportX = viewport_x;
        mViewportY = viewport_y;
        mViewportWidth = viewport_width;
        mViewportHeight = viewport_height;

        mClipRect = Rect(static_cast<float>(viewport_x), static_cast<float>(viewport_y),
            static_cast<float>(viewport_width), static_cast<float>(viewport_height));
        mDrawBuffer.clear();

        // It's common to set GL viewport here too
        glViewport(mViewportX, mViewportY, mViewportWidth, mViewportHeight);
    }

    void Renderer::EndFrame() {
        FlushAndDraw();
        // mBoundFont is reset per frame by SetFont being called or not.
        // If SetFont is not called, previous mBoundFont persists.
        // Clearing it here might be too aggressive if SetFont is not called every frame.
        // Let's assume mBoundFont persists unless explicitly changed.
    }

    void Renderer::SetFont(std::shared_ptr<Font> font) {
        std::shared_ptr<Font> newFont = font ? font : mDefaultFont;
        if (mBoundFont != newFont) {
            FlushAndDraw(); // Flush with old font's texture (if any)
            mBoundFont = newFont;
        }
    }

    void Renderer::SetClip(float x, float y, float w, float h) {
        FlushAndDraw(); // Apply current draw calls with old clip
        mClipRect = Rect(x, y, w, h);
    }

    const Rect& Renderer::GetClip() const {
        return mClipRect;
    }


    void Renderer::ClearClip() {
        FlushAndDraw(); // Apply current draw calls with old clip
        mClipRect = Rect(static_cast<float>(mViewportX), static_cast<float>(mViewportY),
            static_cast<float>(mViewportWidth), static_cast<float>(mViewportHeight));
    }


    void Renderer::DrawRect(float x, float y, float w, float h, float r, float g, float b) {
         r = std::min(1.0f, std::max(0.0f, r * Styles::GlobalTint.r));
         g = std::min(1.0f, std::max(0.0f, g * Styles::GlobalTint.g));
         b = std::min(1.0f, std::max(0.0f, b * Styles::GlobalTint.b));
         w *= mLayoutScale;
         h *= mLayoutScale;

        float clippedX = x, clippedY = y, clippedW = w, clippedH = h;
        if (!ClipRectAgainstCurrent(clippedX, clippedY, clippedW, clippedH)) {
            return;
        }

        Vertex v;
        v.r = r; v.g = g; v.b = b;
        v.texFlag = 0.0f;
        v.u = 0.0f; v.v = 0.0f;

        // Top-left
        v.x = clippedX; v.y = clippedY; mDrawBuffer.push_back(v);
        // Bottom-left
        v.x = clippedX; v.y = clippedY + clippedH; mDrawBuffer.push_back(v);
        // Bottom-right
        v.x = clippedX + clippedW; v.y = clippedY + clippedH; mDrawBuffer.push_back(v);

        // Top-left
        v.x = clippedX; v.y = clippedY; mDrawBuffer.push_back(v);
        // Bottom-right
        v.x = clippedX + clippedW; v.y = clippedY + clippedH; mDrawBuffer.push_back(v);
        // Top-right
        v.x = clippedX + clippedW; v.y = clippedY; mDrawBuffer.push_back(v);
    }

    void Renderer::DrawChar(char32_t character, float penX_baseline, float penY_baseline, float r, float g, float b) {
        r = std::min(1.0f, std::max(0.0f, r * Styles::GlobalTint.r));
        g = std::min(1.0f, std::max(0.0f, g * Styles::GlobalTint.g));
        b = std::min(1.0f, std::max(0.0f, b * Styles::GlobalTint.b));

        std::shared_ptr<Font> currentFont = mBoundFont ? mBoundFont : mDefaultFont;
        if (!currentFont || !currentFont->IsValid()) {
            return;
        }

        GlyphInfo glyph = currentFont->GetGlyph(character);

        if (!glyph.isValid || glyph.width <= 0 || glyph.height <= 0) {
            return;
        }

        // penX_baseline, penY_baseline is the glyph origin on the baseline.
        // glyph.leftBearing is x-offset from origin to left of bitmap.
        // glyph.topBearing is y-offset from origin (baseline) to top of bitmap (positive upwards in font space).
        float quadScreenX = penX_baseline + glyph.leftBearing;
        float quadScreenY = penY_baseline - glyph.height + glyph.topBearing; // THIS WORKS, DO NOT CHANGE!

        float u1_glyph = glyph.u0;
        float v1_glyph = glyph.v0;
        float u2_glyph = glyph.u1;
        float v2_glyph = glyph.v1;

        float finalScreenX = quadScreenX;
        float finalScreenY = quadScreenY;
        float finalWidth = glyph.width;
        float finalHeight = glyph.height;

        finalWidth *= mLayoutScale;
        finalHeight *= mLayoutScale;

        if (!ClipRectAgainstCurrent(finalScreenX, finalScreenY, finalWidth, finalHeight, &u1_glyph, &v1_glyph, &u2_glyph, &v2_glyph)) {
            return;
        }

        if (finalWidth <= 0.0f || finalHeight <= 0.0f) {
            return;
        }

        Vertex v;
        v.r = r; v.g = g; v.b = b;
        v.texFlag = 1.0f;

        // Triangle 1
        v.x = finalScreenX; v.y = finalScreenY; v.u = u1_glyph; v.v = v1_glyph; mDrawBuffer.push_back(v); // Top-Left
        v.x = finalScreenX; v.y = finalScreenY + finalHeight; v.u = u1_glyph; v.v = v2_glyph; mDrawBuffer.push_back(v); // Bottom-Left
        v.x = finalScreenX + finalWidth; v.y = finalScreenY + finalHeight; v.u = u2_glyph; v.v = v2_glyph; mDrawBuffer.push_back(v); // Bottom-Right

        // Triangle 2
        v.x = finalScreenX; v.y = finalScreenY; v.u = u1_glyph; v.v = v1_glyph; mDrawBuffer.push_back(v); // Top-Left
        v.x = finalScreenX + finalWidth; v.y = finalScreenY + finalHeight; v.u = u2_glyph; v.v = v2_glyph; mDrawBuffer.push_back(v); // Bottom-Right
        v.x = finalScreenX + finalWidth; v.y = finalScreenY; v.u = u2_glyph; v.v = v1_glyph; mDrawBuffer.push_back(v); // Top-Right
    }

    float Renderer::DrawText(const std::u32string& text, int startChar, int endChar,
        float x_start, float y_topLeft, float r, float g, float b,
        float lineStartX) {
        std::shared_ptr<Font> currentFont = mBoundFont ? mBoundFont : mDefaultFont;
        if (!currentFont || !currentFont->IsValid()) {
            return 0.0f;
        }

        // If lineStartX is not provided, use x_start as the line start
        if (lineStartX < 0.0f) {
            lineStartX = x_start;
        }

        float fontPixelAscent = currentFont->GetScaledAscent();
        float fontLineHeight = currentFont->GetLineHeight();
        unsigned int tabSpaces = currentFont->GetTabNumSpaces();
        unsigned int spaceWidthPixels = currentFont->GetSpaceWidthPixels();

        float currentPenX = x_start;
        float currentBaselineY = y_topLeft + fontPixelAscent;

        if (startChar < 0) startChar = 0;
        if (endChar > (int)text.length()) endChar = (int)text.length();

        for (size_t i = startChar; i < endChar; ++i) {
            char32_t character = text[i];

            if (character == U'\n') {
                currentPenX = lineStartX;  // Reset to line start
                currentBaselineY += fontLineHeight;
                continue;
            }
            else if (character == U'\t') {
                // Simple fixed-width tab
                float tabWidth = static_cast<float>(spaceWidthPixels * tabSpaces);
                currentPenX += tabWidth;
                continue;
            }

            DrawChar(character, currentPenX, currentBaselineY, r, g, b);

            GlyphInfo glyph = currentFont->GetGlyph(character);
            if (glyph.isValid) {
                currentPenX += glyph.advance;
            }
        }

        return (currentPenX - x_start) * mLayoutScale;
    }

    void Renderer::FlushAndDraw() {
        if (mDrawBuffer.empty()) {
            return;
        }

        std::shared_ptr<Font> fontToUse = mBoundFont ? mBoundFont : mDefaultFont;
        bool hasTexturedGlyphs = false;
        for (const auto& vertex : mDrawBuffer) {
            if (vertex.texFlag > 0.5f) {
                hasTexturedGlyphs = true;
                break;
            }
        }

        // If there are textured glyphs but no valid font/texture, we might have an issue.
        if (hasTexturedGlyphs && (!fontToUse || !fontToUse->IsValid() || fontToUse->GetAtlasTextureHandle() == 0)) {
            // Option 1: Clear only textured parts (complex)
            // Option 2: Draw rects anyway, text won't show (current shader might show black for text)
            // Option 3: Clear entire buffer (simplest, but loses rects)
            // For now, let it try to draw. The shader will sample texture; if no texture bound, result is undefined (often black or white).
            // A better check: if fontToUse or its texture is null, don't even try to bind/use it for text.
            // printf("Warning: Flushing draw buffer with textured glyphs, but font/texture is not fully available.\n");
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // glViewport already set in StartFrame

        glUseProgram(mProgram);
        glBindVertexArray(mVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);

        // Texture binding
        if (hasTexturedGlyphs && fontToUse && fontToUse->IsValid() && fontToUse->GetAtlasTextureHandle() != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fontToUse->GetAtlasTextureHandle());
            GLint texLoc = glGetUniformLocation(mProgram, "uTexture");
            if (texLoc != -1) glUniform1i(texLoc, 0);
        }
        else if (hasTexturedGlyphs) {
            // This case means we expected a texture but couldn't bind one.
            // Drawing will likely be incorrect for textured parts.
        }


        glBufferData(GL_ARRAY_BUFFER, mDrawBuffer.size() * sizeof(Vertex),
            mDrawBuffer.data(), GL_STREAM_DRAW); // Use STREAM_DRAW for buffer that changes every frame

        GLint viewportSizeLoc = glGetUniformLocation(mProgram, "uViewportSize");
        if (viewportSizeLoc != -1) glUniform2f(viewportSizeLoc, static_cast<float>(mViewportWidth), static_cast<float>(mViewportHeight));

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mDrawBuffer.size()));

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
        glDisable(GL_BLEND);

        mDrawBuffer.clear();
    }

    bool Renderer::ClipRectAgainstCurrent(float& inoutX, float& inoutY, float& inoutW, float& inoutH,
        float* u1, float* v1, float* u2, float* v2) {

        // mClipRect is the master clipping rectangle for the current state.
        // itemRect is the rectangle of the item we want to draw.
        Rect itemRect(inoutX, inoutY, inoutW, inoutH);
        Rect clippedRect = itemRect.ClipAgainst(mClipRect);

        if (clippedRect.width <= 0.0f || clippedRect.height <= 0.0f) {
            return false; // Fully clipped
        }

        if (u1 && v1 && u2 && v2) { // If UVs are provided, adjust them
            if (itemRect.width > 1e-6 && itemRect.height > 1e-6) { // Avoid division by zero for original zero-size items
                float originalU_Range = *u2 - *u1;
                float originalV_Range = *v2 - *v1;

                // Calculate ratios of clipped rect start/end within original item rect
                // These are how much of the *original* item rect is cut off from each side
                float ratioLeft = (clippedRect.x - itemRect.x) / itemRect.width;
                float ratioTop = (clippedRect.y - itemRect.y) / itemRect.height;
                // These are how much of the *original* item rect remains, from the left/top edge of original
                float ratioRight = (clippedRect.x + clippedRect.width - itemRect.x) / itemRect.width;
                float ratioBottom = (clippedRect.y + clippedRect.height - itemRect.y) / itemRect.height;


                float new_u1 = *u1 + originalU_Range * ratioLeft;
                float new_v1 = *v1 + originalV_Range * ratioTop;
                float new_u2 = *u1 + originalU_Range * ratioRight; // *u1 (original start) + range * ratio
                float new_v2 = *v1 + originalV_Range * ratioBottom;

                *u1 = new_u1;
                *v1 = new_v1;
                *u2 = new_u2;
                *v2 = new_v2;
            }
            else if (itemRect.width <= 1e-6 || itemRect.height <= 1e-6) {
                // Original item was zero-size; if clipped is also zero-size, return false handled above.
                // If clipped somehow became non-zero (unlikely for correct clipping), UVs remain as passed.
                // Or, if original was zero width/height, UVs are likely degenerate anyway.
            }
        }

        inoutX = clippedRect.x;
        inoutY = clippedRect.y;
        inoutW = clippedRect.width;
        inoutH = clippedRect.height;
        return true;
    }


    bool Renderer::InitGLResources() {
        mProgram = CreateProgram(gVertexShader, gFragmentShader);
        if (mProgram == 0) return false;

        glGenVertexArrays(1, &mVAO);
        if (mVAO == 0) {
            glDeleteProgram(mProgram); mProgram = 0; return false;
        }

        glGenBuffers(1, &mVBO);
        if (mVBO == 0) {
            glDeleteVertexArrays(1, &mVAO); mVAO = 0;
            glDeleteProgram(mProgram); mProgram = 0;
            return false;
        }

        glBindVertexArray(mVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mVBO);

        GLsizei stride = sizeof(Vertex);

        // Position (vec2)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, x));
        // Color (vec3)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, r));
        // TexFlag (float)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, texFlag));
        // UV (vec2)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, u));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return true;
    }

    void Renderer::CleanGLResources() {
        if (mVBO) {
            glDeleteBuffers(1, &mVBO);
            mVBO = 0;
        }
        if (mVAO) {
            glDeleteVertexArrays(1, &mVAO);
            mVAO = 0;
        }
        if (mProgram) {
            glDeleteProgram(mProgram);
            mProgram = 0;
        }
        // mDefaultFont is a shared_ptr, will be managed automatically.
    }
}