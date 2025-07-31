# Model Viewer with ImGui

A simple 3D model viewer with ImGui interface for interactive controls.

## Features

- **3D Cube Rendering**: Displays a colored cube with different colored faces
- **Interactive Controls**: Real-time manipulation of the 3D scene
- **Transform Controls**:
  - Rotation (X, Y, Z axes)
  - Scale
  - Position (X, Y, Z)
- **Display Options**:
  - Wireframe mode toggle
  - Coordinate axes display
  - Background color customization
- **Performance Monitoring**: FPS counter and frame time display

## Controls

### Transform Controls

- **Rotation X/Y/Z**: Sliders to rotate the cube around each axis (0-360 degrees)
- **Scale**: Slider to scale the cube (0.1x to 5x)
- **Position X/Y/Z**: Sliders to move the cube in 3D space

### Display Options

- **Show Wireframe**: Toggle between solid and wireframe rendering
- **Show Axes**: Toggle coordinate axes display (red=X, green=Y, blue=Z)
- **Background Color**: Color picker to change the background color

### Utility

- **Reset Transform**: Button to reset all transformations to default values

## Building

The model viewer is built as part of the main project:

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

- SDL2 (for window management and input)
- OpenGL (for 3D rendering)
- ImGui (for the user interface)

## Technical Details

- Uses OpenGL immediate mode for simple 3D rendering
- ImGui integration with SDL2 and OpenGL3 backends
- C++17 standard
- Cross-platform (tested on macOS)

## Cube Face Colors

- Front face: Red
- Back face: Green
- Top face: Blue
- Bottom face: Yellow
- Right face: Magenta
- Left face: Cyan

This helps distinguish the orientation of the cube during rotation.
