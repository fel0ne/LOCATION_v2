cmake_minimum_required(VERSION 3.15)
project(MyImGuiApp)

# Настройки компилятора
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Добавляем подмодули как библиотеки
add_subdirectory(third_party/imgui)
add_subdirectory(third_party/implot)

# Находим GLFW (библиотеку для создания окон)
find_package(glfw3 3.3 REQUIRED)
find_package(OpenGL REQUIRED)

# Создаем исполняемый файл
add_executable(my_app main.cpp)

# Подключаем все нужные библиотеки
target_link_libraries(my_app PRIVATE
    imgui
    implot
    glfw
    OpenGL::GL
)

# Для Windows: добавляем нужные библиотеки
if(WIN32)
    target_link_libraries(my_app PRIVATE
        opengl32.lib
        gdi32.lib
    )
endif()