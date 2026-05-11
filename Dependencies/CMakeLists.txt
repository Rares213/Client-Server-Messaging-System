add_library(depenslib)

target_sources(depenslib
	PRIVATE
		Include/glad/glad.c

	PUBLIC
	FILE_SET glStuffHeaders
	TYPE HEADERS
	BASE_DIRS
		Include
	FILES
		Include/glad/glad.h
		Include/KHR/khrplatform.h
)

target_compile_features(depenslib PRIVATE cxx_std_23)

add_library(glfw3lib INTERFACE)
target_include_directories(glfw3lib INTERFACE Include)
target_link_directories(glfw3lib INTERFACE Libs)
target_link_libraries(glfw3lib INTERFACE glfw3)



