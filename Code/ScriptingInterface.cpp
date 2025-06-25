#include "ScriptingInterface.h"
#include "srell.hpp"
#include <iostream>
#include <sstream>
#include <cstdarg>

namespace TextEdit {

    // Store the ScriptingInterface instance pointer in the Lua registry
    static const char* SCRIPTING_INTERFACE_KEY = "TextEdit_ScriptingInterface";

    ScriptingInterface::ScriptingInterface(std::shared_ptr<DocumentContainer> container)
        : L(nullptr), mDocumentContainer(container) {
    }

    ScriptingInterface::~ScriptingInterface() {
        if (L) {
            lua_close(L);
        }
    }

    bool ScriptingInterface::Initialize() {
        L = luaL_newstate();
        if (!L) {
            return false;
        }

        luaL_openlibs(L);

        // Store this instance in the Lua registry
        lua_pushlightuserdata(L, (void*)SCRIPTING_INTERFACE_KEY);
        lua_pushlightuserdata(L, this);
        lua_settable(L, LUA_REGISTRYINDEX);

        RegisterLuaFunctions();

        return true;
    }

    std::shared_ptr<Document> ScriptingInterface::FindDocumentByName(const std::string& filename) const {
        if (!mDocumentContainer) return nullptr;

        // Get all open documents from the container
        std::vector<std::shared_ptr<Document>> allDocs = mDocumentContainer->GetAllOpenDocuments();

        // Convert filename to u32string for comparison
        std::u32string u32filename = Utf8ToUtf32(filename.c_str(), static_cast<unsigned int>(filename.length()));

        // Search for a document with matching name
        for (const auto& doc : allDocs) {
            if (doc->GetName() == u32filename) {
                return doc;
            }
        }

        return nullptr;
    }

    std::shared_ptr<Document> ScriptingInterface::CreateNewDocument(const std::string& filename) {
        if (!mDocumentContainer) return nullptr;

        // Create a new document
        auto doc = Document::Create();
        doc->SetSource(filename.c_str(), false);

        // Add it to the document container
        mDocumentContainer->AddDocument(doc);

        return doc;
    }

    void ScriptingInterface::DisplayError(const std::string& error) const {
        // Create or find the error document
        auto errorDoc = FindDocumentByName("Lua Error");
        if (!errorDoc) {
            errorDoc = const_cast<ScriptingInterface*>(this)->CreateNewDocument("Lua Error");
        }

        if (errorDoc) {
            // Clear existing content
            Document::Cursor start(0, 0);
            Document::Cursor end(errorDoc->GetLineCount() - 1,
                (unsigned int)errorDoc->GetLine(errorDoc->GetLineCount() - 1).text.length());
            errorDoc->SetSelection(Document::Span(start, end));
            if (errorDoc->HasSelection()) {
                errorDoc->Remove();
            }

            // Insert the error message
            std::u32string u32error = Utf8ToUtf32(error.c_str(), static_cast<unsigned int>(error.length()));
            errorDoc->Insert(u32error);
        }
    }

    void ScriptingInterface::RegisterLuaFunctions() {
        // C++ functions
        lua_register(L, "CreateCppFile", CreateCppFile);
        lua_register(L, "ReplaceCppFileContent", ReplaceCppFileContent);
        lua_register(L, "ReplaceCppClass", ReplaceCppClass);
        lua_register(L, "ReplaceCppFunction", ReplaceCppFunction);

        // JavaScript functions
        lua_register(L, "CreateJsFile", CreateJsFile);
        lua_register(L, "ReplaceJsFileContents", ReplaceJsFileContents);
        lua_register(L, "ReplaceJsClass", ReplaceJsClass);
        lua_register(L, "ReplaceJsFunction", ReplaceJsFunction);
    }

    bool ScriptingInterface::ExecuteScript(const std::string& script) {
        if (!L) return false;

        if (luaL_loadstring(L, script.c_str()) != LUA_OK) {
            // Get the error message
            const char* errorMsg = lua_tostring(L, -1);
            std::string fullError = "Lua Syntax Error:\n\n";
            fullError += errorMsg ? errorMsg : "Unknown error";

            // Create or find the error document
            auto errorDoc = FindDocumentByName("Lua Error");
            if (!errorDoc) {
                errorDoc = CreateNewDocument("Lua Error");
            }

            if (errorDoc) {
                // Clear existing content
                Document::Cursor start(0, 0);
                Document::Cursor end(errorDoc->GetLineCount() - 1,
                    (unsigned int)errorDoc->GetLine(errorDoc->GetLineCount() - 1).text.length());
                errorDoc->SetSelection(Document::Span(start, end));
                if (errorDoc->HasSelection()) {
                    errorDoc->Remove();
                }

                // Insert the error message
                std::u32string u32error = Utf8ToUtf32(fullError.c_str(), static_cast<unsigned int>(fullError.length()));
                errorDoc->Insert(u32error);
            }

            lua_pop(L, 1);
            return false;
        }

        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            // Get the error message
            const char* errorMsg = lua_tostring(L, -1);
            std::string fullError = "Lua Runtime Error:\n\n";
            fullError += errorMsg ? errorMsg : "Unknown error";

            // Create or find the error document
            auto errorDoc = FindDocumentByName("Lua Error");
            if (!errorDoc) {
                errorDoc = CreateNewDocument("Lua Error");
            }

            if (errorDoc) {
                // Clear existing content
                Document::Cursor start(0, 0);
                Document::Cursor end(errorDoc->GetLineCount() - 1,
                    (unsigned int)errorDoc->GetLine(errorDoc->GetLineCount() - 1).text.length());
                errorDoc->SetSelection(Document::Span(start, end));
                if (errorDoc->HasSelection()) {
                    errorDoc->Remove();
                }

                // Insert the error message
                std::u32string u32error = Utf8ToUtf32(fullError.c_str(), static_cast<unsigned int>(fullError.length()));
                errorDoc->Insert(u32error);
            }

            lua_pop(L, 1);
            return false;
        }

        return true;
    }

    ScriptingInterface* ScriptingInterface::GetInstance(lua_State* L) {
        lua_pushlightuserdata(L, (void*)SCRIPTING_INTERFACE_KEY);
        lua_gettable(L, LUA_REGISTRYINDEX);
        ScriptingInterface* instance = (ScriptingInterface*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        return instance;
    }

    // Helper function to report errors both to Lua and to the editor
    static int ReportError(lua_State* L, const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // Display in editor
        ScriptingInterface* si = ScriptingInterface::GetInstance(L);
        if (si) {
            std::string errorMsg = "Lua Function Error:\n\n";
            errorMsg += buffer;
            si->DisplayError(errorMsg);
        }

        // Report to Lua
        return luaL_error(L, "%s", buffer);
    }

    // Lua binding implementations

    int ScriptingInterface::CreateCppFile(lua_State* L) {
        if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
            return ReportError(L, "CreateCppFile expects exactly one string argument");
        }

        const char* filename = lua_tostring(L, 1);
        ScriptingInterface* si = GetInstance(L);

        if (!si) {
            return ReportError(L, "ScriptingInterface not initialized");
        }

        // Check if document already exists
        auto existingDoc = si->FindDocumentByName(filename);
        if (existingDoc) {
            // Document already exists, don't create a new one
            return 0;
        }

        // Create new document
        si->CreateNewDocument(filename);

        return 0;
    }

    int ScriptingInterface::ReplaceCppFileContent(lua_State* L) {
        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
            return luaL_error(L, "ReplaceCppFileContent expects two string arguments");
        }

        const char* filename = lua_tostring(L, 1);
        const char* content = lua_tostring(L, 2);

        ScriptingInterface* si = GetInstance(L);
        if (!si) {
            return luaL_error(L, "ScriptingInterface not initialized");
        }

        auto doc = si->FindDocumentByName(filename);
        if (!doc) {
            // Create new document if it doesn't exist
            doc = si->CreateNewDocument(filename);
            if (!doc) {
                return luaL_error(L, "Failed to create document '%s'", filename);
            }
        }

        // Select all content
        Document::Cursor start(0, 0);
        Document::Cursor end(doc->GetLineCount() - 1,
            (unsigned int)doc->GetLine(doc->GetLineCount() - 1).text.length());
        doc->SetSelection(Document::Span(start, end));

        // Delete existing content
        if (doc->HasSelection()) {
            doc->Remove();
        }

        // Insert new content
        std::u32string u32content = Utf8ToUtf32(content, static_cast<unsigned int>(strlen(content)));
        doc->Insert(u32content);

        return 0;
    }

    int ScriptingInterface::ReplaceCppClass(lua_State* L) {
        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
            return luaL_error(L, "ReplaceCppClass expects two string arguments");
        }

        const char* filename = lua_tostring(L, 1);
        const char* newClass = lua_tostring(L, 2);

        ScriptingInterface* si = GetInstance(L);
        if (!si) {
            return luaL_error(L, "ScriptingInterface not initialized");
        }

        auto doc = si->FindDocumentByName(filename);
        if (!doc) {
            return ReportError(L, "Document '%s' not found", filename);
        }

        // Convert new class to u32string
        std::u32string newClassU32 = Utf8ToUtf32(newClass, static_cast<unsigned int>(strlen(newClass)));

        // Extract class name from the new definition using srell
        srell::u32regex classNameRegex(UR"(class\s+(\w+))");
        srell::u32smatch match;
        std::u32string className;

        if (srell::regex_search(newClassU32, match, classNameRegex)) {
            className = match[1].str();
        }
        else {
            return luaL_error(L, "Could not extract class name from new class definition");
        }

        // Find the class in the document
        bool foundClass = false;
        Document::Cursor classStart;
        Document::Cursor classEnd;
        int braceCount = 0;
        bool inClass = false;

        // Create regex to find class declaration
        std::u32string classPattern = U"class\\s+" + className + U"\\s*(\\{|:|$)";
        srell::u32regex classRegex(classPattern);

        for (unsigned int line = 0; line < doc->GetLineCount(); ++line) {
            const std::u32string& lineText = doc->GetLine(line).text;

            if (!inClass) {
                // Look for class declaration
                if (srell::regex_search(lineText, classRegex)) {
                    classStart = Document::Cursor(line, 0);
                    inClass = true;
                    foundClass = true;
                }
            }

            if (inClass) {
                // Count braces to find the end of the class
                for (size_t i = 0; i < lineText.length(); ++i) {
                    if (lineText[i] == U'{') {
                        braceCount++;
                    }
                    else if (lineText[i] == U'}') {
                        braceCount--;
                        if (braceCount == 0) {
                            // Find the semicolon after the closing brace
                            size_t semicolonPos = i + 1;
                            while (semicolonPos < lineText.length() &&
                                (lineText[semicolonPos] == U' ' ||
                                    lineText[semicolonPos] == U'\t')) {
                                semicolonPos++;
                            }
                            if (semicolonPos < lineText.length() &&
                                lineText[semicolonPos] == U';') {
                                classEnd = Document::Cursor(line, static_cast<unsigned int>(semicolonPos + 1));
                            }
                            else {
                                classEnd = Document::Cursor(line, static_cast<unsigned int>(i + 1));
                            }
                            inClass = false;
                            break;
                        }
                    }
                }
            }

            if (foundClass && !inClass) {
                break;
            }
        }

        if (!foundClass) {
            std::string classNameU8 = Utf32ToUtf8(className);
            return ReportError(L, "Class '%s' not found in file", classNameU8.c_str());
        }

        // Replace the class
        doc->SetSelection(Document::Span(classStart, classEnd));
        doc->Remove();
        doc->Insert(newClassU32);

        return 0;
    }

    int ScriptingInterface::ReplaceCppFunction(lua_State* L) {
        if (lua_gettop(L) != 3 || !lua_isstring(L, 1) ||
            !lua_isstring(L, 2) || !lua_isstring(L, 3)) {
            return luaL_error(L, "ReplaceCppFunction expects three string arguments");
        }

        const char* filename = lua_tostring(L, 1);
        const char* functionName = lua_tostring(L, 2);
        const char* newFunction = lua_tostring(L, 3);

        ScriptingInterface* si = GetInstance(L);
        if (!si) {
            return luaL_error(L, "ScriptingInterface not initialized");
        }

        auto doc = si->FindDocumentByName(filename);
        if (!doc) {
            return luaL_error(L, "Document '%s' not found", filename);
        }

        // Convert to u32strings
        std::u32string funcNameU32 = Utf8ToUtf32(functionName, static_cast<unsigned int>(strlen(functionName)));
        std::u32string newFunctionU32 = Utf8ToUtf32(newFunction, static_cast<unsigned int>(strlen(newFunction)));

        // Parse the function name (handle namespace::class::function)
        std::u32string className;
        std::u32string funcName;

        size_t lastColon = funcNameU32.rfind(U"::");
        if (lastColon != std::u32string::npos) {
            funcName = funcNameU32.substr(lastColon + 2);
            className = funcNameU32.substr(0, lastColon);
        }
        else {
            funcName = funcNameU32;
        }

        // Create regex pattern for function
        std::u32string pattern;
        if (!className.empty()) {
            // Member function: look for ReturnType ClassName::FunctionName(
            // Handle constructors and destructors
            size_t classNameEnd = className.rfind(U"::");
            std::u32string simpleClassName = (classNameEnd != std::u32string::npos)
                ? className.substr(classNameEnd + 2)
                : className;

            if (funcName == simpleClassName) {
                // Constructor
                pattern = className + U"::" + funcName + UR"(\s*\()";
            }
            else if (funcName == U"~" + simpleClassName) {
                // Destructor
                pattern = className + U"::~" + simpleClassName + UR"(\s*\()";
            }
            else {
                // Regular member function - look for any return type
                pattern = UR"(\w+\s+)" + className + U"::" + funcName + UR"(\s*\()";
            }
        }
        else {
            // Global function
            pattern = UR"(\w+\s+)" + funcName + UR"(\s*\()";
        }

        srell::u32regex funcRegex(pattern);

        // Find the function in the document
        bool foundFunction = false;
        Document::Cursor funcStart;
        Document::Cursor funcEnd;
        int braceCount = 0;
        bool inFunction = false;
        bool foundOpenBrace = false;

        for (unsigned int line = 0; line < doc->GetLineCount(); ++line) {
            const std::u32string& lineText = doc->GetLine(line).text;

            if (!inFunction) {
                // Look for function declaration
                srell::u32smatch match;
                if (srell::regex_search(lineText, match, funcRegex)) {
                    funcStart = Document::Cursor(line, 0);
                    inFunction = true;
                    foundFunction = true;

                    // Check if opening brace is on the same line
                    size_t bracePos = lineText.find(U'{', match.position());
                    if (bracePos != std::u32string::npos) {
                        foundOpenBrace = true;
                        braceCount = 1;
                    }
                }
            }

            if (inFunction) {
                // If we haven't found the opening brace yet, look for it
                if (!foundOpenBrace) {
                    size_t bracePos = lineText.find(U'{');
                    if (bracePos != std::u32string::npos) {
                        foundOpenBrace = true;
                        braceCount = 1;
                    }
                }

                // Count braces to find the end of the function
                if (foundOpenBrace) {
                    for (size_t i = 0; i < lineText.length(); ++i) {
                        if (lineText[i] == U'{') {
                            if (braceCount > 0 || i > 0) { // Don't count the first brace again
                                braceCount++;
                            }
                        }
                        else if (lineText[i] == U'}') {
                            braceCount--;
                            if (braceCount == 0) {
                                funcEnd = Document::Cursor(line, static_cast<unsigned int>(i + 1));
                                inFunction = false;
                                break;
                            }
                        }
                    }
                }
            }

            if (foundFunction && !inFunction) {
                break;
            }
        }

        if (!foundFunction) {
            // Function not found - append it to the end of the file
            unsigned int lastLine = doc->GetLineCount() - 1;
            unsigned int lastCol = static_cast<unsigned int>(doc->GetLine(lastLine).text.length());
            doc->PlaceCursor(Document::Cursor(lastLine, lastCol));

            // Add newlines if needed
            if (lastCol > 0) {
                doc->Insert(U"\n\n");
            }

            doc->Insert(newFunctionU32);
        }
        else {
            // Replace the existing function
            doc->SetSelection(Document::Span(funcStart, funcEnd));
            doc->Remove();
            doc->Insert(newFunctionU32);
        }

        return 0;
    }

    // JavaScript Lua binding implementations

    int ScriptingInterface::CreateJsFile(lua_State* L) {
        if (lua_gettop(L) != 1 || !lua_isstring(L, 1)) {
            return luaL_error(L, "CreateJsFile expects exactly one string argument");
        }

        const char* filename = lua_tostring(L, 1);
        ScriptingInterface* si = GetInstance(L);

        if (!si) {
            return luaL_error(L, "ScriptingInterface not initialized");
        }

        // Check if document already exists
        auto existingDoc = si->FindDocumentByName(filename);
        if (existingDoc) {
            // Document already exists, don't create a new one
            return 0;
        }

        // Create new document
        si->CreateNewDocument(filename);

        return 0;
    }

    int ScriptingInterface::ReplaceJsFileContents(lua_State* L) {
        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
            return luaL_error(L, "ReplaceJsFileContents expects two string arguments");
        }

        const char* filename = lua_tostring(L, 1);
        const char* content = lua_tostring(L, 2);

        ScriptingInterface* si = GetInstance(L);
        if (!si) {
            return luaL_error(L, "ScriptingInterface not initialized");
        }

        auto doc = si->FindDocumentByName(filename);
        if (!doc) {
            // Create new document if it doesn't exist
            doc = si->CreateNewDocument(filename);
            if (!doc) {
                return luaL_error(L, "Failed to create document '%s'", filename);
            }
        }

        // Select all content
        Document::Cursor start(0, 0);
        Document::Cursor end(doc->GetLineCount() - 1,
            (unsigned int)doc->GetLine(doc->GetLineCount() - 1).text.length());
        doc->SetSelection(Document::Span(start, end));

        // Delete existing content
        if (doc->HasSelection()) {
            doc->Remove();
        }

        // Insert new content
        std::u32string u32content = Utf8ToUtf32(content, static_cast<unsigned int>(strlen(content)));
        doc->Insert(u32content);

        return 0;
    }

    int ScriptingInterface::ReplaceJsClass(lua_State* L) {
        if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
            return luaL_error(L, "ReplaceJsClass expects two string arguments");
        }

        const char* filename = lua_tostring(L, 1);
        const char* newClass = lua_tostring(L, 2);

        ScriptingInterface* si = GetInstance(L);
        if (!si) {
            return luaL_error(L, "ScriptingInterface not initialized");
        }

        auto doc = si->FindDocumentByName(filename);
        if (!doc) {
            return luaL_error(L, "Document '%s' not found", filename);
        }

        // Convert new class to u32string
        std::u32string newClassU32 = Utf8ToUtf32(newClass, static_cast<unsigned int>(strlen(newClass)));

        // Extract class name from the new definition using srell
        // JavaScript class syntax: class ClassName { or class ClassName extends BaseClass {
        srell::u32regex classNameRegex(UR"(class\s+(\w+)(?:\s+extends\s+\w+)?\s*\{)");
        srell::u32smatch match;
        std::u32string className;

        if (srell::regex_search(newClassU32, match, classNameRegex)) {
            className = match[1].str();
        }
        else {
            return luaL_error(L, "Could not extract class name from new class definition");
        }

        // Find the class in the document
        bool foundClass = false;
        Document::Cursor classStart;
        Document::Cursor classEnd;
        int braceCount = 0;
        bool inClass = false;

        // Create regex to find class declaration
        std::u32string classPattern = U"class\\s+" + className + U"(?:\\s+extends\\s+\\w+)?\\s*\\{";
        srell::u32regex classRegex(classPattern);

        for (unsigned int line = 0; line < doc->GetLineCount(); ++line) {
            const std::u32string& lineText = doc->GetLine(line).text;

            if (!inClass) {
                // Look for class declaration
                if (srell::regex_search(lineText, classRegex)) {
                    classStart = Document::Cursor(line, 0);
                    inClass = true;
                    foundClass = true;
                    // Start brace count at 1 since we found the opening brace
                    braceCount = 1;
                }
            }

            if (inClass) {
                // Count braces to find the end of the class
                for (size_t i = 0; i < lineText.length(); ++i) {
                    if (lineText[i] == U'{') {
                        // Don't count the first brace again
                        if (!(line == classStart.line && i == 0)) {
                            braceCount++;
                        }
                    }
                    else if (lineText[i] == U'}') {
                        braceCount--;
                        if (braceCount == 0) {
                            // In JavaScript, classes might have a semicolon after the closing brace
                            size_t endPos = i + 1;
                            // Skip whitespace
                            while (endPos < lineText.length() &&
                                (lineText[endPos] == U' ' ||
                                    lineText[endPos] == U'\t')) {
                                endPos++;
                            }
                            // Include semicolon if present
                            if (endPos < lineText.length() && lineText[endPos] == U';') {
                                endPos++;
                            }
                            classEnd = Document::Cursor(line, static_cast<unsigned int>(endPos));
                            inClass = false;
                            break;
                        }
                    }
                }
            }

            if (foundClass && !inClass) {
                break;
            }
        }

        if (!foundClass) {
            std::string classNameU8 = Utf32ToUtf8(className);
            return luaL_error(L, "Class '%s' not found in file", classNameU8.c_str());
        }

        // Replace the class
        doc->SetSelection(Document::Span(classStart, classEnd));
        doc->Remove();
        doc->Insert(newClassU32);

        return 0;
    }

    int ScriptingInterface::ReplaceJsFunction(lua_State* L) {
        int numArgs = lua_gettop(L);
        if (numArgs != 4 || !lua_isstring(L, 1) || !lua_isstring(L, 2) ||
            !lua_isstring(L, 3) || !lua_isstring(L, 4)) {
            return ReportError(L, "ReplaceJsFunction expects four string arguments (filename, className, functionName, newFunction)");
        }

        const char* filename = lua_tostring(L, 1);
        const char* className = lua_tostring(L, 2);
        const char* functionName = lua_tostring(L, 3);
        const char* newFunction = lua_tostring(L, 4);

        ScriptingInterface* si = GetInstance(L);
        if (!si) {
            return ReportError(L, "ScriptingInterface not initialized");
        }

        auto doc = si->FindDocumentByName(filename);
        if (!doc) {
            return ReportError(L, "Document '%s' not found", filename);
        }

        // Convert to u32strings
        std::u32string classNameU32 = (className && strlen(className) > 0)
            ? Utf8ToUtf32(className, static_cast<unsigned int>(strlen(className)))
            : U"";
        std::u32string funcNameU32 = Utf8ToUtf32(functionName, static_cast<unsigned int>(strlen(functionName)));
        std::u32string newFunctionU32 = Utf8ToUtf32(newFunction, static_cast<unsigned int>(strlen(newFunction)));

        bool foundFunction = false;
        Document::Cursor funcStart;
        Document::Cursor funcEnd;

        if (!classNameU32.empty()) {
            // Search for function within a class
            // First find the class
            std::u32string classPattern = U"class\\s+" + classNameU32 + U"(?:\\s+extends\\s+\\w+)?\\s*\\{";
            srell::u32regex classRegex(classPattern);

            bool foundClassDeclaration = false;
            unsigned int classStartLine = 0;
            unsigned int classEndLine = 0;

            // First pass: find the class boundaries
            for (unsigned int line = 0; line < doc->GetLineCount(); ++line) {
                const std::u32string& lineText = doc->GetLine(line).text;

                if (!foundClassDeclaration) {
                    srell::u32smatch classMatch;
                    if (srell::regex_search(lineText, classMatch, classRegex)) {
                        foundClassDeclaration = true;
                        classStartLine = line;

                        // Find where the class ends by counting braces
                        int braceCount = 0;
                        bool foundOpenBrace = false;

                        // Check for opening brace on the same line
                        size_t bracePos = lineText.find(U'{', classMatch.position());
                        if (bracePos != std::u32string::npos) {
                            foundOpenBrace = true;
                            braceCount = 1;

                            // Continue counting on this line
                            for (size_t i = bracePos + 1; i < lineText.length(); ++i) {
                                if (lineText[i] == U'{') {
                                    braceCount++;
                                }
                                else if (lineText[i] == U'}') {
                                    braceCount--;
                                    if (braceCount == 0) {
                                        classEndLine = line;
                                        break;
                                    }
                                }
                            }
                        }

                        // If class not closed yet, continue on next lines
                        if (braceCount > 0 || !foundOpenBrace) {
                            for (unsigned int scanLine = line + 1; scanLine < doc->GetLineCount(); ++scanLine) {
                                const std::u32string& scanLineText = doc->GetLine(scanLine).text;

                                if (!foundOpenBrace) {
                                    // Still looking for opening brace
                                    size_t openPos = scanLineText.find(U'{');
                                    if (openPos != std::u32string::npos) {
                                        foundOpenBrace = true;
                                        braceCount = 1;
                                        // Continue from after the brace
                                        for (size_t i = openPos + 1; i < scanLineText.length(); ++i) {
                                            if (scanLineText[i] == U'{') {
                                                braceCount++;
                                            }
                                            else if (scanLineText[i] == U'}') {
                                                braceCount--;
                                                if (braceCount == 0) {
                                                    classEndLine = scanLine;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                                else {
                                    // Count braces
                                    for (size_t i = 0; i < scanLineText.length(); ++i) {
                                        if (scanLineText[i] == U'{') {
                                            braceCount++;
                                        }
                                        else if (scanLineText[i] == U'}') {
                                            braceCount--;
                                            if (braceCount == 0) {
                                                classEndLine = scanLine;
                                                break;
                                            }
                                        }
                                    }
                                }

                                if (braceCount == 0) break;
                            }
                        }
                        break;
                    }
                }
            }

            if (!foundClassDeclaration) {
                std::string classNameU8 = Utf32ToUtf8(classNameU32);
                return ReportError(L, "Class '%s' not found in file", classNameU8.c_str());
            }

            // Second pass: look for the function within the class boundaries
            for (unsigned int line = classStartLine; line <= classEndLine && line < doc->GetLineCount(); ++line) {
                const std::u32string& lineText = doc->GetLine(line).text;

                // Check if this line contains the start of our function
                size_t funcNamePos = lineText.find(funcNameU32);
                if (funcNamePos != std::u32string::npos) {
                    // Found the function name, now check if it's actually a function declaration
                    // Look for the pattern from the function name position
                    std::u32string lineFromFuncName = lineText.substr(funcNamePos);

                    // Simple pattern: functionName followed by optional whitespace, then '('
                    std::u32string simpleFuncPattern = funcNameU32 + UR"(\s*\()";
                    srell::u32regex simpleFuncRegex(simpleFuncPattern);

                    if (srell::regex_search(lineFromFuncName, simpleFuncRegex)) {
                        // This looks like our function, check for async/static/etc before it
                        bool isValidMethod = true;

                        // Check what comes before the function name
                        if (funcNamePos > 0) {
                            std::u32string beforeFunc = lineText.substr(0, funcNamePos);
                            // Trim trailing whitespace
                            while (!beforeFunc.empty() && (beforeFunc.back() == U' ' || beforeFunc.back() == U'\t')) {
                                beforeFunc.pop_back();
                            }

                            // Check if it ends with a valid method modifier or is empty
                            if (!beforeFunc.empty()) {
                                // Check if it ends with async, static, get, set, or just whitespace
                                srell::u32regex modifierRegex(UR"((?:^|\s)(?:async|static|get|set)\s*$)");
                                if (!srell::regex_search(beforeFunc, modifierRegex)) {
                                    // There's something else before the function name that's not a valid modifier
                                    isValidMethod = false;
                                }
                            }
                        }

                        if (isValidMethod) {
                            funcStart = Document::Cursor(line, 0);
                            foundFunction = true;

                            // Find the opening brace for this function
                            size_t bracePos = lineText.find(U'{', funcNamePos);
                            if (bracePos == std::u32string::npos) {
                                // Opening brace might be on the next line(s)
                                unsigned int searchLine = line + 1;
                                while (searchLine < doc->GetLineCount() && searchLine <= classEndLine) {
                                    const std::u32string& nextLineText = doc->GetLine(searchLine).text;
                                    bracePos = nextLineText.find(U'{');
                                    if (bracePos != std::u32string::npos) {
                                        // Found the opening brace on a subsequent line
                                        // Start counting from here
                                        int funcBraceCount = 1;

                                        // Process rest of this line
                                        for (size_t i = bracePos + 1; i < nextLineText.length(); ++i) {
                                            if (nextLineText[i] == U'{') {
                                                funcBraceCount++;
                                            }
                                            else if (nextLineText[i] == U'}') {
                                                funcBraceCount--;
                                                if (funcBraceCount == 0) {
                                                    funcEnd = Document::Cursor(searchLine, static_cast<unsigned int>(i + 1));
                                                    break;
                                                }
                                            }
                                        }

                                        if (funcBraceCount > 0) {
                                            // Continue searching on next lines
                                            searchLine++;
                                            while (searchLine < doc->GetLineCount() && funcBraceCount > 0) {
                                                const std::u32string& searchLineText = doc->GetLine(searchLine).text;
                                                for (size_t i = 0; i < searchLineText.length(); ++i) {
                                                    if (searchLineText[i] == U'{') {
                                                        funcBraceCount++;
                                                    }
                                                    else if (searchLineText[i] == U'}') {
                                                        funcBraceCount--;
                                                        if (funcBraceCount == 0) {
                                                            funcEnd = Document::Cursor(searchLine, static_cast<unsigned int>(i + 1));
                                                            break;
                                                        }
                                                    }
                                                }
                                                searchLine++;
                                            }
                                        }
                                        break;
                                    }
                                    searchLine++;
                                }
                            }
                            else {
                                // Opening brace is on the same line
                                int funcBraceCount = 1;

                                // Process rest of this line
                                for (size_t i = bracePos + 1; i < lineText.length(); ++i) {
                                    if (lineText[i] == U'{') {
                                        funcBraceCount++;
                                    }
                                    else if (lineText[i] == U'}') {
                                        funcBraceCount--;
                                        if (funcBraceCount == 0) {
                                            funcEnd = Document::Cursor(line, static_cast<unsigned int>(i + 1));
                                            break;
                                        }
                                    }
                                }

                                if (funcBraceCount > 0) {
                                    // Continue searching on next lines
                                    unsigned int searchLine = line + 1;
                                    while (searchLine < doc->GetLineCount() && funcBraceCount > 0) {
                                        const std::u32string& searchLineText = doc->GetLine(searchLine).text;
                                        for (size_t i = 0; i < searchLineText.length(); ++i) {
                                            if (searchLineText[i] == U'{') {
                                                funcBraceCount++;
                                            }
                                            else if (searchLineText[i] == U'}') {
                                                funcBraceCount--;
                                                if (funcBraceCount == 0) {
                                                    funcEnd = Document::Cursor(searchLine, static_cast<unsigned int>(i + 1));
                                                    break;
                                                }
                                            }
                                        }
                                        searchLine++;
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }

            if (!foundFunction) {
                // Need to insert the function before the class closing brace
                // The closing brace is at classEndLine
                const std::u32string& endLineText = doc->GetLine(classEndLine).text;
                for (size_t i = 0; i < endLineText.length(); ++i) {
                    if (endLineText[i] == U'}') {
                        // Found the closing brace
                        doc->PlaceCursor(Document::Cursor(classEndLine, static_cast<unsigned int>(i)));
                        doc->Insert(U"\n    " + newFunctionU32 + U"\n");
                        return 0;
                    }
                }

                std::string classNameU8 = Utf32ToUtf8(classNameU32);
                return ReportError(L, "Could not find closing brace for class '%s'", classNameU8.c_str());
            }
        }
        else {
            // Search for global function
            std::u32string funcPattern1 = U"function\\s+" + funcNameU32 + UR"(\s*\([^)]*\)\s*\{)";
            std::u32string funcPattern2 = U"(?:const|let|var)\\s+" + funcNameU32 + UR"(\s*=\s*function\s*\([^)]*\)\s*\{)";
            std::u32string funcPattern3 = U"(?:const|let|var)\\s+" + funcNameU32 + UR"(\s*=\s*\([^)]*\)\s*=>\s*\{)";

            srell::u32regex funcRegex1(funcPattern1);
            srell::u32regex funcRegex2(funcPattern2);
            srell::u32regex funcRegex3(funcPattern3);

            for (unsigned int line = 0; line < doc->GetLineCount() && !foundFunction; ++line) {
                const std::u32string& lineText = doc->GetLine(line).text;
                srell::u32smatch match;

                bool matched = false;
                size_t bracePos = std::u32string::npos;

                if (srell::regex_search(lineText, match, funcRegex1) ||
                    srell::regex_search(lineText, match, funcRegex2) ||
                    srell::regex_search(lineText, match, funcRegex3)) {
                    matched = true;
                    bracePos = lineText.find(U'{', match.position() + match.length() - 1);
                }

                if (matched && bracePos != std::u32string::npos) {
                    funcStart = Document::Cursor(line, 0);
                    foundFunction = true;

                    // Find the end by counting braces
                    int funcBraceCount = 1;

                    // Process rest of current line
                    for (size_t i = bracePos + 1; i < lineText.length(); ++i) {
                        if (lineText[i] == U'{') {
                            funcBraceCount++;
                        }
                        else if (lineText[i] == U'}') {
                            funcBraceCount--;
                            if (funcBraceCount == 0) {
                                funcEnd = Document::Cursor(line, static_cast<unsigned int>(i + 1));
                                break;
                            }
                        }
                    }

                    // Continue on next lines if needed
                    if (funcBraceCount > 0) {
                        unsigned int searchLine = line + 1;
                        while (searchLine < doc->GetLineCount()) {
                            const std::u32string& searchLineText = doc->GetLine(searchLine).text;
                            for (size_t i = 0; i < searchLineText.length(); ++i) {
                                if (searchLineText[i] == U'{') {
                                    funcBraceCount++;
                                }
                                else if (searchLineText[i] == U'}') {
                                    funcBraceCount--;
                                    if (funcBraceCount == 0) {
                                        funcEnd = Document::Cursor(searchLine, static_cast<unsigned int>(i + 1));
                                        break;
                                    }
                                }
                            }
                            if (funcBraceCount == 0) break;
                            searchLine++;
                        }
                    }
                }
            }
        }

        if (!foundFunction) {
            // Function not found - append to end of file for global functions
            if (classNameU32.empty()) {
                unsigned int lastLine = doc->GetLineCount() - 1;
                unsigned int lastCol = static_cast<unsigned int>(doc->GetLine(lastLine).text.length());
                doc->PlaceCursor(Document::Cursor(lastLine, lastCol));

                if (lastCol > 0) {
                    doc->Insert(U"\n\n");
                }

                doc->Insert(newFunctionU32);
            }
            // For class methods, the insertion is handled above when we find the class closing brace
        }
        else {
            // Replace the existing function
            doc->SetSelection(Document::Span(funcStart, funcEnd));
            doc->Remove();
            doc->Insert(newFunctionU32);
        }

        return 0;
    }

} // namespace TextEdit