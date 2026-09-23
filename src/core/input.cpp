#include "core/input.h"

#include <GLFW/glfw3.h>

#include "core/window.h"

namespace gfx {

void processInput(Window& window) {
    if (window.isKeyPressed(GLFW_KEY_ESCAPE)) {
        window.close();
    }
}

}  // namespace gfx
