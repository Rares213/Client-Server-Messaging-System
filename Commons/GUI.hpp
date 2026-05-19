#pragma once

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "Window.hpp"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <memory>
#include <utility>

namespace gui
{
	using WinSize = std::pair<int, int>;
	
	class GUIComponent
	{
	public:
		virtual ~GUIComponent() {}

		virtual void showMenuComponent() = 0;
	};

	class GUI
	{
	public:
		static void initGUI(const wnd::WindowSpec& win_spec)
		{
			GUI::m_window = std::make_unique<wnd::Window>(win_spec);
			
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

			// Setup Platform/Renderer backends
			ImGui_ImplGlfw_InitForOpenGL(GUI::m_window->getWindowHandle(), true);
			ImGui_ImplOpenGL3_Init("#version 330");
		}
		
		static wnd::Window* getWindow()
		{
			if (m_window == nullptr)
			{
				return nullptr;
			}
			else
			{
				return m_window.get();
			}
		}

		static int shouldClose() { return GUI::m_window->windowShouldClose(); }

		static void pollEvents() { glfwPollEvents(); }

		static void clearScreen() { GUI::m_window->clearScreen(); }

		static void updateScreen() { glfwSwapBuffers(GUI::m_window->getWindowHandle()); }

		static WinSize getScreenSize() 
		{
			WinSize win_dim{ 0, 0 };
			glfwGetWindowSize(m_window->getWindowHandle(), &win_dim.first, &win_dim.second);

			return win_dim;
		}

	private:

		inline static std::unique_ptr<wnd::Window> m_window;

	};
}
