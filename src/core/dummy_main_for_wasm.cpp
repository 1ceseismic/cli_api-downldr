// src/core/dummy_main_for_wasm.cpp
// This file is used to satisfy CMake's add_executable requirement when building a Wasm module
// that is primarily a library of functions rather than a standalone application.
// If --no-entry is used effectively in CMAKE_EXE_LINKER_FLAGS, this main might not even be included
// in the final Wasm, or it could be empty.

// int main() { return 0; } // Only if --no-entry isn't fully effective for library-style modules
// For many Emscripten versions, even with --no-entry for a module,
// emcc/CMake might still expect at least one source file for the add_executable target.
// An empty file or a file with just comments is usually sufficient.
// If linking errors occur related to `main`, uncommenting the empty main might be a fallback.
