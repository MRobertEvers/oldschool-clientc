#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// ImGui headers
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768
#define GUI_WINDOW_WIDTH 400
#define GUI_WINDOW_HEIGHT 600

typedef struct
{
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float scale;
    float position_x;
    float position_y;
    float position_z;
    char model_name[256];
    bool show_wireframe;
    bool show_axes;
    float clear_color[4];
} ModelViewerState;

static ModelViewerState g_state = {
    .rotation_x = 0.0f,
    .rotation_y = 0.0f,
    .rotation_z = 0.0f,
    .model_name = "Cube",
    .scale = 100.0f,
    .position_x = 0.0f,
    .position_y = 0.0f,
    .position_z = -5.0f,
    .show_wireframe = false,
    .show_axes = true,
    .clear_color = { 0.2f, 0.3f, 0.3f, 1.0f }
};

// Window and renderer handles
static SDL_Window* g_main_window = NULL;
static SDL_Renderer* g_main_renderer = NULL;
static SDL_Window* g_gui_window = NULL;
static SDL_Renderer* g_gui_renderer = NULL;

// Simple 3D point structure
typedef struct
{
    float x, y, z;
} Point3D;

// Simple 2D point structure
typedef struct
{
    int x, y;
} Point2D;

// Transform a 3D point to 2D screen coordinates
static Point2D
project_point(Point3D p, float scale, float pos_x, float pos_y, float pos_z)
{
    // Simple perspective projection
    float distance = 200.0f;
    float factor = distance / (distance + p.z + pos_z);

    Point2D result;
    result.x = (int)((p.x + pos_x) * factor * scale + SCREEN_WIDTH / 2);
    result.y = (int)((p.y + pos_y) * factor * scale + SCREEN_HEIGHT / 2);
    return result;
}

// Rotate a point around the X axis
static Point3D
rotate_x(Point3D p, float angle)
{
    float rad = angle * M_PI / 180.0f;
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);

    Point3D result;
    result.x = p.x;
    result.y = p.y * cos_a - p.z * sin_a;
    result.z = p.y * sin_a + p.z * cos_a;
    return result;
}

// Rotate a point around the Y axis
static Point3D
rotate_y(Point3D p, float angle)
{
    float rad = angle * M_PI / 180.0f;
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);

    Point3D result;
    result.x = p.x * cos_a + p.z * sin_a;
    result.y = p.y;
    result.z = -p.x * sin_a + p.z * cos_a;
    return result;
}

// Rotate a point around the Z axis
static Point3D
rotate_z(Point3D p, float angle)
{
    float rad = angle * M_PI / 180.0f;
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);

    Point3D result;
    result.x = p.x * cos_a - p.y * sin_a;
    result.y = p.x * sin_a + p.y * cos_a;
    result.z = p.z;
    return result;
}

// Draw a line between two points
static void
draw_line(SDL_Renderer* renderer, Point2D p1, Point2D p2, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, p1.x, p1.y, p2.x, p2.y);
}

// Draw a filled rectangle using scanline algorithm
static void
draw_rect(SDL_Renderer* renderer, Point2D p1, Point2D p2, Point2D p3, Point2D p4, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    // Create an array of points for the polygon
    Point2D points[4] = { p1, p2, p3, p4 };

    // Find bounding box
    int min_y = points[0].y, max_y = points[0].y;
    for( int i = 1; i < 4; i++ )
    {
        if( points[i].y < min_y )
            min_y = points[i].y;
        if( points[i].y > max_y )
            max_y = points[i].y;
    }

    // Simple scanline fill algorithm
    for( int y = min_y; y <= max_y; y++ )
    {
        // Find intersections with the polygon edges
        int intersections[4];
        int num_intersections = 0;

        for( int i = 0; i < 4; i++ )
        {
            Point2D current = points[i];
            Point2D next = points[(i + 1) % 4];

            // Check if this edge intersects with the scanline
            if( (current.y <= y && next.y > y) || (next.y <= y && current.y > y) )
            {
                if( next.y != current.y )
                {
                    float x = current.x +
                              (float)(y - current.y) * (next.x - current.x) / (next.y - current.y);
                    intersections[num_intersections++] = (int)x;
                }
            }
        }

        // Sort intersections
        for( int i = 0; i < num_intersections - 1; i++ )
        {
            for( int j = i + 1; j < num_intersections; j++ )
            {
                if( intersections[i] > intersections[j] )
                {
                    int temp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = temp;
                }
            }
        }

        // Draw horizontal lines between pairs of intersections
        for( int i = 0; i < num_intersections - 1; i += 2 )
        {
            if( i + 1 < num_intersections )
            {
                SDL_RenderDrawLine(renderer, intersections[i], y, intersections[i + 1], y);
            }
        }
    }
}

static void
render_axes(SDL_Renderer* renderer)
{
    // Define axis endpoints
    Point3D x_end = { 2.0f, 0.0f, 0.0f };
    Point3D y_end = { 0.0f, 2.0f, 0.0f };
    Point3D z_end = { 0.0f, 0.0f, 2.0f };
    Point3D origin = { 0.0f, 0.0f, 0.0f };

    // Apply transformations
    x_end = rotate_x(x_end, g_state.rotation_x);
    x_end = rotate_y(x_end, g_state.rotation_y);
    x_end = rotate_z(x_end, g_state.rotation_z);

    y_end = rotate_x(y_end, g_state.rotation_x);
    y_end = rotate_y(y_end, g_state.rotation_y);
    y_end = rotate_z(y_end, g_state.rotation_z);

    z_end = rotate_x(z_end, g_state.rotation_x);
    z_end = rotate_y(z_end, g_state.rotation_y);
    z_end = rotate_z(z_end, g_state.rotation_z);

    origin = rotate_x(origin, g_state.rotation_x);
    origin = rotate_y(origin, g_state.rotation_y);
    origin = rotate_z(origin, g_state.rotation_z);

    // Project to screen coordinates
    Point2D x_screen = project_point(
        x_end, g_state.scale, g_state.position_x, g_state.position_y, g_state.position_z);
    Point2D y_screen = project_point(
        y_end, g_state.scale, g_state.position_x, g_state.position_y, g_state.position_z);
    Point2D z_screen = project_point(
        z_end, g_state.scale, g_state.position_x, g_state.position_y, g_state.position_z);
    Point2D origin_screen = project_point(
        origin, g_state.scale, g_state.position_x, g_state.position_y, g_state.position_z);

    // Draw axes
    SDL_Color red = { 255, 0, 0, 255 };
    SDL_Color green = { 0, 255, 0, 255 };
    SDL_Color blue = { 0, 0, 255, 255 };

    draw_line(renderer, origin_screen, x_screen, red);
    draw_line(renderer, origin_screen, y_screen, green);
    draw_line(renderer, origin_screen, z_screen, blue);
}

static void
render_cube(SDL_Renderer* renderer)
{
    // Define cube vertices
    Point3D vertices[8] = {
        { -1.0f, -1.0f, -1.0f }, // 0
        { 1.0f,  -1.0f, -1.0f }, // 1
        { 1.0f,  1.0f,  -1.0f }, // 2
        { -1.0f, 1.0f,  -1.0f }, // 3
        { -1.0f, -1.0f, 1.0f  }, // 4
        { 1.0f,  -1.0f, 1.0f  }, // 5
        { 1.0f,  1.0f,  1.0f  }, // 6
        { -1.0f, 1.0f,  1.0f  }  // 7
    };

    // Define faces (each face is defined by 4 vertices)
    int faces[6][4] = {
        { 0, 1, 2, 3 }, // Back face
        { 5, 4, 7, 6 }, // Front face
        { 4, 0, 3, 7 }, // Left face
        { 1, 5, 6, 2 }, // Right face
        { 3, 2, 6, 7 }, // Top face
        { 4, 5, 1, 0 }  // Bottom face
    };

    SDL_Color face_colors[6] = {
        { 0,   255, 0,   255 }, // Green (back)
        { 255, 0,   0,   255 }, // Red (front)
        { 0,   255, 255, 255 }, // Cyan (left)
        { 255, 0,   255, 255 }, // Magenta (right)
        { 0,   0,   255, 255 }, // Blue (top)
        { 255, 255, 0,   255 }  // Yellow (bottom)
    };

    // Transform all vertices
    Point3D transformed_vertices[8];
    for( int i = 0; i < 8; i++ )
    {
        Point3D v = vertices[i];
        v = rotate_x(v, g_state.rotation_x);
        v = rotate_y(v, g_state.rotation_y);
        v = rotate_z(v, g_state.rotation_z);
        transformed_vertices[i] = v;
    }

    // Project vertices to screen coordinates
    Point2D screen_vertices[8];
    for( int i = 0; i < 8; i++ )
    {
        screen_vertices[i] = project_point(
            transformed_vertices[i],
            g_state.scale,
            g_state.position_x,
            g_state.position_y,
            g_state.position_z);
    }

    // Draw faces
    for( int face = 0; face < 6; face++ )
    {
        Point2D p1 = screen_vertices[faces[face][0]];
        Point2D p2 = screen_vertices[faces[face][1]];
        Point2D p3 = screen_vertices[faces[face][2]];
        Point2D p4 = screen_vertices[faces[face][3]];

        if( g_state.show_wireframe )
        {
            // Draw wireframe
            draw_line(renderer, p1, p2, face_colors[face]);
            draw_line(renderer, p2, p3, face_colors[face]);
            draw_line(renderer, p3, p4, face_colors[face]);
            draw_line(renderer, p4, p1, face_colors[face]);
        }
        else
        {
            // Draw filled face
            draw_rect(renderer, p1, p2, p3, p4, face_colors[face]);
        }
    }
}

static void
render_scene(SDL_Renderer* renderer)
{
    // Clear screen with background color
    SDL_SetRenderDrawColor(
        renderer,
        (Uint8)(g_state.clear_color[0] * 255),
        (Uint8)(g_state.clear_color[1] * 255),
        (Uint8)(g_state.clear_color[2] * 255),
        (Uint8)(g_state.clear_color[3] * 255));
    SDL_RenderClear(renderer);

    // Render coordinate axes if enabled
    if( g_state.show_axes )
    {
        render_axes(renderer);
    }

    // Render the cube
    render_cube(renderer);
}

static void
render_imgui()
{
    // Clear the GUI renderer background
    SDL_SetRenderDrawColor(g_gui_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_gui_renderer);

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Main control window
    ImGui::Begin("Model Viewer Controls");
    // React Compo
    ImGui::Text("Transform Controls");
    ImGui::Separator();

    ImGui::SliderFloat("Rotation X", &g_state.rotation_x, 0.0f, 360.0f);
    ImGui::SliderFloat("Rotation Y", &g_state.rotation_y, 0.0f, 360.0f);
    ImGui::SliderFloat("Rotation Z", &g_state.rotation_z, 0.0f, 360.0f);

    ImGui::SliderFloat("Scale", &g_state.scale, 0.1f, 100.0f);

    ImGui::SliderFloat("Position X", &g_state.position_x, -10.0f, 10.0f);
    ImGui::SliderFloat("Position Y", &g_state.position_y, -10.0f, 10.0f);
    ImGui::SliderFloat("Position Z", &g_state.position_z, -20.0f, 5.0f);

    ImGui::Separator();
    ImGui::Text("Display Options");
    ImGui::InputText("Model Name", g_state.model_name, IM_ARRAYSIZE(g_state.model_name));

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
        g_state.scale = 100.0f;
        g_state.position_x = 0.0f;
        g_state.position_y = 0.0f;
        g_state.position_z = -5.0f;
    }

    ImGui::End();

    // Info window
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("Info");
    ImGui::Text("Software Rendered Model Viewer with ImGui");
    ImGui::Text("Use the controls to manipulate the cube");
    ImGui::Text(
        "Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate,
        ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_gui_renderer);
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

    // Create main window for 3D rendering
    g_main_window = SDL_CreateWindow(
        "Software Rendered Model Viewer - 3D Scene",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if( !g_main_window )
    {
        printf("Error creating main window: %s\n", SDL_GetError());
        return -1;
    }

    // Create main renderer (software renderer)
    g_main_renderer = SDL_CreateRenderer(g_main_window, -1, SDL_RENDERER_SOFTWARE);
    if( !g_main_renderer )
    {
        printf("Error creating main renderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RenderSetLogicalSize(g_main_renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create GUI window
    g_gui_window = SDL_CreateWindow(
        "Model Viewer Controls",
        SDL_WINDOWPOS_CENTERED + SCREEN_WIDTH / 2 + 50,
        SDL_WINDOWPOS_CENTERED,
        GUI_WINDOW_WIDTH,
        GUI_WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    if( !g_gui_window )
    {
        printf("Error creating GUI window: %s\n", SDL_GetError());
        return -1;
    }

    // Create GUI renderer
    g_gui_renderer = SDL_CreateRenderer(g_gui_window, -1, SDL_RENDERER_SOFTWARE);
    if( !g_gui_renderer )
    {
        printf("Error creating GUI renderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_RenderSetLogicalSize(g_gui_renderer, GUI_WINDOW_WIDTH, GUI_WINDOW_HEIGHT);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends for GUI window
    ImGui_ImplSDL2_InitForSDLRenderer(g_gui_window, g_gui_renderer);
    ImGui_ImplSDLRenderer2_Init(g_gui_renderer);

    // Main loop
    bool done = false;
    while( !done )
    {
        SDL_Event event;
        while( SDL_PollEvent(&event) )
        {
            // Process events for both windows
            if( event.type == SDL_WINDOWEVENT )
            {
                // Route window events to the appropriate window
                if( event.window.windowID == SDL_GetWindowID(g_gui_window) )
                {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                }

                if( event.window.event == SDL_WINDOWEVENT_CLOSE )
                {
                    if( event.window.windowID == SDL_GetWindowID(g_main_window) ||
                        event.window.windowID == SDL_GetWindowID(g_gui_window) )
                    {
                        done = true;
                    }
                }
            }
            else if(
                event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL ||
                event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
                event.type == SDL_TEXTINPUT )
            {
                // Route input events to ImGui if they're for the GUI window
                if( event.window.windowID == SDL_GetWindowID(g_gui_window) )
                {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                }
            }
            else if( event.type == SDL_QUIT )
            {
                done = true;
            }
        }

        // Render 3D scene in main window
        render_scene(g_main_renderer);
        SDL_RenderPresent(g_main_renderer);

        // Render ImGui in GUI window
        render_imgui();
        SDL_RenderPresent(g_gui_renderer);
    }

    // Cleanup
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(g_gui_renderer);
    SDL_DestroyWindow(g_gui_window);
    SDL_DestroyRenderer(g_main_renderer);
    SDL_DestroyWindow(g_main_window);
    SDL_Quit();

    return 0;
}