#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
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
constexpr float kTurretX = 0.0f;
constexpr float kTurretY = -0.85f;
constexpr float kTurretRadius = 0.07f;
constexpr float kShotRadius = 0.04f;
constexpr float kAimLength = 0.25f;
constexpr float kAimTurnSpeed = 1.5f;
constexpr float kMinimumAimAngle = 20.0f * kPi / 180.0f;
constexpr float kMaximumAimAngle = 160.0f * kPi / 180.0f;
constexpr float kShotSpeed = 1.6f;
constexpr double kFixedDeltaTime = 1.0 / 60.0;
constexpr float kWorldMinimum = -1.0f;
constexpr float kWorldMaximum = 1.0f;

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
    GLsizei turretFirst = 0;
    GLsizei turretCount = 0;
    GLsizei aimLineFirst = 0;
    GLsizei shotFirst = 0;
    GLsizei shotCount = 0;
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

SceneMesh createSceneMesh(float angle, float shotX, float shotY) {
    SceneMesh mesh;
    mesh.vertices.reserve((kCircleSegments + 2) * 4 + 4);

    mesh.turretFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    appendCircle(mesh, kTurretX, kTurretY, kTurretRadius);
    mesh.turretCount = static_cast<GLsizei>(mesh.vertices.size() / 2) - mesh.turretFirst;

    const float directionX = std::cos(angle);
    const float directionY = std::sin(angle);
    mesh.aimLineFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    mesh.vertices.insert(mesh.vertices.end(), {
        kTurretX + kTurretRadius * directionX,
        kTurretY + kTurretRadius * directionY,
        kTurretX + (kTurretRadius + kAimLength) * directionX,
        kTurretY + (kTurretRadius + kAimLength) * directionY
    });

    mesh.shotFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    appendCircle(mesh, shotX, shotY, kShotRadius);
    mesh.shotCount = static_cast<GLsizei>(mesh.vertices.size() / 2) - mesh.shotFirst;
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

void bounceInsideWorld(float& position, float& velocity) {
    constexpr float minimum = kWorldMinimum + kShotRadius;
    constexpr float maximum = kWorldMaximum - kShotRadius;

    while (position < minimum || position > maximum) {
        if (position < minimum) {
            position = minimum + (minimum - position);
            velocity = std::abs(velocity);
        } else {
            position = maximum - (position - maximum);
            velocity = -std::abs(velocity);
        }
    }
}

}  // namespace

int main() {
    try {
        gfx::GlfwContext glfw;
        gfx::Window window({.width = 600, .height = 600, .title = "Unit I - Reyes"});

        const GLuint program = createProgram();
        const GLint colorLocation = glGetUniformLocation(program, "uColor");
        GLuint vao = 0;
        GLuint vbo = 0;
        createMeshBuffers(vao, vbo);

        float aimAngle = kPi / 2.0f;
        float shotX = kTurretX;
        float shotY = kTurretY + kTurretRadius + kShotRadius;
        float shotVelocityX = 0.0f;
        float shotVelocityY = 0.0f;
        bool shotInFlight = false;
        bool wasSpaceDown = false;
        double lastFrameTime = glfwGetTime();
        double simulationAccumulator = 0.0;

        glClearColor(0.08f, 0.28f, 0.42f, 1.0f);

        while (!window.shouldClose()) {
            const double currentFrameTime = glfwGetTime();
            const double frameDeltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            gfx::processInput(window);

            const bool leftPressed = window.isKeyPressed(GLFW_KEY_LEFT);
            const bool rightPressed = window.isKeyPressed(GLFW_KEY_RIGHT);
            const float aimDirection =
                static_cast<float>(leftPressed) - static_cast<float>(rightPressed);
            aimAngle = std::clamp(
                aimAngle + aimDirection * kAimTurnSpeed * static_cast<float>(frameDeltaTime),
                kMinimumAimAngle,
                kMaximumAimAngle);

            const bool spaceDown = window.isKeyPressed(GLFW_KEY_SPACE);
            if (spaceDown && !wasSpaceDown) {
                const float directionX = std::cos(aimAngle);
                const float directionY = std::sin(aimAngle);
                shotX = kTurretX + (kTurretRadius + kShotRadius) * directionX;
                shotY = kTurretY + (kTurretRadius + kShotRadius) * directionY;
                shotVelocityX = kShotSpeed * std::cos(aimAngle);
                shotVelocityY = kShotSpeed * std::sin(aimAngle);
                shotInFlight = true;
            }
            wasSpaceDown = spaceDown;

            simulationAccumulator += frameDeltaTime;
            while (simulationAccumulator >= kFixedDeltaTime) {
                if (shotInFlight) {
                    shotX += shotVelocityX * static_cast<float>(kFixedDeltaTime);
                    shotY += shotVelocityY * static_cast<float>(kFixedDeltaTime);
                    bounceInsideWorld(shotX, shotVelocityX);
                    bounceInsideWorld(shotY, shotVelocityY);
                }
                simulationAccumulator -= kFixedDeltaTime;
            }

            if (!shotInFlight) {
                shotX = kTurretX + (kTurretRadius + kShotRadius) * std::cos(aimAngle);
                shotY = kTurretY + (kTurretRadius + kShotRadius) * std::sin(aimAngle);
            }

            const SceneMesh mesh = createSceneMesh(aimAngle, shotX, shotY);
            uploadMesh(vbo, mesh);

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(program);
            glBindVertexArray(vao);

            glUniform4f(colorLocation, 0.55f, 0.55f, 0.58f, 1.0f);
            glDrawArrays(GL_TRIANGLE_FAN, mesh.turretFirst, mesh.turretCount);

            glUniform4f(colorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
            glDrawArrays(GL_LINES, mesh.aimLineFirst, 2);
            glDrawArrays(GL_TRIANGLE_FAN, mesh.shotFirst, mesh.shotCount);

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
