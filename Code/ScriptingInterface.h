#pragma once

#include <string>
#include <memory>
#include <vector>
#include "Document.h"
#include "DocumentContainer.h"

extern "C" {
#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
}

// Forward declarations of global UTF conversion functions
std::u32string Utf8ToUtf32(const char* utf8_string, unsigned int bytes);
std::string Utf32ToUtf8(const std::u32string& utf32_string);

namespace TextEdit {

    class ScriptingInterface {
    public:
        ScriptingInterface(std::shared_ptr<DocumentContainer> container);
        ~ScriptingInterface();

        // Initialize Lua state and register functions
        bool Initialize();

        // Execute a Lua script
        bool ExecuteScript(const std::string& script);

        // Get the Lua state (for advanced usage)
        lua_State* GetLuaState() {
            return L;
        }

        void DisplayError(const std::string& error) const;
        static ScriptingInterface* GetInstance(lua_State* L);
    private:
        lua_State* L;
        std::shared_ptr<DocumentContainer> mDocumentContainer;

        // Lua binding functions - C++
        static int CreateCppFile(lua_State* L);
        static int ReplaceCppFileContent(lua_State* L);
        static int ReplaceCppClass(lua_State* L);
        static int ReplaceCppFunction(lua_State* L);

        // Lua binding functions - JavaScript
        static int CreateJsFile(lua_State* L);
        static int ReplaceJsFileContents(lua_State* L);
        static int ReplaceJsClass(lua_State* L);
        static int ReplaceJsFunction(lua_State* L);

        // Helper functions
        std::shared_ptr<Document> FindDocumentByName(const std::string& filename) const;
        std::shared_ptr<Document> CreateNewDocument(const std::string& filename);

        // Register all Lua functions
        void RegisterLuaFunctions();
    };

} // namespace TextEdit