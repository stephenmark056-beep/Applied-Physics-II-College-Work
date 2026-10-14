#include <glad/glad.h>

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

constexpr const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";

constexpr const char* kFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; }
)";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compilation failed: ") + log);
    }
    return shader;
}

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(std::string("Program linking failed: ") + log);
    }
    return program;
}

std::vector<float> generateCircleVertices(float radius, int segments) {
    std::vector<float> vertices;
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);

    for (int i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / segments * 2.0f * kPi;
        vertices.push_back(radius * cosf(angle));
        vertices.push_back(radius * sinf(angle));
    }
    return vertices;
}

GLuint createDynamicVao(GLuint& vbo) {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return vao;
}

void uploadDynamic(GLuint vbo, const std::vector<float>& data) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace

int main() {
    try {
        gfx::GlfwContext glfw;
        gfx::Window window({.width = 600, .height = 600, .title = "Unit I - Reyes"});

        GLuint program = createProgram(kVertexShaderSource, kFragmentShaderSource);
        GLint colorLoc = glGetUniformLocation(program, "uColor");
        std::vector<float> turretVerts = generateCircleVertices(kTurretRadius, kCircleSegments);
        std::vector<float> shotVerts = generateCircleVertices(kShotRadius, kCircleSegments);
        GLuint circleVbo = 0;
        GLuint circleVao = createDynamicVao(circleVbo);
        GLuint lineVbo = 0;
        GLuint lineVao = createDynamicVao(lineVbo);

        glClearColor(0.08f, 0.28f, 0.42f, 1.0f);

        while (!window.shouldClose()) {
            gfx::processInput(window);

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(program);

            std::vector<float> translatedTurret = turretVerts;
            for (size_t i = 0; i < translatedTurret.size(); i += 2) {
                translatedTurret[i] += kTurretX;
                translatedTurret[i + 1] += kTurretY;
            }
            uploadDynamic(circleVbo, translatedTurret);
            glUniform4f(colorLoc, 0.55f, 0.55f, 0.58f, 1.0f);
            glBindVertexArray(circleVao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(turretVerts.size() / 2));

            glBindVertexArray(lineVao);
            std::vector<float> aimLineVerts = {
                kTurretX, kTurretY + kTurretRadius,
                kTurretX, kTurretY + kTurretRadius + kAimLength,
            };
            uploadDynamic(lineVbo, aimLineVerts);
            glUniform4f(colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(aimLineVerts.size() / 2));

            std::vector<float> translatedShot = shotVerts;
            for (size_t i = 0; i < translatedShot.size(); i += 2) {
                translatedShot[i] += kTurretX;
                translatedShot[i + 1] += kTurretY + kTurretRadius + kShotRadius;
            }
            uploadDynamic(circleVbo, translatedShot);
            glBindVertexArray(circleVao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(shotVerts.size() / 2));

            window.swapBuffers();
            window.pollEvents();
        }

        glDeleteVertexArrays(1, &circleVao);
        glDeleteBuffers(1, &circleVbo);
        glDeleteVertexArrays(1, &lineVao);
        glDeleteBuffers(1, &lineVbo);
        glDeleteProgram(program);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}