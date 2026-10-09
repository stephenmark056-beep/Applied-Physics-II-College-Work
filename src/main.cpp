#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <cmath>
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
constexpr float kRestitution = 0.4f;
constexpr float kGravity = -1.8f;
constexpr int kConstraintIterations = 5;

struct PointMass {
    float posX, posY;
    float prevX, prevY;
    float accelX, accelY;
    float mass;
    bool pinned;
};

struct StickConstraint {
    int a, b;
    float restLength;
};

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
        float angle = (float)i/segments * 2.0f * kPi;
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

float distance(const PointMass& a, const PointMass& b) {
    float dx = b.posX - a.posX;
    float dy = b.posY - a.posY;
    return sqrtf(dx * dx + dy * dy);
}

void solveStickConstraint(std::vector<PointMass>& points, const StickConstraint& constraint) {
    PointMass& a = points[constraint.a];
    PointMass& b = points[constraint.b];

    float dx = b.posX - a.posX;
    float dy = b.posY - a.posY;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1e-6f) return;

    float inverseMassA = a.pinned ? 0.0f : 1.0f / a.mass;
    float inverseMassB = b.pinned ? 0.0f : 1.0f / b.mass;
    float inverseMassSum = inverseMassA + inverseMassB;
    if (inverseMassSum <= 0.0f) return;

    float correction = (dist - constraint.restLength) / dist;
    float correctionX = dx * correction;
    float correctionY = dy * correction;
    float correctionA = inverseMassA / inverseMassSum;
    float correctionB = inverseMassB / inverseMassSum;

    float moveAX = correctionX * correctionA;
    float moveAY = correctionY * correctionA;
    float moveBX = -correctionX * correctionB;
    float moveBY = -correctionY * correctionB;

    a.posX += moveAX;
    a.posY += moveAY;
    a.prevX += moveAX;
    a.prevY += moveAY;
    b.posX += moveBX;
    b.posY += moveBY;
    b.prevX += moveBX;
    b.prevY += moveBY;
}

void applyGravity(PointMass& point) {
    if (!point.pinned) {
        point.accelY += kGravity;
    }
}

void updatePoint(PointMass& point, float deltaTime) {
    if (point.pinned) {
        point.prevX = point.posX;
        point.prevY = point.posY;
        point.accelX = 0.0f;
        point.accelY = 0.0f;
        return;
    }

    float nextX = point.posX + (point.posX - point.prevX) + point.accelX * deltaTime * deltaTime;
    float nextY = point.posY + (point.posY - point.prevY) + point.accelY * deltaTime * deltaTime;
    point.prevX = point.posX;
    point.prevY = point.posY;
    point.posX = nextX;
    point.posY = nextY;
    point.accelX = 0.0f;
    point.accelY = 0.0f;
}

void resolveWallCollision(PointMass& point) {
    if (point.pinned) return;

    if (point.posX - kPointRadius < -1.0f) {
        float displacement = point.posX - point.prevX;
        point.posX = -1.0f + kPointRadius;
        point.prevX = point.posX + displacement;
    }
    if (point.posX + kPointRadius > 1.0f) {
        float displacement = point.posX - point.prevX;
        point.posX = 1.0f - kPointRadius;
        point.prevX = point.posX + displacement;
    }
    if (point.posY - kPointRadius < -1.0f) {
        float displacement = point.posY - point.prevY;
        point.posY = -1.0f + kPointRadius;
        point.prevY = point.posY + kRestitution * displacement;
    }
    if (point.posY + kPointRadius > 1.0f) {
        float displacement = point.posY - point.prevY;
        point.posY = 1.0f - kPointRadius;
        point.prevY = point.posY + displacement;
    }
}

} // namespace

int main() {
    try {
        gfx::GlfwContext glfw;
        gfx::Window window({.width = 600, .height = 600, .title = "Billiards Sim"});

        GLuint program = createProgram(kVertexShaderSource, kFragmentShaderSource);
        GLint colorLoc = glGetUniformLocation(program, "uColor");
        std::vector<float> circleVerts = generateCircleVertices(kPointRadius, kCircleSegments);
        GLuint circleVbo = 0;
        GLuint circleVao = createDynamicVao(circleVbo);
        GLsizei circleVertexCount = static_cast<GLsizei>(circleVerts.size() / 2);
        GLuint lineVbo = 0;
        GLuint lineVao = createDynamicVao(lineVbo);

        std::vector<PointMass> points;
        std::vector<StickConstraint> sticks;
        points = {
            {0.0f, 0.72f, 0.0f, 0.72f, 0.0f, 0.0f, 1.0f, false},
            {0.0f, 0.46f, 0.0f, 0.46f, 0.0f, 0.0f, 1.0f, false},
            {0.0f, 0.16f, 0.0f, 0.16f, 0.0f, 0.0f, 1.0f, false},
            {-0.25f, 0.40f, -0.25f, 0.40f, 0.0f, 0.0f, 1.0f, false},
            {-0.44f, 0.31f, -0.44f, 0.31f, 0.0f, 0.0f, 1.0f, false},
            {0.25f, 0.40f, 0.25f, 0.40f, 0.0f, 0.0f, 1.0f, false},
            {0.44f, 0.31f, 0.44f, 0.31f, 0.0f, 0.0f, 1.0f, false},
            {-0.13f, -0.13f, -0.13f, -0.13f, 0.0f, 0.0f, 1.0f, false},
            {-0.22f, -0.40f, -0.22f, -0.40f, 0.0f, 0.0f, 1.0f, false},
            {0.13f, -0.13f, 0.13f, -0.13f, 0.0f, 0.0f, 1.0f, false},
            {0.22f, -0.40f, 0.22f, -0.40f, 0.0f, 0.0f, 1.0f, false},
        };

        sticks = {
            {0, 1, distance(points[0], points[1])},
            {1, 2, distance(points[1], points[2])},
            {1, 3, distance(points[1], points[3])},
            {3, 4, distance(points[3], points[4])},
            {1, 5, distance(points[1], points[5])},
            {5, 6, distance(points[5], points[6])},
            {2, 7, distance(points[2], points[7])},
            {7, 8, distance(points[7], points[8])},
            {2, 9, distance(points[2], points[9])},
            {9, 10, distance(points[9], points[10])},
        };

        glClearColor(0.04f, 0.42f, 0.24f, 1.0f);

        float lastFrameTime = static_cast<float>(glfwGetTime());

        while (!window.shouldClose()) {
            float currentFrameTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            gfx::processInput(window);

            for (PointMass& point : points) {
                applyGravity(point);
                updatePoint(point, deltaTime);
            }

            for (int iteration = 0; iteration < kConstraintIterations; ++iteration) {
                for (const StickConstraint& stick : sticks) {
                    solveStickConstraint(points, stick);
                }
                for (PointMass& point : points) {
                    resolveWallCollision(point);
                }
            }

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(program);

            std::vector<float> lineVerts;
            for (const StickConstraint& stick : sticks) {
                lineVerts.push_back(points[stick.a].posX);
                lineVerts.push_back(points[stick.a].posY);
                lineVerts.push_back(points[stick.b].posX);
                lineVerts.push_back(points[stick.b].posY);
            }
            uploadDynamic(lineVbo, lineVerts);
            glUniform4f(colorLoc, 0.31f, 0.76f, 0.97f, 1.0f);
            glBindVertexArray(lineVao);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size() / 2));

            glUniform4f(colorLoc, 0.85f, 0.85f, 0.9f, 1.0f);
            glBindVertexArray(circleVao);
            for (const PointMass& point : points) {
                std::vector<float> translated = circleVerts;
                for (size_t i = 0; i < translated.size(); i += 2) {
                    translated[i] += point.posX;
                    translated[i + 1] += point.posY;
                }
                uploadDynamic(circleVbo, translated);
                glDrawArrays(GL_TRIANGLE_FAN, 0, circleVertexCount);
            }

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