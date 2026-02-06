# third_party/GLEW/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(GLEW)

# Проверяем наличие файлов
message(STATUS "GLEW source dir: ${CMAKE_CURRENT_SOURCE_DIR}")

# Ищем заголовки
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/GL/glew.h")
    message(STATUS "Found GLEW headers")
    
    # Добавляем заголовки
    add_library(glew INTERFACE)
    target_include_directories(glew INTERFACE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    # Ищем библиотеку для Windows
    if(WIN32)
        # Пробуем разные пути к библиотеке
        set(POSSIBLE_LIBS
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/Release/x64/glew32.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/Release/x64/glew32s.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/glew32.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/glew32.lib"
        )
        
        foreach(LIB_PATH ${POSSIBLE_LIBS})
            if(EXISTS "${LIB_PATH}")
                message(STATUS "Found GLEW library: ${LIB_PATH}")
                
                # Создаем импортированную статическую библиотеку
                add_library(glew_static STATIC IMPORTED)
                set_target_properties(glew_static PROPERTIES
                    IMPORTED_LOCATION "${LIB_PATH}"
                    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
                )
                
                # Создаем псевдоним glew_s
                add_library(glew_s ALIAS glew_static)
                
                # Выходим из цикла
                break()
            endif()
        endforeach()
        
        # Если не нашли библиотеку
        if(NOT TARGET glew_s)
            message(WARNING "GLEW library not found. Only headers available.")
        endif()
    endif()
else()
    message(WARNING "GLEW headers not found at: ${CMAKE_CURRENT_SOURCE_DIR}/include/GL/glew.h")
endif()