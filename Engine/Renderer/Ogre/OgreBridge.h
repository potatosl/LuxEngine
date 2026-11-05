#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LuxOgreContext LuxOgreContext;
typedef struct LuxOgreTexture LuxOgreTexture;

typedef struct LuxOgreVertex {
    float x, y, z;
    float r, g, b, a;
    float u, v;
} LuxOgreVertex;

LuxOgreContext* lux_ogre_create(void* sdl_window, const char* plugin_path, int initial_vsync,
                                char* error, size_t error_capacity);
void lux_ogre_destroy(LuxOgreContext* context);
int lux_ogre_begin_frame(LuxOgreContext* context);
int lux_ogre_clear(LuxOgreContext* context, float r, float g, float b, float a);
int lux_ogre_draw_triangles(LuxOgreContext* context, const LuxOgreVertex* vertices, size_t count,
                            const float* view_projection_column_major, LuxOgreTexture* texture,
                            int depth);
int lux_ogre_end_frame(LuxOgreContext* context);
int lux_ogre_set_vsync(LuxOgreContext* context, int enabled);
LuxOgreTexture* lux_ogre_texture_create(LuxOgreContext* context, unsigned width, unsigned height,
                                        const void* rgba_pixels);
void lux_ogre_texture_destroy(LuxOgreContext* context, LuxOgreTexture* texture);

#ifdef __cplusplus
}
#endif
