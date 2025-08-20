#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

extern "C" {
#include "osrs/cache.h"
#include "osrs/filelist.h"
#include "osrs/render.h"
#include "osrs/scene.h"
#include "osrs/scene_tile.h"
#include "osrs/tables/config_floortype.h"
#include "osrs/tables/config_idk.h"
#include "osrs/tables/config_locs.h"
#include "osrs/tables/config_object.h"
#include "osrs/tables/config_sequence.h"
#include "osrs/tables/configs.h"
#include "osrs/tables/sprites.h"
#include "osrs/tables/textures.h"
#include "osrs/xtea_config.h"
}

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   This tool renders a color palette using jagex's 16-bit HSL, 6 bits
//             for hue, 3 for saturation and 7 for lightness, bitpacked and
//             represented as a short.
int g_hsl16_to_rgb_table[65536];

int g_sin_table[2048];
int g_cos_table[2048];
int g_tan_table[2048];

void
init_sin_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(sin((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_sin_table[i] = (int)(sin((double)i * 0.0030679615) * (1 << 16));
}

void
init_cos_table()
{
    // 0.0030679615 = 2 * PI / 2048
    // (int)(cos((double)i * 0.0030679615) * 65536.0);
    for( int i = 0; i < 2048; i++ )
        g_cos_table[i] = (int)(cos((double)i * 0.0030679615) * (1 << 16));
}

void
init_tan_table()
{
    for( int i = 0; i < 2048; i++ )
        g_tan_table[i] = (int)(tan((double)i * 0.0030679615) * (1 << 16));
}

static int
pix3d_set_gamma(int rgb, double gamma)
{
    double r = (double)(rgb >> 16) / 256.0;
    double g = (double)(rgb >> 8 & 0xff) / 256.0;
    double b = (double)(rgb & 0xff) / 256.0;
    double powR = pow(r, gamma);
    double powG = pow(g, gamma);
    double powB = pow(b, gamma);
    int intR = (int)(powR * 256.0);
    int intG = (int)(powG * 256.0);
    int intB = (int)(powB * 256.0);
    return (intR << 16) + (intG << 8) + intB;
}

static void
pix3d_set_brightness(int* palette, double brightness)
{
    double random_brightness = brightness;
    int offset = 0;
    for( int y = 0; y < 512; y++ )
    {
        double hue = (double)(y / 8) / 64.0 + 0.0078125;
        double saturation = (double)(y & 0x7) / 8.0 + 0.0625;
        for( int x = 0; x < 128; x++ )
        {
            double lightness = (double)x / 128.0;
            double r = lightness;
            double g = lightness;
            double b = lightness;
            if( saturation != 0.0 )
            {
                double q;
                if( lightness < 0.5 )
                {
                    q = lightness * (saturation + 1.0);
                }
                else
                {
                    q = lightness + saturation - lightness * saturation;
                }
                double p = lightness * 2.0 - q;
                double t = hue + 0.3333333333333333;
                if( t > 1.0 )
                {
                    t--;
                }
                double d11 = hue - 0.3333333333333333;
                if( d11 < 0.0 )
                {
                    d11++;
                }
                if( t * 6.0 < 1.0 )
                {
                    r = p + (q - p) * 6.0 * t;
                }
                else if( t * 2.0 < 1.0 )
                {
                    r = q;
                }
                else if( t * 3.0 < 2.0 )
                {
                    r = p + (q - p) * (0.6666666666666666 - t) * 6.0;
                }
                else
                {
                    r = p;
                }
                if( hue * 6.0 < 1.0 )
                {
                    g = p + (q - p) * 6.0 * hue;
                }
                else if( hue * 2.0 < 1.0 )
                {
                    g = q;
                }
                else if( hue * 3.0 < 2.0 )
                {
                    g = p + (q - p) * (0.6666666666666666 - hue) * 6.0;
                }
                else
                {
                    g = p;
                }
                if( d11 * 6.0 < 1.0 )
                {
                    b = p + (q - p) * 6.0 * d11;
                }
                else if( d11 * 2.0 < 1.0 )
                {
                    b = q;
                }
                else if( d11 * 3.0 < 2.0 )
                {
                    b = p + (q - p) * (0.6666666666666666 - d11) * 6.0;
                }
                else
                {
                    b = p;
                }
            }
            int intR = (int)(r * 256.0);
            int intG = (int)(g * 256.0);
            int intB = (int)(b * 256.0);
            int rgb = (intR << 16) + (intG << 8) + intB;
            int rgbAdjusted = pix3d_set_gamma(rgb, random_brightness);
            palette[offset++] = rgbAdjusted;
        }
    }
}

void
init_hsl16_to_rgb_table()
{
    // 0 and 128 are both black.
    pix3d_set_brightness(g_hsl16_to_rgb_table, 0.8);
}

// Vertex structure for our 3D objects
typedef struct
{
    simd_float3 position;
    simd_float4 color;
} Vertex;

// Uniform structure for our transformation matrices
typedef struct
{
    simd_float4x4 modelViewMatrix;
    simd_float4x4 projectionMatrix;
} Uniforms;

@interface MetalRenderer : NSObject <MTKViewDelegate>
{
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLBuffer> _uniformBuffer;
    id<MTLBuffer> _axisLinesBuffer; // New buffer for axis lines
    Uniforms _uniforms;
    float _zoomLevel;
    float _yawAngle;
    float _pitchAngle;
    float _cameraHeight;
    float _cameraWorldX;
    float _cameraWorldZ;
    id<MTLDepthStencilState> _depthStencilState;

    struct Scene* _scene;
    struct Cache* _cache; // Added cache member

    // Key repeat system
    NSTimer* _keyRepeatTimer;
    NSMutableSet* _pressedKeys;

    // Key state tracking for WASD
    BOOL _wKeyPressed;
    BOOL _aKeyPressed;
    BOOL _sKeyPressed;
    BOOL _dKeyPressed;
    BOOL _rKeyPressed;
    BOOL _fKeyPressed;
}

- (instancetype)initWithMetalView:(MTKView*)metalView;
- (void)updateUniforms;
- (BOOL)loadScene:(const char*)filename;
- (void)handleRotation:(float)deltaX pitch:(float)deltaY;
- (void)moveCameraUp:(float)amount;
- (void)moveCameraDown:(float)amount;
- (void)moveCameraForward:(float)amount;
- (void)moveCameraBackward:(float)amount;
- (void)moveCameraLeft:(float)amount;
- (void)moveCameraRight:(float)amount;
- (void)handleMouseClick:(NSPoint)point inView:(MTKView*)view;
- (void)handleZoom:(float)delta;
- (void)createAxisLines;
- (void)dealloc; // Added dealloc method
- (void)keyDown:(int)keyCode;
- (void)keyUp:(int)keyCode;
- (void)updateCameraPosition;

@end

@implementation MetalRenderer

- (void)handleRotation:(float)deltaX pitch:(float)deltaY
{
    // Update both yaw and pitch based on gesture movement
    _yawAngle += deltaX;   // Rotate left/right
    _pitchAngle += deltaY; // Rotate up/down

    // Clamp pitch to prevent over-rotation
    _pitchAngle = fmaxf(-M_PI / 2, fminf(M_PI / 2, _pitchAngle));

    [self updateUniforms];
}

- (void)moveCameraUp:(float)amount
{
    _cameraHeight += amount; // Move up in Z direction
    [self updateUniforms];
}

- (void)moveCameraDown:(float)amount
{
    _cameraHeight -= amount; // Move down in Z direction
    [self updateUniforms];
}

- (void)moveCameraForward:(float)amount
{
    // Move forward in the direction the camera is facing
    // In Metal coordinates: X,Y are ground plane, Z is up
    float forwardX = -sinf(_yawAngle) * amount;
    float forwardY = -cosf(_yawAngle) * amount;

    _cameraWorldX += forwardX;
    _cameraWorldZ += forwardY;

    // Debug output (only occasionally to avoid spam)
    static int moveCount = 0;
    if( moveCount++ % 10 == 0 )
    {
        NSLog(
            @"Camera moved to: X=%.2f, Y=%.2f, Z=%.2f",
            _cameraWorldX,
            _cameraWorldZ,
            _cameraHeight);
    }

    [self updateUniforms];
}

- (void)moveCameraBackward:(float)amount
{
    // Move backward (opposite of forward)
    [self moveCameraForward:-amount];
}

- (void)moveCameraLeft:(float)amount
{
    // Move left (perpendicular to forward direction)
    // In Metal coordinates: X,Y are ground plane, Z is up
    float leftX = -cosf(_yawAngle) * amount;
    float leftY = sinf(_yawAngle) * amount;

    _cameraWorldX += leftX;
    _cameraWorldZ += leftY;
    [self updateUniforms];
}

- (void)moveCameraRight:(float)amount
{
    // Move right (opposite of left)
    [self moveCameraLeft:-amount];
}

- (void)updateUniforms
{
    // Create coordinate system transformation matrix
    simd_float4x4 coordinateTransform = {
        .columns[0] = { 1, 0, 0, 0 },
        .columns[1] = { 0, 1, 0, 0 },
        .columns[2] = { 0, 0, 1, 0 },
        .columns[3] = { 0, 0, 0, 1 }
    };

    // Create yaw rotation matrix (around Y axis in Metal coordinates)
    float cosYaw = cosf(_yawAngle);
    float sinYaw = sinf(_yawAngle);
    simd_float4x4 yawMatrix = {
        .columns[0] = { cosYaw,  0, sinYaw, 0 },
        .columns[1] = { 0,       1, 0,      0 },
        .columns[2] = { -sinYaw, 0, cosYaw, 0 },
        .columns[3] = { 0,       0, 0,      1 }
    };

    // Create pitch rotation matrix (around X axis)
    float cosPitch = cosf(_pitchAngle);
    float sinPitch = sinf(_pitchAngle);
    simd_float4x4 pitchMatrix = {
        .columns[0] = { 1, 0,        0,         0 },
        .columns[1] = { 0, cosPitch, -sinPitch, 0 },
        .columns[2] = { 0, sinPitch, cosPitch,  0 },
        .columns[3] = { 0, 0,        0,         1 }
    };

    simd_float4x4 viewMatrix = {
        .columns[0] = { 1,              0,              0,              0 },
        .columns[1] = { 0,              1,              0,              0 },
        .columns[2] = { 0,              0,              1,              0 },
        .columns[3] = { -_cameraWorldX, -_cameraHeight, -_cameraWorldZ, 1 }
    };

    simd_float4x4 zoomMatrix = {
        .columns[0] = { 1, 0, 0, 0          },
        .columns[1] = { 0, 1, 0, 0          },
        .columns[2] = { 0, 0, 1, _zoomLevel },
        .columns[3] = { 0, 0, 0, 1          }
    };

    simd_float4x4 combinedMatrix = simd_mul(
        zoomMatrix,
        simd_mul(pitchMatrix, simd_mul(yawMatrix, simd_mul(viewMatrix, coordinateTransform))));

    _uniforms.modelViewMatrix = combinedMatrix;
    _uniforms.projectionMatrix =
        matrix4x4_perspective(90.0f * (M_PI / 180.0f), 1.0f, 0.001f, 100.0f);

    memcpy(_uniformBuffer.contents, &_uniforms, sizeof(Uniforms));
}

- (BOOL)loadScene:(const char*)filename
{
    // Load cache
    NSLog(@"Attempting to load cache from: %s", CACHE_PATH);

    struct Cache* cache = cache_new_from_directory(CACHE_PATH);
    if( !cache )
    {
        NSLog(@"Failed to create cache from directory: %s", CACHE_PATH);

        // Try alternative cache paths
        const char* alt_paths[] = { "./cache", "cache", "../cache", "../../cache" };
        for( int i = 0; i < 4; i++ )
        {
            NSLog(@"Trying alternative cache path: %s", alt_paths[i]);
            cache = cache_new_from_directory(alt_paths[i]);
            if( cache )
            {
                NSLog(@"Successfully loaded cache from: %s", alt_paths[i]);
                break;
            }
        }

        if( !cache )
        {
            NSLog(@"Failed to load cache from any path");
            return NO;
        }
    }

    NSLog(@"Creating scene from map at coordinates (50, 50)");
    _scene = scene_new_from_map(cache, 50, 50);
    if( !_scene )
    {
        NSLog(@"Failed to create scene from map");
        cache_free(cache);
        return NO;
    }

    _cache = cache;

    return YES;
}

- (void)dealloc
{
    if( _scene )
    {
        scene_free(_scene);
        _scene = NULL;
    }

    if( _cache )
    {
        cache_free(_cache);
        _cache = NULL;
    }
}

- (instancetype)initWithMetalView:(MTKView*)metalView
{
    self = [super init];
    if( self )
    {
        // Initialize the color palette
        pix3d_set_brightness(g_hsl16_to_rgb_table, 0.8);

        _device = MTLCreateSystemDefaultDevice();
        metalView.device = _device;
        metalView.delegate = self;
        metalView.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);

        // Set initial zoom level, yaw, pitch, and camera height
        _zoomLevel = 8.0f; // Zoom out more to see the full scene
        _yawAngle = 0.0f;
        _pitchAngle = -0.3f;  // Look down slightly to see the ground
        _cameraHeight = 1.0f; // Raise camera higher in Z direction (Metal coordinates) to see more
                              // of the scene

        _cameraWorldX = 0.0f;
        _cameraWorldZ = 0.0f;

        // Initialize key states
        _wKeyPressed = NO;
        _aKeyPressed = NO;
        _sKeyPressed = NO;
        _dKeyPressed = NO;
        _rKeyPressed = NO;
        _fKeyPressed = NO;

        _commandQueue = [_device newCommandQueue];

        // Load the scene
        if( ![self loadScene:"../model2"] )
        {
            NSLog(@"Failed to load scene");
            return nil;
        }

        _uniformBuffer = [_device newBufferWithLength:sizeof(Uniforms)
                                              options:MTLResourceStorageModeShared];

        // Create axis lines after device initialization
        [self createAxisLines];

        // Create pipeline state
        NSError* error = nil;

        // Load the compiled shader library
        NSString* shaderPath = [[NSBundle mainBundle] pathForResource:@"Shaders"
                                                               ofType:@"metallib"];
        if( !shaderPath )
        {
            // If not found in bundle, try the current directory
            shaderPath = @"Shaders.metallib";
        }

        NSData* shaderData = [NSData dataWithContentsOfFile:shaderPath];
        if( !shaderData )
        {
            NSLog(@"Failed to load shader data from %@", shaderPath);
            return nil;
        }

        // Convert NSData to dispatch_data_t
        dispatch_data_t dispatchData = dispatch_data_create(
            shaderData.bytes,
            shaderData.length,
            dispatch_get_main_queue(),
            DISPATCH_DATA_DESTRUCTOR_DEFAULT);

        id<MTLLibrary> library = [_device newLibraryWithData:dispatchData error:&error];
        if( !library )
        {
            NSLog(@"Failed to load shader library: %@", error);
            return nil;
        }

        id<MTLFunction> vertexFunction = [library newFunctionWithName:@"vertexShader"];
        id<MTLFunction> fragmentFunction = [library newFunctionWithName:@"fragmentShader"];

        MTLRenderPipelineDescriptor* pipelineDescriptor =
            [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = metalView.colorPixelFormat;

        // Configure backface culling
        pipelineDescriptor.rasterSampleCount = 1;

        // Create vertex descriptor
        MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];

        // Position attribute
        vertexDescriptor.attributes[0].format = MTLVertexFormatFloat3;
        vertexDescriptor.attributes[0].offset = offsetof(Vertex, position);
        vertexDescriptor.attributes[0].bufferIndex = 0;

        // Color attribute
        vertexDescriptor.attributes[1].format = MTLVertexFormatFloat4;
        vertexDescriptor.attributes[1].offset = offsetof(Vertex, color);
        vertexDescriptor.attributes[1].bufferIndex = 0;

        // Set the layout
        vertexDescriptor.layouts[0].stride = sizeof(Vertex);
        vertexDescriptor.layouts[0].stepRate = 1;
        vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        // Set the vertex descriptor
        pipelineDescriptor.vertexDescriptor = vertexDescriptor;

        // Create depth stencil state for backface culling
        MTLDepthStencilDescriptor* depthStencilDesc = [[MTLDepthStencilDescriptor alloc] init];
        depthStencilDesc.depthCompareFunction = MTLCompareFunctionLess;
        depthStencilDesc.depthWriteEnabled = YES;
        id<MTLDepthStencilState> depthStencilState =
            [_device newDepthStencilStateWithDescriptor:depthStencilDesc];

        metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        metalView.clearDepth = 1.0;

        MTLRenderPipelineColorAttachmentDescriptor* colorAttachment =
            pipelineDescriptor.colorAttachments[0];
        colorAttachment.pixelFormat = metalView.colorPixelFormat;
        pipelineDescriptor.depthAttachmentPixelFormat = metalView.depthStencilPixelFormat;

        _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                                 error:&error];
        if( !_pipelineState )
        {
            NSLog(@"Failed to create pipeline state: %@", error);
            return nil;
        }

        // Store depth stencil state
        _depthStencilState = depthStencilState;

        [self updateUniforms];
    }
    return self;
}

- (void)createAxisLines
{
    const int numPoints = 64;  // Number of points in each circle
    const float radius = 0.5f; // Radius of the circles
    Vertex* axisLines = (Vertex*)malloc(numPoints * 3 * sizeof(Vertex)); // 3 circles (X, Y, Z)

    // Create circles for each axis
    for( int i = 0; i < numPoints; i++ )
    {
        float angle = (float)i / numPoints * 2.0f * M_PI;
        float x = cosf(angle) * radius;
        float y = sinf(angle) * radius;

        // XY circle (Z axis) - Metal coordinates: Z is up
        axisLines[i] = (Vertex){
            .position = simd_make_float3(x, y, 0),
            .color = simd_make_float4(0, 0, 1, 1) // Blue for Z (up)
        };

        // XZ circle (Y axis) - Metal coordinates: Y is ground plane
        axisLines[i + numPoints] = (Vertex){
            .position = simd_make_float3(x, 0, y),
            .color = simd_make_float4(0, 1, 0, 1) // Green for Y (ground plane)
        };

        // YZ circle (X axis) - Metal coordinates: X is ground plane
        axisLines[i + numPoints * 2] = (Vertex){
            .position = simd_make_float3(0, x, y),
            .color = simd_make_float4(1, 0, 0, 1) // Red for X (ground plane)
        };
    }

    _axisLinesBuffer = [_device newBufferWithBytes:axisLines
                                            length:numPoints * 3 * sizeof(Vertex)
                                           options:MTLResourceStorageModeShared];
    free(axisLines);
}

- (void)drawInMTKView:(MTKView*)view
{
    // Update camera position based on current key states
    [self updateCameraPosition];

    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;

    if( renderPassDescriptor )
    {
        id<MTLRenderCommandEncoder> renderEncoder =
            [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        [renderEncoder setRenderPipelineState:_pipelineState];
        [renderEncoder setDepthStencilState:_depthStencilState];
        [renderEncoder setCullMode:MTLCullModeFront];
        [renderEncoder setFrontFacingWinding:MTLWindingClockwise];

        // Draw axis lines
        [renderEncoder setVertexBuffer:_axisLinesBuffer offset:0 atIndex:0];
        [renderEncoder setVertexBuffer:_uniformBuffer offset:0 atIndex:1];

        // Draw each circle
        const int numPoints = 64;
        for( int i = 0; i < 3; i++ )
        {
            [renderEncoder drawPrimitives:MTLPrimitiveTypeLineStrip
                              vertexStart:i * numPoints
                              vertexCount:numPoints];
        }

        // Render all models in the scene
        if( _scene && _scene->models_length > 0 )
        {
            int renderedModels = 0;
            for( int modelIndex = 0; modelIndex < _scene->models_length; modelIndex++ )
            {
                struct SceneModel* sceneModel = &_scene->models[modelIndex];

                // Skip models without actual model data
                if( !sceneModel->model )
                    continue;

                renderedModels++;

                // Create vertex buffer for this model
                struct CacheModel* cacheModel = sceneModel->model;
                int vertexCount = cacheModel->face_count * 3;
                Vertex* vertices = (Vertex*)malloc(vertexCount * sizeof(Vertex));

                // Convert model data to Metal vertices
                for( int i = 0; i < cacheModel->face_count; i++ )
                {
                    // Get vertex indices for this face
                    int idx_a = cacheModel->face_indices_a[i];
                    int idx_b = cacheModel->face_indices_b[i];
                    int idx_c = cacheModel->face_indices_c[i];

                    if (sceneModel->lighting->face_colors_hsl_c[i] == -2)
                    continue;

                    // Bounds checking for vertex indices
                    if( idx_a < 0 || idx_a >= cacheModel->vertex_count || idx_b < 0 ||
                        idx_b >= cacheModel->vertex_count || idx_c < 0 ||
                        idx_c >= cacheModel->vertex_count )
                    {
                        NSLog(@"Warning: Invalid vertex index in model %d, face %d", modelIndex, i);
                        continue;
                    }

                    // Get vertex positions in OSRS coordinates
                    // OSRS: X and Z are ground plane, Y is up
                    // The coordinate transformation matrix will handle the conversion to Metal
                    // coordinates
                    float x_a = (cacheModel->vertices_x[idx_a] + sceneModel->region_x) / 1000.0f;
                    float y_a =
                        (cacheModel->vertices_z[idx_a] + sceneModel->region_z) / 1000.0f; // OSRS Z
                    float z_a = (cacheModel->vertices_y[idx_a] + sceneModel->region_height) /
                                1000.0f; // OSRS Y

                    float x_b = (cacheModel->vertices_x[idx_b] + sceneModel->region_x) / 1000.0f;
                    float y_b =
                        (cacheModel->vertices_z[idx_b] + sceneModel->region_z) / 1000.0f; // OSRS Z
                    float z_b = (cacheModel->vertices_y[idx_b] + sceneModel->region_height) /
                                1000.0f; // OSRS Y

                    float x_c = (cacheModel->vertices_x[idx_c] + sceneModel->region_x) / 1000.0f;
                    float y_c =
                        (cacheModel->vertices_z[idx_c] + sceneModel->region_z) / 1000.0f; // OSRS Z
                    float z_c = (cacheModel->vertices_y[idx_c] + sceneModel->region_height) /
                                1000.0f; // OSRS Y

                    // simd_float3 pos_a = simd_make_float3(x_a, y_a, z_a);
                    // simd_float3 pos_b = simd_make_float3(x_b, y_b, z_b);
                    // simd_float3 pos_c = simd_make_float3(x_c, y_c, z_c);
                    simd_float3 pos_a = simd_make_float3(x_a, -z_a, y_a);
                    simd_float3 pos_b = simd_make_float3(x_b, -z_b, y_b);
                    simd_float3 pos_c = simd_make_float3(x_c, -z_c, y_c);

                    // Use lighting colors if available, otherwise fall back to original colors
                    int faceColor;
                    if( sceneModel->lighting && sceneModel->lighting->face_colors_hsl_a )
                    {
                        faceColor = sceneModel->lighting->face_colors_hsl_a[i];
                    }
                    else
                    {
                        faceColor = cacheModel->face_colors[i];
                    }

                    // Convert color from HSL to RGB using the lookup table
                    if( faceColor >= 0 && faceColor < 65536 )
                    {
                        int rgb_color = g_hsl16_to_rgb_table[faceColor];

                        // Convert RGB to float4
                        simd_float4 color_vec = simd_make_float4(
                            ((rgb_color >> 16) & 0xFF) / 255.0f,
                            ((rgb_color >> 8) & 0xFF) / 255.0f,
                            (rgb_color & 0xFF) / 255.0f,
                            1.0f);

                        // Set vertices for this triangle
                        vertices[i * 3] = (Vertex){ pos_a, color_vec };
                        vertices[i * 3 + 1] = (Vertex){ pos_b, color_vec };
                        vertices[i * 3 + 2] = (Vertex){ pos_c, color_vec };
                    }
                    else
                    {
                        // Use a default color if the face color is invalid
                        simd_float4 color_vec = simd_make_float4(0.5f, 0.5f, 0.5f, 1.0f);
                        vertices[i * 3] = (Vertex){ pos_a, color_vec };
                        vertices[i * 3 + 1] = (Vertex){ pos_b, color_vec };
                        vertices[i * 3 + 2] = (Vertex){ pos_c, color_vec };
                    }
                }

                // Create temporary buffer for this model
                id<MTLBuffer> modelVertexBuffer =
                    [_device newBufferWithBytes:vertices
                                         length:vertexCount * sizeof(Vertex)
                                        options:MTLResourceStorageModeShared];

                // Set vertex buffer and draw
                [renderEncoder setVertexBuffer:modelVertexBuffer offset:0 atIndex:0];
                [renderEncoder setVertexBuffer:_uniformBuffer offset:0 atIndex:1];

                [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                                  vertexStart:0
                                  vertexCount:vertexCount];

                // Clean up
                free(vertices);
            }
        }

        [renderEncoder endEncoding];
        [commandBuffer presentDrawable:view.currentDrawable];
    }

    [commandBuffer commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
    // Handle window resize
    float aspect = size.width / size.height;
    _uniforms.projectionMatrix =
        matrix4x4_perspective(65.0f * (M_PI / 180.0f), aspect, 0.1f, 100.0f);
    memcpy(_uniformBuffer.contents, &_uniforms, sizeof(Uniforms));
}

// Helper functions for matrix operations
static simd_float4x4
matrix4x4_identity(void)
{
    simd_float4x4 m = {
        .columns[0] = { 1, 0, 0, 0 },
        .columns[1] = { 0, 1, 0, 0 },
        .columns[2] = { 0, 0, 1, 0 },
        .columns[3] = { 0, 0, 0, 1 }
    };
    return m;
}

static simd_float4x4
matrix4x4_perspective(float fovy, float aspect, float near, float far)
{
    float y = 1.0f / tanf(fovy * 0.5f);
    float x = y / aspect;
    float z = far / (far - near);

    simd_float4x4 m = {
        .columns[0] = { x, 0, 0,         0 },
        .columns[1] = { 0, y, 0,         0 },
        .columns[2] = { 0, 0, z,         1 },
        .columns[3] = { 0, 0, -z * near, 0 }
    };
    return m;
}

- (void)handleMouseClick:(NSPoint)point inView:(MTKView*)view
{}

- (void)handleZoom:(float)delta
{
    // Adjust zoom level based on vertical scroll
    // delta > 0 means scroll up (zoom in)
    // delta < 0 means scroll down (zoom out)
    _zoomLevel = fmaxf(0.5f, fminf(5.0f, _zoomLevel - delta * 0.1f));
    [self updateUniforms];
}

- (void)updateCameraPosition
{
    float moveSpeed = 1.0f;
    float rotateSpeed = 0.005f; // Speed of rotation

    //    if( w_pressed )
    //         {
    //             game.camera_x += (g_sin_table[game.camera_yaw] * speed) >> 16;
    //             game.camera_y -= (g_cos_table[game.camera_yaw] * speed) >> 16;
    //             camera_moved = 1;
    //         }

    //         if( a_pressed )
    //         {
    //             game.camera_x += (g_cos_table[game.camera_yaw] * speed) >> 16;
    //             game.camera_y += (g_sin_table[game.camera_yaw] * speed) >> 16;
    //             camera_moved = 1;
    //         }

    //         if( s_pressed )
    //         {
    //             game.camera_x -= (g_sin_table[game.camera_yaw] * speed) >> 16;
    //             game.camera_y += (g_cos_table[game.camera_yaw] * speed) >> 16;
    //             camera_moved = 1;
    //         }

    if( _wKeyPressed )
    {
        _cameraWorldX += (sin(_yawAngle) * moveSpeed);
        _cameraWorldZ += (cos(_yawAngle) * moveSpeed);
    }
    if( _sKeyPressed )
    {
        _cameraWorldX -= (sin(_yawAngle) * moveSpeed);
        _cameraWorldZ -= (cos(_yawAngle) * moveSpeed);
    }
    if( _aKeyPressed )
    {
        _cameraWorldX -= (cos(_yawAngle) * moveSpeed);
        _cameraWorldZ += (sin(_yawAngle) * moveSpeed);
    }
    if( _dKeyPressed )
    {
        _cameraWorldX += (cos(_yawAngle) * moveSpeed);
        _cameraWorldZ -= (sin(_yawAngle) * moveSpeed);
    }

    // Update camera height based on arrow keys
    // In Metal coordinates: Z is up, so up arrow increases Z
    if( _rKeyPressed )
    {
        _cameraHeight += moveSpeed;
    }
    if( _fKeyPressed )
    {
        _cameraHeight -= moveSpeed;
    }

    [self updateUniforms];
}

- (void)keyDown:(int)keyCode
{
    switch( keyCode )
    {
    case 13: // W key
        _wKeyPressed = YES;
        break;
    case 0: // A key
        _aKeyPressed = YES;
        break;
    case 1: // S key
        _sKeyPressed = YES;
        break;
    case 2: // D key
        _dKeyPressed = YES;
        break;
    case 15: // R key
        _rKeyPressed = YES;
        break;
    case 3: // F key
        _fKeyPressed = YES;
        break;
    }
}

- (void)keyUp:(int)keyCode
{
    switch( keyCode )
    {
    case 13: // W key
        _wKeyPressed = NO;
        break;
    case 0: // A key
        _aKeyPressed = NO;
        break;
    case 1: // S key
        _sKeyPressed = NO;
        break;
    case 2: // D key
        _dKeyPressed = NO;
        break;
    case 15: // R key
        _rKeyPressed = NO;
        break;
    case 3: // F key
        _fKeyPressed = NO;
        break;
    }
}

@end

@interface MetalView : MTKView
{
    NSPanGestureRecognizer* _panGestureRecognizer;
}
@end

@implementation MetalView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if( self )
    {
        // Create and configure the pan gesture recognizer
        _panGestureRecognizer =
            [[NSPanGestureRecognizer alloc] initWithTarget:self
                                                    action:@selector(handlePanGesture:)];
        _panGestureRecognizer.numberOfTouchesRequired = 2; // Require two fingers
        [self addGestureRecognizer:_panGestureRecognizer];
    }
    return self;
}

- (void)handlePanGesture:(NSPanGestureRecognizer*)gesture
{
    MetalRenderer* renderer = (MetalRenderer*)self.delegate;
    NSPoint translation = [gesture translationInView:self];

    // Convert the translation to rotation angles
    // Scale the movement to control rotation speed
    float rotationSpeed = 0.01f;
    float deltaX = translation.x * rotationSpeed;
    float deltaY = -translation.y * rotationSpeed;

    // Update the model's rotation
    [renderer handleRotation:deltaX pitch:deltaY];

    // Reset the translation to prevent accumulation
    [gesture setTranslation:NSZeroPoint inView:self];
}

- (void)keyDown:(NSEvent*)event
{
    MetalRenderer* renderer = (MetalRenderer*)self.delegate;

    // Handle WASD and arrow keys through the renderer's key state system
    switch( event.keyCode )
    {
    case 13: // W key
    case 0:  // A key
    case 1:  // S key
    case 2:  // D key
    case 15: // R key
    case 3:  // F key
             // case 126: // Up arrow
             // case 125: // Down arrow
        [renderer keyDown:event.keyCode];
        break;
    case 53: // Escape key
        [NSApp terminate:nil];
        break;
    }
}

- (void)keyUp:(NSEvent*)event
{
    MetalRenderer* renderer = (MetalRenderer*)self.delegate;

    // Handle WASD and arrow keys through the renderer's key state system
    switch( event.keyCode )
    {
    case 13: // W key
    case 0:  // A key
    case 1:  // S key
    case 2:  // D key
    case 15: // R key
    case 3:  // F key
             // case 126: // Up arrow
             // case 125: // Down arrow
        [renderer keyUp:event.keyCode];
        break;
    }
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    MetalRenderer* renderer = (MetalRenderer*)self.delegate;
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    [renderer handleMouseClick:point inView:self];
}

- (void)scrollWheel:(NSEvent*)event
{
    MetalRenderer* renderer = (MetalRenderer*)self.delegate;
    // Only handle vertical scroll for zoom
    [renderer handleZoom:event.deltaY];
}

@end

@interface CustomWindow : NSWindow
@end

@implementation CustomWindow

- (BOOL)acceptsFirstResponder
{
    return YES;
}

@end

@interface WindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation WindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
    [NSApp terminate:nil];
}

@end

int
main(int argc, const char* argv[])
{
    init_hsl16_to_rgb_table();
    init_sin_table();
    init_cos_table();
    init_tan_table();

    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        printf("Failed to load xtea keys\n");
        return 1;
    }

    @autoreleasepool
    {
        // Create the application first
        NSApplication* app = [NSApplication sharedApplication];

        // Set up the application as a GUI application
        // Otherwise the terminal will maintain keyboard focus
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        // Create the window
        CustomWindow* window = [[CustomWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 800, 600)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];

        // Set up window delegate
        WindowDelegate* windowDelegate = [[WindowDelegate alloc] init];
        window.delegate = windowDelegate;

        // Create and set up the Metal view
        MetalView* metalView = [[MetalView alloc] initWithFrame:window.contentView.bounds];
        [window.contentView addSubview:metalView];

        // Create the renderer
        MetalRenderer* renderer = [[MetalRenderer alloc] initWithMetalView:metalView];
        if( !renderer )
        {
            NSLog(@"Failed to create renderer");
            return 1;
        }

        // Configure window for input
        [window setAcceptsMouseMovedEvents:YES];

        [window makeKeyAndOrderFront:nil];
        [window makeMainWindow];
        [window makeFirstResponder:metalView];
        [window setInitialFirstResponder:metalView];

        // Bring window to front
        [NSApp activateIgnoringOtherApps:YES];
        [window orderFrontRegardless];

        // Run the application
        [app run];
    }
    return 0;
}