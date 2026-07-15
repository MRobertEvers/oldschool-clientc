#include "trspk_metal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <math.h>
#include <string.h>

static void
trspk_metal_mat4_mul_colmajor(const float* a, const float* b, float* out)
{
    float r[16];
    for( int col = 0; col < 4; ++col )
    {
        for( int row = 0; row < 4; ++row )
        {
            r[col * 4 + row] = a[0 * 4 + row] * b[col * 4 + 0] +
                               a[1 * 4 + row] * b[col * 4 + 1] +
                               a[2 * 4 + row] * b[col * 4 + 2] +
                               a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

void*
trspk_metal_create_world_pipeline(void* device_handle, uint32_t color_pixel_format)
{
    id<MTLDevice> device = (__bridge id<MTLDevice>)device_handle;
    if( !device )
        return NULL;

    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSString* exePath = [[NSBundle mainBundle] executablePath];
    if( exePath.length > 0 )
    {
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        [candidates addObject:[exeDir stringByAppendingPathComponent:@"Shaders.metallib"]];
    }
    NSString* bundleShader = [[NSBundle mainBundle] pathForResource:@"Shaders" ofType:@"metallib"];
    if( bundleShader.length > 0 )
        [candidates addObject:bundleShader];
    [candidates addObject:@"Shaders.metallib"];
    [candidates addObject:@"../Shaders.metallib"];
    [candidates addObject:@"../src/Shaders.metallib"];

    NSError* error = nil;
    id<MTLLibrary> library = nil;
    for( NSString* path in candidates )
    {
        if( path.length == 0 )
            continue;
        NSData* data = [NSData dataWithContentsOfFile:path];
        if( !data )
            continue;
        dispatch_data_t dd = dispatch_data_create(
            data.bytes, data.length, dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        library = [device newLibraryWithData:dd error:&error];
        if( library )
            break;
    }

    id<MTLFunction> vert = library ? [library newFunctionWithName:@"vertexShader"] : nil;
    id<MTLFunction> frag = library ? [library newFunctionWithName:@"fragmentShader"] : nil;
    id<MTLRenderPipelineState> pipeline = nil;
    if( vert && frag )
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vert;
        desc.fragmentFunction = frag;
        desc.colorAttachments[0].pixelFormat = (MTLPixelFormat)color_pixel_format;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        [desc release];
    }

    if( vert )
        [vert release];
    if( frag )
        [frag release];
    if( library )
        [library release];
    return (void*)pipeline;
}

float
trspk_metal_dash_yaw_to_radians(int dash_yaw)
{
    return ((float)dash_yaw * 2.0f * (float)M_PI) / 2048.0f;
}

void
trspk_metal_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw = cosf(-yaw);
    float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;
    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;
    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

void
trspk_metal_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;
    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;
    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

void
trspk_metal_remap_projection_opengl_to_metal_z(float* proj_colmajor)
{
    static const float k_clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f,
    };
    float tmp[16];
    trspk_metal_mat4_mul_colmajor(k_clip_z, proj_colmajor, tmp);
    memcpy(proj_colmajor, tmp, sizeof(tmp));
}
