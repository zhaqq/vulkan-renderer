#include <iostream>
#include <GLFW/glfw3.h>

int main()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    // Tell GLFW not to create an OpenGL context. Vulkan manages its own.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vulkan Renderer", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }

    while (!glfwWindowShouldClose(window))
    {
        // glfwPollEvents returns immediately, use it for renderers.
        // glfwWaitEvents blocks until input, better for editor tools at rest.
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}