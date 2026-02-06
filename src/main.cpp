#include "third_party/imgui/imgui.h"
#include "third_party/implot/implot.h"
#include <GLFW/glfw3.h> // для окна

int main() {
    // 1. Создаем окно
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Моя программа", NULL, NULL);
    
    // 2. Инициализируем ImGui
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    
    // 3. Главный цикл программы
    while (!glfwWindowShouldClose(window)) {
        // Очищаем экран
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Начинаем новый кадр ImGui
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // ======= ТВОЙ КОД ЗДЕСЬ =======
        
        // Простое окно с кнопкой
        ImGui::Begin("Мое окно");
        if (ImGui::Button("Нажми меня!")) {
            // Действие при нажатии
            printf("Кнопка нажата!\n");
        }
        ImGui::End();
        
        // Простой график
        ImGui::Begin("График");
        if (ImPlot::BeginPlot("Мой график")) {
            static float data[100];
            for (int i = 0; i < 100; ++i) 
                data[i] = sin(i * 0.1f);
            
            ImPlot::PlotLine("Синус", data, 100);
            ImPlot::EndPlot();
        }
        ImGui::End();
        
        // ==============================
        
        // Рендерим
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // 4. Уборка
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}