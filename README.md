# OpenGL Setup

A verified base environment for OpenGL development in C++. GLFW and GLAD are
already vendored and the VS Code / Cursor build tasks are wired up. All you
need to do is confirm your local toolchain can compile and run it. If
`src/main.cpp` builds and opens a teal window that closes when you press
`Esc`, your environment is ready for OpenGL work.

The window, GLFW and GLAD setup, and input handling are already done in
`src/core/`, so `src/main.cpp` is an empty render loop you can start drawing
in.

## Prerequisites

- **MinGW-w64 via MSYS2**. Install from [msys2.org](https://www.msys2.org/),
  then install the `mingw-w64-x86_64-gcc` toolchain (or equivalent) from
  within the MSYS2 shell.

## Vendored dependencies

`include/glad`, `include/GLFW`, `include/KHR`, `lib/libglfw3dll.a`, and the
root `glfw3.dll` are already included in this repo. You shouldn't need to
reinstall anything, but here's where they came from in case a dependency
ever needs to be regenerated or replaced:

- **GLAD**, generated at [glad.dav1d.de](https://glad.dav1d.de/) with:
  - Language: `C/C++`
  - API: `gl` -> Version `3.3`
  - Profile: `Core`
  - All other APIs: `None`
  - "Omit KHR": unchecked
- **GLFW**, downloaded from the
  [GLFW downloads page](https://www.glfw.org/download.html). Use the Windows
  precompiled binaries, and pull the files from the `lib-mingw-w64` folder
  specifically.

## Setup

1. Clone this repo.
2. Open `.vscode/tasks.json` and edit the `"command"` field to point at your
   own `g++.exe` (e.g. `C:/msys64/mingw64/bin/g++.exe`).
3. Run the `Build Main` task, then run `main.exe`.
4. Confirm a teal window opens and `Esc` closes it.
5. Optionally run the `Build Triangle Example` task and `triangle.exe` to
   see an orange triangle.

## Project structure

```
src/
  main.cpp                empty boilerplate: window, clear color, input, loop
  glad.c                  vendored GLAD loader
  core/
    glfw_context.h/.cpp   gfx::GlfwContext, initializes and terminates GLFW
    window.h/.cpp         gfx::Window, creates the window and loads GLAD
    input.h/.cpp          gfx::processInput, keyboard handling
  examples/
    triangle.cpp          triangle rendered on top of core/
```

Everything in `core/` lives in the `gfx` namespace and uses RAII: an object
sets up its resource in its constructor and releases it in its destructor, so
cleanup happens on every exit path. Startup failures throw
`std::runtime_error`, which `main` catches and prints.

To start a new exercise, put your rendering code inside the loop in
`src/main.cpp`. You shouldn't need to edit `src/core/`.

The project builds as C++20 (`-std=c++20`).

## PATH requirement

Besides pointing `tasks.json` at your `g++.exe`, you also need to add your
compiler's `bin` folder (e.g. `C:\msys64\mingw64\bin`) to your system `PATH`
environment variable.

This is separate from the `tasks.json` compiler path. The `tasks.json`
`"command"` only tells VS Code/Cursor which executable to launch directly.
It doesn't help the processes that compiler spawns internally (such as
`cc1plus.exe`) find their own DLL dependencies. Those child processes
resolve their DLLs through the system PATH, so without this step the build
can fail even though `tasks.json` looks correctly configured.

## Troubleshooting

- **Build fails silently, with no error text at all.** This is almost
  always the missing PATH entry above. Add the compiler's `bin` folder to
  your system PATH and try again.
- **Program compiles but won't run, or fails to launch immediately.**
  `libglfw3dll.a` is an import library: it satisfies the linker, but the
  actual GLFW code lives in `glfw3.dll` at runtime. That DLL has to sit next
  to the built `.exe` (it already does, in the repo root), or the program
  won't start even though it compiled fine.
# opengl-cpp-setup
