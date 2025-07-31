# Software Rendered Model Viewer

This is a software-rendered 3D model viewer built with SDL2 and ImGui. It demonstrates basic 3D graphics rendering without using OpenGL or any hardware acceleration. The application uses a dual-window setup with the 3D scene in one window and the ImGui controls in a separate window.

## Features

- **Dual-Window Interface**: 3D scene and ImGui controls in separate windows
- **Software Rendering**: All 3D graphics are rendered using CPU-based algorithms
- **Interactive Controls**: ImGui interface for real-time manipulation of the 3D scene
- **3D Transformations**: Rotation, scaling, and translation controls
- **Wireframe Mode**: Toggle between filled and wireframe rendering
- **Coordinate Axes**: Visual reference axes (X=Red, Y=Green, Z=Blue)
- **Colorful Cube**: Each face of the cube has a different color for easy identification

## Controls

### Transform Controls

- **Rotation X/Y/Z**: Rotate the cube around each axis (0-360 degrees)
- **Scale**: Scale the cube (0.1x to 5.0x)
- **Position X/Y/Z**: Move the cube in 3D space

### Display Options

- **Show Wireframe**: Toggle between filled polygons and wireframe
- **Show Axes**: Toggle coordinate axes display
- **Background Color**: Change the background color using a color picker
- **Reset Transform**: Reset all transformations to default values

## Technical Details

### Window Management

- **Main Window**: 1024x768 pixels for 3D scene rendering
- **GUI Window**: 400x600 pixels for ImGui controls
- **Independent Rendering**: Each window has its own SDL2 renderer
- **Event Handling**: Both windows respond to close events

### Rendering Pipeline

1. **3D Transformations**: Apply rotation matrices to vertices
2. **Projection**: Simple perspective projection to 2D screen coordinates
3. **Rasterization**: Scanline polygon filling algorithm
4. **Software Rendering**: SDL2 software renderer for all drawing operations

### Key Components

- **Point3D/Point2D**: Simple 3D and 2D point structures
- **Rotation Functions**: Matrix-based rotation around X, Y, Z axes
- **Projection**: Distance-based perspective projection
- **Polygon Filling**: Scanline algorithm for filled polygons
- **ImGui Integration**: SDL2 renderer backend for UI
- **Dual Window Management**: Separate SDL2 windows and renderers for scene and UI

## Building

```bash
mkdir build
cd build
cmake ..
make model_viewer
```

## Running

```bash
./model_viewer
```

## Dependencies

- SDL2 (with software renderer support)
- ImGui (with SDL2 renderer backend)
- C++17 compiler

## Implementation Notes

- The polygon filling algorithm is a basic scanline implementation
- No depth buffering is implemented (faces are drawn in order)
- The projection uses a simple distance-based perspective
- All rendering is done through SDL2's software renderer
- ImGui is integrated using the SDL2 renderer backend

## Future Improvements

- Add depth buffering for proper face ordering
- Implement texture mapping
- Add more complex 3D models
- Improve polygon filling with anti-aliasing
- Add lighting calculations
- Implement backface culling
