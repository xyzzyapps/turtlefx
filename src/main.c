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

/* ============================================================================
 *  Visual Slow-Motion Turtle Debugger (-debug / --debug / F5)
 * ============================================================================ */

typedef struct {
    bool enabled;
    bool paused;
    double sim_time;      /* Simulated clock (seconds) */
    double sim_scale;     /* Time scale multiplier (default 0.15x in -debug) */
    int sim_frame;        /* Simulated frame counter */
    double last_dt_sim;   /* Last simulated dt (seconds) */
    double last_dt_real;  /* Last real wall-clock dt (seconds) */
    double draw_step_f;   /* Fractional turtle primitive playback cursor */
    double draw_rate;     /* Base turtle draw ops/second at 1.0x draw speed */
} DebugState;

typedef struct {
    bool valid;
    bool is_3d;
    float x, y, z;
    float angle_rad;
    int active_seq;
    int total_ops;
    char label[96];
} DebugCursorInfo;

static void debug_reset_playback(DebugState *dbg) {
    dbg->sim_time = 0.0;
    dbg->sim_frame = 0;
    dbg->draw_step_f = 0.0;
}

static void build_debug_scene_slice(const SceneIR *full, SceneIR *out, double step_f, DebugCursorInfo *cur) {
    *out = *full;
    memset(cur, 0, sizeof(*cur));
    cur->total_ops = full->total_draw_ops;

    if (full->total_draw_ops <= 0) {
        snprintf(cur->label, sizeof(cur->label), "IDLE (0 draw ops)");
        return;
    }
    if (step_f >= (double)full->total_draw_ops) {
        cur->active_seq = full->total_draw_ops;
        snprintf(cur->label, sizeof(cur->label), "COMPLETE (Backspace to replay)");
        return;
    }

    if (step_f < 0.0) step_f = 0.0;
    int active_seq = (int)floor(step_f);
    float frac = (float)(step_f - floor(step_f));
    if (frac < 0.03f) frac = 0.03f;
    if (frac > 1.0f) frac = 1.0f;
    cur->active_seq = active_seq + 1;

    out->prim2d_count = 0;
    out->prim3d_count = 0;
    out->sprite_count = 0;
    out->model_count  = 0;

    for (int i = 0; i < full->prim2d_count; i++) {
        Prim2D p = full->prims2d[i];
        if (p.draw_seq > active_seq) continue;
        if (p.draw_seq == active_seq) {
            cur->valid = true;
            cur->is_3d = false;
            if (p.type == PRIM2D_SEGMENT) {
                float nx = p.x0 + (p.x1 - p.x0) * frac;
                float ny = p.y0 + (p.y1 - p.y0) * frac;
                cur->angle_rad = atan2f(p.y1 - p.y0, p.x1 - p.x0);
                p.x1 = nx;
                p.y1 = ny;
                cur->x = nx;
                cur->y = ny;
                snprintf(cur->label, sizeof(cur->label), "SEGMENT (%.0f,%.0f)->(%.0f,%.0f)", p.x0, p.y0, nx, ny);
            } else if (p.type == PRIM2D_CIRCLE) {
                p.radius *= (0.25f + 0.75f * frac);
                cur->x = p.x0; cur->y = p.y0;
                snprintf(cur->label, sizeof(cur->label), "DOT/CIRCLE r=%.1f @ (%.0f,%.0f)", p.radius, p.x0, p.y0);
            } else if (p.type == PRIM2D_RECT) {
                p.x2 *= (0.25f + 0.75f * frac);
                p.y2 *= (0.25f + 0.75f * frac);
                cur->x = p.x0; cur->y = p.y0;
                snprintf(cur->label, sizeof(cur->label), "RECT %.0fx%.0f @ (%.0f,%.0f)", p.x2 * 2.0f, p.y2 * 2.0f, p.x0, p.y0);
            } else if (p.type == PRIM2D_TRIANGLE) {
                float cx = (p.x0 + p.x1 + p.x2) / 3.0f;
                float cy = (p.y0 + p.y1 + p.y2) / 3.0f;
                float sc = 0.25f + 0.75f * frac;
                p.x0 = cx + (p.x0 - cx) * sc; p.y0 = cy + (p.y0 - cy) * sc;
                p.x1 = cx + (p.x1 - cx) * sc; p.y1 = cy + (p.y1 - cy) * sc;
                p.x2 = cx + (p.x2 - cx) * sc; p.y2 = cy + (p.y2 - cy) * sc;
                cur->x = cx; cur->y = cy;
                snprintf(cur->label, sizeof(cur->label), "FILL TRI @ (%.0f,%.0f)", cx, cy);
            }
        }
        out->prims2d[out->prim2d_count++] = p;
    }

    for (int i = 0; i < full->prim3d_count; i++) {
        Prim3D p = full->prims3d[i];
        if (p.draw_seq > active_seq) continue;
        if (p.draw_seq == active_seq) {
            cur->valid = true;
            cur->is_3d = true;
            float sc = (p.op_type == OP3D_CARVE) ? 1.0f : (0.25f + 0.75f * frac);
            cur->x = p.px; cur->y = p.py; cur->z = p.pz;
            const char *op_tag = (p.op_type == OP3D_CARVE) ? "CARVE " :
                                 (p.op_type == OP3D_SMOOTH_UNION ? "SMOOTH " : "");
            if (p.type == PRIM3D_CAPSULE) {
                float len = p.p0 * (0.08f + 0.92f * frac);
                p.p0 = len;
                cur->x = p.px + p.fx * len;
                cur->y = p.py + p.fy * len;
                cur->z = p.pz + p.fz * len;
                snprintf(cur->label, sizeof(cur->label), "%sCAPSULE len=%.2f r=%.2f", op_tag, len, p.p1);
            } else if (p.type == PRIM3D_SPHERE) {
                p.p0 *= sc;
                snprintf(cur->label, sizeof(cur->label), "%sSPHERE r=%.2f", op_tag, p.p0);
            } else if (p.type == PRIM3D_BOX) {
                p.p0 *= sc; p.p1 *= sc; p.p2 *= sc;
                snprintf(cur->label, sizeof(cur->label), "%sBOX %.1fx%.1fx%.1f", op_tag, p.p0 * 2.0f, p.p1 * 2.0f, p.p2 * 2.0f);
            } else if (p.type == PRIM3D_CYLINDER) {
                p.p0 *= sc; p.p1 *= sc;
                snprintf(cur->label, sizeof(cur->label), "%sCYLINDER r=%.2f h=%.2f", op_tag, p.p0, p.p1 * 2.0f);
            } else if (p.type == PRIM3D_CONE) {
                p.p0 *= sc; p.p1 *= sc;
                snprintf(cur->label, sizeof(cur->label), "%sCONE r=%.2f h=%.2f", op_tag, p.p0, p.p1);
            } else if (p.type == PRIM3D_TORUS) {
                p.p0 *= sc; p.p1 *= sc;
                snprintf(cur->label, sizeof(cur->label), "%sTORUS R=%.2f r=%.2f", op_tag, p.p0, p.p1);
            }
        }
        out->prims3d[out->prim3d_count++] = p;
    }

    for (int i = 0; i < full->model_count; i++) {
        PrimModel md = full->models[i];
        if (md.draw_seq > active_seq) continue;
        if (md.draw_seq == active_seq) {
            md.scale *= (0.25f + 0.75f * frac);
            cur->valid = true;
            cur->is_3d = true;
            cur->x = md.px; cur->y = md.py; cur->z = md.pz;
            const char *base = strrchr(md.path, '/');
            snprintf(cur->label, sizeof(cur->label), "MODEL %.40s", base ? (base + 1) : md.path);
        }
        out->models[out->model_count++] = md;
    }

    for (int i = 0; i < full->sprite_count; i++) {
        PrimSprite sp = full->sprites[i];
        if (sp.draw_seq > active_seq) continue;
        if (sp.draw_seq == active_seq) {
            sp.size *= (0.25f + 0.75f * frac);
            cur->valid = true;
            cur->is_3d = (full->mode == RENDER_MODE_3D);
            cur->x = sp.x; cur->y = sp.y; cur->z = sp.z;
            if (sp.inline_art) snprintf(cur->label, sizeof(cur->label), "SPRITE [inline pixel art]");
            else {
                const char *base = strrchr(sp.path, '/');
                snprintf(cur->label, sizeof(cur->label), "SPRITE %.40s", base ? (base + 1) : sp.path);
            }
        }
        out->sprites[out->sprite_count++] = sp;
    }
}

static void draw_debug_overlays(const SceneIR *full, const DebugState *dbg,
                                const DebugCursorInfo *cur, int view_w, int view_h) {
    /* 1. Live Turtle Position Marker / Reticle on the Canvas */
    if (cur->valid) {
        float sx = 0.0f, sy = 0.0f;
        bool on_screen = true;
        if (cur->is_3d) {
            Camera3D cam = {0};
            cam.position   = (Vector3){full->cam_pos[0], full->cam_pos[1], full->cam_pos[2]};
            cam.target     = (Vector3){full->cam_look[0], full->cam_look[1], full->cam_look[2]};
            cam.up         = (Vector3){0.0f, 1.0f, 0.0f};
            cam.fovy       = full->cam_fov;
            cam.projection = CAMERA_PERSPECTIVE;
            Vector2 sp = GetWorldToScreenEx((Vector3){cur->x, cur->y, cur->z}, cam, view_w, view_h);
            sx = sp.x;
            sy = sp.y;
            on_screen = (sx >= -40 && sx <= view_w + 40 && sy >= -40 && sy <= view_h + 40);
        } else if (full->mode == RENDER_MODE_RETRO) {
            sx = (cur->x / (float)(full->retro_w > 0 ? full->retro_w : 320)) * (float)view_w;
            sy = (cur->y / (float)(full->retro_h > 0 ? full->retro_h : 180)) * (float)view_h;
        } else {
            sx = (float)view_w * 0.5f + cur->x;
            sy = (float)view_h * 0.5f - cur->y;
        }

        if (on_screen) {
            Color accent = (Color){46, 230, 138, 255};
            DrawCircleLines((int)sx, (int)sy, 11.0f, accent);
            DrawCircle((int)sx, (int)sy, 3.5f, (Color){255, 240, 180, 255});
            DrawLine((int)sx - 16, (int)sy, (int)sx - 6, (int)sy, accent);
            DrawLine((int)sx + 6,  (int)sy, (int)sx + 16, (int)sy, accent);
            DrawLine((int)sx, (int)sy - 16, (int)sx, (int)sy - 6, accent);
            DrawLine((int)sx, (int)sy + 6,  (int)sx, (int)sy + 16, accent);

            char tag[32];
            snprintf(tag, sizeof(tag), "#%d", cur->active_seq);
            DrawRectangle((int)sx + 14, (int)sy - 18, 44, 18, (Color){10, 12, 18, 215});
            DrawRectangleLines((int)sx + 14, (int)sy - 18, 44, 18, accent);
            DrawText(tag, (int)sx + 19, (int)sy - 15, 12, accent);
        }
    }

    /* 2. Bottom-Right Simulated Clock & Turtle Step Inspector Panel */
    int pw = 456;
    int ph = 152;
    int px = view_w - pw - 14;
    int py = view_h - ph - 14;
    if (px < 12) px = 12;
    if (py < 74) py = 74;

    DrawRectangle(px, py, pw, ph, (Color){12, 13, 18, 228});
    DrawRectangleLines(px, py, pw, ph, (Color){46, 230, 138, 210});

    /* Header row + live state pill */
    DrawText("TURTLE DEBUGGER", px + 14, py + 11, 14, (Color){46, 230, 138, 255});
    Color pill_col = dbg->paused ? (Color){242, 184, 75, 255} : (Color){46, 230, 138, 255};
    char status_txt[48];
    if (dbg->paused) snprintf(status_txt, sizeof(status_txt), "PAUSED");
    else             snprintf(status_txt, sizeof(status_txt), "SLOW %.2fx", dbg->sim_scale);
    DrawRectangle(px + 162, py + 9, 96, 18, (Color){24, 28, 36, 255});
    DrawRectangleLines(px + 162, py + 9, 96, 18, pill_col);
    DrawText(status_txt, px + 170, py + 12, 12, pill_col);

    /* Digital Simulated Clock */
    int mins = (int)(dbg->sim_time / 60.0);
    double secs = fmod(dbg->sim_time, 60.0);
    char clk_line[96];
    snprintf(clk_line, sizeof(clk_line), "SIM %02d:%06.3fs  |  FRAME #%d", mins, secs, dbg->sim_frame);
    DrawText(clk_line, px + 14, py + 34, 15, (Color){240, 244, 250, 255});

    char dt_line[96];
    snprintf(dt_line, sizeof(dt_line), "dt_sim: %.2fms   (wall: %.1fms)   rate: %.1f ops/s",
             dbg->last_dt_sim * 1000.0, dbg->last_dt_real * 1000.0, dbg->draw_rate);
    DrawText(dt_line, px + 14, py + 55, 12, (Color){155, 170, 195, 255});

    /* Turtle step counter + progress bar */
    int shown = cur->active_seq;
    int total = cur->total_ops;
    char step_line[96];
    snprintf(step_line, sizeof(step_line), "STEP %d / %d  |  %.42s", shown, total, cur->label);
    DrawText(step_line, px + 14, py + 76, 12, (Color){226, 234, 245, 255});

    int bar_x = px + 14, bar_y = py + 96, bar_w = 326, bar_h = 10;
    DrawRectangle(bar_x, bar_y, bar_w, bar_h, (Color){26, 30, 40, 255});
    float prog = (total > 0) ? (float)fmin(dbg->draw_step_f / (double)total, 1.0) : 1.0f;
    DrawRectangle(bar_x, bar_y, (int)(bar_w * prog), bar_h, (Color){46, 230, 138, 235});
    DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, (Color){70, 88, 110, 200});

    /* Controls legend */
    DrawText("[Tab] Pause  [,/.] Step Op  [N] +1 Frame", px + 14, py + 114, 11, (Color){145, 160, 185, 255});
    DrawText("[-/+] Speed  [Bksp] Replay  [F5] Exit Debug", px + 14, py + 130, 11, (Color){145, 160, 185, 255});

    /* Analog Simulated Clock Dial (Right Side of Panel) */
    float cx = (float)(px + pw - 56);
    float cy = (float)(py + 62);
    float r  = 40.0f;
    DrawCircle((int)cx, (int)cy, r, (Color){18, 20, 28, 255});
    DrawCircleLines((int)cx, (int)cy, r, (Color){95, 115, 140, 220});
    DrawCircleLines((int)cx, (int)cy, r - 2.0f, (Color){46, 230, 138, 110});

    for (int t = 0; t < 12; t++) {
        float a = ((float)t / 12.0f) * 6.2831853f - 1.5707963f;
        float r0 = (t % 3 == 0) ? (r - 9.0f) : (r - 5.0f);
        DrawLineEx((Vector2){cx + cosf(a) * r0, cy + sinf(a) * r0},
                   (Vector2){cx + cosf(a) * (r - 2.0f), cy + sinf(a) * (r - 2.0f)},
                   (t % 3 == 0) ? 2.0f : 1.0f, (Color){140, 155, 180, 220});
    }

    /* Emerald 60-second hand + Amber 1-second sweep sub-hand */
    float sec_a = (float)(fmod(dbg->sim_time, 60.0) / 60.0) * 6.2831853f - 1.5707963f;
    float sub_a = (float)(fmod(dbg->sim_time, 1.0)) * 6.2831853f - 1.5707963f;
    DrawLineEx((Vector2){cx, cy}, (Vector2){cx + cosf(sec_a) * (r - 12.0f), cy + sinf(sec_a) * (r - 12.0f)},
               2.4f, (Color){46, 230, 138, 255});
    DrawLineEx((Vector2){cx, cy}, (Vector2){cx + cosf(sub_a) * (r - 7.0f), cy + sinf(sub_a) * (r - 7.0f)},
               1.5f, (Color){242, 184, 75, 255});
    DrawCircle((int)cx, (int)cy, 3.5f, WHITE);
    DrawText("SIM CLK", (int)cx - 24, (int)cy + 46, 11, (Color){155, 175, 200, 255});
}

static bool test_space_down(const char *name) {
    return name && strcmp(name, "space") == 0;
}

static bool test_left_down(const char *name) {
    return name && strcmp(name, "left") == 0;
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

        /* Also verify walking LEFT (flip_x=1 + zag_run) renders cleanly without vanishing! */
        if (strstr(BUILTIN_EXAMPLES[i], "07_hades_courtyard.logo") != NULL) {
            LogoHostInput simL = input;
            simL.is_key_down = test_left_down;
            double t = 0.40;
            for (int f = 0; f < 16; f++) {
                t += 0.016;
                logo_vm_run_frame(sess.vm, t, 0.016, simL);
            }
            renderer_draw_to(&renderer, &sess.vm->scene, 960, 540, t, &out_rt);
            Image imgL = LoadImageFromTexture(out_rt.texture);
            ImageFlipVertical(&imgL);
            ExportImage(imgL, "build/preview_7_left.png");
            UnloadImage(imgL);
            printf("[RENDERED] %s (walk left) -> build/preview_7_left.png\n", BUILTIN_EXAMPLES[i]);
        }
        session_free(&sess);
    }

    /* Also render visual -debug mode previews (2D mid-stroke + 3D mid-build) */
    {
        SceneIR *dbg_slice = (SceneIR *)malloc(sizeof(SceneIR));
        const char *dbg_scripts[2] = { BUILTIN_EXAMPLES[1], BUILTIN_EXAMPLES[2] };
        const char *dbg_outs[2]    = { "build/preview_debug_2d.png", "build/preview_debug_3d.png" };
        const double dbg_steps[2]  = { 33.55, 22.60 };
        for (int d = 0; d < 2; d++) {
            ScriptSession ds = {0};
            if (session_load(&ds, dbg_scripts[d], input)) {
                logo_vm_run_frame(ds.vm, 2.35, 0.0025, input);
                DebugState dst = {
                    .enabled = true, .paused = false,
                    .sim_time = 2.35, .sim_scale = 0.15, .sim_frame = 141,
                    .last_dt_sim = 0.0025, .last_dt_real = 0.0167,
                    .draw_step_f = dbg_steps[d], .draw_rate = 6.0
                };
                DebugCursorInfo cur = {0};
                build_debug_scene_slice(&ds.vm->scene, dbg_slice, dst.draw_step_f, &cur);
                renderer_draw_to(&renderer, dbg_slice, 960, 540, dst.sim_time, &out_rt);
                BeginTextureMode(out_rt);
                    draw_debug_overlays(&ds.vm->scene, &dst, &cur, 960, 540);
                EndTextureMode();
                Image dimg = LoadImageFromTexture(out_rt.texture);
                ImageFlipVertical(&dimg);
                ExportImage(dimg, dbg_outs[d]);
                UnloadImage(dimg);
                printf("[RENDERED] %s (-debug) -> %s\n", dbg_scripts[d], dbg_outs[d]);
                session_free(&ds);
            }
        }
        free(dbg_slice);
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

    bool start_debug = false;
    double initial_sim_scale = 0.15;
    const char *initial_script = BUILTIN_EXAMPLES[0];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-debug") == 0 || strcmp(argv[i], "--debug") == 0) {
            start_debug = true;
        } else if ((strcmp(argv[i], "--speed") == 0 || strcmp(argv[i], "-speed") == 0) && i + 1 < argc) {
            initial_sim_scale = atof(argv[++i]);
            if (initial_sim_scale < 0.01) initial_sim_scale = 0.01;
            if (initial_sim_scale > 4.0)  initial_sim_scale = 4.0;
        } else if (argv[i][0] != '-') {
            initial_script = argv[i];
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, start_debug
        ? "TurtleFX [DEBUG] — Slow Turtle & Simulated Clock"
        : "TurtleFX — Modern Logo Shader Engine (C + raylib)");
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

    SceneIR *debug_scene = (SceneIR *)malloc(sizeof(SceneIR));
    DebugState dbg = {
        .enabled = start_debug,
        .paused = false,
        .sim_time = 0.0,
        .sim_scale = initial_sim_scale,
        .sim_frame = 0,
        .last_dt_sim = 0.0,
        .last_dt_real = 0.016,
        .draw_step_f = 0.0,
        .draw_rate = 6.0
    };
    DebugCursorInfo dbg_cur = {0};

    bool show_hud = true;
    double watch_timer = 0.0;
    int screenshot_seq = 1;
    double screenshot_msg_timer = 0.0;
    char screenshot_msg[128] = {0};
    bool first_debug_frame = true;

    while (!WindowShouldClose()) {
        float dt_real = GetFrameTime();
        if (dt_real > 0.05f) dt_real = 0.05f;
        if (screenshot_msg_timer > 0.0) screenshot_msg_timer -= dt_real;

        input.view_w = GetScreenWidth();
        input.view_h = GetScreenHeight();
        input.mouse_x = (float)GetMouseX();
        input.mouse_y = (float)GetMouseY();

        /* Built-in example switching (1-7), manual reload (R), HUD toggle (H), Debug (F5), Shot (F12/P) */
        bool reloaded = false;
        if (IsKeyPressed(KEY_ONE))   { session_load(&sess, BUILTIN_EXAMPLES[0], input); reloaded = true; }
        if (IsKeyPressed(KEY_TWO))   { session_load(&sess, BUILTIN_EXAMPLES[1], input); reloaded = true; }
        if (IsKeyPressed(KEY_THREE)) { session_load(&sess, BUILTIN_EXAMPLES[2], input); reloaded = true; }
        if (IsKeyPressed(KEY_FOUR))  { session_load(&sess, BUILTIN_EXAMPLES[3], input); reloaded = true; }
        if (IsKeyPressed(KEY_FIVE))  { session_load(&sess, BUILTIN_EXAMPLES[4], input); reloaded = true; }
        if (IsKeyPressed(KEY_SIX))   { session_load(&sess, BUILTIN_EXAMPLES[5], input); reloaded = true; }
        if (IsKeyPressed(KEY_SEVEN)) { session_load(&sess, BUILTIN_EXAMPLES[6], input); reloaded = true; }
        if (IsKeyPressed(KEY_R))     { session_load(&sess, sess.path, input);           reloaded = true; }
        if (IsKeyPressed(KEY_H))     show_hud = !show_hud;
        if (IsKeyPressed(KEY_F5)) {
            dbg.enabled = !dbg.enabled;
            if (dbg.enabled) {
                dbg.paused = false;
                dbg.sim_time = sess.elapsed;
                dbg.draw_step_f = 0.0;
                first_debug_frame = true;
            }
        }
        bool want_screenshot = IsKeyPressed(KEY_F12) || IsKeyPressed(KEY_P);

        /* Hot-reload watched .logo file on save */
        watch_timer += dt_real;
        if (watch_timer >= 0.3) {
            watch_timer = 0.0;
            time_t mt = file_mtime(sess.path);
            if (mt != 0 && mt != sess.mtime) {
                session_load(&sess, sess.path, input);
                reloaded = true;
            }
        }
        if (reloaded) {
            debug_reset_playback(&dbg);
            first_debug_frame = true;
        }

        /* Interactive Debug Mode Controls */
        bool step_one_frame = false;
        if (dbg.enabled) {
            if (IsKeyPressed(KEY_TAB)) dbg.paused = !dbg.paused;
            if (IsKeyPressed(KEY_BACKSPACE)) {
                dbg.draw_step_f = 0.0;
                dbg.paused = false;
            }
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
                dbg.sim_scale = fmin(dbg.sim_scale * 1.5, 2.0);
                dbg.draw_rate = fmin(dbg.draw_rate * 1.4, 60.0);
            }
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_LEFT_BRACKET)) {
                dbg.sim_scale = fmax(dbg.sim_scale / 1.5, 0.02);
                dbg.draw_rate = fmax(dbg.draw_rate / 1.4, 0.5);
            }
            if (IsKeyPressed(KEY_PERIOD)) {
                dbg.paused = true;
                dbg.draw_step_f = floor(dbg.draw_step_f) + 1.0;
            }
            if (IsKeyPressed(KEY_COMMA)) {
                dbg.paused = true;
                dbg.draw_step_f = fmax(0.0, floor(dbg.draw_step_f) - 1.0);
            }
            if (IsKeyPressed(KEY_N)) {
                dbg.paused = true;
                step_one_frame = true;
            }
        }

        double run_dt = dt_real;
        bool should_step_vm = true;
        if (dbg.enabled) {
            dbg.last_dt_real = dt_real;
            if (step_one_frame) {
                run_dt = 1.0 / 60.0;
                dbg.sim_time += run_dt;
                dbg.sim_frame++;
                dbg.last_dt_sim = run_dt;
                sess.elapsed = dbg.sim_time;
            } else if (!dbg.paused) {
                run_dt = dt_real * dbg.sim_scale;
                dbg.sim_time += run_dt;
                dbg.sim_frame++;
                dbg.last_dt_sim = run_dt;
                sess.elapsed = dbg.sim_time;
                dbg.draw_step_f += dt_real * dbg.draw_rate;
            } else {
                dbg.last_dt_sim = 0.0;
                should_step_vm = first_debug_frame;
                run_dt = 0.0;
            }
            first_debug_frame = false;
        } else {
            sess.elapsed += dt_real;
            dbg.sim_time = sess.elapsed;
            dbg.sim_frame++;
        }

        bool has_err = (sess.load_err[0] != '\0') || !sess.vm || sess.vm->has_error;
        if (!has_err && should_step_vm) {
            logo_vm_run_frame(sess.vm, sess.elapsed, run_dt, input);
            if (sess.vm->has_error) {
                snprintf(sess.load_err, sizeof(sess.load_err), "%s", sess.vm->error);
                has_err = true;
            }
        }

        const SceneIR *scene_to_draw = NULL;
        if (!has_err) {
            if (dbg.enabled && debug_scene) {
                if (dbg.draw_step_f > (double)sess.vm->scene.total_draw_ops) {
                    dbg.draw_step_f = (double)sess.vm->scene.total_draw_ops;
                }
                build_debug_scene_slice(&sess.vm->scene, debug_scene, dbg.draw_step_f, &dbg_cur);
                scene_to_draw = debug_scene;
            } else {
                scene_to_draw = &sess.vm->scene;
            }
        }

        BeginDrawing();
            ClearBackground((Color){12, 14, 22, 255});

            if (!has_err && scene_to_draw) {
                renderer_draw(&renderer, scene_to_draw, input.view_w, input.view_h, sess.elapsed);
                if (dbg.enabled) {
                    draw_debug_overlays(&sess.vm->scene, &dbg, &dbg_cur, input.view_w, input.view_h);
                }
            } else {
                DrawRectangle(40, 80, input.view_w - 80, 120, (Color){60, 16, 24, 230});
                DrawRectangleLines(40, 80, input.view_w - 80, 120, (Color){240, 80, 96, 255});
                DrawText("LOGO SCRIPT ERROR", 60, 98, 22, (Color){255, 130, 140, 255});
                DrawText(sess.load_err, 60, 132, 18, WHITE);
                DrawText("Fix the script and save to hot-reload (or press 1-7).", 60, 164, 15, LIGHTGRAY);
            }

            if (show_hud && sess.vm) {
                const SceneIR *sc = &sess.vm->scene;
                int prims = (sc->mode == RENDER_MODE_3D) ? sc->prim3d_count : sc->prim2d_count;
                DrawRectangle(12, 12, 1020, 56, (Color){8, 10, 18, 195});
                DrawRectangleLines(12, 12, 1020, 56, (Color){70, 90, 130, 180});

                char line1[256];
                snprintf(line1, sizeof(line1), "TurtleFX%s  |  %.110s  |  Mode: %s  |  Prims: %d  |  %d FPS",
                         dbg.enabled ? " [DEBUG]" : "", sess.path, mode_name(sc->mode), prims, GetFPS());
                DrawText(line1, 24, 20, 16, (Color){230, 240, 255, 255});
                DrawText("[1] Retro [2] 2D [3] 3D [4] Retro3D [5] Shader [6] Actors [7] Hades Iso  [F5] Debug [R] Reload [H] HUD [F12] Shot",
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

    free(debug_scene);
    session_free(&sess);
    host_unload_sounds();
    renderer_shutdown(&renderer);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
