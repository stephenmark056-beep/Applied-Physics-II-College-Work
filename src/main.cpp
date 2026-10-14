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
constexpr float kPointRadius = 0.03f;
constexpr float kFloorY = -1.0f + kPointRadius;
constexpr double kFixedDeltaTime = 1.0 / 60.0;
constexpr float kGravity = -1.8f;
constexpr int kConstraintIterations = 5;
constexpr float kPlayAreaSize = 600.0f;

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
    GLsizei borderCount = 0;
    GLsizei sticksFirst = 0;
    GLsizei sticksCount = 0;
    GLsizei chainSticksCount = 0;
    GLsizei pointsFirst = 0;
    GLsizei pointsCount = 0;
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

using Chain = std::array<Particle, 5>;
using Wheel = std::array<Particle, 9>;

void appendLine(SceneMesh& mesh, float x1, float y1, float x2, float y2) {
    mesh.vertices.insert(mesh.vertices.end(), {x1, y1, x2, y2});
}

SceneMesh createSceneMesh(const Chain& chain, const Wheel& wheel) {
    SceneMesh mesh;
    mesh.vertices.reserve((kCircleSegments + 2) * (chain.size() + wheel.size()) * 2 + 72);

    mesh.borderFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    appendLine(mesh, -1.0f, -1.0f, 1.0f, -1.0f);
    appendLine(mesh, 1.0f, -1.0f, 1.0f, 1.0f);
    appendLine(mesh, 1.0f, 1.0f, -1.0f, 1.0f);
    appendLine(mesh, -1.0f, 1.0f, -1.0f, -1.0f);
    mesh.borderCount = static_cast<GLsizei>(mesh.vertices.size() / 2) - mesh.borderFirst;

    mesh.sticksFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        appendLine(mesh, chain[i].x, chain[i].y, chain[i + 1].x, chain[i + 1].y);
    }
    mesh.chainSticksCount = static_cast<GLsizei>((chain.size() - 1) * 2);
    for (std::size_t i = 1; i < wheel.size(); ++i) {
        appendLine(mesh, wheel[0].x, wheel[0].y, wheel[i].x, wheel[i].y);
        const std::size_t next = i == wheel.size() - 1 ? 1 : i + 1;
        appendLine(mesh, wheel[i].x, wheel[i].y, wheel[next].x, wheel[next].y);
    }
    const float pinX = chain[0].x;
    const float pinY = chain[0].y;
    appendLine(mesh, pinX - 0.025f, pinY, pinX + 0.025f, pinY);
    appendLine(mesh, pinX, pinY - 0.025f, pinX, pinY + 0.025f);
    mesh.sticksCount = static_cast<GLsizei>(mesh.vertices.size() / 2) - mesh.sticksFirst;

    mesh.pointsFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    for (const Particle& point : chain) {
        appendCircle(mesh, point.x, point.y, kPointRadius);
    }
    for (const Particle& point : wheel) {
        appendCircle(mesh, point.x, point.y, kPointRadius);
    }
    mesh.pointsCount = static_cast<GLsizei>(mesh.vertices.size() / 2) - mesh.pointsFirst;
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

void clampToFloor(Particle& point) {
    if (point.y < kFloorY) {
        point.y = kFloorY;
        point.previousY = kFloorY;
    }
}

void bounceWheelAtWalls(Particle& point) {
    constexpr float minimum = -1.0f + kPointRadius;
    constexpr float maximum = 1.0f - kPointRadius;

    if (point.x < minimum) {
        const float incomingVelocity = point.x - point.previousX;
        point.x = minimum;
        point.previousX = point.x + incomingVelocity;
    } else if (point.x > maximum) {
        const float incomingVelocity = point.x - point.previousX;
        point.x = maximum;
        point.previousX = point.x + incomingVelocity;
    }

    if (point.y < minimum) {
        const float incomingVelocity = point.y - point.previousY;
        point.y = minimum;
        point.previousY = point.y + incomingVelocity;
    } else if (point.y > maximum) {
        const float incomingVelocity = point.y - point.previousY;
        point.y = maximum;
        point.previousY = point.y + incomingVelocity;
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

void separateOverlappingWheelPoints(Wheel& wheel, int pinnedPoint) {
    constexpr float minimumDistance = 2.0f * kPointRadius;

    for (std::size_t i = 0; i < wheel.size(); ++i) {
        for (std::size_t j = i + 1; j < wheel.size(); ++j) {
            Particle& a = wheel[i];
            Particle& b = wheel[j];
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance >= minimumDistance) {
                continue;
            }

            if (distance <= 1.0e-6f) {
                dx = 1.0f;
                dy = 0.0f;
                distance = 1.0f;
            }

            const float correction = (minimumDistance - distance) / distance;
            const bool aPinned = static_cast<int>(i) == pinnedPoint;
            const bool bPinned = static_cast<int>(j) == pinnedPoint;
            if (aPinned) {
                b.x += dx * correction;
                b.y += dy * correction;
            } else if (bPinned) {
                a.x -= dx * correction;
                a.y -= dy * correction;
            } else {
                const float halfCorrection = 0.5f * correction;
                a.x -= dx * halfCorrection;
                a.y -= dy * halfCorrection;
                b.x += dx * halfCorrection;
                b.y += dy * halfCorrection;
            }
        }
    }
}

}  // namespace

int main() {
    try {
        gfx::GlfwContext glfw;
        gfx::Window window({.width = 600, .height = 600, .title = "Unit II - Reyes"});

        const GLuint program = createProgram();
        const GLint colorLocation = glGetUniformLocation(program, "uColor");
        GLuint vao = 0;
        GLuint vbo = 0;
        createMeshBuffers(vao, vbo);

        Chain chain{};
        for (std::size_t i = 0; i < chain.size(); ++i) {
            const float x = -0.5f + 0.1f * static_cast<float>(i);
            chain[i] = {x, 0.8f, x, 0.8f};
        }

        Wheel wheel{};
        wheel[0] = {0.5f, 0.2f, 0.5f, 0.2f};
        for (std::size_t i = 0; i < 8; ++i) {
            const float angle = static_cast<float>(i) * 45.0f * kPi / 180.0f;
            const float x = 0.5f + 0.15f * std::cos(angle);
            const float y = 0.2f + 0.15f * std::sin(angle);
            wheel[i + 1] = {x, y, x, y};
        }

        GLFWwindow* nativeWindow = glfwGetCurrentContext();
        int draggedWheelPoint = -1;
        bool wasMouseDown = false;
        double lastFrameTime = glfwGetTime();
        double simulationAccumulator = 0.0;

        glClearColor(0.20f, 0.10f, 0.35f, 1.0f);

        while (!window.shouldClose()) {
            const double currentFrameTime = glfwGetTime();
            const double frameDeltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            gfx::processInput(window);

            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(nativeWindow, &cursorX, &cursorY);
            const float mouseX = static_cast<float>(cursorX) / kPlayAreaSize * 2.0f - 1.0f;
            const float mouseY = 1.0f - static_cast<float>(cursorY) / kPlayAreaSize * 2.0f;
            const bool mouseDown = glfwGetMouseButton(nativeWindow, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            if (mouseDown && !wasMouseDown) {
                float nearestDistanceSquared = 0.2f * 0.2f;
                for (std::size_t i = 0; i < wheel.size(); ++i) {
                    const Particle& point = wheel[i];
                    const float dx = point.x - mouseX;
                    const float dy = point.y - mouseY;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared <= nearestDistanceSquared) {
                        nearestDistanceSquared = distanceSquared;
                        draggedWheelPoint = static_cast<int>(i);
                    }
                }
            }
            if (!mouseDown) {
                draggedWheelPoint = -1;
            }

            if (draggedWheelPoint >= 0) {
                Particle& point = wheel[static_cast<std::size_t>(draggedWheelPoint)];
                point.x = std::clamp(mouseX, -1.0f + kPointRadius, 1.0f - kPointRadius);
                point.y = std::clamp(mouseY, -1.0f + kPointRadius, 1.0f - kPointRadius);
                point.previousX = point.x;
                point.previousY = point.y;
            }
            wasMouseDown = mouseDown;

            simulationAccumulator += std::min(frameDeltaTime, 0.1);
            while (simulationAccumulator >= kFixedDeltaTime) {
                const float deltaTime = static_cast<float>(kFixedDeltaTime);
                for (std::size_t i = 1; i < chain.size(); ++i) {
                    integrate(chain[i], deltaTime);
                    clampToFloor(chain[i]);
                }
                for (std::size_t i = 0; i < wheel.size(); ++i) {
                    if (static_cast<int>(i) != draggedWheelPoint) {
                        Particle& point = wheel[i];
                        integrate(point, deltaTime);
                        bounceWheelAtWalls(point);
                    }
                }

                for (int iteration = 0; iteration < kConstraintIterations; ++iteration) {
                    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
                        constrainDistance(chain[i], chain[i + 1], 0.1f, i == 0);
                    }
                    for (std::size_t i = 1; i < wheel.size(); ++i) {
                        const bool hubPinned = draggedWheelPoint == 0;
                        const bool rimPointPinned = draggedWheelPoint == static_cast<int>(i);
                        constrainDistance(
                            wheel[0], wheel[i], 0.15f, hubPinned, rimPointPinned);
                        const std::size_t next = i == wheel.size() - 1 ? 1 : i + 1;
                        constexpr float rimLength = 0.15f * 2.0f * std::sin(kPi / 8.0f);
                        constrainDistance(
                            wheel[i],
                            wheel[next],
                            rimLength,
                            rimPointPinned,
                            draggedWheelPoint == static_cast<int>(next));
                    }
                    separateOverlappingWheelPoints(wheel, draggedWheelPoint);
                    for (std::size_t i = 1; i < chain.size(); ++i) {
                        clampToFloor(chain[i]);
                    }
                    for (Particle& point : wheel) {
                        bounceWheelAtWalls(point);
                    }
                }
                simulationAccumulator -= kFixedDeltaTime;
            }

            const SceneMesh mesh = createSceneMesh(chain, wheel);
            uploadMesh(vbo, mesh);

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(program);
            glBindVertexArray(vao);

            glUniform4f(colorLocation, 0.55f, 0.55f, 0.62f, 1.0f);
            glDrawArrays(GL_LINES, mesh.borderFirst, mesh.borderCount);

            glUniform4f(colorLocation, 0.31f, 0.76f, 0.97f, 1.0f);
            glDrawArrays(GL_LINES, mesh.sticksFirst, mesh.chainSticksCount);

            glUniform4f(colorLocation, 0.9f, 0.9f, 0.95f, 1.0f);
            glDrawArrays(
                GL_LINES,
                mesh.sticksFirst + mesh.chainSticksCount,
                mesh.sticksCount - mesh.chainSticksCount);

            glUniform4f(colorLocation, 1.0f, 1.0f, 0.0f, 1.0f);
            const GLsizei pointVertexCount = kCircleSegments + 2;
            for (GLsizei i = 0; i < mesh.pointsCount / pointVertexCount; ++i) {
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
