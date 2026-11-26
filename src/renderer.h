#ifndef RENDERER_H
#define RENDERER_H

#include "logo_lang.h"
#include <raylib.h>

typedef struct {
    Shader sh_2d;
    Shader sh_3d;
    Shader sh_bloom_extract;
    Shader sh_blur;
    Shader sh_post;

    /* 1x1 white quad texture for triggering fullscreen fragment shaders */
    Texture2D white_tex;

    /* GPU primitive data textures (RGBA32F, POINT filtered) */
    Texture2D prim2d_tex;   /* SCENE_MAX_2D_PRIMS x 4 */
    Texture2D prim3d_tex;   /* SCENE_MAX_3D_PRIMS x 5 */
    float *prim2d_buf;
    float *prim3d_buf;

    /* Offscreen render targets */
    RenderTexture2D scene_rt;
    int scene_rt_w, scene_rt_h;

    RenderTexture2D bloom_a;
    RenderTexture2D bloom_b;
    int bloom_w, bloom_h;
} LogoRenderer;

void renderer_init(LogoRenderer *r);
void renderer_shutdown(LogoRenderer *r);
void renderer_draw(LogoRenderer *r, const SceneIR *scene, int screen_w, int screen_h);
void renderer_draw_to(LogoRenderer *r, const SceneIR *scene, int w, int h, const RenderTexture2D *target);

#endif /* RENDERER_H */
