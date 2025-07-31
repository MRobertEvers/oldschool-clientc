#include <SDL_opengl.h>

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

typedef struct
{
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float scale;
    float position_x;
    float position_y;
    float position_z;
    bool show_wireframe;
    bool show_axes;
    float clear_color[4];
} ModelViewerState;

static ModelViewerState g_state = {
    .rotation_x = 0.0f,
    .rotation_y = 0.0f,
    .rotation_z = 0.0f,
    .scale = 1.0f,
    .position_x = 0.0f,
    .position_y = 0.0f,
    .position_z = -5.0f,
    .show_wireframe = false,
    .show_axes = true,
    .clear_color = { 0.2f, 0.3f, 0.3f, 1.0f }
};

static void
render_axes()
{
    glBegin(GL_LINES);

    // X axis (red)
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(2.0f, 0.0f, 0.0f);

    // Y axis (green)
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 2.0f, 0.0f);

    // Z axis (blue)
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, 2.0f);

    glEnd();
    glColor3f(1.0f, 1.0f, 1.0f);
}

static void
render_cube()
{
    if( g_state.show_wireframe )
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }
    else
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    glBegin(GL_QUADS);

    // Front face (red)
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(-1.0f, -1.0f, 1.0f);
    glVertex3f(1.0f, -1.0f, 1.0f);
    glVertex3f(1.0f, 1.0f, 1.0f);
    glVertex3f(-1.0f, 1.0f, 1.0f);

    // Back face (green)
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, 1.0f, -1.0f);
    glVertex3f(1.0f, 1.0f, -1.0f);
    glVertex3f(1.0f, -1.0f, -1.0f);

    // Top face (blue)
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-1.0f, 1.0f, -1.0f);
    glVertex3f(-1.0f, 1.0f, 1.0f);
    glVertex3f(1.0f, 1.0f, 1.0f);
    glVertex3f(1.0f, 1.0f, -1.0f);

    // Bottom face (yellow)
    glColor3f(1.0f, 1.0f, 0.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f, -1.0f, 1.0f);
    glVertex3f(-1.0f, -1.0f, 1.0f);

    // Right face (magenta)
    glColor3f(1.0f, 0.0f, 1.0f);
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f, 1.0f, -1.0f);
    glVertex3f(1.0f, 1.0f, 1.0f);
    glVertex3f(1.0f, -1.0f, 1.0f);

    // Left face (cyan)
    glColor3f(0.0f, 1.0f, 1.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f, 1.0f);
    glVertex3f(-1.0f, 1.0f, 1.0f);
    glVertex3f(-1.0f, 1.0f, -1.0f);

    glEnd();

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColor3f(1.0f, 1.0f, 1.0f);
}

static void
render_scene()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    // Set camera position
    glTranslatef(g_state.position_x, g_state.position_y, g_state.position_z);

    // Apply rotations
    glRotatef(g_state.rotation_x, 1.0f, 0.0f, 0.0f);
    glRotatef(g_state.rotation_y, 0.0f, 1.0f, 0.0f);
    glRotatef(g_state.rotation_z, 0.0f, 0.0f, 1.0f);

    // Apply scale
    glScalef(g_state.scale, g_state.scale, g_state.scale);

    // Render coordinate axes if enabled
    if( g_state.show_axes )
    {
        render_axes();
    }

    // Render the cube
    render_cube();
}

static void
render_imgui()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Main control window
    ImGui::Begin("Model Viewer Controls");

    ImGui::Text("Transform Controls");
    ImGui::Separator();

    ImGui::SliderFloat("Rotation X", &g_state.rotation_x, 0.0f, 360.0f);
    ImGui::SliderFloat("Rotation Y", &g_state.rotation_y, 0.0f, 360.0f);
    ImGui::SliderFloat("Rotation Z", &g_state.rotation_z, 0.0f, 360.0f);

    ImGui::SliderFloat("Scale", &g_state.scale, 0.1f, 5.0f);

    ImGui::SliderFloat("Position X", &g_state.position_x, -10.0f, 10.0f);
    ImGui::SliderFloat("Position Y", &g_state.position_y, -10.0f, 10.0f);
    ImGui::SliderFloat("Position Z", &g_state.position_z, -20.0f, 5.0f);

    ImGui::Separator();
    ImGui::Text("Display Options");

    ImGui::Checkbox("Show Wireframe", &g_state.show_wireframe);
    ImGui::Checkbox("Show Axes", &g_state.show_axes);

    ImGui::Separator();
    ImGui::Text("Background Color");
    ImGui::ColorEdit4("Clear Color", g_state.clear_color);

    ImGui::Separator();
    if( ImGui::Button("Reset Transform") )
    {
        g_state.rotation_x = 0.0f;
        g_state.rotation_y = 0.0f;
        g_state.rotation_z = 0.0f;
        g_state.scale = 1.0f;
        g_state.position_x = 0.0f;
        g_state.position_y = 0.0f;
        g_state.position_z = -5.0f;
    }

    ImGui::End();

    // Info window
    ImGui::Begin("Info");
    ImGui::Text("Simple Model Viewer with ImGui");
    ImGui::Text("Use the controls to manipulate the cube");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int
main(int argc, char** argv)
{
    // Initialize SDL
    if( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0 )
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    // Create window
    SDL_Window* window = SDL_CreateWindow(
        "Model Viewer with ImGui",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if( !window )
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Create OpenGL context
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if( !gl_context )
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 120");

    // Setup OpenGL
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(
        g_state.clear_color[0],
        g_state.clear_color[1],
        g_state.clear_color[2],
        g_state.clear_color[3]);

    // Main loop
    bool done = false;
    while( !done )
    {
        SDL_Event event;
        while( SDL_PollEvent(&event) )
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if( event.type == SDL_QUIT )
                done = true;
            if( event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window) )
                done = true;
        }

        // Update clear color
        glClearColor(
            g_state.clear_color[0],
            g_state.clear_color[1],
            g_state.clear_color[2],
            g_state.clear_color[3]);

        // Render scene
        render_scene();

        // Render ImGui
        render_imgui();

        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}