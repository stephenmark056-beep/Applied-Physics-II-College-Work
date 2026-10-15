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
constexpr float kTurretX = 0.0f;
constexpr float kTurretY = -0.85f;
constexpr float kTurretRadius = 0.07f;
constexpr float kAimLineLength = 0.25f;
constexpr float kAimTurnSpeed = 1.5f;
constexpr float kMinimumAimAngle = 20.0f * kPi / 180.0f;
constexpr float kMaximumAimAngle = 160.0f * kPi / 180.0f;
constexpr float kShotSpeed = 1.6f;
constexpr double kFixedDeltaTime = 1.0 / 60.0;

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
    GLsizei aimLineFirst = 0;
    GLsizei turretFirst = 0;
    GLsizei turretCount = 0;
    GLsizei ballsFirst = 0;
    GLsizei ballVertexCount = 0;
};

struct Ball {
    float x;
    float y;
    float radius;
    float restitution;
    float red;
    float green;
    float blue;
    float velocityX;
    float velocityY;
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

void appendLine(SceneMesh& mesh, float x1, float y1, float x2, float y2) {
    mesh.vertices.insert(mesh.vertices.end(), {x1, y1, x2, y2});
}

SceneMesh createSceneMesh(const std::array<Ball, 4>& balls, float aimAngle) {
    SceneMesh mesh;
    mesh.vertices.reserve(4 + (kCircleSegments + 2) * (balls.size() + 1) * 2);

    mesh.aimLineFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    const float directionX = std::cos(aimAngle);
    const float directionY = std::sin(aimAngle);
    appendLine(
        mesh,
        kTurretX + kTurretRadius * directionX,
        kTurretY + kTurretRadius * directionY,
        kTurretX + (kTurretRadius + kAimLineLength) * directionX,
        kTurretY + (kTurretRadius + kAimLineLength) * directionY);

    mesh.turretFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    appendCircle(mesh, kTurretX, kTurretY, kTurretRadius);
    mesh.turretCount = static_cast<GLsizei>(mesh.vertices.size() / 2) - mesh.turretFirst;

    mesh.ballsFirst = static_cast<GLsizei>(mesh.vertices.size() / 2);
    for (const Ball& ball : balls) {
        appendCircle(mesh, ball.x, ball.y, ball.radius);
    }
    mesh.ballVertexCount = kCircleSegments + 2;
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

void bounceBallAtWindowEdges(Ball& ball) {
    const float minimum = -1.0f + ball.radius;
    const float maximum = 1.0f - ball.radius;

    if (ball.x < minimum) {
        ball.x = minimum;
        if (ball.velocityX < 0.0f) {
            ball.velocityX = -ball.velocityX * ball.restitution;
        }
    } else if (ball.x > maximum) {
        ball.x = maximum;
        if (ball.velocityX > 0.0f) {
            ball.velocityX = -ball.velocityX * ball.restitution;
        }
    }

    if (ball.y < minimum) {
        ball.y = minimum;
        if (ball.velocityY < 0.0f) {
            ball.velocityY = -ball.velocityY * ball.restitution;
        }
    } else if (ball.y > maximum) {
        ball.y = maximum;
        if (ball.velocityY > 0.0f) {
            ball.velocityY = -ball.velocityY * ball.restitution;
        }
    }
}

void resolveBallCollision(Ball& a, Ball& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float minimumDistance = a.radius + b.radius;
    const float distanceSquared = dx * dx + dy * dy;
    if (distanceSquared >= minimumDistance * minimumDistance) {
        return;
    }

    const float distance = std::sqrt(distanceSquared);
    const float normalX = distance > 1.0e-6f ? dx / distance : 1.0f;
    const float normalY = distance > 1.0e-6f ? dy / distance : 0.0f;
    const float overlap = minimumDistance - distance;
    a.x -= normalX * overlap * 0.5f;
    a.y -= normalY * overlap * 0.5f;
    b.x += normalX * overlap * 0.5f;
    b.y += normalY * overlap * 0.5f;

    const float velocityAAlongNormal =
        a.velocityX * normalX + a.velocityY * normalY;
    const float velocityBAlongNormal =
        b.velocityX * normalX + b.velocityY * normalY;
    if (velocityAAlongNormal <= velocityBAlongNormal) {
        return;
    }

    const float normalVelocityDifference = velocityBAlongNormal - velocityAAlongNormal;
    a.velocityX += normalVelocityDifference * normalX;
    a.velocityY += normalVelocityDifference * normalY;
    b.velocityX -= normalVelocityDifference * normalX;
    b.velocityY -= normalVelocityDifference * normalY;
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

        std::array<Ball, 4> balls{{
            {kTurretX, kTurretY + kTurretRadius + 0.04f, 0.04f, 0.9f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f},
            {-0.45f, 0.45f, 0.10f, 0.6f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 0.60f, 0.07f, 0.9f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
            {0.45f, 0.35f, 0.05f, 0.3f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}
        }};
        float aimAngle = kPi / 2.0f;
        double lastFrameTime = glfwGetTime();
        double simulationAccumulator = 0.0;
        bool shotInFlight = false;
        bool wasSpaceDown = false;

        glClearColor(0.05f, 0.35f, 0.15f, 1.0f);

        while (!window.shouldClose()) {
            const double currentFrameTime = glfwGetTime();
            const float frameDeltaTime = static_cast<float>(currentFrameTime - lastFrameTime);
            lastFrameTime = currentFrameTime;

            gfx::processInput(window);

            const bool leftPressed = window.isKeyPressed(GLFW_KEY_LEFT);
            const bool rightPressed = window.isKeyPressed(GLFW_KEY_RIGHT);
            const float aimDirection =
                static_cast<float>(leftPressed) - static_cast<float>(rightPressed);
            aimAngle = std::clamp(
                aimAngle + aimDirection * kAimTurnSpeed * frameDeltaTime,
                kMinimumAimAngle,
                kMaximumAimAngle);

            const bool spaceDown = window.isKeyPressed(GLFW_KEY_SPACE);
            if (spaceDown && !wasSpaceDown) {
                const float directionX = std::cos(aimAngle);
                const float directionY = std::sin(aimAngle);
                const float shotDistance = kTurretRadius + balls[0].radius;
                balls[0].x = kTurretX + shotDistance * directionX;
                balls[0].y = kTurretY + shotDistance * directionY;
                balls[0].velocityX = kShotSpeed * directionX;
                balls[0].velocityY = kShotSpeed * directionY;
                shotInFlight = true;
            }
            wasSpaceDown = spaceDown;

            if (!shotInFlight) {
                const float ballDistance = kTurretRadius + balls[0].radius;
                balls[0].x = kTurretX + ballDistance * std::cos(aimAngle);
                balls[0].y = kTurretY + ballDistance * std::sin(aimAngle);
            }

            simulationAccumulator += std::min(frameDeltaTime, 0.1f);
            while (simulationAccumulator >= kFixedDeltaTime) {
                for (Ball& ball : balls) {
                    ball.x += ball.velocityX * static_cast<float>(kFixedDeltaTime);
                    ball.y += ball.velocityY * static_cast<float>(kFixedDeltaTime);
                    bounceBallAtWindowEdges(ball);
                }
                for (std::size_t i = 0; i < balls.size(); ++i) {
                    for (std::size_t j = i + 1; j < balls.size(); ++j) {
                        resolveBallCollision(balls[i], balls[j]);
                        bounceBallAtWindowEdges(balls[i]);
                        bounceBallAtWindowEdges(balls[j]);
                    }
                }
                simulationAccumulator -= kFixedDeltaTime;
            }

            const SceneMesh mesh = createSceneMesh(balls, aimAngle);
            uploadMesh(vbo, mesh);

            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(program);
            glBindVertexArray(vao);

            glUniform4f(colorLocation, 0.55f, 0.55f, 0.58f, 1.0f);
            glDrawArrays(GL_TRIANGLE_FAN, mesh.turretFirst, mesh.turretCount);

            glUniform4f(colorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
            glDrawArrays(GL_LINES, mesh.aimLineFirst, 2);

            for (std::size_t i = 0; i < balls.size(); ++i) {
                const Ball& ball = balls[i];
                glUniform4f(colorLocation, ball.red, ball.green, ball.blue, 1.0f);
                glDrawArrays(
                    GL_TRIANGLE_FAN,
                    mesh.ballsFirst + static_cast<GLsizei>(i) * mesh.ballVertexCount,
                    mesh.ballVertexCount);
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