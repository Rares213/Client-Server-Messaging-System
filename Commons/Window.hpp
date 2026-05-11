#pragma once

#include "glad/glad.h"
#include "GLFW/glfw3.h"

#include <iostream>
#include <exception>
#include <stdexcept>
#include <string>

namespace wnd
{
	struct WindowSpec
	{
		int width = 1280;
		int height = 720;
		std::string window_name = "< blank >";
		bool enable_debug_gl = false;
		bool enable_vsync = false;
	};
	
	class Window
	{
	public:
		Window(const WindowSpec& window_spec)
		{
            glfwSetErrorCallback(glfwErrorCallback);
            glfwInit();
            
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

            if (window_spec.enable_debug_gl == true) { glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true); }

            m_window_handle = glfwCreateWindow(window_spec.width, window_spec.height, window_spec.window_name.c_str(), NULL, NULL);

            if (m_window_handle == NULL)
            {
                throw std::runtime_error("Failed to create window");
            }

            glfwSetFramebufferSizeCallback(m_window_handle, framebufferSizeCallback);

            glfwMakeContextCurrent(m_window_handle);

            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
            {
                throw std::runtime_error("Failed to initialize glad");
            }
            
            if (window_spec.enable_vsync)
            {
                glfwSwapInterval(0);
            }
            else
            {
                glfwSwapInterval(1);
            }

            //glEnable(GL_DEPTH_TEST);
		}
		~Window() 
        { 
            glfwTerminate();
            if (m_window_handle != nullptr) { glfwDestroyWindow(m_window_handle); }
        }

		GLFWwindow* getWindowHandle() { return m_window_handle; }
		int windowShouldClose() { return glfwWindowShouldClose(m_window_handle); }

		void clearScreen() { glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); }
		void updateScreen() { glfwSwapBuffers(m_window_handle); }

	private:
		GLFWwindow* m_window_handle = nullptr;

	private:

        static void glfwErrorCallback(int error, const char* description)
        {
            std::cerr << "\nGLFW error" << "\ncode: " << error << "\ndescription: " << description << std::endl;
        }

        static void framebufferSizeCallback(GLFWwindow* window, int width, int height)
        {
            glViewport(0, 0, width, height);

        }
	};
}