#include "GameForger/Editor/SplashScreen.hpp"

#include <array>
#include <cstdio>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "stb_image.h"

namespace gameforger::editor
{
	namespace
	{
		constexpr const char* vertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec2 position;
layout (location = 1) in vec2 uv;
out vec2 fragUv;

void main()
{
	fragUv = uv;
	gl_Position = vec4(position, 0.0, 1.0);
}
)glsl";

		constexpr const char* fragmentShaderSource = R"glsl(
#version 460 core
in vec2 fragUv;
out vec4 fragColor;
uniform sampler2D image;

void main()
{
	fragColor = texture(image, fragUv);
}
)glsl";

		GLuint compileShader(const GLenum type, const char* source, const char* label)
		{
			const GLuint shader = glCreateShader(type);
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);
			GLint success = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (success == GL_FALSE)
			{
				std::array<char, 1024> log{};
				glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
				std::fprintf(stderr, "Splash %s shader compilation failed: %s\n", label, log.data());
			}
			return shader;
		}
	}

	void showSplashScreen(const std::filesystem::path& imagePath, const double durationSeconds)
	{
		int imageWidth = 0;
		int imageHeight = 0;
		int imageChannels = 0;
		unsigned char* pixels = stbi_load(imagePath.string().c_str(), &imageWidth, &imageHeight, &imageChannels, 4);
		if (pixels == nullptr)
		{
			std::fprintf(stderr, "Splash screen image could not be loaded: %s\n", imagePath.string().c_str());
			return;
		}

		constexpr int maxWidth = 720;
		const int windowWidth = imageWidth > maxWidth ? maxWidth : imageWidth;
		const int windowHeight = static_cast<int>(
			static_cast<double>(windowWidth) * static_cast<double>(imageHeight) / static_cast<double>(imageWidth));

		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
		glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		GLFWwindow* splashWindow = glfwCreateWindow(windowWidth, windowHeight, "GameForgerAI", nullptr, nullptr);

		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
		glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		if (splashWindow == nullptr)
		{
			stbi_image_free(pixels);
			return;
		}

		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		if (monitor != nullptr)
		{
			int monitorX = 0;
			int monitorY = 0;
			int monitorWidth = 0;
			int monitorHeight = 0;
			glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorWidth, &monitorHeight);
			glfwSetWindowPos(
				splashWindow,
				monitorX + (monitorWidth - windowWidth) / 2,
				monitorY + (monitorHeight - windowHeight) / 2);
		}

		glfwMakeContextCurrent(splashWindow);
		if (gladLoadGL(glfwGetProcAddress) == 0)
		{
			stbi_image_free(pixels);
			glfwDestroyWindow(splashWindow);
			return;
		}

		GLuint texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imageWidth, imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		stbi_image_free(pixels);

		const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource, "vertex");
		const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource, "fragment");
		const GLuint program = glCreateProgram();
		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);
		glLinkProgram(program);
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		// Fullscreen quad, position(2) + uv(2). stb_image loads bottom-up
		// (StbImageImpl.cpp turns on stbi_set_flip_vertically_on_load for the
		// whole process), so texture v=0 is the BOTTOM of the source image -
		// pair it with the screen's bottom edge. (The previous mapping
		// predated that global flip and drew every splash upside down.)
		constexpr std::array<float, 16> quadVertices{
			-1.0F, -1.0F, 0.0F, 0.0F,
			1.0F, -1.0F, 1.0F, 0.0F,
			1.0F, 1.0F, 1.0F, 1.0F,
			-1.0F, 1.0F, 0.0F, 1.0F,
		};
		constexpr std::array<unsigned int, 6> quadIndices{0, 1, 2, 0, 2, 3};

		GLuint vertexArray = 0;
		GLuint vertexBuffer = 0;
		GLuint indexBuffer = 0;
		glGenVertexArrays(1, &vertexArray);
		glGenBuffers(1, &vertexBuffer);
		glGenBuffers(1, &indexBuffer);
		glBindVertexArray(vertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(quadVertices.size() * sizeof(float)),
			quadVertices.data(),
			GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
		glBufferData(
			GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(quadIndices.size() * sizeof(unsigned int)),
			quadIndices.data(),
			GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);

		const double startTime = glfwGetTime();
		while (glfwGetTime() - startTime < durationSeconds && glfwWindowShouldClose(splashWindow) == GLFW_FALSE)
		{
			glfwPollEvents();

			int framebufferWidth = 0;
			int framebufferHeight = 0;
			glfwGetFramebufferSize(splashWindow, &framebufferWidth, &framebufferHeight);
			glViewport(0, 0, framebufferWidth, framebufferHeight);
			glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			glUseProgram(program);
			glBindTexture(GL_TEXTURE_2D, texture);
			glBindVertexArray(vertexArray);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
			glBindVertexArray(0);
			glUseProgram(0);

			glfwSwapBuffers(splashWindow);
		}

		glDeleteVertexArrays(1, &vertexArray);
		glDeleteBuffers(1, &vertexBuffer);
		glDeleteBuffers(1, &indexBuffer);
		glDeleteTextures(1, &texture);
		glDeleteProgram(program);
		glfwDestroyWindow(splashWindow);
	}
}
