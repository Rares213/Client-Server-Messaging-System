cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
	cmake_policy(SET CMP0141 NEW)
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("MessagingApp")

if (MSVC)
	add_compile_options(/W4 /Wall)
elseif(GNU)
	add_compile_options(-Wextra -Wall)
endif()

option(COMPILE_TESTS "Compile the tests" OFF)
option(COMPILE_CLIENT "Compile the client side" ON)
option(COMPILE_SERVER "Compile the server side" ON)

if(COMPILE_TESTS)
	message("COMPILING THE TESTS")
	add_executable(testApp Tests/test.cpp)
	target_compile_features(testApp PRIVATE cxx_std_23)
	target_compile_options(testApp PRIVATE /O2)
endif()

if(COMPILE_SERVER)
	message("COMPILING THE SERVER SIDE")
	add_executable(server Server/server.cpp)
	target_compile_features(server PRIVATE cxx_std_23)
	if(MSVC)
		target_compile_options(server PRIVATE /O2)
	elseif(GNU)
		target_compile_options(server PRIVATE -O2)
	endif()
endif()

if(COMPILE_CLIENT)
	message("COMPILING THE CLIENT SIDE")
	
	add_subdirectory(Dependencies)
	
	add_executable(client Client/client.cpp)
	target_sources(client
		PRIVATE
			imgui/imgui.cpp
			imgui/imgui_demo.cpp
			imgui/imgui_draw.cpp
			imgui/imgui_impl_glfw.cpp
			imgui/imgui_impl_opengl3.cpp
			imgui/imgui_tables.cpp
			imgui/imgui_widgets.cpp
	
		PUBLIC
			FILE_SET imguiHeaders
			BASE_DIRS
				imgui
			TYPE HEADERS
			FILES
				imgui/imgui.h
				imgui/imgui_impl_glfw.h
				imgui/imgui_impl_opengl3.h
				imgui/imgui_impl_opengl3_loader.h
				imgui/imgui_internal.h
	
		INTERFACE
			imgui/imconfig.h
			imgui/imstd_textedit.h
			imgui/imstd_rectpack.h
			imgui/imstb_truetype.h
	)

	target_link_libraries(client PRIVATE depenslib glfw3lib)
	target_compile_features(client PRIVATE cxx_std_23)

	if(MSVC)
		#target_compile_options(client PRIVATE /O2)
	elseif(GNU)
		target_compile_options(client PRIVATE -O2)
	endif()
endif()

