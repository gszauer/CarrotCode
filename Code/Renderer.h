#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include "glad.h"
#endif // _WIN32

#ifdef __EMSCRIPTEN__
#define GL_GLEXT_PROTOTYPES 1

#include <emscripten.h>
#include <SDL.h>
#include <GLES3/gl3.h> 
#include <SDL_syswm.h>
#endif

extern unsigned int Roboto_Size;
extern unsigned char Roboto[];
extern unsigned int NotoEmoji_Size;
extern unsigned char NotoEmoji[];

namespace TextEdit {
    struct Rect {
        float x;
        float y;
        float width;
        float height;

        inline Rect() : x(0.0f), y(0.0f),
            width(0.0f), height(0.0f) {
        }
        inline Rect(float _x, float _y, float w, float h)
            : x(_x), y(_y), width(w), height(h) {
        }

        inline bool Contains(float _x, float _y) const {
            return (_x >= x) && (_x <= x + width) &&
                (_y >= y) && (_y <= y + height);
        }

        inline bool Intersects(const Rect& other) const {
            return !(x + width <= other.x ||
                other.x + other.width <= x ||
                y + height <= other.y ||
                other.y + other.height <= y);
        }

        inline Rect ClipAgainst(const Rect& other) const {
            float x1 = std::max(x, other.x);
            float y1 = std::max(y, other.y);
            float x2 = std::min(x + width, other.x + other.width);
            float y2 = std::min(y + height, other.y + other.height);

            if (x2 < x1 || y2 < y1) {
                return Rect();
            }
            return Rect(x1, y1, x2 - x1, y2 - y1);
        }
    };

    class Font;

    struct Vertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float texFlag;  // 0.0 = no texture, 1.0 = use texture
        float u;
        float v;
    };

    class Renderer {
    protected:
        Rect mClipRect;
        std::shared_ptr<Font> mBoundFont;
        std::shared_ptr<Font> mDefaultFont;

        // GPU Resources
        GLuint mProgram;
        GLuint mVBO;
        GLuint mVAO;

        // draw buffer
        std::vector<Vertex> mDrawBuffer;

        // viewport
        unsigned int mViewportX;
        unsigned int mViewportY;
        unsigned int mViewportWidth;
        unsigned int mViewportHeight;

        float mLayoutScale; // User scale, applied after DPI adjustments

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;
    public:
        Renderer();
        static std::shared_ptr<Renderer> Create(float dpi, std::shared_ptr<Font> defaultFont = nullptr);
        virtual ~Renderer();

        void StartFrame(unsigned int viewport_x, unsigned int viewport_y,
            unsigned int viewport_width, unsigned int viewport_height);
        void EndFrame();

        void SetFont(std::shared_ptr<Font> font);

        void SetClip(float x, float y, float w, float h);
        const Rect& GetClip() const;
        void ClearClip();

        void DrawRect(float x, float y, float w, float h, float r, float g, float b);
        void DrawChar(char32_t character, float x, float y, float r, float g, float b);
        
        float DrawText(const std::u32string& text, int startChar, int onePastEndChar,
            float x, float y, float r, float g, float b,
            float lineStartX = -1.0f);
        
        inline float DrawText(const std::u32string& text, float x, float y,
            float r, float g, float b, float lineStartX = -1.0f) {
            return DrawText(text, 0, (int)text.length(), x, y, r, g, b, lineStartX);
        }

        inline void SetLayoutScale(float scl) {
            mLayoutScale = scl;
        }
    private:
        void FlushAndDraw();
        bool ClipRectAgainstCurrent(float& inoutX, float& inoutY, float& inoutW, float& inoutH,
            float* u1 = nullptr, float* v1 = nullptr,
            float* u2 = nullptr, float* v2 = nullptr);
        bool InitGLResources();
        void CleanGLResources();
    };
}