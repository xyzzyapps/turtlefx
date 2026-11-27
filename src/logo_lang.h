#ifndef LOGO_LANG_H
#define LOGO_LANG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 *  TurtleFX — Modern Logo Dialect + Shader-Backed Scene IR
 * ============================================================================ */

#define LOGO_MAX_IDENT      64
#define LOGO_MAX_ERR        256
#define LOGO_MAX_ARGS       16
#define LOGO_MAX_LIST       64
#define LOGO_MAX_PARAMS     12
#define LOGO_MAX_VARS       256
#define LOGO_MAX_PROCS      64
#define LOGO_MAX_STACK      64
#define LOGO_MAX_TURTLE_STK 64
#define LOGO_MAX_FILL_PTS   128

#define LOGO_MAX_ACTOR_DEFS   32
#define LOGO_MAX_ACTOR_FIELDS 12
#define LOGO_MAX_ACTORS_LIVE  256
#define LOGO_MAX_MSG_ARGS     6
#define LOGO_MAX_MESSAGES     256

#define SCENE_MAX_2D_PRIMS  1024
#define SCENE_MAX_3D_PRIMS  256
#define SCENE_MAX_SPRITES   128
#define SCENE_MAX_MODELS    64

/* ----------------------------------------------------------------------------
 *  Rendering Modes, Palettes, Primitives, FX
 * ---------------------------------------------------------------------------- */

typedef enum {
    RENDER_MODE_2D = 0,
    RENDER_MODE_RETRO = 1,
    RENDER_MODE_3D = 2
} RenderMode;

typedef enum {
    PALETTE_NONE = 0,
    PALETTE_PICO8 = 1,
    PALETTE_GAMEBOY = 2,
    PALETTE_CGA = 3,
    PALETTE_SUNSET = 4
} PaletteKind;

typedef enum {
    PRIM2D_SEGMENT = 1,
    PRIM2D_CIRCLE = 2,
    PRIM2D_RECT = 3,
    PRIM2D_TRIANGLE = 4
} Prim2DType;

typedef enum {
    LOGO_BLEND_ALPHA = 0,
    LOGO_BLEND_ADD = 1
} LogoBlendMode;

typedef enum {
    PRIM3D_SPHERE = 1,
    PRIM3D_CAPSULE = 2,
    PRIM3D_BOX = 3,
    PRIM3D_CYLINDER = 4,
    PRIM3D_CONE = 5,
    PRIM3D_TORUS = 6
} Prim3DType;

typedef enum {
    MAT3D_MATTE = 0,
    MAT3D_GLOSSY = 1,
    MAT3D_EMISSIVE = 2,
    MAT3D_STONE = 3,
    MAT3D_TILES = 4
} Mat3DType;

typedef enum {
    OP3D_UNION = 0,
    OP3D_SMOOTH_UNION = 1,
    OP3D_CARVE = 2
} Op3DType;

typedef struct {
    int type;       /* Prim2DType */
    float x0, y0;
    float x1, y1;
    float x2, y2;   /* Triangle 3rd vertex, or rect (halfW, halfH) */
    float radius;   /* Segment half-width, circle radius, or rect angle (rad) */
    float r, g, b, a;
    float glow;
    int blend;      /* BlendModeKind */
} Prim2D;

typedef struct {
    int type;       /* Prim3DType */
    float px, py, pz;
    float fx, fy, fz; /* Forward axis (unit) */
    float ux, uy, uz; /* Up axis (unit) */
    float p0, p1, p2; /* Dimensions */
    float r, g, b;
    int mat_type;   /* Mat3DType */
    float mat_param;/* Glossiness or emissive strength */
    float smooth_k;
    int op_type;    /* Op3DType */
} Prim3D;

typedef struct {
    char path[256];
    const char *inline_art; /* non-NULL for inline """...""" ASCII pixel-art sprites */
    float x, y, z;
    float size;
    float rotation;
    int cols, rows, frame;
    bool flip_x;
    float r, g, b, a;
} PrimSprite;

typedef struct {
    char path[256];
    float px, py, pz;
    float fx, fy, fz;
    float ux, uy, uz;
    float scale;
    float r, g, b, a;
    bool wireframe;
} PrimModel;

typedef struct {
    RenderMode mode;
    int retro_w, retro_h;
    bool retro_fx_enabled;
    bool retro_dither;
    PaletteKind palette;

    float clear_r, clear_g, clear_b;
    float sky_top[3];
    float sky_bot[3];
    bool sky_gradient;

    /* Post-FX */
    bool crt_enabled;
    float crt_scanlines;
    float crt_curve;

    bool bloom_enabled;
    float bloom_strength;
    float bloom_radius;

    float vignette;

    bool fog_enabled;
    float fog_color[3];
    float fog_near;
    float fog_far;

    /* Optional user-authored GLSL 330 fragment shader (file path OR inline multiline source) */
    bool custom_shader_enabled;
    char custom_shader_path[256];
    const char *custom_shader_code;
    float custom_params[4];

    /* 3D Camera & Light */
    float cam_pos[3];
    float cam_look[3];
    float cam_fov;

    float light_dir[3];
    float light_color[3];
    bool light_shadows;

    /* Emitted primitives for current frame */
    Prim2D prims2d[SCENE_MAX_2D_PRIMS];
    int prim2d_count;

    Prim3D prims3d[SCENE_MAX_3D_PRIMS];
    int prim3d_count;

    PrimSprite sprites[SCENE_MAX_SPRITES];
    int sprite_count;

    PrimModel models[SCENE_MAX_MODELS];
    int model_count;
} SceneIR;

/* ----------------------------------------------------------------------------
 *  Runtime Values
 * ---------------------------------------------------------------------------- */

typedef enum {
    VAL_NUM = 0,
    VAL_COLOR,
    VAL_VEC3,
    VAL_STR,
    VAL_SYM
} ValType;

typedef struct {
    ValType type;
    double num;
    float vec[LOGO_MAX_LIST]; /* COLOR (rgba), VEC3 (xyz), or numeric list */
    int list_len;
    char str[64];
    const char *long_str;     /* Non-NULL for multiline / long string literals owned by AST */
} LogoVal;

/* ----------------------------------------------------------------------------
 *  AST Types
 * ---------------------------------------------------------------------------- */

typedef enum {
    EXPR_NUM,
    EXPR_COLOR,
    EXPR_STR,
    EXPR_IDENT,
    EXPR_LIST,
    EXPR_INDEX,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_LTE, OP_GT, OP_GTE,
    OP_AND, OP_OR,
    OP_NEG, OP_NOT
} OpKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct {
    char name[LOGO_MAX_IDENT]; /* empty if positional */
    Expr *value;
} CallArg;

struct Expr {
    ExprKind kind;
    int line;
    union {
        double num;
        float color[4];
        struct {
            char small[64];
            char *heap; /* non-NULL for multiline """...""" strings */
        } str;
        char ident[LOGO_MAX_IDENT];
        struct {
            Expr **items;
            int count;
        } list;
        struct {
            char name[LOGO_MAX_IDENT];
            Expr *index;
        } index;
        struct {
            OpKind op;
            Expr *operand;
        } unary;
        struct {
            OpKind op;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            char name[LOGO_MAX_IDENT];
            CallArg *args;
            int arg_count;
        } call;
    } as;
};

typedef struct {
    Stmt **items;
    int count;
    int cap;
} StmtList;

typedef enum {
    STMT_LET,
    STMT_ASSIGN,
    STMT_INDEX_ASSIGN,
    STMT_IF,
    STMT_REPEAT,
    STMT_WHILE,
    STMT_PROC,
    STMT_ON_FRAME,
    STMT_ACTOR_DEF,
    STMT_ON_MSG,
    STMT_SPAWN,
    STMT_DESPAWN,
    STMT_SEND,
    STMT_STOP,
    STMT_CMD,
    STMT_BLOCK_CMD /* branch [...], at x y [...], fill [...], smooth k [...], carve [...] */
} StmtKind;

struct Stmt {
    StmtKind kind;
    int line;
    union {
        struct {
            char name[LOGO_MAX_IDENT];
            Expr *init;
        } let_stmt;
        struct {
            char name[LOGO_MAX_IDENT];
            OpKind compound_op; /* OP_EQ = plain '=', else += -= *= /= */
            Expr *value;
        } assign_stmt;
        struct {
            char name[LOGO_MAX_IDENT];
            Expr *index;
            OpKind compound_op;
            Expr *value;
        } index_assign_stmt;
        struct {
            Expr *cond;
            StmtList then_branch;
            StmtList else_branch;
        } if_stmt;
        struct {
            Expr *count;
            char iter_var[LOGO_MAX_IDENT]; /* empty if no 'with i' */
            StmtList body;
        } repeat_stmt;
        struct {
            Expr *cond;
            StmtList body;
        } while_stmt;
        struct {
            char name[LOGO_MAX_IDENT];
            char params[LOGO_MAX_PARAMS][LOGO_MAX_IDENT];
            int param_count;
            StmtList body;
        } proc_stmt;
        struct {
            char dt_param[LOGO_MAX_IDENT];
            StmtList body;
        } on_frame_stmt;
        struct {
            char name[LOGO_MAX_IDENT];
            StmtList fields;
            StmtList handlers;
        } actor_def_stmt;
        struct {
            char msg_name[LOGO_MAX_IDENT];
            char params[LOGO_MAX_PARAMS][LOGO_MAX_IDENT];
            int param_count;
            StmtList body;
        } on_msg_stmt;
        struct {
            Expr *count; /* NULL means 1 */
            char actor_name[LOGO_MAX_IDENT];
            bool at_here;
            char iter_var[LOGO_MAX_IDENT];
            StmtList init_body;
        } spawn_stmt;
        struct {
            bool is_broadcast;
            Expr *target; /* handle expr for send, actor type sym for broadcast */
            char msg_name[LOGO_MAX_IDENT];
            CallArg *args;
            int arg_count;
        } send_stmt;
        struct {
            char name[LOGO_MAX_IDENT];
            CallArg *args;
            int arg_count;
        } cmd_stmt;
        struct {
            char name[LOGO_MAX_IDENT];
            CallArg *args;
            int arg_count;
            StmtList body;
        } block_cmd_stmt;
    } as;
};

typedef struct {
    StmtList top_level;
    char error[LOGO_MAX_ERR];
    bool has_error;
} LogoProgram;

/* ----------------------------------------------------------------------------
 *  Host Input Callback Interface (decouples VM from raylib for unit testing)
 * ---------------------------------------------------------------------------- */

typedef struct {
    bool (*is_key_down)(const char *key_name);
    bool (*is_key_pressed)(const char *key_name);
    void (*play_sound)(const char *path, float volume, float pitch);
    float mouse_x;
    float mouse_y;
    int view_w;
    int view_h;
} LogoHostInput;

/* ----------------------------------------------------------------------------
 *  VM State
 * ---------------------------------------------------------------------------- */

typedef struct {
    char name[LOGO_MAX_IDENT];
    LogoVal val;
} LogoVar;

typedef struct {
    /* 2D/3D shared position & color/material */
    float x, y, z;
    float heading; /* 2D heading in degrees (0 = up / +Y in 2D, 0 = up in retro) */

    /* 3D orthonormal frame */
    float fx, fy, fz; /* forward */
    float ux, uy, uz; /* up */
    float rx, ry, rz; /* right */

    bool pen_down;
    float color[4];
    float pen_width;
    float glow;
    int blend;

    int mat_type;
    float mat_param;
    float smooth_k;
    int op_type;
} TurtleState;

typedef struct {
    bool alive;
    bool pending_despawn;
    uint32_t handle;
    int def_index;
    TurtleState turtle;
    LogoVar fields[LOGO_MAX_ACTOR_FIELDS];
    int field_count;
} ActorInstance;

typedef struct {
    bool is_broadcast;
    uint32_t target_handle;
    int target_def;
    char msg_name[LOGO_MAX_IDENT];
    LogoVal args[LOGO_MAX_MSG_ARGS];
    int arg_count;
} ActorMessage;

typedef struct {
    LogoProgram *prog;
    Stmt *procs[LOGO_MAX_PROCS];
    int proc_count;
    Stmt *on_frame;

    /* Actor definitions, live pool, and message mailbox */
    Stmt *actor_defs[LOGO_MAX_ACTOR_DEFS];
    int actor_def_count;
    ActorInstance actors[LOGO_MAX_ACTORS_LIVE];
    uint32_t next_actor_handle;
    int current_actor; /* -1 when running global code */
    int spawner_actor; /* -1 unless inside spawn [...] init block */
    ActorMessage msg_queue[LOGO_MAX_MESSAGES];
    int msg_count;

    /* Global variables (persist across frames!) */
    LogoVar globals[LOGO_MAX_VARS];
    int global_count;

    /* Local variable stack for procedures & repeat iterators */
    LogoVar locals[LOGO_MAX_VARS];
    int local_count;
    int frame_base[LOGO_MAX_STACK];
    int call_depth;
    bool stopping;

    /* Turtle state + stack */
    TurtleState turtle;
    TurtleState turtle_stack[LOGO_MAX_TURTLE_STK];
    int turtle_sp;

    /* Active fill polygon recording */
    bool recording_fill;
    float fill_x[LOGO_MAX_FILL_PTS];
    float fill_y[LOGO_MAX_FILL_PTS];
    int fill_count;

    /* Frame timing */
    double time;
    double dt;
    LogoHostInput input;

    /* Persistent mode/FX settings configured at init */
    SceneIR base_scene;
    SceneIR scene;

    char error[LOGO_MAX_ERR];
    bool has_error;
    uint32_t rng_state;
} LogoVM;

/* Parser API */
LogoProgram *logo_parse(const char *source);
void logo_program_free(LogoProgram *prog);

/* VM API */
void logo_vm_init(LogoVM *vm, LogoProgram *prog);
void logo_vm_run_init(LogoVM *vm, LogoHostInput input);
void logo_vm_run_frame(LogoVM *vm, double time, double dt, LogoHostInput input);

/* Helper */
void logo_scene_defaults(SceneIR *s);

#endif /* LOGO_LANG_H */
