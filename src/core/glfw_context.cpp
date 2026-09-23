#include "core/glfw_context.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

namespace {

void onGlfwError(int code, const char* description) {
    std::cerr << "GLFW error " << code << ": " << description << '\n';
}

}  // namespace

namespace gfx {

GlfwContext::GlfwContext() {
    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
}

GlfwContext::~GlfwContext() {
    glfwTerminate();
}

}  // namespace gfx
