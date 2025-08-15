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

// Model structure to match the C code
typedef struct
{
    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    int* face_colors;
} Model;

@interface MetalRenderer : NSObject <MTKViewDelegate>
{
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _uniformBuffer;
    id<MTLBuffer> _axisLinesBuffer; // New buffer for axis lines
    Uniforms _uniforms;
    Model _model;
    float _zoomLevel;
    float _yawAngle;
    float _pitchAngle;
    float _cameraHeight;
    id<MTLDepthStencilState> _depthStencilState;
}

- (instancetype)initWithMetalView:(MTKView*)metalView;
- (void)updateUniforms;
- (BOOL)loadModel:(const char*)filename;
- (void)handleRotation:(float)deltaX pitch:(float)deltaY;
- (void)moveCameraUp:(float)amount;
- (void)moveCameraDown:(float)amount;
- (void)handleMouseClick:(NSPoint)point inView:(MTKView*)view;
- (void)handleZoom:(float)delta;
- (void)createAxisLines;

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
    _cameraHeight += amount;
    [self updateUniforms];
}

- (void)moveCameraDown:(float)amount
{
    _cameraHeight -= amount;
    [self updateUniforms];
}

- (void)updateUniforms
{
    // Create rotation matrix for 180-degree roll (around Z axis)
    float cosRoll = cosf(M_PI); // 180 degrees = π radians
    float sinRoll = sinf(M_PI);
    simd_float4x4 rollMatrix = {
        .columns[0] = { cosRoll, -sinRoll, 0, 0 },
        .columns[1] = { sinRoll, cosRoll,  0, 0 },
        .columns[2] = { 0,       0,        1, 0 },
        .columns[3] = { 0,       0,        0, 1 }
    };

    // Create yaw rotation matrix (around Y axis)
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

    // Create translation matrix to move model back (zoom out) and up/down
    simd_float4x4 translationMatrix = {
        .columns[0] = { 1, 0,             0,          0 },
        .columns[1] = { 0, 1,             0,          0 },
        .columns[2] = { 0, 0,             1,          0 },
        .columns[3] = { 0, _cameraHeight, _zoomLevel, 1 }
    };

    // Combine matrices: first translate to origin, then rotate, then translate back
    // This ensures the model rotates around its own center
    simd_float4x4 combinedMatrix =
        simd_mul(translationMatrix, simd_mul(pitchMatrix, simd_mul(yawMatrix, rollMatrix)));
    _uniforms.modelViewMatrix = combinedMatrix;
    _uniforms.projectionMatrix = matrix4x4_perspective(90.0f * (M_PI / 180.0f), 1.0f, 0.1f, 100.0f);

    memcpy(_uniformBuffer.contents, &_uniforms, sizeof(Uniforms));
}

- (BOOL)loadModel:(const char*)filename
{
    // Load vertices file
    struct Cache* cache = cache_new_from_directory(CACHE_PATH);

    struct CacheModel* model = model_new_from_cache(cache, 9319);

    _model.vertex_count = model->vertex_count;
    _model.vertices_x = model->vertices_x;
    _model.vertices_y = model->vertices_y;
    _model.vertices_z = model->vertices_z;
    _model.face_count = model->face_count;
    _model.face_indices_a = model->face_indices_a;
    _model.face_indices_b = model->face_indices_b;
    _model.face_indices_c = model->face_indices_c;
    _model.face_colors = model->face_colors;

    return YES;
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
        _zoomLevel = 2.0f;
        _yawAngle = 0.0f;
        _pitchAngle = 0.0f;
        _cameraHeight = -0.35f;

        _commandQueue = [_device newCommandQueue];

        // Load the model
        if( ![self loadModel:"../model2"] )
        {
            NSLog(@"Failed to load model");
            return nil;
        }

        // Convert model data to Metal vertices
        Vertex* vertices = (Vertex*)malloc(_model.face_count * 3 * sizeof(Vertex));
        for( int i = 0; i < _model.face_count; i++ )
        {
            // Get vertex indices for this face
            int idx_a = _model.face_indices_a[i];
            int idx_b = _model.face_indices_b[i];
            int idx_c = _model.face_indices_c[i];

            // Get vertex positions
            simd_float3 pos_a = simd_make_float3(
                _model.vertices_x[idx_a] / 1000.0f, // Scale down for better view
                _model.vertices_y[idx_a] / 1000.0f,
                _model.vertices_z[idx_a] / 1000.0f);
            simd_float3 pos_b = simd_make_float3(
                _model.vertices_x[idx_b] / 1000.0f,
                _model.vertices_y[idx_b] / 1000.0f,
                _model.vertices_z[idx_b] / 1000.0f);
            simd_float3 pos_c = simd_make_float3(
                _model.vertices_x[idx_c] / 1000.0f,
                _model.vertices_y[idx_c] / 1000.0f,
                _model.vertices_z[idx_c] / 1000.0f);

            // Convert color from HSL to RGB using the lookup table
            int hsl_color = _model.face_colors[i];
            int rgb_color = g_hsl16_to_rgb_table[hsl_color];

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

        _vertexBuffer = [_device newBufferWithBytes:vertices
                                             length:_model.face_count * 3 * sizeof(Vertex)
                                            options:MTLResourceStorageModeShared];
        free(vertices);

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

        // Configure the view for depth testing
        metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        metalView.clearDepth = 1.0;

        // Set cull mode to back
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

        // XY circle (Z axis)
        axisLines[i] = (Vertex){
            .position = simd_make_float3(x, y, 0),
            .color = simd_make_float4(0, 0, 1, 1) // Blue for Z
        };

        // XZ circle (Y axis)
        axisLines[i + numPoints] = (Vertex){
            .position = simd_make_float3(x, 0, y),
            .color = simd_make_float4(0, 1, 0, 1) // Green for Y
        };

        // YZ circle (X axis)
        axisLines[i + numPoints * 2] = (Vertex){
            .position = simd_make_float3(0, x, y),
            .color = simd_make_float4(1, 0, 0, 1) // Red for X
        };
    }

    _axisLinesBuffer = [_device newBufferWithBytes:axisLines
                                            length:numPoints * 3 * sizeof(Vertex)
                                           options:MTLResourceStorageModeShared];
    free(axisLines);
}

- (void)drawInMTKView:(MTKView*)view
{
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;

    if( renderPassDescriptor )
    {
        id<MTLRenderCommandEncoder> renderEncoder =
            [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        [renderEncoder setRenderPipelineState:_pipelineState];
        [renderEncoder setDepthStencilState:_depthStencilState];
        [renderEncoder setCullMode:MTLCullModeBack];
        [renderEncoder setFrontFacingWinding:MTLWindingClockwise];

        // Draw the model
        [renderEncoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0];
        [renderEncoder setVertexBuffer:_uniformBuffer offset:0 atIndex:1];
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle
                          vertexStart:0
                          vertexCount:_model.face_count * 3];

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
    float moveAmount = 0.1f; // Adjust this value to change movement speed

    switch( event.keyCode )
    {
    case 126: // Up arrow
        [renderer moveCameraUp:moveAmount];
        break;
    case 125: // Down arrow
        [renderer moveCameraDown:moveAmount];
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