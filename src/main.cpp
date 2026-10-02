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

    constexpr float kBallRadius = 0.1f;
    constexpr int kCircleSegments = 40;
    constexpr float kPi = 3.14159265358979323846f;

    struct Ball {
        float posX, posY;
        float velX, velY;
        float radius;
    };

    struct Vec2 {
        float x, y;
    };

    constexpr const char* kVertexShaderSource = R"(#version 330 core
layout (location = 0) in vec2 aPos;
uniform vec2 uCenter;
void main() { gl_Position = vec4(aPos + uCenter, 0.0, 1.0); }
)";

    constexpr const char* kFragmentShaderSource = R"(#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(0.85, 0.85, 0.9, 1.0); }
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

        // center

        vertices.push_back(0.0f);
        vertices.push_back(0.0f);

        for (int i = 0; i <= segments; ++i) {
            float angle = (float)i / segments * 2.0f * kPi;
            vertices.push_back(radius * cosf(angle));
            vertices.push_back(radius * sinf(angle));
        }
        return vertices;
    }

    struct CircleMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
    };

    CircleMesh createCircleMesh(const std::vector<float>& vertices) {
        CircleMesh mesh;
        mesh.vertexCount = static_cast<GLsizei>(vertices.size() / 2);

        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);

        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        return mesh;
    }

    void drawBall(GLuint program, const CircleMesh& mesh, const Ball& ball) {
        glUseProgram(program);
        GLint loc = glGetUniformLocation(program, "uCenter");
        glUniform2f(loc, ball.posX, ball.posY);
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLE_FAN, 0, mesh.vertexCount);
    }

    void updateBall(Ball& ball, float deltaTime) {
        ball.posX += ball.velX * deltaTime;
        ball.posY += ball.velY * deltaTime;
    }

    void resolveWallCollision(Ball& ball) {
        if (ball.posX - ball.radius < -1.0f) {
            ball.posX = -1.0f + ball.radius;
            ball.velX = -ball.velX;
        }
        if (ball.posX + ball.radius > 1.0f) {
            ball.posX = 1.0f - ball.radius;
            ball.velX = -ball.velX;
        }
        if (ball.posY - ball.radius < -1.0f) {
            ball.posY = -1.0f + ball.radius;
            ball.velY = -ball.velY;
        }
        if (ball.posY + ball.radius > 1.0f) {
            ball.posY = 1.0f - ball.radius;
            ball.velY = -ball.velY;
        }
    }

    bool checkBallCollision(const Ball& a, const Ball& b) {
        float dx = b.posX - a.posX;
        float dy = b.posY - a.posY;
        float distance = sqrtf(dx * dx + dy * dy);
        return distance < (a.radius + b.radius);
    }

    Vec2 computeCollisionNormal(const Ball& a, const Ball& b, float& outDistance) {
        float dx = b.posX - a.posX;
        float dy = b.posY - a.posY;
        outDistance = sqrtf(dx * dx + dy * dy);
        if (outDistance == 0.0f) {
            return { 1.0f, 0.0f };
        }
        return { dx / outDistance, dy / outDistance };
    }

    void exchangeNormalVelocity(Ball& a, Ball& b, const Vec2& normal) {
        float dotA = a.velX * normal.x + a.velY * normal.y;
        float dotB = b.velX * normal.x + b.velY * normal.y;

      
        float vANormal = dotA;
        float vBNormal = dotB;

        float vANew = vBNormal;
        float vBNew = vANormal;

        float deltaA = vANew - vANormal;
        float deltaB = vBNew - vBNormal;

        a.velX += deltaA * normal.x;
        a.velY += deltaA * normal.y;
        b.velX += deltaB * normal.x;
        b.velY += deltaB * normal.y;
    }

    void correctPenetration(Ball& a, Ball& b, const Vec2& normal, float distance) {
        float overlap = (a.radius + b.radius) - distance;
        if (overlap <= 0.0f) return;
        float pushEach = overlap / 2.0f;

        a.posX -= normal.x * pushEach;
        a.posY -= normal.y * pushEach;
        b.posX += normal.x * pushEach;
        b.posY += normal.y * pushEach;
    }

    void resolveBallCollision(Ball& a, Ball& b) {
        float distance;
        Vec2 normal = computeCollisionNormal(a, b, distance);

        exchangeNormalVelocity(a, b, normal);
        correctPenetration(a, b, normal, distance);
    }

}
  int main() {
    try {
        gfx::GlfwContext glfw;
        gfx::Window window({.width = 600, .height = 600, .title = "Billiards Sim: Clone Repository Testing"});

        GLuint program = createProgram(kVertexShaderSource, kFragmentShaderSource);
        CircleMesh mesh = createCircleMesh(generateCircleVertices(kBallRadius, kCircleSegments));



        // Lets add more Balls to the simulation Later On

        std::vector<Ball> balls = {
            {0.0f, 0.0f, 0.0f, 0.0f, kBallRadius}
        };

        glClearColor(0.04f, 0.42f, 0.24f, 1.0f);

        float lastFrameTime = static_cast<float>(glfwGetTime());

        while (!window.shouldClose()) {
            float currentFrameTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            gfx::processInput(window);

            
            // Ill update it later on so that we can move the balls for know lets just render a static 2D ball on the screen 
            

            glClear(GL_COLOR_BUFFER_BIT);
            for (const Ball& ball : balls) {
                drawBall(program, mesh, ball);
            }

            window.swapBuffers();
            window.pollEvents();
        }

        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteProgram(program);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;

}