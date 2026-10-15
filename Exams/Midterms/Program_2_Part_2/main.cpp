#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/glfw_context.h"
#include "core/input.h"
#include "core/window.h"

namespace {

constexpr int kCircleSegments = 40;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kChainPointCount = 10;
constexpr std::size_t kShapePointCount = 5;
constexpr float kPointRadius = 0.03f;
constexpr float kShapeRadius = 0.18f;
constexpr float kPlayAreaSize = 600.0f;
constexpr double kFixedDeltaTime = 1.0 / 60.0;
constexpr float kGravity = -1.8f;
constexpr int kConstraintIterations = 5;

constexpr const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

constexpr const char* kFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; }
)";

struct SceneMesh {
    std::vector<float> vertices;
    GLsizei borderFirst = 0;
    GLsizei borderVertexCount = 0;
    GLsizei sticksFirst = 0;
    GLsizei sticksVertexCount = 0;
    GLsizei pointsFirst = 0;
};

struct Particle {
    float x;
    float y;
    float previousX;
    float previousY;
};

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compilation failed: ") + log);
    }
    return shader;
}

GLuint createProgram() {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    GLuint fragmentShader = 0;
    GLuint program = 0;

    try {
        fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
        program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) {
            char log[512];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("Program linking failed: ") + log);
        }
    } catch (...) {
        if (program != 0) {
            glDeleteProgram(program);
        }
        if (fragmentShader != 0) {
            glDeleteShader(fragmentShader);
        }
        glDeleteShader(vertexShader);
        throw;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

void appendCircle(SceneMesh& mesh, float centerX, float centerY, float radius) {
    mesh.vertices.push_back(centerX);
    mesh.vertices.push_back(centerY);

    for (int i = 0; i <= kCircleSegments; ++i) {
        const float angle = static_cast<float>(i) / kCircleSegments * 2.0f * kPi;
        mesh.vertices.push_back(centerX + radius * std::cos(angle));
        mesh.vertices.push_back(centerY + radius * std::sin(angle));
    }
}

using Chain = std::array<Particle, kChainPointCount>;
using Shape = std::array<Particle, kShapePointCount>;

void appendLine(SceneMesh& mesh, float x1, float y1, float x2, float y2) {
    mesh.vertices.insert(mesh.vertices.end(), {x1, y1, x2, y2});
}

SceneMesh createSceneMesh(const Chain& chain, const Shape& shape) {
    SceneMesh mesh;
    constexpr std::size_t chainStickCount = kChainPointCount - 1;
    constexpr std::size_t shapeStickCount = kShapePointCount * (kShapePointCount - 1) / 2;
    mesh.vertices.reserve(
        16 +
        (chain.size() + shape.size()) * (kCircleSegments + 2) * 2 +
        (chainStickCount + shapeStickCount) * 4);

    mesh.borderFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    appendLine(mesh, -1.0f, -1.0f, 1.0f, -1.0f);
    appendLine(mesh, 1.0f, -1.0f, 1.0f, 1.0f);
    appendLine(mesh, 1.0f, 1.0f, -1.0f, 1.0f);
    appendLine(mesh, -1.0f, 1.0f, -1.0f, -1.0f);
    mesh.borderVertexCount = 8;

    mesh.sticksFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        appendLine(mesh, chain[i].x, chain[i].y, chain[i + 1].x, chain[i + 1].y);
    }
    for (std::size_t i = 0; i < shape.size(); ++i) {
        for (std::size_t j = i + 1; j < shape.size(); ++j) {
            appendLine(mesh, shape[i].x, shape[i].y, shape[j].x, shape[j].y);
        }
    }
    mesh.sticksVertexCount =
        static_cast<GLsizei>((chainStickCount + shapeStickCount) * 2);

    mesh.pointsFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    for (const Particle& point : chain) {
        appendCircle(mesh, point.x, point.y, kPointRadius);
    }
    for (const Particle& point : shape) {
        appendCircle(mesh, point.x, point.y, kPointRadius);
    }
    return mesh;
}

void createMeshBuffers(GLuint& vao, GLuint& vbo) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void uploadMesh(GLuint vbo, const SceneMesh& mesh) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
        mesh.vertices.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void integrate(Particle& point, float deltaTime) {
    const float velocityX = point.x - point.previousX;
    const float velocityY = point.y - point.previousY;
    point.previousX = point.x;
    point.previousY = point.y;
    point.x += velocityX;
    point.y += velocityY + kGravity * deltaTime * deltaTime;
}

void clampToPlayArea(Particle& point) {
    constexpr float minimum = -1.0f + kPointRadius;
    constexpr float maximum = 1.0f - kPointRadius;
    const float clampedX = std::clamp(point.x, minimum, maximum);
    const float clampedY = std::clamp(point.y, minimum, maximum);
    if (point.x != clampedX || point.y != clampedY) {
        point.x = clampedX;
        point.y = clampedY;
        point.previousX = clampedX;
        point.previousY = clampedY;
    }
}

void constrainDistance(
    Particle& a,
    Particle& b,
    float restLength,
    bool aPinned = false,
    bool bPinned = false) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 1.0e-6f || (aPinned && bPinned)) {
        return;
    }

    const float correction = (distance - restLength) / distance;
    const float halfCorrection = 0.5f * correction;
    if (!aPinned) {
        a.x += dx * halfCorrection;
        a.y += dy * halfCorrection;
    }
    if (!bPinned) {
        b.x -= dx * halfCorrection;
        b.y -= dy * halfCorrection;
    }
}

}  // namespace

int main() {
    try {
        gfx::GlfwContext glfw;
        gfx::Window window({.width = 600, .height = 600, .title = "Midterm - Reyes"});

        const GLuint program = createProgram();
        const GLint colorLocation = glGetUniformLocation(program, "uColor");
        GLuint vao = 0;
        GLuint vbo = 0;
        createMeshBuffers(vao, vbo);

        Chain chain{};
        for (std::size_t i = 0; i < chain.size(); ++i) {
            const float x = -0.2f + 0.08f * static_cast<float>(i);
            chain[i] = {x, 0.9f, x, 0.9f};
        }

        Shape shape{};
        for (std::size_t i = 0; i < shape.size(); ++i) {
            const float angle = (90.0f + 72.0f * static_cast<float>(i)) * kPi / 180.0f;
            const float x = 0.55f + kShapeRadius * std::cos(angle);
            const float y = 0.55f + kShapeRadius * std::sin(angle);
            shape[i] = {x, y, x, y};
        }

        GLFWwindow* nativeWindow = glfwGetCurrentContext();
        int draggedPoint = -1;
        bool wasMouseDown = false;
        double lastFrameTime = glfwGetTime();
        double simulationAccumulator = 0.0;

        glClearColor(0.05f, 0.35f, 0.15f, 1.0f);

        while (!window.shouldClose()) {
            const double currentFrameTime = glfwGetTime();
            const double frameDeltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            gfx::processInput(window);

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(nativeWindow, &cursorX, &cursorY);
            const float mouseX = 2.0f * static_cast<float>(cursorX) / kPlayAreaSize - 1.0f;
            const float mouseY = 1.0f - 2.0f * static_cast<float>(cursorY) / kPlayAreaSize;
            const bool mouseDown =
                glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            if (mouseDown && !wasMouseDown) {
                float nearestDistanceSquared = 0.2f * 0.2f;
                draggedPoint = -1;
                for (std::size_t i = 0; i < chain.size(); ++i) {
                    const float dx = chain[i].x - mouseX;
                    const float dy = chain[i].y - mouseY;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared < nearestDistanceSquared) {
                        nearestDistanceSquared = distanceSquared;
                        draggedPoint = static_cast<int>(i);
                    }
                }
                for (std::size_t i = 0; i < shape.size(); ++i) {
                    const float dx = shape[i].x - mouseX;
                    const float dy = shape[i].y - mouseY;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared < nearestDistanceSquared) {
                        nearestDistanceSquared = distanceSquared;
                        draggedPoint = static_cast<int>(chain.size() + i);
                    }
                }
            }
            if (!mouseDown) {
                draggedPoint = -1;
            }

            if (draggedPoint >= 0) {
                Particle& point = draggedPoint < static_cast<int>(chain.size())
                    ? chain[static_cast<std::size_t>(draggedPoint)]
                    : shape[static_cast<std::size_t>(draggedPoint) - chain.size()];
                point.x = mouseX;
                point.y = mouseY;
                if (draggedPoint >= static_cast<int>(chain.size())) {
                    clampToPlayArea(point);
                }
                point.previousX = point.x;
                point.previousY = point.y;
            }
            wasMouseDown = mouseDown;

            simulationAccumulator += std::min(frameDeltaTime, 0.1);
            while (simulationAccumulator >= kFixedDeltaTime) {
                const float deltaTime = static_cast<float>(kFixedDeltaTime);
                for (std::size_t i = 1; i < chain.size(); ++i) {
                    if (draggedPoint != static_cast<int>(i)) {
                        integrate(chain[i], deltaTime);
                    }
                }
                for (std::size_t i = 0; i < shape.size(); ++i) {
                    if (draggedPoint != static_cast<int>(chain.size() + i)) {
                        integrate(shape[i], deltaTime);
                        clampToPlayArea(shape[i]);
                    }
                }

                for (int iteration = 0; iteration < kConstraintIterations; ++iteration) {
                    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
                        const bool firstPinned = i == 0 && draggedPoint != 0;
                        const bool secondPinned = draggedPoint == static_cast<int>(i + 1);
                        constrainDistance(
                            chain[i],
                            chain[i + 1],
                            0.08f,
                            firstPinned || draggedPoint == static_cast<int>(i),
                            secondPinned);
                    }
                    for (std::size_t i = 0; i < shape.size(); ++i) {
                        for (std::size_t j = i + 1; j < shape.size(); ++j) {
                            const std::size_t separation =
                                std::min(j - i, shape.size() - (j - i));
                            const float restLength =
                                2.0f * kShapeRadius *
                                std::sin(static_cast<float>(separation) * kPi /
                                         static_cast<float>(kShapePointCount));
                            constrainDistance(
                                shape[i],
                                shape[j],
                                restLength,
                                draggedPoint == static_cast<int>(chain.size() + i),
                                draggedPoint == static_cast<int>(chain.size() + j));
                        }
                    }
                    for (Particle& point : shape) {
                        clampToPlayArea(point);
                    }
                }
                simulationAccumulator -= kFixedDeltaTime;
            }

            const SceneMesh mesh = createSceneMesh(chain, shape);
            uploadMesh(vbo, mesh);

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(program);
            glBindVertexArray(vao);

            glUniform4f(colorLocation, 0.55f, 0.55f, 0.62f, 1.0f);
            glDrawArrays(GL_LINES, mesh.borderFirst, mesh.borderVertexCount);

            glUniform4f(colorLocation, 0.31f, 0.76f, 0.97f, 1.0f);
            glDrawArrays(GL_LINES, mesh.sticksFirst, mesh.sticksVertexCount);

            glUniform4f(colorLocation, 1.0f, 1.0f, 0.0f, 1.0f);
            const GLsizei pointVertexCount = kCircleSegments + 2;
            for (GLsizei i = 0;
                 i < static_cast<GLsizei>(chain.size() + shape.size());
                 ++i) {
                glDrawArrays(
                    GL_TRIANGLE_FAN,
                    mesh.pointsFirst + i * pointVertexCount,
                    pointVertexCount);
            }

            window.swapBuffers();
            window.pollEvents();
        }

        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(program);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}