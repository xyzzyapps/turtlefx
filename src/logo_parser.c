#include "logo_lang.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 *  Lexer
 * ============================================================================ */

typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_NUM,
    TOK_COLOR,
    TOK_STR,
    TOK_IDENT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_COLON,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_BANG
} TokKind;

typedef struct {
    TokKind kind;
    int line;
    bool space_before;
    bool attached_right; /* next char is alphanumeric/digit/dot with no space */
    double num;
    float color[4];
    char text[64];
    char *long_str;
} Token;

typedef struct {
    const char *src;
    size_t pos;
    int line;
    Token cur;
    Token peek;
    bool has_peek;
    char error[LOGO_MAX_ERR];
    bool has_error;
} Parser;

static int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void str_copy_lower(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static void parser_error(Parser *p, int line, const char *fmt, ...) {
    if (p->has_error) return;
    p->has_error = true;
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(p->error, sizeof(p->error), "Line %d: %.200s", line, msg);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static Token lex_next(Parser *p) {
    Token t;
    memset(&t, 0, sizeof(t));
    bool saw_space = false;

    while (p->src[p->pos]) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            saw_space = true;
            p->pos++;
            continue;
        }
        /* Comments: ; ... or // ... */
        if (c == ';' || (c == '/' && p->src[p->pos + 1] == '/')) {
            while (p->src[p->pos] && p->src[p->pos] != '\n') p->pos++;
            continue;
        }
        break;
    }

    t.line = p->line;
    t.space_before = saw_space;

    char c = p->src[p->pos];
    if (!c) {
        t.kind = TOK_EOF;
        return t;
    }

    if (c == '\n') {
        p->pos++;
        p->line++;
        t.kind = TOK_NEWLINE;
        return t;
    }

    /* Hex color: #RGB, #RRGGBB, #RRGGBBAA */
    if (c == '#') {
        p->pos++;
        size_t start = p->pos;
        while (hex_val(p->src[p->pos]) >= 0) p->pos++;
        size_t len = p->pos - start;
        t.kind = TOK_COLOR;
        t.color[3] = 1.0f;
        if (len == 3) {
            int r = hex_val(p->src[start]);
            int g = hex_val(p->src[start + 1]);
            int b = hex_val(p->src[start + 2]);
            t.color[0] = (r * 17) / 255.0f;
            t.color[1] = (g * 17) / 255.0f;
            t.color[2] = (b * 17) / 255.0f;
        } else if (len == 6 || len == 8) {
            int r = (hex_val(p->src[start]) << 4) | hex_val(p->src[start + 1]);
            int g = (hex_val(p->src[start + 2]) << 4) | hex_val(p->src[start + 3]);
            int b = (hex_val(p->src[start + 4]) << 4) | hex_val(p->src[start + 5]);
            t.color[0] = r / 255.0f;
            t.color[1] = g / 255.0f;
            t.color[2] = b / 255.0f;
            if (len == 8) {
                int a = (hex_val(p->src[start + 6]) << 4) | hex_val(p->src[start + 7]);
                t.color[3] = a / 255.0f;
            }
        } else {
            parser_error(p, t.line, "Invalid hex color literal");
        }
        return t;
    }

    /* String literal: single-line "..." or multiline """...""" */
    if (c == '"') {
        if (p->src[p->pos + 1] == '"' && p->src[p->pos + 2] == '"') {
            p->pos += 3;
            /* Strip optional single leading newline so """\n#version 330 starts cleanly at line 1 */
            if (p->src[p->pos] == '\r' && p->src[p->pos + 1] == '\n') { p->pos += 2; p->line++; }
            else if (p->src[p->pos] == '\n') { p->pos++; p->line++; }

            size_t start = p->pos;
            while (p->src[p->pos]) {
                if (p->src[p->pos] == '"' && p->src[p->pos + 1] == '"' && p->src[p->pos + 2] == '"') {
                    break;
                }
                if (p->src[p->pos] == '\n') p->line++;
                p->pos++;
            }
            size_t raw_len = p->pos - start;
            if (p->src[p->pos] == '"' && p->src[p->pos + 1] == '"' && p->src[p->pos + 2] == '"') {
                p->pos += 3;
            } else {
                parser_error(p, t.line, "Unterminated multiline string literal (expected \"\"\")");
            }

            char *buf = (char *)malloc(raw_len + 1);
            size_t out = 0;
            for (size_t i = 0; i < raw_len; i++) {
                if (p->src[start + i] != '\r') buf[out++] = p->src[start + i];
            }
            buf[out] = '\0';
            t.long_str = buf;
            snprintf(t.text, sizeof(t.text), "%s", buf);
            t.kind = TOK_STR;
            return t;
        }

        p->pos++;
        size_t out = 0;
        while (p->src[p->pos] && p->src[p->pos] != '"' && p->src[p->pos] != '\n') {
            if (out + 1 < sizeof(t.text)) t.text[out++] = p->src[p->pos];
            p->pos++;
        }
        if (p->src[p->pos] == '"') p->pos++;
        else parser_error(p, t.line, "Unterminated string literal");
        t.text[out] = '\0';
        t.kind = TOK_STR;
        return t;
    }

    /* Special dimension identifiers like 2d / 3d */
    if ((c == '2' || c == '3') &&
        (p->src[p->pos + 1] == 'd' || p->src[p->pos + 1] == 'D') &&
        !isalnum((unsigned char)p->src[p->pos + 2])) {
        t.kind = TOK_IDENT;
        t.text[0] = c;
        t.text[1] = 'd';
        t.text[2] = '\0';
        p->pos += 2;
        return t;
    }

    /* Number */
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p->src[p->pos + 1]))) {
        char *endp = NULL;
        t.num = strtod(p->src + p->pos, &endp);
        p->pos = (size_t)(endp - p->src);
        t.kind = TOK_NUM;
        return t;
    }

    /* Identifier */
    if (isalpha((unsigned char)c) || c == '_') {
        size_t out = 0;
        while (isalnum((unsigned char)p->src[p->pos]) || p->src[p->pos] == '_') {
            if (out + 1 < sizeof(t.text)) {
                t.text[out++] = (char)tolower((unsigned char)p->src[p->pos]);
            }
            p->pos++;
        }
        t.text[out] = '\0';
        t.kind = TOK_IDENT;
        return t;
    }

    /* Two-char operators */
    char n = p->src[p->pos + 1];
    if (c == '=' && n == '=') { p->pos += 2; t.kind = TOK_EQ; return t; }
    if (c == '!' && n == '=') { p->pos += 2; t.kind = TOK_NEQ; return t; }
    if (c == '<' && n == '=') { p->pos += 2; t.kind = TOK_LTE; return t; }
    if (c == '>' && n == '=') { p->pos += 2; t.kind = TOK_GTE; return t; }
    if (c == '+' && n == '=') { p->pos += 2; t.kind = TOK_PLUS_EQ; return t; }
    if (c == '-' && n == '=') { p->pos += 2; t.kind = TOK_MINUS_EQ; return t; }
    if (c == '*' && n == '=') { p->pos += 2; t.kind = TOK_STAR_EQ; return t; }
    if (c == '/' && n == '=') { p->pos += 2; t.kind = TOK_SLASH_EQ; return t; }

    p->pos++;
    t.attached_right = (isdigit((unsigned char)n) || n == '.');
    switch (c) {
        case '(': t.kind = TOK_LPAREN; break;
        case ')': t.kind = TOK_RPAREN; break;
        case '[': t.kind = TOK_LBRACKET; break;
        case ']': t.kind = TOK_RBRACKET; break;
        case ',': t.kind = TOK_COMMA; break;
        case ':': t.kind = TOK_COLON; break;
        case '+': t.kind = TOK_PLUS; break;
        case '-': t.kind = TOK_MINUS; break;
        case '*': t.kind = TOK_STAR; break;
        case '/': t.kind = TOK_SLASH; break;
        case '%': t.kind = TOK_PERCENT; break;
        case '=': t.kind = TOK_ASSIGN; break;
        case '<': t.kind = TOK_LT; break;
        case '>': t.kind = TOK_GT; break;
        case '!': t.kind = TOK_BANG; break;
        default:
            parser_error(p, t.line, "Unexpected character '%c'", c);
            t.kind = TOK_EOF;
            break;
    }
    return t;
}

static void advance(Parser *p) {
    if (p->has_peek) {
        p->cur = p->peek;
        p->has_peek = false;
    } else {
        p->cur = lex_next(p);
    }
}

static Token peek_tok(Parser *p) {
    if (!p->has_peek) {
        p->peek = lex_next(p);
        p->has_peek = true;
    }
    return p->peek;
}

static void skip_newlines(Parser *p) {
    while (p->cur.kind == TOK_NEWLINE) advance(p);
}

static bool match_ident(Parser *p, const char *kw) {
    return p->cur.kind == TOK_IDENT && str_eq_ci(p->cur.text, kw);
}

/* ============================================================================
 *  AST Allocation & Cleanup
 * ============================================================================ */

static Expr *new_expr(ExprKind kind, int line) {
    Expr *e = (Expr *)calloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

static Stmt *new_stmt(StmtKind kind, int line) {
    Stmt *s = (Stmt *)calloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}

static void stmt_list_push(StmtList *list, Stmt *s) {
    if (!s) return;
    if (list->count >= list->cap) {
        int ncap = list->cap ? list->cap * 2 : 8;
        list->items = (Stmt **)realloc(list->items, (size_t)ncap * sizeof(Stmt *));
        list->cap = ncap;
    }
    list->items[list->count++] = s;
}

static void expr_free(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_STR:
            free(e->as.str.heap);
            break;
        case EXPR_LIST:
            for (int i = 0; i < e->as.list.count; i++) expr_free(e->as.list.items[i]);
            free(e->as.list.items);
            break;
        case EXPR_INDEX:
            expr_free(e->as.index.index);
            break;
        case EXPR_UNARY:
            expr_free(e->as.unary.operand);
            break;
        case EXPR_BINARY:
            expr_free(e->as.binary.left);
            expr_free(e->as.binary.right);
            break;
        case EXPR_CALL:
            for (int i = 0; i < e->as.call.arg_count; i++) expr_free(e->as.call.args[i].value);
            free(e->as.call.args);
            break;
        default:
            break;
    }
    free(e);
}

static void stmt_list_free(StmtList *list);

static void stmt_free(Stmt *s) {
    if (!s) return;
    switch (s->kind) {
        case STMT_LET:
            expr_free(s->as.let_stmt.init);
            break;
        case STMT_ASSIGN:
            expr_free(s->as.assign_stmt.value);
            break;
        case STMT_INDEX_ASSIGN:
            expr_free(s->as.index_assign_stmt.index);
            expr_free(s->as.index_assign_stmt.value);
            break;
        case STMT_IF:
            expr_free(s->as.if_stmt.cond);
            stmt_list_free(&s->as.if_stmt.then_branch);
            stmt_list_free(&s->as.if_stmt.else_branch);
            break;
        case STMT_REPEAT:
            expr_free(s->as.repeat_stmt.count);
            stmt_list_free(&s->as.repeat_stmt.body);
            break;
        case STMT_WHILE:
            expr_free(s->as.while_stmt.cond);
            stmt_list_free(&s->as.while_stmt.body);
            break;
        case STMT_PROC:
            stmt_list_free(&s->as.proc_stmt.body);
            break;
        case STMT_ON_FRAME:
            stmt_list_free(&s->as.on_frame_stmt.body);
            break;
        case STMT_ACTOR_DEF:
            stmt_list_free(&s->as.actor_def_stmt.fields);
            stmt_list_free(&s->as.actor_def_stmt.handlers);
            break;
        case STMT_ON_MSG:
            stmt_list_free(&s->as.on_msg_stmt.body);
            break;
        case STMT_SPAWN:
            expr_free(s->as.spawn_stmt.count);
            stmt_list_free(&s->as.spawn_stmt.init_body);
            break;
        case STMT_SEND:
            expr_free(s->as.send_stmt.target);
            for (int i = 0; i < s->as.send_stmt.arg_count; i++) expr_free(s->as.send_stmt.args[i].value);
            free(s->as.send_stmt.args);
            break;
        case STMT_CMD:
            for (int i = 0; i < s->as.cmd_stmt.arg_count; i++) expr_free(s->as.cmd_stmt.args[i].value);
            free(s->as.cmd_stmt.args);
            break;
        case STMT_BLOCK_CMD:
            for (int i = 0; i < s->as.block_cmd_stmt.arg_count; i++) expr_free(s->as.block_cmd_stmt.args[i].value);
            free(s->as.block_cmd_stmt.args);
            stmt_list_free(&s->as.block_cmd_stmt.body);
            break;
        default:
            break;
    }
    free(s);
}

static void stmt_list_free(StmtList *list) {
    for (int i = 0; i < list->count; i++) stmt_free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void logo_program_free(LogoProgram *prog) {
    if (!prog) return;
    stmt_list_free(&prog->top_level);
    free(prog);
}

/* ============================================================================
 *  Expression Parser
 * ============================================================================ */

static Expr *parse_expr(Parser *p, bool stop_on_neg_arg);

static void parse_call_args(Parser *p, CallArg **out_args, int *out_count) {
    CallArg tmp[LOGO_MAX_ARGS];
    int count = 0;
    skip_newlines(p);
    while (p->cur.kind != TOK_RPAREN && p->cur.kind != TOK_EOF && !p->has_error) {
        if (count >= LOGO_MAX_ARGS) {
            parser_error(p, p->cur.line, "Too many arguments (max %d)", LOGO_MAX_ARGS);
            break;
        }
        memset(&tmp[count], 0, sizeof(CallArg));
        if (p->cur.kind == TOK_IDENT && peek_tok(p).kind == TOK_COLON) {
            str_copy_lower(tmp[count].name, sizeof(tmp[count].name), p->cur.text);
            advance(p); /* ident */
            advance(p); /* ':' */
        }
        skip_newlines(p);
        tmp[count].value = parse_expr(p, false);
        count++;
        skip_newlines(p);
        if (p->cur.kind == TOK_COMMA) {
            advance(p);
            skip_newlines(p);
        } else {
            break;
        }
    }
    if (p->cur.kind == TOK_RPAREN) advance(p);
    else parser_error(p, p->cur.line, "Expected ')' after arguments");

    *out_count = count;
    if (count > 0) {
        *out_args = (CallArg *)malloc((size_t)count * sizeof(CallArg));
        memcpy(*out_args, tmp, (size_t)count * sizeof(CallArg));
    } else {
        *out_args = NULL;
    }
}

static Expr *parse_primary(Parser *p) {
    Token t = p->cur;
    if (t.kind == TOK_NUM) {
        advance(p);
        Expr *e = new_expr(EXPR_NUM, t.line);
        e->as.num = t.num;
        /* Optional percentage suffix like 100% when not followed by an operand */
        if (p->cur.kind == TOK_PERCENT) {
            Token nxt = peek_tok(p);
            if (nxt.kind == TOK_COMMA || nxt.kind == TOK_RPAREN ||
                nxt.kind == TOK_RBRACKET || nxt.kind == TOK_NEWLINE || nxt.kind == TOK_EOF) {
                advance(p);
            }
        }
        return e;
    }
    if (t.kind == TOK_COLOR) {
        advance(p);
        Expr *e = new_expr(EXPR_COLOR, t.line);
        memcpy(e->as.color, t.color, sizeof(t.color));
        return e;
    }
    if (t.kind == TOK_STR) {
        p->cur.long_str = NULL; /* transfer ownership to Expr */
        advance(p);
        Expr *e = new_expr(EXPR_STR, t.line);
        snprintf(e->as.str.small, sizeof(e->as.str.small), "%s", t.text);
        e->as.str.heap = t.long_str;
        return e;
    }
    if (t.kind == TOK_IDENT) {
        advance(p);
        if (p->cur.kind == TOK_LPAREN && !p->cur.space_before) {
            advance(p);
            Expr *e = new_expr(EXPR_CALL, t.line);
            str_copy_lower(e->as.call.name, sizeof(e->as.call.name), t.text);
            parse_call_args(p, &e->as.call.args, &e->as.call.arg_count);
            return e;
        }
        /* Attached bracket `arr[i]` (no space before '[') is list indexing;
           spaced bracket `ident [ ... ]` remains a Logo command/control block! */
        if (p->cur.kind == TOK_LBRACKET && !p->cur.space_before) {
            advance(p);
            Expr *e = new_expr(EXPR_INDEX, t.line);
            str_copy_lower(e->as.index.name, sizeof(e->as.index.name), t.text);
            e->as.index.index = parse_expr(p, false);
            if (p->cur.kind == TOK_RBRACKET) advance(p);
            else parser_error(p, p->cur.line, "Expected ']' after list index");
            return e;
        }
        Expr *e = new_expr(EXPR_IDENT, t.line);
        str_copy_lower(e->as.ident, sizeof(e->as.ident), t.text);
        return e;
    }
    if (t.kind == TOK_LPAREN) {
        advance(p);
        Expr *e = parse_expr(p, false);
        if (p->cur.kind == TOK_RPAREN) advance(p);
        else parser_error(p, p->cur.line, "Expected ')'");
        return e;
    }
    if (t.kind == TOK_LBRACKET) {
        /* Vector / list literal: [x, y, z, ...] */
        advance(p);
        Expr *e = new_expr(EXPR_LIST, t.line);
        Expr *items[LOGO_MAX_LIST];
        int count = 0;
        skip_newlines(p);
        while (p->cur.kind != TOK_RBRACKET && p->cur.kind != TOK_EOF && !p->has_error) {
            if (count < LOGO_MAX_LIST) {
                items[count++] = parse_expr(p, false);
            } else {
                expr_free(parse_expr(p, false));
            }
            skip_newlines(p);
            if (p->cur.kind == TOK_COMMA) {
                advance(p);
                skip_newlines(p);
            } else {
                break;
            }
        }
        if (p->cur.kind == TOK_RBRACKET) advance(p);
        else parser_error(p, p->cur.line, "Expected ']' in list literal");
        if (count > 0) {
            e->as.list.items = (Expr **)malloc((size_t)count * sizeof(Expr *));
            memcpy(e->as.list.items, items, (size_t)count * sizeof(Expr *));
            e->as.list.count = count;
        }
        return e;
    }

    parser_error(p, t.line, "Unexpected token in expression");
    advance(p);
    return new_expr(EXPR_NUM, t.line);
}

static Expr *parse_unary(Parser *p) {
    if (p->cur.kind == TOK_MINUS) {
        int line = p->cur.line;
        advance(p);
        Expr *e = new_expr(EXPR_UNARY, line);
        e->as.unary.op = OP_NEG;
        e->as.unary.operand = parse_unary(p);
        return e;
    }
    if (p->cur.kind == TOK_BANG || match_ident(p, "not")) {
        int line = p->cur.line;
        advance(p);
        Expr *e = new_expr(EXPR_UNARY, line);
        e->as.unary.op = OP_NOT;
        e->as.unary.operand = parse_unary(p);
        return e;
    }
    return parse_primary(p);
}

static Expr *parse_mul(Parser *p) {
    Expr *left = parse_unary(p);
    while (p->cur.kind == TOK_STAR || p->cur.kind == TOK_SLASH || p->cur.kind == TOK_PERCENT) {
        OpKind op = (p->cur.kind == TOK_STAR) ? OP_MUL :
                    (p->cur.kind == TOK_SLASH) ? OP_DIV : OP_MOD;
        int line = p->cur.line;
        advance(p);
        Expr *right = parse_unary(p);
        Expr *bin = new_expr(EXPR_BINARY, line);
        bin->as.binary.op = op;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

static Expr *parse_add(Parser *p, bool stop_on_neg_arg) {
    Expr *left = parse_mul(p);
    while (p->cur.kind == TOK_PLUS || p->cur.kind == TOK_MINUS) {
        /* Disambiguate `at 0 -4` vs `at 0 - 4` in space-separated bare command args */
        if (stop_on_neg_arg && p->cur.kind == TOK_MINUS &&
            p->cur.space_before && p->cur.attached_right) {
            break;
        }
        OpKind op = (p->cur.kind == TOK_PLUS) ? OP_ADD : OP_SUB;
        int line = p->cur.line;
        advance(p);
        Expr *right = parse_mul(p);
        Expr *bin = new_expr(EXPR_BINARY, line);
        bin->as.binary.op = op;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

static Expr *parse_cmp(Parser *p, bool stop_on_neg_arg) {
    Expr *left = parse_add(p, stop_on_neg_arg);
    while (p->cur.kind == TOK_EQ || p->cur.kind == TOK_NEQ ||
           p->cur.kind == TOK_LT || p->cur.kind == TOK_LTE ||
           p->cur.kind == TOK_GT || p->cur.kind == TOK_GTE) {
        OpKind op = OP_EQ;
        switch (p->cur.kind) {
            case TOK_EQ:  op = OP_EQ; break;
            case TOK_NEQ: op = OP_NEQ; break;
            case TOK_LT:  op = OP_LT; break;
            case TOK_LTE: op = OP_LTE; break;
            case TOK_GT:  op = OP_GT; break;
            case TOK_GTE: op = OP_GTE; break;
            default: break;
        }
        int line = p->cur.line;
        advance(p);
        Expr *right = parse_add(p, stop_on_neg_arg);
        Expr *bin = new_expr(EXPR_BINARY, line);
        bin->as.binary.op = op;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

static Expr *parse_and(Parser *p, bool stop_on_neg_arg) {
    Expr *left = parse_cmp(p, stop_on_neg_arg);
    while (match_ident(p, "and")) {
        int line = p->cur.line;
        advance(p);
        Expr *right = parse_cmp(p, stop_on_neg_arg);
        Expr *bin = new_expr(EXPR_BINARY, line);
        bin->as.binary.op = OP_AND;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

static Expr *parse_expr(Parser *p, bool stop_on_neg_arg) {
    Expr *left = parse_and(p, stop_on_neg_arg);
    while (match_ident(p, "or")) {
        int line = p->cur.line;
        advance(p);
        Expr *right = parse_and(p, stop_on_neg_arg);
        Expr *bin = new_expr(EXPR_BINARY, line);
        bin->as.binary.op = OP_OR;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

/* ============================================================================
 *  Statement Parser
 * ============================================================================ */

static Stmt *parse_stmt(Parser *p);

static void parse_bracket_block(Parser *p, StmtList *out) {
    skip_newlines(p);
    if (p->cur.kind != TOK_LBRACKET) {
        parser_error(p, p->cur.line, "Expected '[' to start block");
        return;
    }
    advance(p);
    while (p->cur.kind != TOK_RBRACKET && p->cur.kind != TOK_EOF && !p->has_error) {
        skip_newlines(p);
        if (p->cur.kind == TOK_RBRACKET || p->cur.kind == TOK_EOF) break;
        stmt_list_push(out, parse_stmt(p));
    }
    if (p->cur.kind == TOK_RBRACKET) advance(p);
    else parser_error(p, p->cur.line, "Expected ']' to close block");
}

static void parse_end_block(Parser *p, StmtList *out) {
    while (p->cur.kind != TOK_EOF && !p->has_error) {
        skip_newlines(p);
        if (p->cur.kind == TOK_EOF) {
            parser_error(p, p->cur.line, "Expected 'end'");
            break;
        }
        if (match_ident(p, "end")) {
            advance(p);
            break;
        }
        stmt_list_push(out, parse_stmt(p));
    }
}

static bool is_block_cmd(const char *name) {
    return str_eq_ci(name, "branch") || str_eq_ci(name, "save") ||
           str_eq_ci(name, "at") || str_eq_ci(name, "offset") ||
           str_eq_ci(name, "fill") || str_eq_ci(name, "smooth") ||
           str_eq_ci(name, "carve");
}

/* Bare-command arity (allows chaining multiple commands on a single line: `pu fd 10 rt 90 pd`) */
static int bare_command_arity(const char *name) {
    if (str_eq_ci(name, "pu") || str_eq_ci(name, "penup") ||
        str_eq_ci(name, "pd") || str_eq_ci(name, "pendown") ||
        str_eq_ci(name, "home")) return 0;

    if (str_eq_ci(name, "fd") || str_eq_ci(name, "forward") ||
        str_eq_ci(name, "bk") || str_eq_ci(name, "back") ||
        str_eq_ci(name, "rt") || str_eq_ci(name, "right") ||
        str_eq_ci(name, "lt") || str_eq_ci(name, "left") ||
        str_eq_ci(name, "pitch") || str_eq_ci(name, "up") ||
        str_eq_ci(name, "down") || str_eq_ci(name, "yaw") ||
        str_eq_ci(name, "roll") || str_eq_ci(name, "color") ||
        str_eq_ci(name, "pen_width") || str_eq_ci(name, "pw") ||
        str_eq_ci(name, "glow") || str_eq_ci(name, "blend") ||
        str_eq_ci(name, "clear") || str_eq_ci(name, "palette") ||
        str_eq_ci(name, "dot") || str_eq_ci(name, "circle") ||
        str_eq_ci(name, "sphere") || str_eq_ci(name, "mode") ||
        str_eq_ci(name, "sky") || str_eq_ci(name, "material") ||
        str_eq_ci(name, "light") || str_eq_ci(name, "fx") ||
        str_eq_ci(name, "play") || str_eq_ci(name, "look_at")) return 1;

    if (str_eq_ci(name, "rect") || str_eq_ci(name, "capsule") ||
        str_eq_ci(name, "cylinder") || str_eq_ci(name, "cone") ||
        str_eq_ci(name, "torus")) return 2;

    if (str_eq_ci(name, "box")) return 3;

    return 0;
}

static Stmt *parse_stmt(Parser *p) {
    int line = p->cur.line;

    /* let x = expr */
    if (match_ident(p, "let")) {
        advance(p);
        if (p->cur.kind != TOK_IDENT) {
            parser_error(p, line, "Expected variable name after 'let'");
            return NULL;
        }
        Stmt *s = new_stmt(STMT_LET, line);
        str_copy_lower(s->as.let_stmt.name, sizeof(s->as.let_stmt.name), p->cur.text);
        advance(p);
        if (p->cur.kind == TOK_ASSIGN) advance(p);
        else parser_error(p, p->cur.line, "Expected '=' in let declaration");
        s->as.let_stmt.init = parse_expr(p, false);
        return s;
    }

    /* if cond [ ... ] (else [ ... ])? */
    if (match_ident(p, "if")) {
        advance(p);
        Stmt *s = new_stmt(STMT_IF, line);
        s->as.if_stmt.cond = parse_expr(p, false);
        parse_bracket_block(p, &s->as.if_stmt.then_branch);
        /* Allow optional newline before `else` */
        while (p->cur.kind == TOK_NEWLINE && peek_tok(p).kind == TOK_IDENT && str_eq_ci(peek_tok(p).text, "else")) {
            advance(p);
        }
        if (match_ident(p, "else")) {
            advance(p);
            if (match_ident(p, "if")) {
                stmt_list_push(&s->as.if_stmt.else_branch, parse_stmt(p));
            } else {
                parse_bracket_block(p, &s->as.if_stmt.else_branch);
            }
        }
        return s;
    }

    /* repeat count (with i)? [ ... ] */
    if (match_ident(p, "repeat")) {
        advance(p);
        Stmt *s = new_stmt(STMT_REPEAT, line);
        s->as.repeat_stmt.count = parse_expr(p, false);
        if (match_ident(p, "with")) {
            advance(p);
            if (p->cur.kind == TOK_IDENT) {
                str_copy_lower(s->as.repeat_stmt.iter_var, sizeof(s->as.repeat_stmt.iter_var), p->cur.text);
                advance(p);
            } else {
                parser_error(p, p->cur.line, "Expected iterator variable after 'with'");
            }
        }
        parse_bracket_block(p, &s->as.repeat_stmt.body);
        return s;
    }

    /* while cond [ ... ] */
    if (match_ident(p, "while")) {
        advance(p);
        Stmt *s = new_stmt(STMT_WHILE, line);
        s->as.while_stmt.cond = parse_expr(p, false);
        parse_bracket_block(p, &s->as.while_stmt.body);
        return s;
    }

    /* to name(p1, p2) ... end */
    if (match_ident(p, "to")) {
        advance(p);
        if (p->cur.kind != TOK_IDENT) {
            parser_error(p, line, "Expected procedure name after 'to'");
            return NULL;
        }
        Stmt *s = new_stmt(STMT_PROC, line);
        str_copy_lower(s->as.proc_stmt.name, sizeof(s->as.proc_stmt.name), p->cur.text);
        advance(p);
        if (p->cur.kind == TOK_LPAREN) {
            advance(p);
            while (p->cur.kind != TOK_RPAREN && p->cur.kind != TOK_EOF) {
                if (p->cur.kind == TOK_IDENT && s->as.proc_stmt.param_count < LOGO_MAX_PARAMS) {
                    str_copy_lower(s->as.proc_stmt.params[s->as.proc_stmt.param_count++],
                                   LOGO_MAX_IDENT, p->cur.text);
                    advance(p);
                } else {
                    parser_error(p, p->cur.line, "Expected parameter name");
                    break;
                }
                if (p->cur.kind == TOK_COMMA) advance(p);
                else break;
            }
            if (p->cur.kind == TOK_RPAREN) advance(p);
            else parser_error(p, p->cur.line, "Expected ')' after parameters");
        }
        parse_end_block(p, &s->as.proc_stmt.body);
        return s;
    }

    /* on frame(dt) ... end  (top-level game loop) */
    if (match_ident(p, "on")) {
        advance(p);
        if (!match_ident(p, "frame")) {
            parser_error(p, line, "Expected 'frame' after 'on' at top level");
            return NULL;
        }
        advance(p);
        Stmt *s = new_stmt(STMT_ON_FRAME, line);
        strcpy(s->as.on_frame_stmt.dt_param, "dt");
        if (p->cur.kind == TOK_LPAREN) {
            advance(p);
            if (p->cur.kind == TOK_IDENT) {
                str_copy_lower(s->as.on_frame_stmt.dt_param, sizeof(s->as.on_frame_stmt.dt_param), p->cur.text);
                advance(p);
            }
            if (p->cur.kind == TOK_RPAREN) advance(p);
            else parser_error(p, p->cur.line, "Expected ')' after on frame parameter");
        }
        parse_end_block(p, &s->as.on_frame_stmt.body);
        return s;
    }

    /* actor <name> ... end */
    if (match_ident(p, "actor")) {
        advance(p);
        if (p->cur.kind != TOK_IDENT) {
            parser_error(p, line, "Expected actor name after 'actor'");
            return NULL;
        }
        Stmt *s = new_stmt(STMT_ACTOR_DEF, line);
        str_copy_lower(s->as.actor_def_stmt.name, sizeof(s->as.actor_def_stmt.name), p->cur.text);
        advance(p);

        while (p->cur.kind != TOK_EOF && !p->has_error) {
            skip_newlines(p);
            if (p->cur.kind == TOK_EOF) {
                parser_error(p, p->cur.line, "Expected 'end' to close actor '%s'", s->as.actor_def_stmt.name);
                break;
            }
            if (match_ident(p, "end")) {
                advance(p);
                break;
            }
            if (match_ident(p, "let")) {
                stmt_list_push(&s->as.actor_def_stmt.fields, parse_stmt(p));
            } else if (match_ident(p, "on")) {
                int hline = p->cur.line;
                advance(p);
                if (p->cur.kind != TOK_IDENT) {
                    parser_error(p, hline, "Expected handler name after 'on'");
                    break;
                }
                Stmt *h = new_stmt(STMT_ON_MSG, hline);
                str_copy_lower(h->as.on_msg_stmt.msg_name, sizeof(h->as.on_msg_stmt.msg_name), p->cur.text);
                advance(p);
                if (p->cur.kind == TOK_LPAREN) {
                    advance(p);
                    while (p->cur.kind != TOK_RPAREN && p->cur.kind != TOK_EOF) {
                        if (p->cur.kind == TOK_IDENT && h->as.on_msg_stmt.param_count < LOGO_MAX_PARAMS) {
                            str_copy_lower(h->as.on_msg_stmt.params[h->as.on_msg_stmt.param_count++],
                                           LOGO_MAX_IDENT, p->cur.text);
                            advance(p);
                        } else {
                            parser_error(p, p->cur.line, "Expected parameter name in 'on %s'", h->as.on_msg_stmt.msg_name);
                            break;
                        }
                        if (p->cur.kind == TOK_COMMA) advance(p);
                        else break;
                    }
                    if (p->cur.kind == TOK_RPAREN) advance(p);
                    else parser_error(p, p->cur.line, "Expected ')' after handler parameters");
                }
                parse_end_block(p, &h->as.on_msg_stmt.body);
                stmt_list_push(&s->as.actor_def_stmt.handlers, h);
            } else {
                parser_error(p, p->cur.line, "Expected 'let', 'on', or 'end' inside actor '%s'", s->as.actor_def_stmt.name);
                advance(p);
            }
        }
        return s;
    }

    /* spawn [count] <actor> [here] [with i] [ [ ... ] ] */
    if (match_ident(p, "spawn")) {
        advance(p);
        Stmt *s = new_stmt(STMT_SPAWN, line);

        /* Determine if an explicit count precedes the actor name */
        bool has_count = false;
        if (p->cur.kind != TOK_IDENT) {
            has_count = true;
        } else {
            Token nxt = peek_tok(p);
            bool next_starts_tail = (nxt.kind == TOK_LBRACKET || nxt.kind == TOK_NEWLINE || nxt.kind == TOK_EOF ||
                                     (nxt.kind == TOK_IDENT && (str_eq_ci(nxt.text, "here") || str_eq_ci(nxt.text, "with"))));
            if (!next_starts_tail) has_count = true;
        }
        if (has_count) {
            s->as.spawn_stmt.count = parse_expr(p, true);
        }

        if (p->cur.kind != TOK_IDENT) {
            parser_error(p, p->cur.line, "Expected actor type name in spawn");
            return s;
        }
        str_copy_lower(s->as.spawn_stmt.actor_name, sizeof(s->as.spawn_stmt.actor_name), p->cur.text);
        advance(p);

        /* Optional `here` and/or `with i` in either order */
        for (int k = 0; k < 2; k++) {
            if (match_ident(p, "here")) {
                s->as.spawn_stmt.at_here = true;
                advance(p);
            } else if (match_ident(p, "with")) {
                advance(p);
                if (p->cur.kind == TOK_IDENT) {
                    str_copy_lower(s->as.spawn_stmt.iter_var, sizeof(s->as.spawn_stmt.iter_var), p->cur.text);
                    advance(p);
                } else {
                    parser_error(p, p->cur.line, "Expected iterator name after 'with'");
                }
            }
        }

        if (p->cur.kind == TOK_LBRACKET) {
            parse_bracket_block(p, &s->as.spawn_stmt.init_body);
        }
        return s;
    }

    /* despawn */
    if (match_ident(p, "despawn")) {
        advance(p);
        return new_stmt(STMT_DESPAWN, line);
    }

    /* send <target> <msg>(args...)  |  broadcast <actor> <msg>(args...) */
    if (match_ident(p, "send") || match_ident(p, "broadcast")) {
        bool is_bcast = match_ident(p, "broadcast");
        advance(p);
        Stmt *s = new_stmt(STMT_SEND, line);
        s->as.send_stmt.is_broadcast = is_bcast;
        s->as.send_stmt.target = parse_expr(p, true);
        if (p->cur.kind != TOK_IDENT) {
            parser_error(p, p->cur.line, "Expected message name in %s", is_bcast ? "broadcast" : "send");
            return s;
        }
        str_copy_lower(s->as.send_stmt.msg_name, sizeof(s->as.send_stmt.msg_name), p->cur.text);
        advance(p);
        if (p->cur.kind == TOK_LPAREN && !p->cur.space_before) {
            advance(p);
            parse_call_args(p, &s->as.send_stmt.args, &s->as.send_stmt.arg_count);
        }
        return s;
    }

    /* stop / return */
    if (match_ident(p, "stop") || match_ident(p, "return")) {
        advance(p);
        return new_stmt(STMT_STOP, line);
    }

    if (p->cur.kind != TOK_IDENT) {
        parser_error(p, line, "Expected statement or command");
        advance(p);
        return NULL;
    }

    char name[LOGO_MAX_IDENT];
    str_copy_lower(name, sizeof(name), p->cur.text);
    advance(p);

    /* Indexed assignment: arr[i] = expr, arr[i] += expr, etc. */
    if (!is_block_cmd(name) && p->cur.kind == TOK_LBRACKET && !p->cur.space_before) {
        advance(p);
        Stmt *s = new_stmt(STMT_INDEX_ASSIGN, line);
        strcpy(s->as.index_assign_stmt.name, name);
        s->as.index_assign_stmt.index = parse_expr(p, false);
        if (p->cur.kind == TOK_RBRACKET) advance(p);
        else parser_error(p, p->cur.line, "Expected ']' in indexed assignment");

        switch (p->cur.kind) {
            case TOK_ASSIGN:   s->as.index_assign_stmt.compound_op = OP_EQ; advance(p); break;
            case TOK_PLUS_EQ:  s->as.index_assign_stmt.compound_op = OP_ADD; advance(p); break;
            case TOK_MINUS_EQ: s->as.index_assign_stmt.compound_op = OP_SUB; advance(p); break;
            case TOK_STAR_EQ:  s->as.index_assign_stmt.compound_op = OP_MUL; advance(p); break;
            case TOK_SLASH_EQ: s->as.index_assign_stmt.compound_op = OP_DIV; advance(p); break;
            default:
                parser_error(p, p->cur.line, "Expected '=', '+=', '-=', '*=', or '/=' after list index");
                break;
        }
        s->as.index_assign_stmt.value = parse_expr(p, false);
        return s;
    }

    /* Assignment: x = expr, x += expr, etc. */
    if (p->cur.kind == TOK_ASSIGN || p->cur.kind == TOK_PLUS_EQ ||
        p->cur.kind == TOK_MINUS_EQ || p->cur.kind == TOK_STAR_EQ ||
        p->cur.kind == TOK_SLASH_EQ) {
        Stmt *s = new_stmt(STMT_ASSIGN, line);
        strcpy(s->as.assign_stmt.name, name);
        switch (p->cur.kind) {
            case TOK_ASSIGN:   s->as.assign_stmt.compound_op = OP_EQ; break;
            case TOK_PLUS_EQ:  s->as.assign_stmt.compound_op = OP_ADD; break;
            case TOK_MINUS_EQ: s->as.assign_stmt.compound_op = OP_SUB; break;
            case TOK_STAR_EQ:  s->as.assign_stmt.compound_op = OP_MUL; break;
            case TOK_SLASH_EQ: s->as.assign_stmt.compound_op = OP_DIV; break;
            default: break;
        }
        advance(p);
        s->as.assign_stmt.value = parse_expr(p, false);
        return s;
    }

    /* Scoped block commands: branch [...], at x y [...], fill [...], smooth k [...], carve [...] */
    if (is_block_cmd(name)) {
        Stmt *s = new_stmt(STMT_BLOCK_CMD, line);
        strcpy(s->as.block_cmd_stmt.name, name);
        CallArg tmp[LOGO_MAX_ARGS];
        int count = 0;
        if (p->cur.kind == TOK_LPAREN && !p->cur.space_before) {
            advance(p);
            parse_call_args(p, &s->as.block_cmd_stmt.args, &s->as.block_cmd_stmt.arg_count);
        } else {
            while (p->cur.kind != TOK_LBRACKET && p->cur.kind != TOK_NEWLINE &&
                   p->cur.kind != TOK_EOF && !p->has_error && count < LOGO_MAX_ARGS) {
                memset(&tmp[count], 0, sizeof(CallArg));
                tmp[count].value = parse_expr(p, true);
                count++;
                if (p->cur.kind == TOK_COMMA) advance(p);
            }
            if (count > 0) {
                s->as.block_cmd_stmt.args = (CallArg *)malloc((size_t)count * sizeof(CallArg));
                memcpy(s->as.block_cmd_stmt.args, tmp, (size_t)count * sizeof(CallArg));
                s->as.block_cmd_stmt.arg_count = count;
            }
        }
        parse_bracket_block(p, &s->as.block_cmd_stmt.body);
        return s;
    }

    /* Command or procedure call */
    Stmt *s = new_stmt(STMT_CMD, line);
    strcpy(s->as.cmd_stmt.name, name);
    if (p->cur.kind == TOK_LPAREN && !p->cur.space_before) {
        advance(p);
        parse_call_args(p, &s->as.cmd_stmt.args, &s->as.cmd_stmt.arg_count);
    } else {
        int arity = bare_command_arity(name);
        if (arity > 0) {
            s->as.cmd_stmt.args = (CallArg *)calloc((size_t)arity, sizeof(CallArg));
            s->as.cmd_stmt.arg_count = arity;
            for (int i = 0; i < arity; i++) {
                s->as.cmd_stmt.args[i].value = parse_expr(p, arity > 1);
                if (p->cur.kind == TOK_COMMA) advance(p);
            }
        }
    }
    return s;
}

LogoProgram *logo_parse(const char *source) {
    LogoProgram *prog = (LogoProgram *)calloc(1, sizeof(LogoProgram));
    if (!source) {
        prog->has_error = true;
        strcpy(prog->error, "Empty source");
        return prog;
    }

    Parser p;
    memset(&p, 0, sizeof(p));
    p.src = source;
    p.line = 1;
    advance(&p);

    while (p.cur.kind != TOK_EOF && !p.has_error) {
        skip_newlines(&p);
        if (p.cur.kind == TOK_EOF) break;
        stmt_list_push(&prog->top_level, parse_stmt(&p));
    }

    if (p.cur.long_str) free(p.cur.long_str);
    if (p.has_peek && p.peek.long_str) free(p.peek.long_str);

    if (p.has_error) {
        prog->has_error = true;
        snprintf(prog->error, sizeof(prog->error), "%s", p.error);
    }
    return prog;
}
