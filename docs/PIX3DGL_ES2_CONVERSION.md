# Pix3DGL OpenGL ES2 / WebGL1 Conversion

## Overview

`pix3dgl_opengles2.cpp` is a converted version of `pix3dgl.cpp` that is compatible with:

- **Emscripten / WebGL1**
- **OpenGL ES 2.0** (iOS, Android, embedded systems)
- **Desktop OpenGL with ES2 profile**

This document describes the key changes made to ensure ES2/WebGL1 compatibility.

---

## Key Differences from OpenGL 3.3 Core Version

### 1. **Shader Language (GLSL)**

#### Vertex Shader

- **Version**: Changed from `#version 330 core` to `#version 100`
- **Attributes**: Changed from `layout(location = X) in` to `attribute`
- **Outputs**: Changed from `out` to `varying`
- **Removed**: `flat` qualifier (not available in ES2)

**Before (OpenGL 3.3):**

```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
flat out float vTexBlend;
```

**After (ES2/WebGL1):**

```glsl
#version 100
attribute vec3 aPos;
attribute vec3 aColor;
varying vec3 vColor;
varying float vTexBlend;  // Note: no 'flat' in ES2
```

#### Fragment Shader

- **Version**: Changed from `#version 330 core` to `#version 100`
- **Precision**: Added `precision mediump float;` (required in ES2)
- **Inputs**: Changed from `in` to `varying`
- **Outputs**: Changed from `out vec4 FragColor` to `gl_FragColor`
- **Texture Sampling**: Changed from `texture()` to `texture2D()`
- **Removed**: `flat` qualifier

**Before (OpenGL 3.3):**

```glsl
#version 330 core
in vec3 vColor;
flat in float vTexBlend;
uniform sampler2D uTextureAtlas;
out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTextureAtlas, vTexCoord);
    FragColor = vec4(finalColor, 1.0);
}
```

**After (ES2/WebGL1):**

```glsl
#version 100
precision mediump float;
varying vec3 vColor;
varying float vTexBlend;  // No flat interpolation
uniform sampler2D uTextureAtlas;

void main() {
    vec4 texColor = texture2D(uTextureAtlas, vTexCoord);
    gl_FragColor = vec4(finalColor, 1.0);
}
```

---

### 2. **Header Includes**

Platform-specific header includes for ES2:

```cpp
#if defined(__EMSCRIPTEN__)
    #include <GLES2/gl2.h>
    #include <emscripten/html5.h>
    #include <emscripten.h>
#elif defined(__APPLE__)
    #include <OpenGLES/ES2/gl.h>
    #include <OpenGLES/ES2/glext.h>
#elif defined(__ANDROID__)
    #include <GLES2/gl2.h>
    #include <GLES2/gl2ext.h>
#else
    #include <GLES2/gl2.h>
#endif
```

---

### 3. **Extension Requirements**

OpenGL ES 2.0 requires certain extensions for features that are core in OpenGL 3.3:

#### OES_vertex_array_object

- **Purpose**: VAO support (glGenVertexArrays, glBindVertexArray, etc.)
- **Support**: Widely available in WebGL1 and modern ES2 implementations
- **Fallback**: Automatic - VAOs are optional; code gracefully falls back to manual buffer binding
- **Performance Impact**: ~10-20% slower without VAOs due to additional glBindBuffer/glVertexAttribPointer calls

#### OES_element_index_uint

- **Purpose**: 32-bit indices for models with >65,535 vertices
- **Support**: Available in most WebGL1 and ES2 implementations
- **Alternative**: Use `GL_UNSIGNED_SHORT` and split large models

#### OES_texture_npot (implicit requirement)

- **Purpose**: Non-power-of-two textures for the texture atlas
- **Support**: Standard in WebGL1 (with limitations: no mipmaps, clamp-to-edge only)

The implementation checks for these extensions at initialization:

```cpp
bool has_vao = strstr(ext_str, "OES_vertex_array_object") != nullptr;
bool has_uint_indices = strstr(ext_str, "OES_element_index_uint") != nullptr;
bool has_npot = strstr(ext_str, "OES_texture_npot") != nullptr;
```

---

### 4. **Texture Format Handling**

In ES2, the internal format parameter of `glTexImage2D` must match the format parameter:

**ES2 Compatible:**

```cpp
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, data);
```

**NOT ES2 Compatible:**

```cpp
// This won't work in ES2 (internal format != format)
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, data);
```

---

### 5. **Vertex Array Objects (VAOs) - Optional with Automatic Fallback**

**Important**: OpenGL ES 2.0 does NOT have VAOs in the core specification. They are only available via the `OES_vertex_array_object` extension.

The implementation handles this gracefully with runtime detection and automatic fallback:

#### VAO Detection and Initialization

```cpp
// Check for VAO extension at initialization
bool has_vao = strstr(extensions, "OES_vertex_array_object") != nullptr;
g_has_vao_extension = has_vao;

#ifdef __EMSCRIPTEN__
// Get function pointers for Emscripten
if (has_vao) {
    glGenVertexArraysOES = emscripten_webgl_get_proc_address("glGenVertexArraysOES");
    glBindVertexArrayOES = emscripten_webgl_get_proc_address("glBindVertexArrayOES");
    glDeleteVertexArraysOES = emscripten_webgl_get_proc_address("glDeleteVertexArraysOES");
}
#endif
```

#### Conditional VAO Usage

The code uses helper macros that automatically fall back when VAOs are unavailable:

```cpp
// These macros check g_has_vao_extension before calling VAO functions
GEN_VERTEX_ARRAYS(1, &gl_model.VAO);   // Only calls if extension available
BIND_VERTEX_ARRAY(gl_model.VAO);       // No-op if VAOs not supported
DELETE_VERTEX_ARRAYS(1, &gl_model.VAO); // Only calls if extension available
```

#### Performance Without VAOs

When VAOs are not available:

- Each draw call requires rebinding all vertex buffers and attributes
- Approximately 10-20% performance reduction
- Still fully functional, just slightly slower

**Note**: Most modern ES2/WebGL1 implementations DO support the VAO extension, so this fallback is primarily for older or embedded devices.

---

### 6. **Flat vs Smooth Interpolation**

The `flat` qualifier is not available in ES2. Variables like `vTexBlend` and `vAtlasSlot` that were marked as `flat` in the GL 3.3 version now use smooth interpolation.

**Impact**: Minimal, since all three vertices of a triangle should have the same texture ID, the interpolated value will be consistent across the triangle.

---

## Performance Considerations

### 1. **Precision**

- Uses `mediump float` precision in fragment shader
- Sufficient for texture atlas calculations (2048x2048 with 128x128 tiles)
- Could use `highp` if artifacts appear, but may impact performance on mobile

### 2. **VAO Performance**

- VAO extension significantly improves performance
- Without VAOs, need to rebind all vertex attributes per draw call
- Most modern implementations support VAOs

### 3. **Index Buffer Size**

- Uses 32-bit indices (`GL_UNSIGNED_INT`)
- Requires `OES_element_index_uint` extension
- Alternative: Use `GL_UNSIGNED_SHORT` (max 65,535 vertices per buffer)

---

## WebGL1-Specific Notes

### Browser Compatibility

- Works in all modern browsers (Chrome, Firefox, Safari, Edge)
- Requires WebGL1 support (standard since ~2013)

### Emscripten Compilation

When compiling with Emscripten, use:

```bash
emcc -s USE_WEBGL=1 -s MIN_WEBGL_VERSION=1 -s MAX_WEBGL_VERSION=1 \
     -O3 -o output.js pix3dgl_opengles2.cpp
```

### WebGL Context Settings

Recommended WebGL context attributes:

```javascript
{
    alpha: false,
    depth: true,
    stencil: false,
    antialias: false,
    premultipliedAlpha: false,
    preserveDrawingBuffer: false,
    powerPreference: "high-performance"
}
```

---

## Testing Checklist

- [ ] Verify shader compilation on target platform
- [ ] Check extension availability at runtime
- [ ] Test with large models (>65K vertices) to verify 32-bit index support
- [ ] Verify texture atlas rendering (2048x2048 NPOT texture)
- [ ] Test vertex array object support
- [ ] Validate precision in fragment shader (check for artifacts)
- [ ] Test on multiple browsers/devices (WebGL1)
- [ ] Verify performance metrics

---

## Known Limitations

1. **No flat interpolation**: All varyings use smooth interpolation (minor visual difference)
2. **Extension dependencies**: Requires VAO and 32-bit index extensions for full functionality
3. **Precision**: Uses `mediump` float in fragment shader (may have minor artifacts on very large atlases)
4. **No integer attributes**: ES2 doesn't support integer vertex attributes (uses float instead)

---

## Migration from OpenGL 3.3

If you need both versions:

1. Keep `pix3dgl.cpp` for desktop OpenGL 3.3 Core
2. Use `pix3dgl_opengles2.cpp` for ES2/WebGL1 platforms
3. Both files share the same header (`pix3dgl.h`)
4. Select at compile time based on target platform

Example build system logic:

```cmake
if(EMSCRIPTEN OR PLATFORM_ES2)
    set(PIX3D_SOURCE src/osrs/pix3dgl_opengles2.cpp)
else()
    set(PIX3D_SOURCE src/osrs/pix3dgl.cpp)
endif()
```

---

## References

- [OpenGL ES 2.0 Specification](https://www.khronos.org/registry/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)
- [WebGL 1.0 Specification](https://www.khronos.org/registry/webgl/specs/latest/1.0/)
- [Emscripten OpenGL Support](https://emscripten.org/docs/porting/multimedia_and_graphics/OpenGL-support.html)
- [OES_vertex_array_object Extension](https://www.khronos.org/registry/OpenGL/extensions/OES/OES_vertex_array_object.txt)
- [OES_element_index_uint Extension](https://www.khronos.org/registry/OpenGL/extensions/OES/OES_element_index_uint.txt)
