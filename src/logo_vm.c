#include "logo_lang.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float DEG2RAD_F = (float)(M_PI / 180.0);

static void vm_error(LogoVM *vm, int line, const char *fmt, ...) {
    if (vm->has_error) return;
    vm->has_error = true;
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(vm->error, sizeof(vm->error), "Runtime (line %d): %.200s", line, msg);
}

static bool seq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* ============================================================================
 *  Defaults & Math Helpers
 * ============================================================================ */

void logo_scene_defaults(SceneIR *s) {
    memset(s, 0, sizeof(*s));
    s->mode = RENDER_MODE_2D;
    s->retro_w = 320;
    s->retro_h = 180;
    s->palette = PALETTE_NONE;

    s->clear_r = 0.05f;
    s->clear_g = 0.06f;
    s->clear_b = 0.09f;

    s->sky_top[0] = 0.06f; s->sky_top[1] = 0.09f; s->sky_top[2] = 0.16f;
    s->sky_bot[0] = 0.12f; s->sky_bot[1] = 0.11f; s->sky_bot[2] = 0.29f;
    s->sky_gradient = true;

    s->cam_pos[0] = 0.0f; s->cam_pos[1] = 4.0f; s->cam_pos[2] = 10.0f;
    s->cam_look[0] = 0.0f; s->cam_look[1] = 1.5f; s->cam_look[2] = 0.0f;
    s->cam_fov = 48.0f;

    s->light_dir[0] = -0.45f; s->light_dir[1] = -0.82f; s->light_dir[2] = -0.35f;
    s->light_color[0] = 1.0f; s->light_color[1] = 0.94f; s->light_color[2] = 0.85f;
    s->light_shadows = true;

    s->fog_near = 8.0f;
    s->fog_far = 24.0f;
}

static void turtle_reset(TurtleState *t) {
    memset(t, 0, sizeof(*t));
    t->heading = 0.0f;
    /* 3D default frame: forward = +Z, up = +Y, right = +X */
    t->fx = 0.0f; t->fy = 0.0f; t->fz = 1.0f;
    t->ux = 0.0f; t->uy = 1.0f; t->uz = 0.0f;
    t->rx = 1.0f; t->ry = 0.0f; t->rz = 0.0f;

    t->pen_down = true;
    t->color[0] = 1.0f; t->color[1] = 1.0f; t->color[2] = 1.0f; t->color[3] = 1.0f;
    t->pen_width = 2.0f;
    t->glow = 0.0f;
    t->blend = LOGO_BLEND_ALPHA;

    t->mat_type = MAT3D_MATTE;
    t->mat_param = 0.0f;
    t->smooth_k = 0.0f;
    t->op_type = OP3D_UNION;
}

static void vec3_norm(float *x, float *y, float *z) {
    float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
    if (len > 1e-6f) {
        *x /= len;
        *y /= len;
        *z /= len;
    }
}

static void vec3_cross(float ax, float ay, float az,
                       float bx, float by, float bz,
                       float *ox, float *oy, float *oz) {
    *ox = ay * bz - az * by;
    *oy = az * bx - ax * bz;
    *oz = ax * by - ay * bx;
}

/* Rodrigues rotation of (vx,vy,vz) around unit axis (ax,ay,az) by rad */
static void vec3_rotate_axis(float *vx, float *vy, float *vz,
                             float ax, float ay, float az, float rad) {
    float c = cosf(rad);
    float s = sinf(rad);
    float dot = (*vx) * ax + (*vy) * ay + (*vz) * az;
    float cx, cy, cz;
    vec3_cross(ax, ay, az, *vx, *vy, *vz, &cx, &cy, &cz);
    float nx = (*vx) * c + cx * s + ax * dot * (1.0f - c);
    float ny = (*vy) * c + cy * s + ay * dot * (1.0f - c);
    float nz = (*vz) * c + cz * s + az * dot * (1.0f - c);
    *vx = nx; *vy = ny; *vz = nz;
}

static void turtle_reortho(TurtleState *t) {
    vec3_norm(&t->fx, &t->fy, &t->fz);
    /* Right-handed basis: right = cross(up, fwd), up = cross(fwd, right) */
    vec3_cross(t->ux, t->uy, t->uz, t->fx, t->fy, t->fz, &t->rx, &t->ry, &t->rz);
    vec3_norm(&t->rx, &t->ry, &t->rz);
    vec3_cross(t->fx, t->fy, t->fz, t->rx, t->ry, t->rz, &t->ux, &t->uy, &t->uz);
    vec3_norm(&t->ux, &t->uy, &t->uz);
}

static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    if (s > 1.0f) s *= 0.01f;
    if (l > 1.0f) l *= 0.01f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (l < 0.0f) l = 0.0f;
    if (l > 1.0f) l = 1.0f;

    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c * 0.5f;
    float r1 = 0, g1 = 0, b1 = 0;
    if (h < 60)       { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }
    *r = r1 + m; *g = g1 + m; *b = b1 + m;
}

static LogoVal make_num(double v) {
    LogoVal r;
    memset(&r, 0, sizeof(r));
    r.type = VAL_NUM;
    r.num = v;
    return r;
}

static LogoVal make_color(float r, float g, float b, float a) {
    LogoVal v;
    memset(&v, 0, sizeof(v));
    v.type = VAL_COLOR;
    v.vec[0] = r; v.vec[1] = g; v.vec[2] = b; v.vec[3] = a;
    return v;
}

static LogoVal make_sym(const char *s) {
    LogoVal v;
    memset(&v, 0, sizeof(v));
    v.type = VAL_SYM;
    snprintf(v.str, sizeof(v.str), "%s", s);
    return v;
}

static double val_as_num(LogoVal v) {
    return (v.type == VAL_NUM) ? v.num : 0.0;
}

static bool val_truthy(LogoVal v) {
    if (v.type == VAL_NUM) return fabs(v.num) > 1e-9;
    if (v.type == VAL_SYM) return !seq(v.str, "off") && !seq(v.str, "false") && !seq(v.str, "none");
    if (v.type == VAL_STR) return v.str[0] != '\0';
    return true;
}

static PaletteKind parse_palette_sym(const char *name) {
    if (seq(name, "pico8"))   return PALETTE_PICO8;
    if (seq(name, "gameboy") || seq(name, "gb")) return PALETTE_GAMEBOY;
    if (seq(name, "cga"))     return PALETTE_CGA;
    if (seq(name, "sunset"))  return PALETTE_SUNSET;
    return PALETTE_NONE;
}

/* ============================================================================
 *  Variables
 * ============================================================================ */

static LogoVar *find_var(LogoVM *vm, const char *name) {
    int base = (vm->call_depth > 0) ? vm->frame_base[vm->call_depth - 1] : 0;
    for (int i = vm->local_count - 1; i >= base; i--) {
        if (seq(vm->locals[i].name, name)) return &vm->locals[i];
    }
    for (int i = vm->global_count - 1; i >= 0; i--) {
        if (seq(vm->globals[i].name, name)) return &vm->globals[i];
    }
    return NULL;
}

static void declare_var(LogoVM *vm, const char *name, LogoVal val, bool force_local) {
    if (force_local || vm->call_depth > 0) {
        int base = (vm->call_depth > 0) ? vm->frame_base[vm->call_depth - 1] : 0;
        for (int i = vm->local_count - 1; i >= base; i--) {
            if (seq(vm->locals[i].name, name)) {
                vm->locals[i].val = val;
                return;
            }
        }
        if (vm->local_count < LOGO_MAX_VARS) {
            strcpy(vm->locals[vm->local_count].name, name);
            vm->locals[vm->local_count].val = val;
            vm->local_count++;
        }
    } else {
        for (int i = 0; i < vm->global_count; i++) {
            if (seq(vm->globals[i].name, name)) {
                vm->globals[i].val = val;
                return;
            }
        }
        if (vm->global_count < LOGO_MAX_VARS) {
            strcpy(vm->globals[vm->global_count].name, name);
            vm->globals[vm->global_count].val = val;
            vm->global_count++;
        }
    }
}

/* ============================================================================
 *  2D / 3D Primitive Emission + Ear-Clipping Polygon Triangulator
 * ============================================================================ */

static void emit_prim2d(LogoVM *vm, Prim2D p) {
    if (vm->scene.prim2d_count < SCENE_MAX_2D_PRIMS) {
        vm->scene.prims2d[vm->scene.prim2d_count++] = p;
    }
}

static void emit_prim3d(LogoVM *vm, Prim3D p) {
    if (vm->scene.prim3d_count < SCENE_MAX_3D_PRIMS) {
        vm->scene.prims3d[vm->scene.prim3d_count++] = p;
    }
}

static float tri_area2(float ax, float ay, float bx, float by, float cx, float cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

static bool point_in_tri(float px, float py,
                         float ax, float ay, float bx, float by, float cx, float cy) {
    float d1 = tri_area2(px, py, ax, ay, bx, by);
    float d2 = tri_area2(px, py, bx, by, cx, cy);
    float d3 = tri_area2(px, py, cx, cy, ax, ay);
    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

static void flush_fill_polygon(LogoVM *vm) {
    int n = vm->fill_count;
    if (n >= 3) {
        float dx = vm->fill_x[n - 1] - vm->fill_x[0];
        float dy = vm->fill_y[n - 1] - vm->fill_y[0];
        if (dx * dx + dy * dy < 1e-4f) n--;
    }
    if (n < 3) return;

    int idx[LOGO_MAX_FILL_PTS];
    float signed_area = 0.0f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        signed_area += vm->fill_x[i] * vm->fill_y[j] - vm->fill_x[j] * vm->fill_y[i];
    }
    /* Ensure CCW winding */
    for (int i = 0; i < n; i++) {
        idx[i] = (signed_area >= 0.0f) ? i : (n - 1 - i);
    }

    int rem = n;
    int guard = 0;
    while (rem > 3 && guard++ < n * n) {
        bool clipped = false;
        for (int i = 0; i < rem; i++) {
            int i0 = idx[(i + rem - 1) % rem];
            int i1 = idx[i];
            int i2 = idx[(i + 1) % rem];
            float ax = vm->fill_x[i0], ay = vm->fill_y[i0];
            float bx = vm->fill_x[i1], by = vm->fill_y[i1];
            float cx = vm->fill_x[i2], cy = vm->fill_y[i2];
            if (tri_area2(ax, ay, bx, by, cx, cy) <= 1e-5f) continue;

            bool contains = false;
            for (int k = 0; k < rem; k++) {
                int ik = idx[k];
                if (ik == i0 || ik == i1 || ik == i2) continue;
                if (point_in_tri(vm->fill_x[ik], vm->fill_y[ik], ax, ay, bx, by, cx, cy)) {
                    contains = true;
                    break;
                }
            }
            if (!contains) {
                Prim2D p = {0};
                p.type = PRIM2D_TRIANGLE;
                p.x0 = ax; p.y0 = ay;
                p.x1 = bx; p.y1 = by;
                p.x2 = cx; p.y2 = cy;
                p.r = vm->turtle.color[0];
                p.g = vm->turtle.color[1];
                p.b = vm->turtle.color[2];
                p.a = vm->turtle.color[3];
                p.glow = vm->turtle.glow;
                p.blend = vm->turtle.blend;
                emit_prim2d(vm, p);

                for (int m = i; m < rem - 1; m++) idx[m] = idx[m + 1];
                rem--;
                clipped = true;
                break;
            }
        }
        if (!clipped) break;
    }
    if (rem == 3) {
        Prim2D p = {0};
        p.type = PRIM2D_TRIANGLE;
        p.x0 = vm->fill_x[idx[0]]; p.y0 = vm->fill_y[idx[0]];
        p.x1 = vm->fill_x[idx[1]]; p.y1 = vm->fill_y[idx[1]];
        p.x2 = vm->fill_x[idx[2]]; p.y2 = vm->fill_y[idx[2]];
        p.r = vm->turtle.color[0];
        p.g = vm->turtle.color[1];
        p.b = vm->turtle.color[2];
        p.a = vm->turtle.color[3];
        p.glow = vm->turtle.glow;
        p.blend = vm->turtle.blend;
        emit_prim2d(vm, p);
    }
}

/* ============================================================================
 *  Expression Evaluation
 * ============================================================================ */

static LogoVal eval_expr(LogoVM *vm, Expr *e);

static LogoVal eval_arg_named(LogoVM *vm, CallArg *args, int count,
                              const char *name, int pos_index, LogoVal fallback) {
    for (int i = 0; i < count; i++) {
        if (args[i].name[0] && seq(args[i].name, name)) {
            return eval_expr(vm, args[i].value);
        }
    }
    int pos = 0;
    for (int i = 0; i < count; i++) {
        if (!args[i].name[0]) {
            if (pos == pos_index) return eval_expr(vm, args[i].value);
            pos++;
        }
    }
    return fallback;
}

static bool has_arg(CallArg *args, int count, const char *name, int pos_index) {
    for (int i = 0; i < count; i++) {
        if (args[i].name[0] && seq(args[i].name, name)) return true;
    }
    int pos = 0;
    for (int i = 0; i < count; i++) {
        if (!args[i].name[0]) {
            if (pos == pos_index) return true;
            pos++;
        }
    }
    return false;
}

static LogoVal eval_expr(LogoVM *vm, Expr *e) {
    if (!e || vm->has_error) return make_num(0);

    switch (e->kind) {
        case EXPR_NUM:
            return make_num(e->as.num);

        case EXPR_COLOR:
            return make_color(e->as.color[0], e->as.color[1], e->as.color[2], e->as.color[3]);

        case EXPR_STR: {
            LogoVal v = {0};
            v.type = VAL_STR;
            snprintf(v.str, sizeof(v.str), "%s", e->as.str.small);
            v.long_str = e->as.str.heap;
            return v;
        }

        case EXPR_LIST: {
            LogoVal v = {0};
            v.type = VAL_VEC3;
            v.list_len = e->as.list.count < LOGO_MAX_LIST ? e->as.list.count : LOGO_MAX_LIST;
            for (int i = 0; i < v.list_len; i++) {
                v.vec[i] = (float)val_as_num(eval_expr(vm, e->as.list.items[i]));
            }
            return v;
        }

        case EXPR_INDEX: {
            LogoVar *var = find_var(vm, e->as.index.name);
            int idx = (int)val_as_num(eval_expr(vm, e->as.index.index));
            if (!var) {
                vm_error(vm, e->line, "Unknown list variable '%s'", e->as.index.name);
                return make_num(0);
            }
            if (idx < 0 || idx >= LOGO_MAX_LIST) {
                vm_error(vm, e->line, "List index %d out of bounds for '%s'", idx, e->as.index.name);
                return make_num(0);
            }
            return make_num((double)var->val.vec[idx]);
        }

        case EXPR_IDENT: {
            const char *id = e->as.ident;
            if (seq(id, "time"))   return make_num(vm->time);
            if (seq(id, "dt"))     return make_num(vm->dt);
            if (seq(id, "pi"))     return make_num(M_PI);
            if (seq(id, "width"))  return make_num(vm->scene.mode == RENDER_MODE_RETRO ? vm->scene.retro_w : vm->input.view_w);
            if (seq(id, "height")) return make_num(vm->scene.mode == RENDER_MODE_RETRO ? vm->scene.retro_h : vm->input.view_h);
            if (seq(id, "true") || seq(id, "on"))   return make_num(1.0);
            if (seq(id, "false") || seq(id, "off")) return make_num(0.0);

            LogoVar *var = find_var(vm, id);
            if (var) return var->val;

            /* Treat unknown bare identifiers as symbolic atoms (e.g. pico8, bayer, add, 2d, 3d) */
            return make_sym(id);
        }

        case EXPR_UNARY: {
            LogoVal o = eval_expr(vm, e->as.unary.operand);
            if (e->as.unary.op == OP_NEG) return make_num(-val_as_num(o));
            if (e->as.unary.op == OP_NOT) return make_num(val_truthy(o) ? 0.0 : 1.0);
            return make_num(0);
        }

        case EXPR_BINARY: {
            if (e->as.binary.op == OP_AND) {
                LogoVal l = eval_expr(vm, e->as.binary.left);
                if (!val_truthy(l)) return make_num(0.0);
                return make_num(val_truthy(eval_expr(vm, e->as.binary.right)) ? 1.0 : 0.0);
            }
            if (e->as.binary.op == OP_OR) {
                LogoVal l = eval_expr(vm, e->as.binary.left);
                if (val_truthy(l)) return make_num(1.0);
                return make_num(val_truthy(eval_expr(vm, e->as.binary.right)) ? 1.0 : 0.0);
            }
            double l = val_as_num(eval_expr(vm, e->as.binary.left));
            double r = val_as_num(eval_expr(vm, e->as.binary.right));
            switch (e->as.binary.op) {
                case OP_ADD: return make_num(l + r);
                case OP_SUB: return make_num(l - r);
                case OP_MUL: return make_num(l * r);
                case OP_DIV: return make_num(fabs(r) > 1e-9 ? l / r : 0.0);
                case OP_MOD: {
                    if (fabs(r) <= 1e-9) return make_num(0.0);
                    double m = fmod(l, r);
                    if (m < 0.0) m += fabs(r);
                    return make_num(m);
                }
                case OP_EQ:  return make_num(fabs(l - r) < 1e-6 ? 1.0 : 0.0);
                case OP_NEQ: return make_num(fabs(l - r) >= 1e-6 ? 1.0 : 0.0);
                case OP_LT:  return make_num(l < r ? 1.0 : 0.0);
                case OP_LTE: return make_num(l <= r ? 1.0 : 0.0);
                case OP_GT:  return make_num(l > r ? 1.0 : 0.0);
                case OP_GTE: return make_num(l >= r ? 1.0 : 0.0);
                default:     return make_num(0.0);
            }
        }

        case EXPR_CALL: {
            const char *fn = e->as.call.name;
            CallArg *args = e->as.call.args;
            int ac = e->as.call.arg_count;

            if (seq(fn, "sin"))   return make_num(sin(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)))));
            if (seq(fn, "cos"))   return make_num(cos(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)))));
            if (seq(fn, "tan"))   return make_num(tan(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)))));
            if (seq(fn, "dsin"))  return make_num(sin(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0))) * M_PI / 180.0));
            if (seq(fn, "dcos"))  return make_num(cos(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0))) * M_PI / 180.0));
            if (seq(fn, "rad"))   return make_num(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0))) * M_PI / 180.0);
            if (seq(fn, "deg"))   return make_num(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0))) * 180.0 / M_PI);
            if (seq(fn, "abs"))   return make_num(fabs(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)))));
            if (seq(fn, "sqrt"))  return make_num(sqrt(fmax(0.0, val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0))))));
            if (seq(fn, "floor")) return make_num(floor(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)))));
            if (seq(fn, "ceil"))  return make_num(ceil(val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)))));
            if (seq(fn, "fract")) {
                double x = val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)));
                return make_num(x - floor(x));
            }
            if (seq(fn, "min")) {
                double a = val_as_num(eval_arg_named(vm, args, ac, "a", 0, make_num(0)));
                double b = val_as_num(eval_arg_named(vm, args, ac, "b", 1, make_num(0)));
                return make_num(a < b ? a : b);
            }
            if (seq(fn, "max")) {
                double a = val_as_num(eval_arg_named(vm, args, ac, "a", 0, make_num(0)));
                double b = val_as_num(eval_arg_named(vm, args, ac, "b", 1, make_num(0)));
                return make_num(a > b ? a : b);
            }
            if (seq(fn, "clamp")) {
                double x  = val_as_num(eval_arg_named(vm, args, ac, "x", 0, make_num(0)));
                double lo = val_as_num(eval_arg_named(vm, args, ac, "min", 1, make_num(0)));
                double hi = val_as_num(eval_arg_named(vm, args, ac, "max", 2, make_num(1)));
                if (x < lo) x = lo;
                if (x > hi) x = hi;
                return make_num(x);
            }
            if (seq(fn, "pow")) {
                double a = val_as_num(eval_arg_named(vm, args, ac, "a", 0, make_num(0)));
                double b = val_as_num(eval_arg_named(vm, args, ac, "b", 1, make_num(1)));
                return make_num(pow(a, b));
            }
            if (seq(fn, "lerp") || seq(fn, "mix")) {
                double a = val_as_num(eval_arg_named(vm, args, ac, "a", 0, make_num(0)));
                double b = val_as_num(eval_arg_named(vm, args, ac, "b", 1, make_num(1)));
                double t = val_as_num(eval_arg_named(vm, args, ac, "t", 2, make_num(0.5)));
                return make_num(a + (b - a) * t);
            }
            if (seq(fn, "hsl")) {
                float h = (float)val_as_num(eval_arg_named(vm, args, ac, "h", 0, make_num(0)));
                float s = (float)val_as_num(eval_arg_named(vm, args, ac, "s", 1, make_num(100)));
                float l = (float)val_as_num(eval_arg_named(vm, args, ac, "l", 2, make_num(50)));
                float r, g, b;
                hsl_to_rgb(h, s, l, &r, &g, &b);
                return make_color(r, g, b, 1.0f);
            }
            if (seq(fn, "rgb")) {
                float r = (float)val_as_num(eval_arg_named(vm, args, ac, "r", 0, make_num(1)));
                float g = (float)val_as_num(eval_arg_named(vm, args, ac, "g", 1, make_num(1)));
                float b = (float)val_as_num(eval_arg_named(vm, args, ac, "b", 2, make_num(1)));
                if (r > 1.0f || g > 1.0f || b > 1.0f) { r /= 255.0f; g /= 255.0f; b /= 255.0f; }
                return make_color(r, g, b, 1.0f);
            }
            if (seq(fn, "key")) {
                LogoVal k = eval_arg_named(vm, args, ac, "name", 0, make_sym(""));
                bool down = vm->input.is_key_down ? vm->input.is_key_down(k.str) : false;
                return make_num(down ? 1.0 : 0.0);
            }
            if (seq(fn, "key_pressed")) {
                LogoVal k = eval_arg_named(vm, args, ac, "name", 0, make_sym(""));
                bool pressed = vm->input.is_key_pressed ? vm->input.is_key_pressed(k.str) : false;
                return make_num(pressed ? 1.0 : 0.0);
            }
            if (seq(fn, "mouse_x")) return make_num(vm->input.mouse_x);
            if (seq(fn, "mouse_y")) return make_num(vm->input.mouse_y);
            if (seq(fn, "rand")) {
                vm->rng_state = vm->rng_state * 1664525u + 1013904223u;
                return make_num((double)(vm->rng_state >> 8) / (double)(1u << 24));
            }
            return make_num(0);
        }
    }
    return make_num(0);
}

/* ============================================================================
 *  Statement Execution
 * ============================================================================ */

static void exec_list(LogoVM *vm, StmtList *list);

static Stmt *find_proc(LogoVM *vm, const char *name) {
    for (int i = 0; i < vm->proc_count; i++) {
        if (seq(vm->procs[i]->as.proc_stmt.name, name)) return vm->procs[i];
    }
    return NULL;
}

static void push_turtle(LogoVM *vm) {
    if (vm->turtle_sp < LOGO_MAX_TURTLE_STK) {
        vm->turtle_stack[vm->turtle_sp++] = vm->turtle;
    }
}

static void pop_turtle(LogoVM *vm) {
    if (vm->turtle_sp > 0) {
        vm->turtle = vm->turtle_stack[--vm->turtle_sp];
    }
}

static void turtle_forward(LogoVM *vm, float dist) {
    if (vm->scene.mode == RENDER_MODE_3D) {
        vm->turtle.x += vm->turtle.fx * dist;
        vm->turtle.y += vm->turtle.fy * dist;
        vm->turtle.z += vm->turtle.fz * dist;
        return;
    }

    /* 2D & Retro: heading 0 = up (+Y in 2D centered coords, -Y in retro top-left coords) */
    float rad = vm->turtle.heading * DEG2RAD_F;
    float dx = sinf(rad) * dist;
    float dy = (vm->scene.mode == RENDER_MODE_RETRO) ? (-cosf(rad) * dist) : (cosf(rad) * dist);
    float nx = vm->turtle.x + dx;
    float ny = vm->turtle.y + dy;

    if (vm->recording_fill) {
        if (vm->fill_count < LOGO_MAX_FILL_PTS) {
            vm->fill_x[vm->fill_count] = nx;
            vm->fill_y[vm->fill_count] = ny;
            vm->fill_count++;
        }
    } else if (vm->turtle.pen_down) {
        Prim2D p = {0};
        p.type = PRIM2D_SEGMENT;
        p.x0 = vm->turtle.x; p.y0 = vm->turtle.y;
        p.x1 = nx;           p.y1 = ny;
        p.radius = vm->turtle.pen_width * 0.5f;
        p.r = vm->turtle.color[0];
        p.g = vm->turtle.color[1];
        p.b = vm->turtle.color[2];
        p.a = vm->turtle.color[3];
        p.glow = vm->turtle.glow;
        p.blend = vm->turtle.blend;
        emit_prim2d(vm, p);
    }

    vm->turtle.x = nx;
    vm->turtle.y = ny;
}

static void exec_fx_call(LogoVM *vm, Expr *fx_expr) {
    if (!fx_expr) return;
    if (fx_expr->kind == EXPR_CALL) {
        const char *fn = fx_expr->as.call.name;
        CallArg *a = fx_expr->as.call.args;
        int ac = fx_expr->as.call.arg_count;
        if (seq(fn, "crt")) {
            vm->scene.crt_enabled = true;
            vm->scene.crt_scanlines = (float)val_as_num(eval_arg_named(vm, a, ac, "scanlines", 0, make_num(0.35)));
            vm->scene.crt_curve     = (float)val_as_num(eval_arg_named(vm, a, ac, "curve", 1, make_num(0.12)));
        } else if (seq(fn, "bloom")) {
            vm->scene.bloom_enabled  = true;
            vm->scene.bloom_strength = (float)val_as_num(eval_arg_named(vm, a, ac, "strength", 0, make_num(0.6)));
            vm->scene.bloom_radius   = (float)val_as_num(eval_arg_named(vm, a, ac, "radius", 1, make_num(10.0)));
        } else if (seq(fn, "vignette")) {
            vm->scene.vignette = (float)val_as_num(eval_arg_named(vm, a, ac, "strength", 0, make_num(0.3)));
        } else if (seq(fn, "retro")) {
            vm->scene.retro_fx_enabled = true;
            vm->scene.retro_w = (int)val_as_num(eval_arg_named(vm, a, ac, "w", 0, make_num(320)));
            vm->scene.retro_h = (int)val_as_num(eval_arg_named(vm, a, ac, "h", 1, make_num(180)));
            if (vm->scene.retro_w < 64) vm->scene.retro_w = 64;
            if (vm->scene.retro_h < 36) vm->scene.retro_h = 36;
            LogoVal d = eval_arg_named(vm, a, ac, "dither", 2, make_sym("bayer"));
            vm->scene.retro_dither = val_truthy(d);
            LogoVal pal = eval_arg_named(vm, a, ac, "palette", 3, make_sym("none"));
            if (pal.type == VAL_SYM || pal.type == VAL_STR) {
                vm->scene.palette = parse_palette_sym(pal.str);
            }
        } else if (seq(fn, "fog")) {
            vm->scene.fog_enabled = true;
            LogoVal col = eval_arg_named(vm, a, ac, "color", 0, make_color(0.06f, 0.22f, 0.06f, 1.0f));
            vm->scene.fog_color[0] = col.vec[0];
            vm->scene.fog_color[1] = col.vec[1];
            vm->scene.fog_color[2] = col.vec[2];
            vm->scene.fog_near = (float)val_as_num(eval_arg_named(vm, a, ac, "near", 1, make_num(6.0)));
            vm->scene.fog_far  = (float)val_as_num(eval_arg_named(vm, a, ac, "far", 2, make_num(18.0)));
        } else if (seq(fn, "shader")) {
            LogoVal src = eval_arg_named(vm, a, ac, "path", 0,
                          eval_arg_named(vm, a, ac, "code", 0, make_sym("")));
            const char *txt = src.long_str ? src.long_str : src.str;
            if (txt[0]) {
                vm->scene.custom_shader_enabled = true;
                bool is_inline = (strchr(txt, '\n') != NULL) || (strncmp(txt, "#version", 8) == 0);
                if (is_inline) {
                    vm->scene.custom_shader_code = txt;
                    vm->scene.custom_shader_path[0] = '\0';
                } else {
                    vm->scene.custom_shader_code = NULL;
                    snprintf(vm->scene.custom_shader_path, sizeof(vm->scene.custom_shader_path), "%s", txt);
                }
                vm->scene.custom_params[0] = (float)val_as_num(eval_arg_named(vm, a, ac, "p0", 1,
                    eval_arg_named(vm, a, ac, "amount", 1, eval_arg_named(vm, a, ac, "strength", 1, make_num(0.0)))));
                vm->scene.custom_params[1] = (float)val_as_num(eval_arg_named(vm, a, ac, "p1", 2,
                    eval_arg_named(vm, a, ac, "speed", 2, make_num(1.0))));
                vm->scene.custom_params[2] = (float)val_as_num(eval_arg_named(vm, a, ac, "p2", 3,
                    eval_arg_named(vm, a, ac, "scale", 3, make_num(1.0))));
                vm->scene.custom_params[3] = (float)val_as_num(eval_arg_named(vm, a, ac, "p3", 4,
                    eval_arg_named(vm, a, ac, "mix", 4, make_num(1.0))));
            }
        }
    }
}

static void exec_cmd(LogoVM *vm, Stmt *s) {
    const char *cmd = s->as.cmd_stmt.name;
    CallArg *a = s->as.cmd_stmt.args;
    int ac = s->as.cmd_stmt.arg_count;

    /* ---- Mode & Global Render Config ---- */
    if (seq(cmd, "mode")) {
        if (ac >= 1 && a[0].value->kind == EXPR_CALL && seq(a[0].value->as.call.name, "retro")) {
            vm->scene.mode = RENDER_MODE_RETRO;
            vm->scene.retro_fx_enabled = true;
            CallArg *ra = a[0].value->as.call.args;
            int rac = a[0].value->as.call.arg_count;
            vm->scene.retro_w = (int)val_as_num(eval_arg_named(vm, ra, rac, "w", 0, make_num(320)));
            vm->scene.retro_h = (int)val_as_num(eval_arg_named(vm, ra, rac, "h", 1, make_num(180)));
            vm->scene.retro_dither = false;
        } else if (ac >= 1) {
            LogoVal v = eval_expr(vm, a[0].value);
            if (seq(v.str, "retro")) {
                vm->scene.mode = RENDER_MODE_RETRO;
                vm->scene.retro_fx_enabled = true;
            } else if (seq(v.str, "3d")) {
                vm->scene.mode = RENDER_MODE_3D;
            } else {
                vm->scene.mode = RENDER_MODE_2D;
            }
        }
        return;
    }

    if (seq(cmd, "palette")) {
        LogoVal v = eval_arg_named(vm, a, ac, "name", 0, make_sym("none"));
        vm->scene.palette = parse_palette_sym(v.str);
        return;
    }

    if (seq(cmd, "fx")) {
        if (ac >= 1) exec_fx_call(vm, a[0].value);
        return;
    }

    if (seq(cmd, "play")) {
        LogoVal path = eval_arg_named(vm, a, ac, "path", 0, make_sym(""));
        float vol   = (float)val_as_num(eval_arg_named(vm, a, ac, "volume", 1, make_num(1.0)));
        float pitch = (float)val_as_num(eval_arg_named(vm, a, ac, "pitch", 2, make_num(1.0)));
        if (path.str[0] && vm->input.play_sound) {
            vm->input.play_sound(path.str, vol, pitch);
        }
        return;
    }

    if (seq(cmd, "clear")) {
        LogoVal c = eval_arg_named(vm, a, ac, "color", 0, make_color(0, 0, 0, 1));
        vm->scene.clear_r = c.vec[0];
        vm->scene.clear_g = c.vec[1];
        vm->scene.clear_b = c.vec[2];
        return;
    }

    if (seq(cmd, "sky")) {
        if (ac >= 1 && a[0].value->kind == EXPR_CALL && seq(a[0].value->as.call.name, "gradient")) {
            CallArg *ga = a[0].value->as.call.args;
            int gac = a[0].value->as.call.arg_count;
            LogoVal top = eval_arg_named(vm, ga, gac, "top", 0, make_color(0.06f, 0.09f, 0.16f, 1.0f));
            LogoVal bot = eval_arg_named(vm, ga, gac, "bottom", 1, make_color(0.12f, 0.11f, 0.29f, 1.0f));
            vm->scene.sky_gradient = true;
            memcpy(vm->scene.sky_top, top.vec, 3 * sizeof(float));
            memcpy(vm->scene.sky_bot, bot.vec, 3 * sizeof(float));
        } else if (ac >= 1) {
            LogoVal c = eval_expr(vm, a[0].value);
            vm->scene.sky_gradient = false;
            memcpy(vm->scene.sky_top, c.vec, 3 * sizeof(float));
            memcpy(vm->scene.sky_bot, c.vec, 3 * sizeof(float));
        }
        return;
    }

    if (seq(cmd, "light")) {
        if (ac >= 1 && a[0].value->kind == EXPR_CALL) {
            CallArg *la = a[0].value->as.call.args;
            int lac = a[0].value->as.call.arg_count;
            LogoVal dir = eval_arg_named(vm, la, lac, "dir", 0, (LogoVal){.type=VAL_VEC3, .vec={-1,-2,-1,0}});
            LogoVal col = eval_arg_named(vm, la, lac, "color", 1, make_color(1, 0.94f, 0.85f, 1));
            LogoVal sh  = eval_arg_named(vm, la, lac, "shadows", 2, make_sym("soft"));
            vm->scene.light_dir[0] = dir.vec[0];
            vm->scene.light_dir[1] = dir.vec[1];
            vm->scene.light_dir[2] = dir.vec[2];
            vec3_norm(&vm->scene.light_dir[0], &vm->scene.light_dir[1], &vm->scene.light_dir[2]);
            memcpy(vm->scene.light_color, col.vec, 3 * sizeof(float));
            vm->scene.light_shadows = val_truthy(sh);
        }
        return;
    }

    if (seq(cmd, "camera")) {
        LogoVal pos  = eval_arg_named(vm, a, ac, "pos", 0, (LogoVal){.type=VAL_VEC3, .vec={0,4,10,0}});
        LogoVal look = eval_arg_named(vm, a, ac, "look", 1, (LogoVal){.type=VAL_VEC3, .vec={0,1.5f,0,0}});
        float fov    = (float)val_as_num(eval_arg_named(vm, a, ac, "fov", 2, make_num(48.0)));
        memcpy(vm->scene.cam_pos, pos.vec, 3 * sizeof(float));
        memcpy(vm->scene.cam_look, look.vec, 3 * sizeof(float));
        vm->scene.cam_fov = fov;
        return;
    }

    /* ---- Turtle Movement & Orientation ---- */
    if (seq(cmd, "fd") || seq(cmd, "forward")) {
        float d = (float)val_as_num(eval_arg_named(vm, a, ac, "dist", 0, make_num(0)));
        turtle_forward(vm, d);
        return;
    }
    if (seq(cmd, "bk") || seq(cmd, "back")) {
        float d = (float)val_as_num(eval_arg_named(vm, a, ac, "dist", 0, make_num(0)));
        turtle_forward(vm, -d);
        return;
    }
    if (seq(cmd, "rt") || seq(cmd, "right") || seq(cmd, "yaw")) {
        float deg = (float)val_as_num(eval_arg_named(vm, a, ac, "deg", 0, make_num(0)));
        vm->turtle.heading += deg;
        if (vm->scene.mode == RENDER_MODE_3D) {
            float rad = -deg * DEG2RAD_F;
            vec3_rotate_axis(&vm->turtle.fx, &vm->turtle.fy, &vm->turtle.fz,
                             vm->turtle.ux, vm->turtle.uy, vm->turtle.uz, rad);
            turtle_reortho(&vm->turtle);
        }
        return;
    }
    if (seq(cmd, "lt") || seq(cmd, "left")) {
        float deg = (float)val_as_num(eval_arg_named(vm, a, ac, "deg", 0, make_num(0)));
        vm->turtle.heading -= deg;
        if (vm->scene.mode == RENDER_MODE_3D) {
            float rad = deg * DEG2RAD_F;
            vec3_rotate_axis(&vm->turtle.fx, &vm->turtle.fy, &vm->turtle.fz,
                             vm->turtle.ux, vm->turtle.uy, vm->turtle.uz, rad);
            turtle_reortho(&vm->turtle);
        }
        return;
    }
    if (seq(cmd, "pitch") || seq(cmd, "up")) {
        float deg = (float)val_as_num(eval_arg_named(vm, a, ac, "deg", 0, make_num(0)));
        float rad = -deg * DEG2RAD_F;
        vec3_rotate_axis(&vm->turtle.fx, &vm->turtle.fy, &vm->turtle.fz,
                         vm->turtle.rx, vm->turtle.ry, vm->turtle.rz, rad);
        vec3_rotate_axis(&vm->turtle.ux, &vm->turtle.uy, &vm->turtle.uz,
                         vm->turtle.rx, vm->turtle.ry, vm->turtle.rz, rad);
        turtle_reortho(&vm->turtle);
        return;
    }
    if (seq(cmd, "down")) {
        float deg = (float)val_as_num(eval_arg_named(vm, a, ac, "deg", 0, make_num(0)));
        float rad = deg * DEG2RAD_F;
        vec3_rotate_axis(&vm->turtle.fx, &vm->turtle.fy, &vm->turtle.fz,
                         vm->turtle.rx, vm->turtle.ry, vm->turtle.rz, rad);
        vec3_rotate_axis(&vm->turtle.ux, &vm->turtle.uy, &vm->turtle.uz,
                         vm->turtle.rx, vm->turtle.ry, vm->turtle.rz, rad);
        turtle_reortho(&vm->turtle);
        return;
    }
    if (seq(cmd, "roll")) {
        float deg = (float)val_as_num(eval_arg_named(vm, a, ac, "deg", 0, make_num(0)));
        float rad = deg * DEG2RAD_F;
        vec3_rotate_axis(&vm->turtle.ux, &vm->turtle.uy, &vm->turtle.uz,
                         vm->turtle.fx, vm->turtle.fy, vm->turtle.fz, rad);
        turtle_reortho(&vm->turtle);
        return;
    }
    if (seq(cmd, "pu") || seq(cmd, "penup"))   { vm->turtle.pen_down = false; return; }
    if (seq(cmd, "pd") || seq(cmd, "pendown")) { vm->turtle.pen_down = true;  return; }
    if (seq(cmd, "home")) {
        vm->turtle.x = vm->turtle.y = vm->turtle.z = 0.0f;
        vm->turtle.heading = 0.0f;
        vm->turtle.fx = 0; vm->turtle.fy = 0; vm->turtle.fz = 1;
        vm->turtle.ux = 0; vm->turtle.uy = 1; vm->turtle.uz = 0;
        vm->turtle.rx = 1; vm->turtle.ry = 0; vm->turtle.rz = 0;
        return;
    }

    /* ---- Style & Material ---- */
    if (seq(cmd, "color")) {
        LogoVal c = eval_arg_named(vm, a, ac, "c", 0, make_color(1, 1, 1, 1));
        memcpy(vm->turtle.color, c.vec, 4 * sizeof(float));
        return;
    }
    if (seq(cmd, "pen_width") || seq(cmd, "pw")) {
        vm->turtle.pen_width = (float)val_as_num(eval_arg_named(vm, a, ac, "w", 0, make_num(2.0)));
        return;
    }
    if (seq(cmd, "glow")) {
        vm->turtle.glow = (float)val_as_num(eval_arg_named(vm, a, ac, "g", 0, make_num(0.0)));
        return;
    }
    if (seq(cmd, "blend")) {
        LogoVal b = eval_arg_named(vm, a, ac, "mode", 0, make_sym("alpha"));
        vm->turtle.blend = seq(b.str, "add") ? LOGO_BLEND_ADD : LOGO_BLEND_ALPHA;
        return;
    }
    if (seq(cmd, "material")) {
        if (ac >= 1 && a[0].value->kind == EXPR_CALL) {
            const char *mn = a[0].value->as.call.name;
            CallArg *ma = a[0].value->as.call.args;
            int mac = a[0].value->as.call.arg_count;
            LogoVal col = eval_arg_named(vm, ma, mac, "color", 0, make_color(1, 1, 1, 1));
            memcpy(vm->turtle.color, col.vec, 4 * sizeof(float));
            if (seq(mn, "emissive")) {
                vm->turtle.mat_type = MAT3D_EMISSIVE;
                vm->turtle.mat_param = (float)val_as_num(eval_arg_named(vm, ma, mac, "strength", 1, make_num(2.0)));
            } else if (seq(mn, "glossy")) {
                vm->turtle.mat_type = MAT3D_GLOSSY;
                vm->turtle.mat_param = (float)val_as_num(eval_arg_named(vm, ma, mac, "rough", 1, make_num(0.25)));
            } else {
                vm->turtle.mat_type = MAT3D_MATTE;
                vm->turtle.mat_param = 0.0f;
            }
        }
        return;
    }

    /* ---- 2D / Retro Primitives ---- */
    if (seq(cmd, "dot") || seq(cmd, "circle")) {
        float r = (float)val_as_num(eval_arg_named(vm, a, ac, "r", 0, make_num(2.0)));
        Prim2D p = {0};
        p.type = PRIM2D_CIRCLE;
        p.x0 = vm->turtle.x; p.y0 = vm->turtle.y;
        p.radius = r;
        p.r = vm->turtle.color[0]; p.g = vm->turtle.color[1];
        p.b = vm->turtle.color[2]; p.a = vm->turtle.color[3];
        p.glow = vm->turtle.glow;
        p.blend = vm->turtle.blend;
        emit_prim2d(vm, p);
        return;
    }
    if (seq(cmd, "rect")) {
        float w = (float)val_as_num(eval_arg_named(vm, a, ac, "w", 0, make_num(10.0)));
        float h = (float)val_as_num(eval_arg_named(vm, a, ac, "h", 1, make_num(10.0)));
        Prim2D p = {0};
        p.type = PRIM2D_RECT;
        p.x0 = vm->turtle.x; p.y0 = vm->turtle.y;
        p.x2 = w * 0.5f;     p.y2 = h * 0.5f;
        p.radius = vm->turtle.heading * DEG2RAD_F;
        p.r = vm->turtle.color[0]; p.g = vm->turtle.color[1];
        p.b = vm->turtle.color[2]; p.a = vm->turtle.color[3];
        p.glow = vm->turtle.glow;
        p.blend = vm->turtle.blend;
        emit_prim2d(vm, p);
        return;
    }

    /* ---- 3D SDF Primitives ---- */
    if (seq(cmd, "sphere") || seq(cmd, "capsule") || seq(cmd, "box") ||
        seq(cmd, "cylinder") || seq(cmd, "cone") || seq(cmd, "torus")) {
        Prim3D p = {0};
        p.px = vm->turtle.x;  p.py = vm->turtle.y;  p.pz = vm->turtle.z;
        p.fx = vm->turtle.fx; p.fy = vm->turtle.fy; p.fz = vm->turtle.fz;
        p.ux = vm->turtle.ux; p.uy = vm->turtle.uy; p.uz = vm->turtle.uz;
        p.r = vm->turtle.color[0];
        p.g = vm->turtle.color[1];
        p.b = vm->turtle.color[2];
        p.mat_type  = vm->turtle.mat_type;
        p.mat_param = vm->turtle.mat_param;
        p.smooth_k  = vm->turtle.smooth_k;
        p.op_type   = vm->turtle.op_type;

        if (seq(cmd, "sphere")) {
            p.type = PRIM3D_SPHERE;
            p.p0 = (float)val_as_num(eval_arg_named(vm, a, ac, "radius", 0, make_num(1.0)));
        } else if (seq(cmd, "capsule")) {
            p.type = PRIM3D_CAPSULE;
            p.p0 = (float)val_as_num(eval_arg_named(vm, a, ac, "length", 0, make_num(1.0)));
            p.p1 = (float)val_as_num(eval_arg_named(vm, a, ac, "radius", 1, make_num(0.25)));
        } else if (seq(cmd, "box")) {
            p.type = PRIM3D_BOX;
            p.p0 = (float)val_as_num(eval_arg_named(vm, a, ac, "w", 0, make_num(1.0))) * 0.5f;
            p.p1 = (float)val_as_num(eval_arg_named(vm, a, ac, "h", 1, make_num(1.0))) * 0.5f;
            p.p2 = (float)val_as_num(eval_arg_named(vm, a, ac, "d", 2, make_num(1.0))) * 0.5f;
        } else if (seq(cmd, "cylinder")) {
            p.type = PRIM3D_CYLINDER;
            p.p0 = (float)val_as_num(eval_arg_named(vm, a, ac, "radius", 0, make_num(1.0)));
            p.p1 = (float)val_as_num(eval_arg_named(vm, a, ac, "height", 1, make_num(1.0))) * 0.5f;
        } else if (seq(cmd, "cone")) {
            p.type = PRIM3D_CONE;
            p.p0 = (float)val_as_num(eval_arg_named(vm, a, ac, "radius", 0, make_num(1.0)));
            p.p1 = (float)val_as_num(eval_arg_named(vm, a, ac, "height", 1, make_num(2.0)));
        } else if (seq(cmd, "torus")) {
            p.type = PRIM3D_TORUS;
            p.p0 = (float)val_as_num(eval_arg_named(vm, a, ac, "major", 0, make_num(1.0)));
            p.p1 = (float)val_as_num(eval_arg_named(vm, a, ac, "minor", 1, make_num(0.25)));
        }
        emit_prim3d(vm, p);
        return;
    }

    /* ---- User-defined Procedure Call ---- */
    Stmt *proc = find_proc(vm, cmd);
    if (proc) {
        if (vm->call_depth >= LOGO_MAX_STACK - 1) {
            vm_error(vm, s->line, "Recursion depth limit reached in '%s'", cmd);
            return;
        }
        /* Evaluate args in caller scope before pushing frame */
        LogoVal evaled[LOGO_MAX_PARAMS];
        int pc = proc->as.proc_stmt.param_count;
        for (int i = 0; i < pc; i++) {
            evaled[i] = eval_arg_named(vm, a, ac, proc->as.proc_stmt.params[i], i, make_num(0));
        }

        int saved_locals = vm->local_count;
        vm->frame_base[vm->call_depth++] = saved_locals;
        for (int i = 0; i < pc; i++) {
            declare_var(vm, proc->as.proc_stmt.params[i], evaled[i], true);
        }
        exec_list(vm, &proc->as.proc_stmt.body);
        vm->stopping = false;
        vm->call_depth--;
        vm->local_count = saved_locals;
        return;
    }

    vm_error(vm, s->line, "Unknown command or procedure '%s'", cmd);
}

static void exec_block_cmd(LogoVM *vm, Stmt *s) {
    const char *cmd = s->as.block_cmd_stmt.name;
    CallArg *a = s->as.block_cmd_stmt.args;
    int ac = s->as.block_cmd_stmt.arg_count;

    if (seq(cmd, "branch") || seq(cmd, "save")) {
        push_turtle(vm);
        exec_list(vm, &s->as.block_cmd_stmt.body);
        pop_turtle(vm);
        return;
    }

    if (seq(cmd, "at")) {
        float x = (float)val_as_num(eval_arg_named(vm, a, ac, "x", 0, make_num(0)));
        float y = (float)val_as_num(eval_arg_named(vm, a, ac, "y", 1, make_num(0)));
        float z = (float)val_as_num(eval_arg_named(vm, a, ac, "z", 2, make_num(0)));
        push_turtle(vm);
        vm->turtle.x = x;
        vm->turtle.y = y;
        if (vm->scene.mode == RENDER_MODE_3D && has_arg(a, ac, "z", 2)) {
            vm->turtle.z = z;
        }
        vm->turtle.heading = 0.0f;
        vm->turtle.fx = 0; vm->turtle.fy = 0; vm->turtle.fz = 1;
        vm->turtle.ux = 0; vm->turtle.uy = 1; vm->turtle.uz = 0;
        vm->turtle.rx = 1; vm->turtle.ry = 0; vm->turtle.rz = 0;
        exec_list(vm, &s->as.block_cmd_stmt.body);
        pop_turtle(vm);
        return;
    }

    if (seq(cmd, "offset")) {
        float dx = (float)val_as_num(eval_arg_named(vm, a, ac, "x", 0, make_num(0)));
        float dy = (float)val_as_num(eval_arg_named(vm, a, ac, "y", 1, make_num(0)));
        float dz = (float)val_as_num(eval_arg_named(vm, a, ac, "z", 2, make_num(0)));
        push_turtle(vm);
        if (vm->scene.mode == RENDER_MODE_3D) {
            vm->turtle.x += dx; vm->turtle.y += dy; vm->turtle.z += dz;
        } else {
            float rad = vm->turtle.heading * DEG2RAD_F;
            float c = cosf(rad), sn = sinf(rad);
            if (vm->scene.mode == RENDER_MODE_RETRO) {
                vm->turtle.x += dx * c + dy * sn;
                vm->turtle.y += dx * sn - dy * c;
            } else {
                vm->turtle.x += dx * c + dy * sn;
                vm->turtle.y += -dx * sn + dy * c;
            }
        }
        exec_list(vm, &s->as.block_cmd_stmt.body);
        pop_turtle(vm);
        return;
    }

    if (seq(cmd, "fill")) {
        push_turtle(vm);
        bool prev_rec = vm->recording_fill;
        vm->recording_fill = true;
        vm->fill_count = 1;
        vm->fill_x[0] = vm->turtle.x;
        vm->fill_y[0] = vm->turtle.y;
        exec_list(vm, &s->as.block_cmd_stmt.body);
        flush_fill_polygon(vm);
        vm->recording_fill = prev_rec;
        vm->fill_count = 0;
        pop_turtle(vm);
        return;
    }

    if (seq(cmd, "smooth")) {
        float k = (float)val_as_num(eval_arg_named(vm, a, ac, "k", 0, make_num(0.5)));
        push_turtle(vm);
        vm->turtle.smooth_k = k;
        vm->turtle.op_type = OP3D_SMOOTH_UNION;
        exec_list(vm, &s->as.block_cmd_stmt.body);
        pop_turtle(vm);
        return;
    }

    if (seq(cmd, "carve")) {
        push_turtle(vm);
        vm->turtle.op_type = OP3D_CARVE;
        vm->turtle.smooth_k = 0.0f;
        exec_list(vm, &s->as.block_cmd_stmt.body);
        pop_turtle(vm);
        return;
    }
}

static void exec_stmt(LogoVM *vm, Stmt *s) {
    if (!s || vm->has_error || vm->stopping) return;

    switch (s->kind) {
        case STMT_PROC:
        case STMT_ON_FRAME:
            /* Already registered during init */
            break;

        case STMT_LET: {
            LogoVal v = eval_expr(vm, s->as.let_stmt.init);
            declare_var(vm, s->as.let_stmt.name, v, false);
            break;
        }

        case STMT_ASSIGN: {
            LogoVal rhs = eval_expr(vm, s->as.assign_stmt.value);
            LogoVar *var = find_var(vm, s->as.assign_stmt.name);
            if (!var) {
                declare_var(vm, s->as.assign_stmt.name, rhs, false);
                break;
            }
            if (s->as.assign_stmt.compound_op == OP_EQ) {
                var->val = rhs;
            } else {
                double a = val_as_num(var->val);
                double b = val_as_num(rhs);
                switch (s->as.assign_stmt.compound_op) {
                    case OP_ADD: a += b; break;
                    case OP_SUB: a -= b; break;
                    case OP_MUL: a *= b; break;
                    case OP_DIV: a = (fabs(b) > 1e-9) ? a / b : 0.0; break;
                    default: break;
                }
                var->val = make_num(a);
            }
            break;
        }

        case STMT_INDEX_ASSIGN: {
            LogoVar *var = find_var(vm, s->as.index_assign_stmt.name);
            int idx = (int)val_as_num(eval_expr(vm, s->as.index_assign_stmt.index));
            double rhs = val_as_num(eval_expr(vm, s->as.index_assign_stmt.value));
            if (!var) {
                vm_error(vm, s->line, "Unknown list variable '%s'", s->as.index_assign_stmt.name);
                break;
            }
            if (idx < 0 || idx >= LOGO_MAX_LIST) {
                vm_error(vm, s->line, "List index %d out of bounds for '%s'", idx, s->as.index_assign_stmt.name);
                break;
            }
            double cur = (double)var->val.vec[idx];
            switch (s->as.index_assign_stmt.compound_op) {
                case OP_EQ:  cur = rhs; break;
                case OP_ADD: cur += rhs; break;
                case OP_SUB: cur -= rhs; break;
                case OP_MUL: cur *= rhs; break;
                case OP_DIV: cur = (fabs(rhs) > 1e-9) ? cur / rhs : 0.0; break;
                default: break;
            }
            var->val.vec[idx] = (float)cur;
            if (idx >= var->val.list_len) var->val.list_len = idx + 1;
            break;
        }

        case STMT_IF: {
            LogoVal c = eval_expr(vm, s->as.if_stmt.cond);
            if (val_truthy(c)) exec_list(vm, &s->as.if_stmt.then_branch);
            else if (s->as.if_stmt.else_branch.count > 0) exec_list(vm, &s->as.if_stmt.else_branch);
            break;
        }

        case STMT_REPEAT: {
            int n = (int)val_as_num(eval_expr(vm, s->as.repeat_stmt.count));
            if (n > 20000) n = 20000;
            bool has_iter = s->as.repeat_stmt.iter_var[0] != '\0';
            int saved_locals = vm->local_count;
            for (int i = 0; i < n && !vm->has_error && !vm->stopping; i++) {
                if (has_iter) declare_var(vm, s->as.repeat_stmt.iter_var, make_num((double)i), true);
                exec_list(vm, &s->as.repeat_stmt.body);
            }
            vm->local_count = saved_locals;
            break;
        }

        case STMT_WHILE: {
            int guard = 0;
            while (val_truthy(eval_expr(vm, s->as.while_stmt.cond)) &&
                   !vm->has_error && !vm->stopping && guard++ < 10000) {
                exec_list(vm, &s->as.while_stmt.body);
            }
            break;
        }

        case STMT_STOP:
            vm->stopping = true;
            break;

        case STMT_CMD:
            exec_cmd(vm, s);
            break;

        case STMT_BLOCK_CMD:
            exec_block_cmd(vm, s);
            break;
    }
}

static void exec_list(LogoVM *vm, StmtList *list) {
    for (int i = 0; i < list->count && !vm->has_error && !vm->stopping; i++) {
        exec_stmt(vm, list->items[i]);
    }
}

/* ============================================================================
 *  Public VM Lifecycle
 * ============================================================================ */

void logo_vm_init(LogoVM *vm, LogoProgram *prog) {
    memset(vm, 0, sizeof(*vm));
    vm->prog = prog;
    vm->rng_state = 0x12345678u;
    logo_scene_defaults(&vm->base_scene);
    vm->scene = vm->base_scene;
    turtle_reset(&vm->turtle);

    if (!prog) return;
    for (int i = 0; i < prog->top_level.count; i++) {
        Stmt *s = prog->top_level.items[i];
        if (s->kind == STMT_PROC && vm->proc_count < LOGO_MAX_PROCS) {
            vm->procs[vm->proc_count++] = s;
        } else if (s->kind == STMT_ON_FRAME) {
            vm->on_frame = s;
        }
    }
}

void logo_vm_run_init(LogoVM *vm, LogoHostInput input) {
    if (!vm->prog || vm->prog->has_error) return;
    vm->input = input;
    vm->time = 0.0;
    vm->dt = 0.016;
    logo_scene_defaults(&vm->scene);
    turtle_reset(&vm->turtle);
    vm->turtle_sp = 0;
    vm->call_depth = 0;
    vm->local_count = 0;
    vm->stopping = false;

    exec_list(vm, &vm->prog->top_level);
    vm->base_scene = vm->scene;
}

void logo_vm_run_frame(LogoVM *vm, double time, double dt, LogoHostInput input) {
    if (!vm->prog || vm->prog->has_error || vm->has_error) return;
    vm->time = time;
    vm->dt = dt;
    vm->input = input;

    /* Reset per-frame scene primitives while keeping init-configured mode/FX/sky/light */
    vm->scene = vm->base_scene;
    if (vm->on_frame) {
        vm->scene.prim2d_count = 0;
        vm->scene.prim3d_count = 0;
        turtle_reset(&vm->turtle);
        vm->turtle_sp = 0;
        vm->call_depth = 0;
        vm->local_count = 0;
        vm->stopping = false;

        vm->frame_base[vm->call_depth++] = 0;
        declare_var(vm, vm->on_frame->as.on_frame_stmt.dt_param, make_num(dt), true);
        exec_list(vm, &vm->on_frame->as.on_frame_stmt.body);
        vm->call_depth--;
        vm->local_count = 0;
    }
}
