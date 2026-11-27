#include "logo_lang.h"
#include "renderer.h"

#include <ctype.h>
#include <math.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *BUILTIN_EXAMPLES[] = {
    "examples/01_retro_arcade.logo",
    "examples/02_neon_2d.logo",
    "examples/03_3d_crystal_tree.logo",
    "examples/04_retro_3d.logo",
    "examples/05_custom_shader.logo",
    "examples/06_actor_swarm.logo",
    "examples/07_hades_courtyard.logo"
};
static const int NUM_EXAMPLES = 7;

/* Simple cached WAV sound bank (lazy-loaded on first play("file.wav")) */
#define MAX_CACHED_SOUNDS 32
typedef struct {
    char path[256];
    Sound sound;
    bool loaded;
} CachedSound;

static CachedSound g_sounds[MAX_CACHED_SOUNDS];
static int g_sound_count = 0;

static void host_play_sound(const char *path, float volume, float pitch) {
    if (!path || !path[0] || !IsAudioDeviceReady()) return;
    for (int i = 0; i < g_sound_count; i++) {
        if (strcmp(g_sounds[i].path, path) == 0) {
            if (g_sounds[i].loaded) {
                SetSoundVolume(g_sounds[i].sound, volume);
                SetSoundPitch(g_sounds[i].sound, pitch > 0.05f ? pitch : 1.0f);
                PlaySound(g_sounds[i].sound);
            }
            return;
        }
    }
    if (g_sound_count >= MAX_CACHED_SOUNDS) return;
    CachedSound *slot = &g_sounds[g_sound_count++];
    snprintf(slot->path, sizeof(slot->path), "%s", path);
    if (FileExists(path)) {
        slot->sound = LoadSound(path);
        slot->loaded = (slot->sound.frameCount > 0);
        if (slot->loaded) {
            SetSoundVolume(slot->sound, volume);
            SetSoundPitch(slot->sound, pitch > 0.05f ? pitch : 1.0f);
            PlaySound(slot->sound);
        }
    }
}

static void host_unload_sounds(void) {
    for (int i = 0; i < g_sound_count; i++) {
        if (g_sounds[i].loaded) UnloadSound(g_sounds[i].sound);
    }
    g_sound_count = 0;
}

static int map_key_name(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "left") == 0  || strcmp(name, "a") == 0) return KEY_LEFT;
    if (strcmp(name, "right") == 0 || strcmp(name, "d") == 0) return KEY_RIGHT;
    if (strcmp(name, "up") == 0    || strcmp(name, "w") == 0) return KEY_UP;
    if (strcmp(name, "down") == 0  || strcmp(name, "s") == 0) return KEY_DOWN;
    if (strcmp(name, "space") == 0) return KEY_SPACE;
    if (strcmp(name, "enter") == 0) return KEY_ENTER;
    if (strcmp(name, "shift") == 0) return KEY_LEFT_SHIFT;
    if (strlen(name) == 1) {
        char c = (char)toupper((unsigned char)name[0]);
        if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return KEY_ZERO + (c - '0');
    }
    return 0;
}

static bool host_is_key_down(const char *name) {
    int k = map_key_name(name);
    if (k == KEY_LEFT)  return IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
    if (k == KEY_RIGHT) return IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    if (k == KEY_UP)    return IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W);
    if (k == KEY_DOWN)  return IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);
    return k ? IsKeyDown(k) : false;
}

static bool host_is_key_pressed(const char *name) {
    int k = map_key_name(name);
    return k ? IsKeyPressed(k) : false;
}

static char *read_entire_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static time_t file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return st.st_mtime;
    return 0;
}

typedef struct {
    char path[512];
    time_t mtime;
    LogoProgram *prog;
    LogoVM *vm;
    double elapsed;
    char load_err[LOGO_MAX_ERR];
} ScriptSession;

static void session_free(ScriptSession *sess) {
    if (sess->prog) { logo_program_free(sess->prog); sess->prog = NULL; }
    if (sess->vm)   { free(sess->vm); sess->vm = NULL; }
}

static bool session_load(ScriptSession *sess, const char *path, LogoHostInput input) {
    if (sess->prog) {
        logo_program_free(sess->prog);
        sess->prog = NULL;
    }
    if (!sess->vm) sess->vm = (LogoVM *)calloc(1, sizeof(LogoVM));
    else memset(sess->vm, 0, sizeof(LogoVM));
    sess->load_err[0] = '\0';
    strncpy(sess->path, path, sizeof(sess->path) - 1);
    sess->mtime = file_mtime(path);
    sess->elapsed = 0.0;

    char *src = read_entire_file(path);
    if (!src) {
        snprintf(sess->load_err, sizeof(sess->load_err), "Could not open script: %s", path);
        return false;
    }

    sess->prog = logo_parse(src);
    free(src);

    if (sess->prog->has_error) {
        snprintf(sess->load_err, sizeof(sess->load_err), "%s", sess->prog->error);
        return false;
    }

    logo_vm_init(sess->vm, sess->prog);
    logo_vm_run_init(sess->vm, input);
    if (sess->vm->has_error) {
        snprintf(sess->load_err, sizeof(sess->load_err), "%s", sess->vm->error);
        return false;
    }
    return true;
}

static const char *mode_name(RenderMode m) {
    switch (m) {
        case RENDER_MODE_RETRO: return "RETRO";
        case RENDER_MODE_2D:    return "2D";
        case RENDER_MODE_3D:    return "3D";
    }
    return "?";
}

static bool test_space_down(const char *name) {
    return name && strcmp(name, "space") == 0;
}

/* Headless verification mode: --verify [optional script path] */
static int run_verify_mode(int argc, char **argv) {
    LogoHostInput dummy = {0};
    dummy.view_w = 1280;
    dummy.view_h = 720;

    int failures = 0;
    int count = (argc > 2) ? (argc - 2) : NUM_EXAMPLES;

    for (int i = 0; i < count; i++) {
        const char *path = (argc > 2) ? argv[i + 2] : BUILTIN_EXAMPLES[i];
        ScriptSession sess = {0};
        bool ok = session_load(&sess, path, dummy);
        if (ok) {
            /* If verifying 01_retro_arcade.logo, simulate holding Space for 90 frames
               and verify laser fire actually destroys enemies! */
            if (strstr(path, "01_retro_arcade.logo") != NULL) {
                LogoHostInput sim = dummy;
                sim.is_key_down = test_space_down;
                double t = 0.0;
                for (int f = 0; f < 90 && !sess.vm->has_error; f++) {
                    t += 0.016;
                    logo_vm_run_frame(sess.vm, t, 0.016, sim);
                }
                double kills = 0.0;
                for (int g = 0; g < sess.vm->global_count; g++) {
                    if (strcmp(sess.vm->globals[g].name, "kills") == 0) {
                        kills = sess.vm->globals[g].val.num;
                    }
                }
                printf("       -> Simulated Spacebar combat check: kills=%.0f\n", kills);
                if (kills < 1.0) {
                    snprintf(sess.load_err, sizeof(sess.load_err), "Expected >= 1 enemy kill during combat simulation");
                    ok = false;
                }
            } else if (strstr(path, "06_actor_swarm.logo") != NULL) {
                double t = 0.0;
                for (int f = 0; f < 140 && !sess.vm->has_error; f++) {
                    t += 0.016;
                    logo_vm_run_frame(sess.vm, t, 0.016, dummy);
                }
                double kills = 0.0;
                for (int g = 0; g < sess.vm->global_count; g++) {
                    if (strcmp(sess.vm->globals[g].name, "kills") == 0) kills = sess.vm->globals[g].val.num;
                }
                printf("       -> Actor Model swarm check: kills=%.0f\n", kills);
                if (kills < 1.0) {
                    snprintf(sess.load_err, sizeof(sess.load_err), "Expected >= 1 drone kill via Actor message passing");
                    ok = false;
                }
            } else if (strstr(path, "07_hades_courtyard.logo") != NULL) {
                LogoHostInput sim = dummy;
                sim.is_key_down = test_space_down;
                double t = 0.0;
                for (int f = 0; f < 60 && !sess.vm->has_error; f++) {
                    t += 0.016;
                    logo_vm_run_frame(sess.vm, t, 0.016, sim);
                }
                double hits = 0.0;
                for (int g = 0; g < sess.vm->global_count; g++) {
                    if (strcmp(sess.vm->globals[g].name, "total_hits") == 0) hits = sess.vm->globals[g].val.num;
                }
                printf("       -> Hades courtyard combat check: total_hits=%.0f (prims3D=%d)\n",
                       hits, sess.vm->scene.prim3d_count);
                if (hits < 1.0) {
                    snprintf(sess.load_err, sizeof(sess.load_err), "Expected >= 1 hit on Skelly in Hades courtyard");
                    ok = false;
                }
            } else {
                logo_vm_run_frame(sess.vm, 1.0, 0.016, dummy);
            }
            if (sess.vm->has_error) ok = false;
        }
        if (ok) {
            const SceneIR *s = &sess.vm->scene;
            printf("[PASS] %-34s | mode=%-5s | 2D=%3d | 3D=%3d | bloom=%d crt=%d retro=%d pal=%d\n",
                   path, mode_name(s->mode), s->prim2d_count, s->prim3d_count,
                   s->bloom_enabled, s->crt_enabled, s->retro_fx_enabled, (int)s->palette);
        } else {
            const char *err = sess.load_err[0] ? sess.load_err : (sess.vm ? sess.vm->error : "Load failed");
            printf("[FAIL] %-34s | %s\n", path, err);
            failures++;
        }
        session_free(&sess);
    }
    return failures == 0 ? 0 : 1;
}

/* GPU shader compile & render smoke test: --render-test */
static int run_render_test(void) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(960, 540, "TurtleFX Render Test");

    LogoRenderer renderer;
    renderer_init(&renderer);

    bool shaders_ok = IsShaderValid(renderer.sh_2d) &&
                      IsShaderValid(renderer.sh_3d) &&
                      IsShaderValid(renderer.sh_bloom_extract) &&
                      IsShaderValid(renderer.sh_blur) &&
                      IsShaderValid(renderer.sh_post);
    printf("Shaders valid: 2D=%d 3D=%d BloomExtract=%d Blur=%d Post=%d\n",
           IsShaderValid(renderer.sh_2d), IsShaderValid(renderer.sh_3d),
           IsShaderValid(renderer.sh_bloom_extract), IsShaderValid(renderer.sh_blur),
           IsShaderValid(renderer.sh_post));

    LogoHostInput input = {0};
    input.view_w = 960;
    input.view_h = 540;

    RenderTexture2D out_rt = LoadRenderTexture(960, 540);
    int failures = shaders_ok ? 0 : 1;
    for (int i = 0; i < NUM_EXAMPLES; i++) {
        ScriptSession sess = {0};
        if (!session_load(&sess, BUILTIN_EXAMPLES[i], input)) {
            printf("[FAIL] %s: %s\n", BUILTIN_EXAMPLES[i], sess.load_err);
            failures++;
            continue;
        }
        if (strstr(BUILTIN_EXAMPLES[i], "06_actor_swarm.logo") != NULL) {
            /* Step 95 frames so lasers, drones, explosions & shockwaves are all mid-action! */
            double t = 0.0;
            for (int f = 0; f < 95; f++) {
                t += 0.016;
                logo_vm_run_frame(sess.vm, t, 0.016, input);
            }
        } else if (strstr(BUILTIN_EXAMPLES[i], "07_hades_courtyard.logo") != NULL) {
            /* Step 22 frames with Space held so Zagreus lands a mid-swing crimson slash on Skelly! */
            LogoHostInput sim = input;
            sim.is_key_down = test_space_down;
            double t = 0.0;
            for (int f = 0; f < 22; f++) {
                t += 0.016;
                logo_vm_run_frame(sess.vm, t, 0.016, sim);
            }
        } else {
            logo_vm_run_frame(sess.vm, 1.5, 0.016, input);
        }

        /* Render scene + post-FX (+ optional custom shader) into deterministic offscreen RT */
        renderer_draw_to(&renderer, &sess.vm->scene, 960, 540, 1.5, &out_rt);

        Image img = LoadImageFromTexture(out_rt.texture);
        ImageFlipVertical(&img);
        char out_path[128];
        snprintf(out_path, sizeof(out_path), "build/preview_%d.png", i + 1);
        ExportImage(img, out_path);
        UnloadImage(img);
        printf("[RENDERED] %s -> %s\n", BUILTIN_EXAMPLES[i], out_path);
        session_free(&sess);
    }

    UnloadRenderTexture(out_rt);

    renderer_shutdown(&renderer);
    CloseWindow();
    return failures == 0 ? 0 : 1;
}

/* Autonomous demo input pilot for Example 1 during --make-media */
static double g_demo_time = 0.0;
static bool demo_autopilot_key(const char *name) {
    if (!name) return false;
    if (strcmp(name, "space") == 0) return true;
    double wave = sin(g_demo_time * 2.6);
    if (strcmp(name, "left") == 0)  return wave < -0.25;
    if (strcmp(name, "right") == 0) return wave >  0.25;
    return false;
}

/* Generates assets/screenshot.png (2x2 showcase) and build/demo_frames/frame_%04d.png */
static int run_make_media(void) {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1280, 720, "TurtleFX Media Generator");
    SetTraceLogLevel(LOG_WARNING);

    LogoRenderer renderer;
    renderer_init(&renderer);

    /* 1. Render 2x2 Showcase Screenshot (1280x720, 4x 640x360 quadrants) */
    const char *labels[4] = {
        "1. RETRO PIXEL  (PICO-8 + CRT + Combat)",
        "2. MODERN 2D  (SDF Vector + Bloom)",
        "3. 3D RAYMARCHING  (SDF + Carve + Smooth)",
        "4. RETRO 3D  (GameBoy Bayer Dither + Fog)"
    };
    RenderTexture2D quad_rt = LoadRenderTexture(640, 360);
    Image collage = GenImageColor(1280, 720, (Color){10, 12, 18, 255});

    for (int i = 0; i < 4; i++) {
        LogoHostInput in = {0};
        in.view_w = 640;
        in.view_h = 360;
        ScriptSession sess = {0};
        if (!session_load(&sess, BUILTIN_EXAMPLES[i], in)) continue;

        if (i == 0) {
            in.is_key_down = demo_autopilot_key;
            double t = 0.0;
            /* Step to the exact moment an enemy explodes into radial turtle shrapnel */
            for (int f = 0; f < 46; f++) {
                t += 1.0 / 60.0;
                g_demo_time = t;
                logo_vm_run_frame(sess.vm, t, 1.0 / 60.0, in);
            }
        } else {
            logo_vm_run_frame(sess.vm, 1.8, 1.0 / 60.0, in);
        }

        renderer_draw_to(&renderer, &sess.vm->scene, 640, 360, 1.8, &quad_rt);
        BeginTextureMode(quad_rt);
            DrawRectangle(10, 10, 336, 26, (Color){8, 10, 18, 215});
            DrawRectangleLines(10, 10, 336, 26, (Color){80, 105, 150, 200});
            DrawText(labels[i], 18, 17, 13, (Color){230, 240, 255, 255});
        EndTextureMode();

        Image qimg = LoadImageFromTexture(quad_rt.texture);
        ImageFlipVertical(&qimg);
        int ox = (i % 2) * 640;
        int oy = (i / 2) * 360;
        ImageDraw(&collage, qimg, (Rectangle){0, 0, 640, 360}, (Rectangle){(float)ox, (float)oy, 640, 360}, WHITE);
        UnloadImage(qimg);
        session_free(&sess);
    }
    ExportImage(collage, "assets/screenshot.png");
    UnloadImage(collage);
    UnloadRenderTexture(quad_rt);
    printf("[MEDIA] Saved showcase screenshot -> assets/screenshot.png\n");

    /* 2. Render 360 frames (12s @ 30fps, 1280x720) cycling through all 4 examples */
    RenderTexture2D vid_rt = LoadRenderTexture(1280, 720);
    const int seg_frames[4] = {120, 80, 85, 75}; /* total = 360 frames = 12.0s */
    int frame_idx = 0;

    for (int i = 0; i < 4; i++) {
        LogoHostInput in = {0};
        in.view_w = 1280;
        in.view_h = 720;
        if (i == 0) in.is_key_down = demo_autopilot_key;

        ScriptSession sess = {0};
        if (!session_load(&sess, BUILTIN_EXAMPLES[i], in)) continue;

        double t = 0.0;
        for (int f = 0; f < seg_frames[i]; f++) {
            for (int sub = 0; sub < 2; sub++) {
                t += 1.0 / 60.0;
                g_demo_time = t;
                logo_vm_run_frame(sess.vm, t, 1.0 / 60.0, in);
            }

            renderer_draw_to(&renderer, &sess.vm->scene, 1280, 720, t, &vid_rt);
            BeginTextureMode(vid_rt);
                const SceneIR *sc = &sess.vm->scene;
                int prims = (sc->mode == RENDER_MODE_3D) ? sc->prim3d_count : sc->prim2d_count;
                DrawRectangle(16, 16, 560, 36, (Color){8, 10, 18, 205});
                DrawRectangleLines(16, 16, 560, 36, (Color){75, 95, 140, 190});
                char hud[256];
                snprintf(hud, sizeof(hud), "TurtleFX  |  %.90s  |  Mode: %s  |  Prims: %d",
                         sess.path, mode_name(sc->mode), prims);
                DrawText(hud, 28, 26, 16, (Color){230, 240, 255, 255});
            EndTextureMode();

            Image vimg = LoadImageFromTexture(vid_rt.texture);
            ImageFlipVertical(&vimg);
            char fpath[128];
            snprintf(fpath, sizeof(fpath), "build/demo_frames/frame_%04d.png", frame_idx++);
            ExportImage(vimg, fpath);
            UnloadImage(vimg);
        }
        session_free(&sess);
    }

    UnloadRenderTexture(vid_rt);
    renderer_shutdown(&renderer);
    CloseWindow();
    printf("[MEDIA] Rendered %d video frames -> build/demo_frames/\n", frame_idx);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--verify") == 0) {
        return run_verify_mode(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "--render-test") == 0) {
        return run_render_test();
    }
    if (argc >= 2 && strcmp(argv[1], "--make-media") == 0) {
        return run_make_media();
    }

    const char *initial_script = (argc >= 2) ? argv[1] : BUILTIN_EXAMPLES[0];

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "TurtleFX — Modern Logo Shader Engine (C + raylib)");
    InitAudioDevice();
    SetTargetFPS(60);

    LogoRenderer renderer;
    renderer_init(&renderer);

    LogoHostInput input = {
        .is_key_down = host_is_key_down,
        .is_key_pressed = host_is_key_pressed,
        .play_sound = host_play_sound,
        .mouse_x = 0,
        .mouse_y = 0,
        .view_w = GetScreenWidth(),
        .view_h = GetScreenHeight()
    };

    ScriptSession sess = {0};
    session_load(&sess, initial_script, input);

    bool show_hud = true;
    double watch_timer = 0.0;
    int screenshot_seq = 1;
    double screenshot_msg_timer = 0.0;
    char screenshot_msg[128] = {0};

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        if (screenshot_msg_timer > 0.0) screenshot_msg_timer -= dt;

        input.view_w = GetScreenWidth();
        input.view_h = GetScreenHeight();
        input.mouse_x = (float)GetMouseX();
        input.mouse_y = (float)GetMouseY();

        /* Built-in example switching (1-7), manual reload (R), HUD toggle (H), Screenshot (F12/P) */
        if (IsKeyPressed(KEY_ONE))   session_load(&sess, BUILTIN_EXAMPLES[0], input);
        if (IsKeyPressed(KEY_TWO))   session_load(&sess, BUILTIN_EXAMPLES[1], input);
        if (IsKeyPressed(KEY_THREE)) session_load(&sess, BUILTIN_EXAMPLES[2], input);
        if (IsKeyPressed(KEY_FOUR))  session_load(&sess, BUILTIN_EXAMPLES[3], input);
        if (IsKeyPressed(KEY_FIVE))  session_load(&sess, BUILTIN_EXAMPLES[4], input);
        if (IsKeyPressed(KEY_SIX))   session_load(&sess, BUILTIN_EXAMPLES[5], input);
        if (IsKeyPressed(KEY_SEVEN)) session_load(&sess, BUILTIN_EXAMPLES[6], input);
        if (IsKeyPressed(KEY_R))     session_load(&sess, sess.path, input);
        if (IsKeyPressed(KEY_H))     show_hud = !show_hud;
        bool want_screenshot = IsKeyPressed(KEY_F12) || IsKeyPressed(KEY_P);

        /* Hot-reload watched .logo file on save */
        watch_timer += dt;
        if (watch_timer >= 0.3) {
            watch_timer = 0.0;
            time_t mt = file_mtime(sess.path);
            if (mt != 0 && mt != sess.mtime) {
                session_load(&sess, sess.path, input);
            }
        }

        sess.elapsed += dt;
        bool has_err = (sess.load_err[0] != '\0') || !sess.vm || sess.vm->has_error;
        if (!has_err) {
            logo_vm_run_frame(sess.vm, sess.elapsed, dt, input);
            if (sess.vm->has_error) {
                snprintf(sess.load_err, sizeof(sess.load_err), "%s", sess.vm->error);
                has_err = true;
            }
        }

        BeginDrawing();
            ClearBackground((Color){12, 14, 22, 255});

            if (!has_err) {
                renderer_draw(&renderer, &sess.vm->scene, input.view_w, input.view_h, sess.elapsed);
            } else {
                DrawRectangle(40, 80, input.view_w - 80, 120, (Color){60, 16, 24, 230});
                DrawRectangleLines(40, 80, input.view_w - 80, 120, (Color){240, 80, 96, 255});
                DrawText("LOGO SCRIPT ERROR", 60, 98, 22, (Color){255, 130, 140, 255});
                DrawText(sess.load_err, 60, 132, 18, WHITE);
                DrawText("Fix the script and save to hot-reload (or press 1-6).", 60, 164, 15, LIGHTGRAY);
            }

            if (show_hud && sess.vm) {
                const SceneIR *sc = &sess.vm->scene;
                int prims = (sc->mode == RENDER_MODE_3D) ? sc->prim3d_count : sc->prim2d_count;
                DrawRectangle(12, 12, 980, 56, (Color){8, 10, 18, 195});
                DrawRectangleLines(12, 12, 980, 56, (Color){70, 90, 130, 180});

                char line1[256];
                snprintf(line1, sizeof(line1), "TurtleFX  |  %.120s  |  Mode: %s  |  Prims: %d  |  %d FPS",
                         sess.path, mode_name(sc->mode), prims, GetFPS());
                DrawText(line1, 24, 20, 16, (Color){230, 240, 255, 255});
                DrawText("[1] Retro [2] 2D [3] 3D [4] Retro3D [5] Shader [6] Actors [7] Hades Iso  [R] Reload [H] HUD [F12] Shot",
                         24, 44, 13, (Color){155, 175, 210, 255});
            }

            if (screenshot_msg_timer > 0.0) {
                DrawRectangle(12, 76, 320, 28, (Color){16, 48, 32, 220});
                DrawRectangleLines(12, 76, 320, 28, (Color){64, 220, 130, 255});
                DrawText(screenshot_msg, 22, 83, 14, (Color){180, 255, 210, 255});
            }
        EndDrawing();

        if (want_screenshot) {
            char spath[64];
            snprintf(spath, sizeof(spath), "screenshot_%03d.png", screenshot_seq++);
            TakeScreenshot(spath);
            snprintf(screenshot_msg, sizeof(screenshot_msg), "Saved %s", spath);
            screenshot_msg_timer = 2.5;
        }
    }

    session_free(&sess);
    host_unload_sounds();
    renderer_shutdown(&renderer);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
