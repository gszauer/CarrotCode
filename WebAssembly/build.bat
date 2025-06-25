copy Template\\manifest.json Build\\manifest.json
copy Template\\sw.js Build\\sw.js
copy Template\\icon.png Build\\icon.png

emcc wasm.cpp -o Build/index.html --shell-file="Template/shell.html"  ^
     -fno-rtti -fno-exceptions  ^
     -sUSE_WEBGL2 -sUSE_SDL=2 -sFULL_ES3=1 ^
     -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912    ^
     -sEXPORTED_FUNCTIONS="['_main', '_malloc', '_free', '_emscripten_file_dropped', '_TriggerSaveAsCallback', '_TriggerSelectFileCallback', '_TriggerYesNoCallback', '_AllocateMemory', '_FreeMemory', '_CopyToMemory']" ^
     -sEXPORTED_RUNTIME_METHODS=setValue,ccall,cwrap,UTF8ToString