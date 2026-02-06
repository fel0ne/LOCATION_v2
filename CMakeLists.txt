cmake_minimum_required(VERSION 3.14)
project(LOCATION_V2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

# =================== SDL3 ===================
add_subdirectory(${THIRD_PARTY_DIR}/SDL3)

# =================== GLEW ===================
# glew-cmake имеет CMakeLists.txt в корне
set(GLEW_PATH ${THIRD_PARTY_DIR}/GLEW)

if(EXISTS "${GLEW_PATH}/include/GL/glew.h")
    message(STATUS "Using GLEW from: ${GLEW_PATH}")
    
    # Добавляем заголовки
    include_directories(${GLEW_PATH}/include)
    
    # Ищем библиотеку
    if(WIN32)
        # Пробуем разные пути
        if(EXISTS "${GLEW_PATH}/lib/Release/x64/glew32.lib")
            set(GLEW_LIB "${GLEW_PATH}/lib/Release/x64/glew32.lib")
        elseif(EXISTS "${GLEW_PATH}/glew32.lib")
            set(GLEW_LIB "${GLEW_PATH}/glew32.lib")
        elseif(EXISTS "${GLEW_PATH}/lib/glew32.lib")
            set(GLEW_LIB "${GLEW_PATH}/lib/glew32.lib")
        endif()
        
        if(GLEW_LIB)
            message(STATUS "Found GLEW library: ${GLEW_LIB}")
            
            # Создаем импортированную библиотеку
            add_library(glew_s STATIC IMPORTED)
            set_target_properties(glew_s PROPERTIES
                IMPORTED_LOCATION "${GLEW_LIB}"
                INTERFACE_INCLUDE_DIRECTORIES "${GLEW_PATH}/include"
            )
            
            
        else()
            message(WARNING "GLEW library not found. Using headers only.")
            # Создаем пустую интерфейсную цель
            add_library(glew_s INTERFACE)
            target_include_directories(glew_s INTERFACE ${GLEW_PATH}/include)
        endif()
    endif()
else()
    message(FATAL_ERROR "GLEW not found. Download from: https://sourceforge.net/projects/glew/files/glew/2.2.0/")
endif()

# =================== OpenGL ===================
find_package(OpenGL REQUIRED)

include_directories(${INCLUDE_DIR})

# =================== ImGui ===================
# Проверьте бэкенд для SDL3
if(EXISTS "${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl3.cpp")
    set(IMGUI_SDL_BACKEND "${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl3.cpp")
else()
    set(IMGUI_SDL_BACKEND "${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp")
endif()

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${IMGUI_SDL_BACKEND}
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC 
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/SDL3/include
)

# =================== ImPlot ===================
add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)

# =================== Main Executable ===================
add_executable(main ${SRC_DIR}/main.cpp)

# Подключение библиотек (glew-cmake создает target glew_s)
target_link_libraries(main PRIVATE 
    imgui
    implot
    SDL3::SDL3
    OpenGL::GL
    glew_s  # glew-cmake создает статическую библиотеку
)

# Добавить директории заголовков
target_include_directories(main PRIVATE
    ${THIRD_PARTY_DIR}/SDL3/include
    ${THIRD_PARTY_DIR}/GLEW/include
)

if(WIN32)
    # Копирование SDL3.dll
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL3::SDL3>"
            "$<TARGET_FILE_DIR:main>"
        COMMENT "Copying SDL3.dll to executable directory"
    )
    
    # Копирование GLEW DLL если есть
    if(EXISTS "${THIRD_PARTY_DIR}/GLEW/bin/Release/x64/glew32.dll")
        add_custom_command(TARGET main POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${THIRD_PARTY_DIR}/GLEW/bin/Release/x64/glew32.dll"
                "$<TARGET_FILE_DIR:main>"
            COMMENT "Copying glew32.dll to executable directory"
        )
    endif()
    
    # Копирование OpenGL32.dll обычно не нужно, он системный
endif()