#include "renderer.h"
#include "shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static time_t shader_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return st.st_mtime;
    return 0;
}

static void ensure_post_rt(LogoRenderer *r, int w, int h) {
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (r->post_rt_w != w || r->post_rt_h != h) {
        if (r->post_rt.id != 0) UnloadRenderTexture(r->post_rt);
        r->post_rt = LoadRenderTexture(w, h);
        SetTextureFilter(r->post_rt.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(r->post_rt.texture, TEXTURE_WRAP_CLAMP);
        r->post_rt_w = w;
        r->post_rt_h = h;
    }
}

static uint64_t fnv1a_hash(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint64_t)(*p);
        h *= 1099511628211ULL;
    }
    return h;
}

static void ensure_custom_shader(LogoRenderer *r, const SceneIR *s, double time_sec) {
    if (!s->custom_shader_enabled) return;

    /* Case 1: Inline GLSL source passed via multiline """...""" string */
    if (s->custom_shader_code && s->custom_shader_code[0]) {
        uint64_t h = fnv1a_hash(s->custom_shader_code);
        if (r->custom_is_inline && r->custom_code_hash == h && r->sh_custom.id != 0) return;

        if (r->sh_custom.id != 0) {
            UnloadShader(r->sh_custom);
            r->sh_custom = (Shader){0};
        }
        r->custom_is_inline = true;
        r->custom_code_hash = h;
        r->custom_path[0] = '\0';
        r->sh_custom = LoadShaderFromMemory(NULL, s->custom_shader_code);
        return;
    }

    /* Case 2: External .fs file path with automatic hot-reload */
    if (!s->custom_shader_path[0]) return;
    bool path_changed = r->custom_is_inline || strcmp(r->custom_path, s->custom_shader_path) != 0;
    bool check_reload = path_changed || (time_sec - r->custom_check_t >= 0.35) || (time_sec < r->custom_check_t);
    if (!check_reload) return;
    r->custom_check_t = time_sec;

    time_t mt = shader_file_mtime(s->custom_shader_path);
    if (!path_changed && mt == r->custom_mtime && r->sh_custom.id != 0) return;

    if (r->sh_custom.id != 0) {
        UnloadShader(r->sh_custom);
        r->sh_custom = (Shader){0};
    }
    r->custom_is_inline = false;
    r->custom_code_hash = 0;
    snprintf(r->custom_path, sizeof(r->custom_path), "%s", s->custom_shader_path);
    r->custom_mtime = mt;
    if (mt != 0) {
        r->sh_custom = LoadShader(NULL, s->custom_shader_path);
    }
}

static Texture2D make_float_data_tex(int w, int h) {
    Image img = {0};
    img.data = calloc((size_t)w * (size_t)h * 4, sizeof(float));
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
    free(img.data);
    return tex;
}

static void ensure_scene_rt(LogoRenderer *r, int w, int h, bool pixel_filter) {
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (r->scene_rt_w != w || r->scene_rt_h != h) {
        if (r->scene_rt.id != 0) UnloadRenderTexture(r->scene_rt);
        r->scene_rt = LoadRenderTexture(w, h);
        r->scene_rt_w = w;
        r->scene_rt_h = h;
    }
    SetTextureFilter(r->scene_rt.texture, pixel_filter ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(r->scene_rt.texture, TEXTURE_WRAP_CLAMP);
}

static void ensure_bloom_rts(LogoRenderer *r, int w, int h) {
    int bw = w / 3;
    int bh = h / 3;
    if (bw < 64) bw = 64;
    if (bh < 36) bh = 36;
    if (r->bloom_w != bw || r->bloom_h != bh) {
        if (r->bloom_a.id != 0) UnloadRenderTexture(r->bloom_a);
        if (r->bloom_b.id != 0) UnloadRenderTexture(r->bloom_b);
        r->bloom_a = LoadRenderTexture(bw, bh);
        r->bloom_b = LoadRenderTexture(bw, bh);
        SetTextureFilter(r->bloom_a.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(r->bloom_b.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(r->bloom_a.texture, TEXTURE_WRAP_CLAMP);
        SetTextureWrap(r->bloom_b.texture, TEXTURE_WRAP_CLAMP);
        r->bloom_w = bw;
        r->bloom_h = bh;
    }
}

void renderer_init(LogoRenderer *r) {
    memset(r, 0, sizeof(*r));
    r->sh_2d            = LoadShaderFromMemory(NULL, FRAG_SCENE_2D);
    r->sh_3d            = LoadShaderFromMemory(NULL, FRAG_SCENE_3D);
    r->sh_bloom_extract = LoadShaderFromMemory(NULL, FRAG_BLOOM_EXTRACT);
    r->sh_blur          = LoadShaderFromMemory(NULL, FRAG_BLUR);
    r->sh_post          = LoadShaderFromMemory(NULL, FRAG_POST);

    Image white = GenImageColor(1, 1, WHITE);
    r->white_tex = LoadTextureFromImage(white);
    UnloadImage(white);

    r->prim2d_tex = make_float_data_tex(SCENE_MAX_2D_PRIMS, 4);
    r->prim3d_tex = make_float_data_tex(SCENE_MAX_3D_PRIMS, 5);
    r->prim2d_buf = (float *)calloc((size_t)SCENE_MAX_2D_PRIMS * 4 * 4, sizeof(float));
    r->prim3d_buf = (float *)calloc((size_t)SCENE_MAX_3D_PRIMS * 5 * 4, sizeof(float));
}

void renderer_shutdown(LogoRenderer *r) {
    UnloadShader(r->sh_2d);
    UnloadShader(r->sh_3d);
    UnloadShader(r->sh_bloom_extract);
    UnloadShader(r->sh_blur);
    UnloadShader(r->sh_post);
    if (r->sh_custom.id != 0) UnloadShader(r->sh_custom);
    UnloadTexture(r->white_tex);
    UnloadTexture(r->prim2d_tex);
    UnloadTexture(r->prim3d_tex);
    if (r->scene_rt.id != 0) UnloadRenderTexture(r->scene_rt);
    if (r->post_rt.id != 0) UnloadRenderTexture(r->post_rt);
    if (r->bloom_a.id != 0) UnloadRenderTexture(r->bloom_a);
    if (r->bloom_b.id != 0) UnloadRenderTexture(r->bloom_b);
    free(r->prim2d_buf);
    free(r->prim3d_buf);
    memset(r, 0, sizeof(*r));
}

static void upload_prims2d(LogoRenderer *r, const SceneIR *s) {
    int n = s->prim2d_count;
    int stride = SCENE_MAX_2D_PRIMS * 4;
    for (int i = 0; i < n; i++) {
        const Prim2D *p = &s->prims2d[i];
        float *row0 = r->prim2d_buf + 0 * stride + i * 4;
        float *row1 = r->prim2d_buf + 1 * stride + i * 4;
        float *row2 = r->prim2d_buf + 2 * stride + i * 4;
        float *row3 = r->prim2d_buf + 3 * stride + i * 4;

        row0[0] = (float)p->type; row0[1] = p->x0; row0[2] = p->y0; row0[3] = p->x1;
        row1[0] = p->y1;          row1[1] = p->x2; row1[2] = p->y2; row1[3] = p->radius;
        row2[0] = p->r;           row2[1] = p->g;  row2[2] = p->b;  row2[3] = p->a;
        row3[0] = p->glow;        row3[1] = (float)p->blend; row3[2] = 0; row3[3] = 0;
    }
    if (n > 0) UpdateTexture(r->prim2d_tex, r->prim2d_buf);
}

static void upload_prims3d(LogoRenderer *r, const SceneIR *s) {
    int n = s->prim3d_count;
    int stride = SCENE_MAX_3D_PRIMS * 4;
    for (int i = 0; i < n; i++) {
        const Prim3D *p = &s->prims3d[i];
        float *row0 = r->prim3d_buf + 0 * stride + i * 4;
        float *row1 = r->prim3d_buf + 1 * stride + i * 4;
        float *row2 = r->prim3d_buf + 2 * stride + i * 4;
        float *row3 = r->prim3d_buf + 3 * stride + i * 4;
        float *row4 = r->prim3d_buf + 4 * stride + i * 4;

        row0[0] = (float)p->type; row0[1] = p->px; row0[2] = p->py; row0[3] = p->pz;
        row1[0] = p->fx;          row1[1] = p->fy; row1[2] = p->fz; row1[3] = p->p0;
        row2[0] = p->ux;          row2[1] = p->uy; row2[2] = p->uz; row2[3] = p->p1;
        row3[0] = p->r;           row3[1] = p->g;  row3[2] = p->b;  row3[3] = p->p2;
        row4[0] = (float)p->mat_type;
        row4[1] = p->mat_param;
        row4[2] = p->smooth_k;
        row4[3] = (float)p->op_type;
    }
    if (n > 0) UpdateTexture(r->prim3d_tex, r->prim3d_buf);
}

void renderer_draw(LogoRenderer *r, const SceneIR *s, int screen_w, int screen_h, double time_sec) {
    renderer_draw_to(r, s, screen_w, screen_h, time_sec, NULL);
}

void renderer_draw_to(LogoRenderer *r, const SceneIR *s, int screen_w, int screen_h, double time_sec, const RenderTexture2D *target) {
    /* Decide internal render target resolution */
    int target_w = screen_w;
    int target_h = screen_h;
    bool pixel_art = false;

    if (s->mode == RENDER_MODE_RETRO || s->retro_fx_enabled) {
        target_w = s->retro_w;
        target_h = s->retro_h;
        pixel_art = true;
    } else if (s->mode == RENDER_MODE_3D) {
        /* Crisp 3D raymarching resolution (keeps 60+ FPS smoothly on integrated GPUs) */
        target_w = (screen_w > 960) ? 960 : screen_w;
        target_h = (screen_h > 540) ? 540 : screen_h;
    }

    ensure_scene_rt(r, target_w, target_h, pixel_art);
    ensure_bloom_rts(r, screen_w, screen_h);

    /* ---- Pass 1: Render Scene via Hidden 2D/Retro or 3D SDF Shader ---- */
    if (s->mode == RENDER_MODE_3D) {
        upload_prims3d(r, s);
        float res[2] = {(float)target_w, (float)target_h};
        int shadows = s->light_shadows ? 1 : 0;
        int fog_en  = s->fog_enabled ? 1 : 0;

        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uPrimCount"), &s->prim3d_count, SHADER_UNIFORM_INT);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uResolution"), res, SHADER_UNIFORM_VEC2);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uCamPos"), s->cam_pos, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uCamLook"), s->cam_look, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uCamFov"), &s->cam_fov, SHADER_UNIFORM_FLOAT);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uSkyTop"), s->sky_top, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uSkyBot"), s->sky_bot, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uLightDir"), s->light_dir, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uLightColor"), s->light_color, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uShadows"), &shadows, SHADER_UNIFORM_INT);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uFogEnabled"), &fog_en, SHADER_UNIFORM_INT);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uFogColor"), s->fog_color, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uFogNear"), &s->fog_near, SHADER_UNIFORM_FLOAT);
        SetShaderValue(r->sh_3d, GetShaderLocation(r->sh_3d, "uFogFar"), &s->fog_far, SHADER_UNIFORM_FLOAT);

        BeginTextureMode(r->scene_rt);
            ClearBackground(BLACK);
            BeginShaderMode(r->sh_3d);
                SetShaderValueTexture(r->sh_3d, GetShaderLocation(r->sh_3d, "uPrims"), r->prim3d_tex);
                DrawTexturePro(r->white_tex,
                               (Rectangle){0, 0, 1, 1},
                               (Rectangle){0, 0, (float)target_w, (float)target_h},
                               (Vector2){0, 0}, 0.0f, WHITE);
            EndShaderMode();
        EndTextureMode();
    } else {
        upload_prims2d(r, s);
        float res[2] = {(float)target_w, (float)target_h};
        float clr[3] = {s->clear_r, s->clear_g, s->clear_b};
        int is_retro = (s->mode == RENDER_MODE_RETRO) ? 1 : 0;

        SetShaderValue(r->sh_2d, GetShaderLocation(r->sh_2d, "uPrimCount"), &s->prim2d_count, SHADER_UNIFORM_INT);
        SetShaderValue(r->sh_2d, GetShaderLocation(r->sh_2d, "uResolution"), res, SHADER_UNIFORM_VEC2);
        SetShaderValue(r->sh_2d, GetShaderLocation(r->sh_2d, "uClearColor"), clr, SHADER_UNIFORM_VEC3);
        SetShaderValue(r->sh_2d, GetShaderLocation(r->sh_2d, "uIsRetro"), &is_retro, SHADER_UNIFORM_INT);

        BeginTextureMode(r->scene_rt);
            ClearBackground(BLACK);
            BeginShaderMode(r->sh_2d);
                SetShaderValueTexture(r->sh_2d, GetShaderLocation(r->sh_2d, "uPrims"), r->prim2d_tex);
                DrawTexturePro(r->white_tex,
                               (Rectangle){0, 0, 1, 1},
                               (Rectangle){0, 0, (float)target_w, (float)target_h},
                               (Vector2){0, 0}, 0.0f, WHITE);
            EndShaderMode();
        EndTextureMode();
    }

    /* ---- Pass 2: Optional 2-Pass Separable Bloom ---- */
    if (s->bloom_enabled) {
        BeginTextureMode(r->bloom_a);
            ClearBackground(BLACK);
            BeginShaderMode(r->sh_bloom_extract);
                DrawTexturePro(r->scene_rt.texture,
                               (Rectangle){0, 0, (float)target_w, -(float)target_h},
                               (Rectangle){0, 0, (float)r->bloom_w, (float)r->bloom_h},
                               (Vector2){0, 0}, 0.0f, WHITE);
            EndShaderMode();
        EndTextureMode();

        float rad_scale = (s->bloom_radius > 0.1f) ? (s->bloom_radius / 8.0f) : 1.0f;
        float dir_x[2] = {rad_scale / (float)r->bloom_w, 0.0f};
        float dir_y[2] = {0.0f, rad_scale / (float)r->bloom_h};

        SetShaderValue(r->sh_blur, GetShaderLocation(r->sh_blur, "uDir"), dir_x, SHADER_UNIFORM_VEC2);
        BeginTextureMode(r->bloom_b);
            ClearBackground(BLACK);
            BeginShaderMode(r->sh_blur);
                DrawTexturePro(r->bloom_a.texture,
                               (Rectangle){0, 0, (float)r->bloom_w, -(float)r->bloom_h},
                               (Rectangle){0, 0, (float)r->bloom_w, (float)r->bloom_h},
                               (Vector2){0, 0}, 0.0f, WHITE);
            EndShaderMode();
        EndTextureMode();

        SetShaderValue(r->sh_blur, GetShaderLocation(r->sh_blur, "uDir"), dir_y, SHADER_UNIFORM_VEC2);
        BeginTextureMode(r->bloom_a);
            ClearBackground(BLACK);
            BeginShaderMode(r->sh_blur);
                DrawTexturePro(r->bloom_b.texture,
                               (Rectangle){0, 0, (float)r->bloom_w, -(float)r->bloom_h},
                               (Rectangle){0, 0, (float)r->bloom_w, (float)r->bloom_h},
                               (Vector2){0, 0}, 0.0f, WHITE);
            EndShaderMode();
        EndTextureMode();
    }

    /* ---- Pass 3: Unified Post-FX Composite to Screen ---- */
    float out_res[2]   = {(float)screen_w, (float)screen_h};
    float retro_res[2] = {(float)target_w, (float)target_h};
    int bloom_en = s->bloom_enabled ? 1 : 0;
    int crt_en   = s->crt_enabled ? 1 : 0;
    int pal      = (int)s->palette;
    int dither   = s->retro_dither ? 1 : 0;

    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uOutRes"), out_res, SHADER_UNIFORM_VEC2);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uRetroRes"), retro_res, SHADER_UNIFORM_VEC2);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uBloomEnabled"), &bloom_en, SHADER_UNIFORM_INT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uBloomStrength"), &s->bloom_strength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uCrtEnabled"), &crt_en, SHADER_UNIFORM_INT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uCrtScanlines"), &s->crt_scanlines, SHADER_UNIFORM_FLOAT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uCrtCurve"), &s->crt_curve, SHADER_UNIFORM_FLOAT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uVignette"), &s->vignette, SHADER_UNIFORM_FLOAT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uPalette"), &pal, SHADER_UNIFORM_INT);
    SetShaderValue(r->sh_post, GetShaderLocation(r->sh_post, "uDither"), &dither, SHADER_UNIFORM_INT);

    ensure_custom_shader(r, s, time_sec);
    bool use_custom = s->custom_shader_enabled && IsShaderValid(r->sh_custom);

    if (use_custom) {
        ensure_post_rt(r, screen_w, screen_h);
        BeginTextureMode(r->post_rt);
    } else if (target) {
        BeginTextureMode(*target);
    }
    BeginShaderMode(r->sh_post);
        SetShaderValueTexture(r->sh_post, GetShaderLocation(r->sh_post, "uBloomTex"), r->bloom_a.texture);
        DrawTexturePro(r->scene_rt.texture,
                       (Rectangle){0, 0, (float)target_w, -(float)target_h},
                       (Rectangle){0, 0, (float)screen_w, (float)screen_h},
                       (Vector2){0, 0}, 0.0f, WHITE);
    EndShaderMode();
    if (use_custom || target) EndTextureMode();

    /* ---- Pass 4: Optional User Custom Fragment Shader ---- */
    if (use_custom) {
        float t_f = (float)time_sec;
        SetShaderValue(r->sh_custom, GetShaderLocation(r->sh_custom, "uResolution"), out_res, SHADER_UNIFORM_VEC2);
        SetShaderValue(r->sh_custom, GetShaderLocation(r->sh_custom, "uTime"), &t_f, SHADER_UNIFORM_FLOAT);
        SetShaderValue(r->sh_custom, GetShaderLocation(r->sh_custom, "uParams"), s->custom_params, SHADER_UNIFORM_VEC4);

        if (target) BeginTextureMode(*target);
        BeginShaderMode(r->sh_custom);
            DrawTexturePro(r->post_rt.texture,
                           (Rectangle){0, 0, (float)screen_w, -(float)screen_h},
                           (Rectangle){0, 0, (float)screen_w, (float)screen_h},
                           (Vector2){0, 0}, 0.0f, WHITE);
        EndShaderMode();
        if (target) EndTextureMode();
    }
}
