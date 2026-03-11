/* POSIX.1b for nanosleep / struct timespec */
#define _POSIX_C_SOURCE 199309L

/*
 * vbsp - WaspSrc Map Editor
 * Build Engine-inspired 2D map editor for WaspSrc BSP v29 / MAP files.
 *
 * Compile : gcc -std=c99 -Wall -Wextra -o vbsp vbsp.c -lX11 -lm
 * Usage   : ./vbsp [file.bsp | file.map]
 *
 * ── Keyboard reference ──────────────────────────────────────────────────────
 *  O          Open file  (terminal prompt)
 *  S          Save map   (auto-names .map beside the loaded file)
 *  Shift+S    Save As    (terminal prompt)
 *  N          New map
 *  B          Brush draw mode  (click-drag to stamp an axis-aligned box)
 *  E          Entity place mode
 *  Esc        Select mode / cancel current draw
 *  Del        Delete selected brush
 *  T          Set current texture  (terminal prompt)
 *  G          Cycle grid size  4→8→16→32→64→128→…
 *  Tab        Toggle grid visibility
 *  WASD / ←↑↓→  Scroll all 2-D views
 *  +  /  -    Zoom all views in / out
 *  Scroll     Zoom the view under the cursor
 *  R          Reset all views to origin
 *  1/2/3/4    Set the type of view-pane 1  (Top / Front / Side / 3-D)
 *  LMB        Select, or draw, or place entity depending on mode
 *  LMB drag   Move selected brush (select mode) / extend new brush (draw mode)
 *  RMB drag   Pan the view under the cursor
 * ────────────────────────────────────────────────────────────────────────────
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define WIN_W           1280
#define WIN_H            800
#define STATUS_H          42
#define MAX_BRUSHES     4096
#define MAX_ENTITIES    1024
#define MAX_PLANES        32  /* planes per brush */
#define MAX_EPAIRS        64  /* key/value pairs per entity */
#define VBSP_PI  3.14159265358979323846f

/* View types */
#define VIEW_TOP    1
#define VIEW_FRONT  2
#define VIEW_SIDE   3
#define VIEW_3D     4

/* Editor modes */
#define MODE_SELECT  0
#define MODE_DRAW    1
#define MODE_ENTITY  2

/* WaspSrc 1 BSP v29 lump indices */
#define BSP_VERSION      29
#define LUMP_ENTITIES     0
#define LUMP_PLANES       1
#define LUMP_TEXTURES     2
#define LUMP_VERTICES     3
#define LUMP_VISIBILITY   4
#define LUMP_NODES        5
#define LUMP_TEXINFO      6
#define LUMP_FACES        7
#define LUMP_LIGHTING     8
#define LUMP_CLIPNODES    9
#define LUMP_LEAVES      10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES       12
#define LUMP_SURFEDGES   13
#define LUMP_MODELS      14
#define NUM_LUMPS        15

/* ═══════════════════════════════════════════════════════════════════════════
 *  Math primitives
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct { float x, y, z; } vec3_t;

static float v3dot(vec3_t a, vec3_t b)
    { return a.x*b.x + a.y*b.y + a.z*b.z; }

static vec3_t v3sub(vec3_t a, vec3_t b)
    { return (vec3_t){ a.x-b.x, a.y-b.y, a.z-b.z }; }

static vec3_t v3cross(vec3_t a, vec3_t b) {
    return (vec3_t){
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

static vec3_t v3norm(vec3_t v) {
    float l = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (l < 1e-7f) return (vec3_t){0,0,1};
    return (vec3_t){ v.x/l, v.y/l, v.z/l };
}

static float fsnap(float v, float g)
    { return g > 0.0f ? roundf(v/g)*g : v; }

static float fget3(vec3_t v, int axis) {
    switch (axis) { case 0: return v.x; case 1: return v.y; default: return v.z; }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  WaspSrc BSP on-disk structures (little-endian, v29)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct { uint32_t offset, length; } bsp_lump_t;
typedef struct { uint32_t version; bsp_lump_t lumps[NUM_LUMPS]; } bsp_header_t;
typedef struct { vec3_t point; } bsp_vertex_t;
typedef struct { uint16_t v[2]; } bsp_edge_t;
typedef struct {
    vec3_t  mins, maxs, origin;
    int32_t headnode[4];
    int32_t vis_leaves;
    int32_t first_face, num_faces;
} bsp_model_t;

/* Loaded BSP geometry (read-only reference layer, shown in blue) */
typedef struct {
    bsp_vertex_t *vertices;  int num_vertices;
    bsp_edge_t   *edges;     int num_edges;
    bsp_model_t  *models;    int num_models;
    char         *ent_str;
} bsp_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Editor data model
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    vec3_t normal;
    float  dist;
    char   tex[64];
    /* texture axes (WaspSrc valve220 / standard) */
    vec3_t ua, va;
    float  uofs, vofs, uscale, vscale;
} plane_t;

typedef struct {
    plane_t planes[MAX_PLANES];
    int     num_planes;
    int     selected;
} brush_t;

typedef struct { char key[256], value[256]; } epair_t;

typedef struct {
    epair_t pairs[MAX_EPAIRS];
    int     num_pairs;
    int     brush_start, brush_count;
    int     selected;
} entity_t;

typedef struct {
    brush_t  brushes[MAX_BRUSHES];   int num_brushes;
    entity_t entities[MAX_ENTITIES]; int num_entities;
    char     filename[512];
} map_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Viewport
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int   type;               /* VIEW_TOP / FRONT / SIDE / 3D */
    float sx, sy;             /* scroll (world origin mapped to view centre) */
    float zoom;               /* world units per pixel reciprocal */
    int   wx, wy, ww, wh;    /* window rect */
} vport_t;

/* world axes that map to screen X and screen Y for each view type */
static void view_axes(int type, int *ax, int *ay, int *az) {
    switch (type) {
        case VIEW_TOP:   *ax=0; *ay=1; *az=2; return;
        case VIEW_FRONT: *ax=0; *ay=2; *az=1; return;
        case VIEW_SIDE:  *ax=1; *ay=2; *az=0; return;
        default:         *ax=0; *ay=1; *az=2; return;
    }
}

static void w2s(const vport_t *v, float wx, float wy, int *ox, int *oy) {
    *ox = v->wx + (int)((wx - v->sx) * v->zoom + v->ww * 0.5f);
    *oy = v->wy + (int)((-wy + v->sy) * v->zoom + v->wh * 0.5f);
}
static void s2w(const vport_t *v, int ox, int oy, float *wx, float *wy) {
    *wx =  (ox - v->wx - v->ww*0.5f) / v->zoom + v->sx;
    *wy = -(oy - v->wy - v->wh*0.5f) / v->zoom + v->sy;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Editor state
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int   mode;
    /* brush drawing */
    int   drawing;
    float dx0,dy0,dz0, dx1,dy1,dz1;  /* draw box corners (world) */
    /* brush moving */
    int   moving;
    float mv_wx0, mv_wy0;            /* world pos at move start */
    /* 3-D camera */
    float cam_x, cam_y, cam_z;
    float cam_yaw, cam_pitch;
    /* settings */
    float grid;
    int   show_grid;
    int   sel_brush;
    char  texture[64];
    char  status[512];
} editor_t;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Globals
 * ═══════════════════════════════════════════════════════════════════════════ */

static Display    *g_dpy;
static Window      g_win;
static GC          g_gc;
static XFontStruct*g_font;
static Pixmap      g_buf;
static int         g_screen;
static int         g_running = 1;
static int         g_win_w = WIN_W, g_win_h = WIN_H;

static map_t    g_map;
static bsp_t    g_bsp;
static editor_t g_ed;
static vport_t  g_vp[4];

/* palette */
static unsigned long
    C_BG, C_GRID, C_GRID_MAJ, C_BRUSH, C_BRUSH_SEL,
    C_BSP, C_ENT, C_DRAW, C_TEXT, C_DIM, C_BORDER,
    C_AXISN, C_AXISP, C_STATUSBG, C_3DBG;

/* ═══════════════════════════════════════════════════════════════════════════
 *  Color helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static unsigned long mkcolor(int r, int g, int b) {
    XColor c;
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(g_dpy, DefaultColormap(g_dpy, g_screen), &c);
    return c.pixel;
}

static void init_colors(void) {
    C_BG        = mkcolor( 28,  28,  28);
    C_GRID      = mkcolor( 48,  48,  48);
    C_GRID_MAJ  = mkcolor( 68,  68,  68);
    C_BRUSH     = mkcolor(200, 190,  60);
    C_BRUSH_SEL = mkcolor(255,  80,  80);
    C_BSP       = mkcolor( 55, 120, 185);
    C_ENT       = mkcolor( 55, 210, 110);
    C_DRAW      = mkcolor(255, 210,  50);
    C_TEXT      = mkcolor(220, 220, 220);
    C_DIM       = mkcolor(130, 130, 130);
    C_BORDER    = mkcolor( 95,  95,  95);
    C_AXISN     = mkcolor(190,  50,  50);
    C_AXISP     = mkcolor( 50, 175,  50);
    C_STATUSBG  = mkcolor( 18,  18,  18);
    C_3DBG      = mkcolor( 12,  12,  18);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Drawing primitives  (all draw to g_buf)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void dp_fill(int x, int y, int w, int h, unsigned long c) {
    XSetForeground(g_dpy, g_gc, c);
    XFillRectangle(g_dpy, g_buf, g_gc, x, y, (unsigned)w, (unsigned)h);
}
static void dp_line(int x0,int y0,int x1,int y1, unsigned long c) {
    XSetForeground(g_dpy, g_gc, c);
    XDrawLine(g_dpy, g_buf, g_gc, x0, y0, x1, y1);
}
static void dp_rect(int x, int y, int w, int h, unsigned long c) {
    XSetForeground(g_dpy, g_gc, c);
    XDrawRectangle(g_dpy, g_buf, g_gc, x, y, (unsigned)w, (unsigned)h);
}
static void dp_text(int x, int y, const char *s, unsigned long c) {
    XSetForeground(g_dpy, g_gc, c);
    XDrawString(g_dpy, g_buf, g_gc, x, y, s, (int)strlen(s));
}
static void dp_cross(int x, int y, int r, unsigned long c) {
    dp_line(x-r, y, x+r, y, c);
    dp_line(x, y-r, x, y+r, c);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Brush geometry  (vertex enumeration from half-spaces)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Solve 3-plane intersection; returns 1 on success */
static int three_plane_point(const plane_t *a, const plane_t *b,
                              const plane_t *c, vec3_t *out) {
    float d00 = a->normal.x, d01 = a->normal.y, d02 = a->normal.z;
    float d10 = b->normal.x, d11 = b->normal.y, d12 = b->normal.z;
    float d20 = c->normal.x, d21 = c->normal.y, d22 = c->normal.z;
    float det = d00*(d11*d22 - d12*d21)
               -d01*(d10*d22 - d12*d20)
               +d02*(d10*d21 - d11*d20);
    if (fabsf(det) < 1e-6f) return 0;
    float ra = a->dist, rb = b->dist, rc = c->dist;
    out->x = (ra*(d11*d22 - d12*d21)
             -d01*(rb*d22 - d12*rc)
             +d02*(rb*d21 - d11*rc)) / det;
    out->y = (d00*(rb*d22 - d12*rc)
             -ra*(d10*d22 - d12*d20)
             +d02*(d10*rc - rb*d20)) / det;
    out->z = (d00*(d11*rc - rb*d21)
             -d01*(d10*rc - rb*d20)
             +ra*(d10*d21 - d11*d20)) / det;
    return 1;
}

/* Enumerate vertices of a convex brush by triple-plane intersection */
static int brush_verts(const brush_t *br, vec3_t *out, int max_out) {
    int n = 0;
    int np = br->num_planes;
    for (int i = 0; i < np && n < max_out; i++)
    for (int j = i+1; j < np && n < max_out; j++)
    for (int k = j+1; k < np && n < max_out; k++) {
        vec3_t pt;
        if (!three_plane_point(&br->planes[i],
                               &br->planes[j],
                               &br->planes[k], &pt)) continue;
        /* point must lie on or inside every half-space */
        int ok = 1;
        for (int m = 0; m < np; m++) {
            if (v3dot(br->planes[m].normal, pt) > br->planes[m].dist + 0.5f)
                { ok = 0; break; }
        }
        if (!ok) continue;
        /* dedup */
        int dup = 0;
        for (int m = 0; m < n; m++)
            if (fabsf(out[m].x-pt.x)<0.5f &&
                fabsf(out[m].y-pt.y)<0.5f &&
                fabsf(out[m].z-pt.z)<0.5f) { dup=1; break; }
        if (!dup) out[n++] = pt;
    }
    return n;
}

/* Convex-hull centroid + angle sort for 2-D outline drawing */
static float g_cx, g_cy;
static int angle_cmp(const void *a, const void *b) {
    const float *fa = (const float*)a, *fb = (const float*)b;
    float da = atan2f(fa[1]-g_cy, fa[0]-g_cx);
    float db = atan2f(fb[1]-g_cy, fb[0]-g_cx);
    return (da < db) ? -1 : (da > db) ? 1 : 0;
}

/* Translate a brush by (dx,dy,dz) */
static void brush_translate(brush_t *br, float dx, float dy, float dz) {
    vec3_t delta = {dx, dy, dz};
    for (int i = 0; i < br->num_planes; i++)
        br->planes[i].dist += v3dot(br->planes[i].normal, delta);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Plane helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void plane_init(plane_t *p, const char *tex) {
    memset(p, 0, sizeof(*p));
    strncpy(p->tex, tex, sizeof(p->tex)-1);
    p->ua = (vec3_t){1,0,0};
    p->va = (vec3_t){0,0,-1};
    p->uscale = 1.0f; p->vscale = 1.0f;
}

/* Build an axis-aligned box brush from two opposite corners */
static void make_box_brush(brush_t *br,
                           float x0,float y0,float z0,
                           float x1,float y1,float z1,
                           const char *tex) {
    if (x0>x1) { float t=x0; x0=x1; x1=t; }
    if (y0>y1) { float t=y0; y0=y1; y1=t; }
    if (z0>z1) { float t=z0; z0=z1; z1=t; }
    /* enforce minimum size */
    if (x1-x0 < 4.0f) x1 = x0+4.0f;
    if (y1-y0 < 4.0f) y1 = y0+4.0f;
    if (z1-z0 < 4.0f) z1 = z0+4.0f;

    br->num_planes = 6; br->selected = 0;
    static const struct { vec3_t n; float s; } faces[6] = {
        {{1,0,0},1}, {{-1,0,0},-1},
        {{0,1,0},1}, {{0,-1,0},-1},
        {{0,0,1},1}, {{0,0,-1},-1}
    };
    float extents[6] = {x1,-x0, y1,-y0, z1,-z0};
    for (int i = 0; i < 6; i++) {
        plane_t *p = &br->planes[i];
        plane_init(p, tex);
        p->normal = faces[i].n;
        p->dist   = extents[i] * faces[i].s;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  BSP loader (WaspSrc v29)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void bsp_free(void) {
    free(g_bsp.vertices); g_bsp.vertices = NULL;
    free(g_bsp.edges);    g_bsp.edges    = NULL;
    free(g_bsp.models);   g_bsp.models   = NULL;
    free(g_bsp.ent_str);  g_bsp.ent_str  = NULL;
    memset(&g_bsp, 0, sizeof(g_bsp));
}

static int bsp_load(const char *path) {
    bsp_free();
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(g_ed.status, sizeof(g_ed.status),
                 "Error: cannot open '%s'", path);
        return 0;
    }
    bsp_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.version != BSP_VERSION) {
        snprintf(g_ed.status, sizeof(g_ed.status),
                 "Error: not a WaspSrc v29 BSP (got version %u)", hdr.version);
        fclose(f); return 0;
    }

#define LOAD_LUMP(field, count, T, ID) do { \
    bsp_lump_t *lp = &hdr.lumps[ID]; \
    g_bsp.count = (int)(lp->length / sizeof(T)); \
    if (lp->length > 0) { \
        g_bsp.field = (T*)malloc(lp->length); \
        if (!g_bsp.field) { fclose(f); bsp_free(); return 0; } \
        fseek(f, (long)lp->offset, SEEK_SET); \
        if (fread(g_bsp.field, 1, lp->length, f) != lp->length) \
            { fclose(f); bsp_free(); return 0; } \
    } \
} while(0)

    LOAD_LUMP(vertices, num_vertices, bsp_vertex_t, LUMP_VERTICES);
    LOAD_LUMP(edges,    num_edges,    bsp_edge_t,   LUMP_EDGES);
    LOAD_LUMP(models,   num_models,   bsp_model_t,  LUMP_MODELS);

    /* entity string */
    bsp_lump_t *elp = &hdr.lumps[LUMP_ENTITIES];
    if (elp->length > 0) {
        g_bsp.ent_str = (char*)malloc(elp->length + 1);
        if (g_bsp.ent_str) {
            fseek(f, (long)elp->offset, SEEK_SET);
            size_t nr = fread(g_bsp.ent_str, 1, elp->length, f);
            g_bsp.ent_str[nr] = '\0';
        }
    }
    fclose(f);
#undef LOAD_LUMP

    /* centre all views on model 0 */
    if (g_bsp.num_models > 0) {
        bsp_model_t *m = &g_bsp.models[0];
        float cx = (m->mins.x + m->maxs.x) * 0.5f;
        float cy = (m->mins.y + m->maxs.y) * 0.5f;
        float cz = (m->mins.z + m->maxs.z) * 0.5f;
        for (int i = 0; i < 4; i++) { g_vp[i].sx = cx; g_vp[i].sy = cy; }
        g_ed.cam_x = cx; g_ed.cam_y = cy - 600.0f; g_ed.cam_z = cz;
    }

    snprintf(g_ed.status, sizeof(g_ed.status),
             "BSP loaded: %d vertices, %d edges  ('%s')",
             g_bsp.num_vertices, g_bsp.num_edges, path);
    strncpy(g_map.filename, path, sizeof(g_map.filename)-1);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  MAP file saver  (WaspSrc / Valve standard .map text format)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Write 3 non-collinear points on plane p for .map format */
static void write_plane(FILE *f, const plane_t *p) {
    /* tangent basis via Gram-Schmidt */
    vec3_t n = p->normal;
    vec3_t t1 = (fabsf(n.x) < 0.9f) ?
                (vec3_t){1,0,0} : (vec3_t){0,1,0};
    float  d  = v3dot(t1, n);
    t1 = (vec3_t){ t1.x - d*n.x, t1.y - d*n.y, t1.z - d*n.z };
    t1 = v3norm(t1);
    vec3_t t2 = v3cross(n, t1);

    /* anchor point on plane */
    vec3_t o = { n.x * p->dist, n.y * p->dist, n.z * p->dist };
    vec3_t q1 = o;
    vec3_t q2 = { o.x + t1.x*16.0f, o.y + t1.y*16.0f, o.z + t1.z*16.0f };
    vec3_t q3 = { o.x + t2.x*16.0f, o.y + t2.y*16.0f, o.z + t2.z*16.0f };

    fprintf(f,
        "( %.4f %.4f %.4f ) ( %.4f %.4f %.4f ) ( %.4f %.4f %.4f )"
        " %s %.4f %.4f 0 %.4f %.4f\n",
        q1.x, q1.y, q1.z,
        q2.x, q2.y, q2.z,
        q3.x, q3.y, q3.z,
        p->tex, p->uofs, p->vofs, p->uscale, p->vscale);
}

static void write_brush(FILE *f, const brush_t *br) {
    fprintf(f, "{\n");
    for (int i = 0; i < br->num_planes; i++)
        write_plane(f, &br->planes[i]);
    fprintf(f, "}\n");
}

static int map_save(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(g_ed.status, sizeof(g_ed.status),
                 "Error: cannot write '%s'", path);
        return 0;
    }

    /* worldspawn — entity 0 owns the level brushes */
    fprintf(f, "{\n\"classname\" \"worldspawn\"\n");
    for (int i = 0; i < g_map.num_brushes; i++) {
        const entity_t *e0 = &g_map.entities[0];
        /* brushes not claimed by any sub-entity belong to worldspawn */
        int claimed = 0;
        for (int ei = 1; ei < g_map.num_entities; ei++) {
            const entity_t *e = &g_map.entities[ei];
            if (i >= e->brush_start && i < e->brush_start + e->brush_count)
                { claimed = 1; break; }
        }
        if (i < e0->brush_start ||
            i >= e0->brush_start + e0->brush_count)
            if (!claimed) write_brush(f, &g_map.brushes[i]);
    }
    /* also write explicitly assigned worldspawn brushes */
    {
        const entity_t *e0 = &g_map.entities[0];
        for (int bi = e0->brush_start;
             bi < e0->brush_start + e0->brush_count; bi++)
            if (bi >= 0 && bi < g_map.num_brushes)
                write_brush(f, &g_map.brushes[bi]);
    }
    fprintf(f, "}\n");

    /* point / brush entities */
    for (int ei = 1; ei < g_map.num_entities; ei++) {
        const entity_t *e = &g_map.entities[ei];
        fprintf(f, "{\n");
        for (int k = 0; k < e->num_pairs; k++)
            fprintf(f, "\"%s\" \"%s\"\n", e->pairs[k].key, e->pairs[k].value);
        for (int bi = e->brush_start;
             bi < e->brush_start + e->brush_count; bi++)
            if (bi >= 0 && bi < g_map.num_brushes)
                write_brush(f, &g_map.brushes[bi]);
        fprintf(f, "}\n");
    }

    fclose(f);
    snprintf(g_ed.status, sizeof(g_ed.status), "Saved '%s'", path);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  MAP text-format loader
 * ═══════════════════════════════════════════════════════════════════════════ */

static void plane_from_3pts(plane_t *p,
                             vec3_t p1, vec3_t p2, vec3_t p3,
                             const char *tex,
                             float uofs, float vofs, float uscale, float vscale) {
    vec3_t e1 = v3sub(p2, p1);
    vec3_t e2 = v3sub(p3, p1);
    p->normal = v3norm(v3cross(e1, e2));
    p->dist   = v3dot(p->normal, p1);
    plane_init(p, tex);
    strncpy(p->tex, tex, sizeof(p->tex)-1);
    p->uofs   = uofs;   p->vofs   = vofs;
    p->uscale = uscale; p->vscale = vscale;
}

static int map_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(g_ed.status, sizeof(g_ed.status),
                 "Error: cannot open '%s'", path);
        return 0;
    }
    g_map.num_brushes  = 0;
    g_map.num_entities = 0;

    char line[1024];
    int in_ent = 0, in_br = 0;
    entity_t *cur_ent = NULL;
    brush_t  *cur_br  = NULL;

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p==' '||*p=='\t') p++;
        if (!*p || *p=='\n' || *p=='\r' || *p=='/') continue;

        if (*p == '{') {
            if (!in_ent) {
                if (g_map.num_entities < MAX_ENTITIES) {
                    cur_ent = &g_map.entities[g_map.num_entities++];
                    memset(cur_ent, 0, sizeof(*cur_ent));
                    cur_ent->brush_start = g_map.num_brushes;
                    in_ent = 1;
                }
            } else if (!in_br) {
                if (g_map.num_brushes < MAX_BRUSHES) {
                    cur_br = &g_map.brushes[g_map.num_brushes++];
                    memset(cur_br, 0, sizeof(*cur_br));
                    if (cur_ent) cur_ent->brush_count++;
                    in_br = 1;
                }
            }
        } else if (*p == '}') {
            if      (in_br)  { in_br  = 0; cur_br  = NULL; }
            else if (in_ent) { in_ent = 0; cur_ent = NULL; }
        } else if (in_br && cur_br && *p == '(') {
            float ax,ay,az,bx,by,bz,cx,cy,cz;
            char  tex[64]="GROUND1";
            float uo=0,vo=0,rot=0,us=1,vs=1;
            int n = sscanf(p,
                "( %f %f %f ) ( %f %f %f ) ( %f %f %f ) %63s %f %f %f %f %f",
                &ax,&ay,&az, &bx,&by,&bz, &cx,&cy,&cz,
                tex, &uo, &vo, &rot, &us, &vs);
            (void)rot;
            if (n >= 10 && cur_br->num_planes < MAX_PLANES) {
                plane_t *pl = &cur_br->planes[cur_br->num_planes++];
                plane_from_3pts(pl,
                    (vec3_t){ax,ay,az},
                    (vec3_t){bx,by,bz},
                    (vec3_t){cx,cy,cz},
                    tex, uo, vo,
                    (n>=14 ? us : 1.0f),
                    (n>=15 ? vs : 1.0f));
            }
        } else if (in_ent && cur_ent && *p == '"') {
            char key[256]="", val[256]="";
            if (sscanf(p, "\"%255[^\"]\" \"%255[^\"]\"", key, val) == 2
                && cur_ent->num_pairs < MAX_EPAIRS) {
                strncpy(cur_ent->pairs[cur_ent->num_pairs].key,   key, 255);
                strncpy(cur_ent->pairs[cur_ent->num_pairs].value, val, 255);
                cur_ent->num_pairs++;
            }
        }
    }
    fclose(f);
    strncpy(g_map.filename, path, sizeof(g_map.filename)-1);
    snprintf(g_ed.status, sizeof(g_ed.status),
             "MAP loaded: %d brushes, %d entities  ('%s')",
             g_map.num_brushes, g_map.num_entities, path);
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Entity utility
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *ent_get(const entity_t *e, const char *key) {
    for (int i = 0; i < e->num_pairs; i++)
        if (strcmp(e->pairs[i].key, key) == 0) return e->pairs[i].value;
    return "";
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Grid rendering
 * ═══════════════════════════════════════════════════════════════════════════ */

static void render_grid(const vport_t *vp) {
    if (!g_ed.show_grid) return;
    float g = g_ed.grid;
    float wx0, wy0, wx1, wy1;
    s2w(vp, vp->wx, vp->wy, &wx0, &wy1);
    s2w(vp, vp->wx+vp->ww, vp->wy+vp->wh, &wx1, &wy0);
    float major = g * 8.0f;
    /* vertical lines */
    for (float wx = floorf(wx0/g)*g; wx <= wx1; wx += g) {
        int sx, sy0, sy1;
        w2s(vp, wx, wy0, &sx, &sy0); w2s(vp, wx, wy1, &sx, &sy1);
        unsigned long c = (fmodf(fabsf(wx), major) < 0.5f) ? C_GRID_MAJ : C_GRID;
        dp_line(sx, sy0, sx, sy1, c);
    }
    /* horizontal lines */
    for (float wy = floorf(wy0/g)*g; wy <= wy1; wy += g) {
        int sx0, sx1, sy;
        w2s(vp, wx0, wy, &sx0, &sy); w2s(vp, wx1, wy, &sx1, &sy);
        unsigned long c = (fmodf(fabsf(wy), major) < 0.5f) ? C_GRID_MAJ : C_GRID;
        dp_line(sx0, sy, sx1, sy, c);
    }
    /* axes */
    int ox, oy;
    w2s(vp, 0, 0, &ox, &oy);
    dp_line(vp->wx, oy, vp->wx+vp->ww, oy, C_AXISP);
    dp_line(ox, vp->wy, ox, vp->wy+vp->wh, C_AXISN);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  2-D brush rendering
 * ═══════════════════════════════════════════════════════════════════════════ */

static void render_brush_2d(const vport_t *vp, const brush_t *br,
                             unsigned long col) {
    vec3_t verts[128];
    int nv = brush_verts(br, verts, 128);
    if (nv < 2) return;

    int ax, ay, az;
    view_axes(vp->type, &ax, &ay, &az);

    float pts[128][2];
    float sumx = 0, sumy = 0;
    for (int i = 0; i < nv; i++) {
        pts[i][0] = fget3(verts[i], ax);
        pts[i][1] = fget3(verts[i], ay);
        sumx += pts[i][0]; sumy += pts[i][1];
    }
    g_cx = sumx / (float)nv;
    g_cy = sumy / (float)nv;
    qsort(pts, (size_t)nv, sizeof(pts[0]), angle_cmp);

    XSetForeground(g_dpy, g_gc, col);
    for (int i = 0; i < nv; i++) {
        int j = (i+1) % nv;
        int x0,y0,x1,y1;
        w2s(vp, pts[i][0], pts[i][1], &x0, &y0);
        w2s(vp, pts[j][0], pts[j][1], &x1, &y1);
        XDrawLine(g_dpy, g_buf, g_gc, x0, y0, x1, y1);
    }
    int cx, cy;
    w2s(vp, g_cx, g_cy, &cx, &cy);
    dp_cross(cx, cy, 3, col);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  BSP reference geometry (blue)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void render_bsp_2d(const vport_t *vp) {
    if (!g_bsp.vertices || !g_bsp.edges || g_bsp.num_edges <= 0) return;
    int ax, ay, az;
    view_axes(vp->type, &ax, &ay, &az);
    XSetForeground(g_dpy, g_gc, C_BSP);
    for (int i = 0; i < g_bsp.num_edges; i++) {
        int vi0 = (int)g_bsp.edges[i].v[0];
        int vi1 = (int)g_bsp.edges[i].v[1];
        if (vi0 >= g_bsp.num_vertices || vi1 >= g_bsp.num_vertices) continue;
        float wx0 = fget3(g_bsp.vertices[vi0].point, ax);
        float wy0 = fget3(g_bsp.vertices[vi0].point, ay);
        float wx1 = fget3(g_bsp.vertices[vi1].point, ax);
        float wy1 = fget3(g_bsp.vertices[vi1].point, ay);
        int sx0,sy0,sx1,sy1;
        w2s(vp, wx0,wy0, &sx0,&sy0);
        w2s(vp, wx1,wy1, &sx1,&sy1);
        XDrawLine(g_dpy, g_buf, g_gc, sx0,sy0,sx1,sy1);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Entity rendering (2-D)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void render_entities_2d(const vport_t *vp) {
    int ax, ay, az;
    view_axes(vp->type, &ax, &ay, &az);
    for (int i = 1; i < g_map.num_entities; i++) {
        const entity_t *e = &g_map.entities[i];
        const char *org = ent_get(e, "origin");
        if (!org[0]) continue;
        float ox=0,oy=0,oz=0;
        sscanf(org, "%f %f %f", &ox, &oy, &oz);
        vec3_t o3 = {ox,oy,oz};
        int sx, sy;
        w2s(vp, fget3(o3,ax), fget3(o3,ay), &sx, &sy);
        unsigned long col = e->selected ? C_BRUSH_SEL : C_ENT;
        dp_cross(sx, sy, 8, col);
        dp_rect(sx-6, sy-6, 12, 12, col);
        const char *cn = ent_get(e, "classname");
        dp_text(sx+10, sy+4, cn, col);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Simple 3-D wireframe view
 * ═══════════════════════════════════════════════════════════════════════════ */

static void proj3d(float wx, float wy, float wz,
                   const vport_t *vp, int *sx, int *sy) {
    float dx = wx - g_ed.cam_x;
    float dy = wy - g_ed.cam_y;
    float dz = wz - g_ed.cam_z;
    float cy = cosf(g_ed.cam_yaw),   sy_ = sinf(g_ed.cam_yaw);
    float cp = cosf(g_ed.cam_pitch), sp  = sinf(g_ed.cam_pitch);
    float rx =  cy*dx + sy_*dy;
    float ry = -sy_*dx + cy*dy;
    float rz =  dz;
    float fz =  cp*ry - sp*rz;
    float fy =  sp*ry + cp*rz;
    if (fz < 1.0f) fz = 1.0f;
    float fov = 380.0f;
    *sx = vp->wx + vp->ww/2 + (int)(rx * fov / fz);
    *sy = vp->wy + vp->wh/2 - (int)(fy * fov / fz);
}

static void render_3d(const vport_t *vp) {
    dp_fill(vp->wx, vp->wy, vp->ww, vp->wh, C_3DBG);

    /* BSP geometry */
    if (g_bsp.vertices && g_bsp.edges) {
        XSetForeground(g_dpy, g_gc, C_BSP);
        for (int i = 0; i < g_bsp.num_edges; i++) {
            int vi0 = (int)g_bsp.edges[i].v[0];
            int vi1 = (int)g_bsp.edges[i].v[1];
            if (vi0>=g_bsp.num_vertices || vi1>=g_bsp.num_vertices) continue;
            vec3_t *v0 = &g_bsp.vertices[vi0].point;
            vec3_t *v1 = &g_bsp.vertices[vi1].point;
            int x0,y0,x1,y1;
            proj3d(v0->x,v0->y,v0->z, vp, &x0,&y0);
            proj3d(v1->x,v1->y,v1->z, vp, &x1,&y1);
            XDrawLine(g_dpy, g_buf, g_gc, x0,y0,x1,y1);
        }
    }

    /* editor brushes */
    for (int bi = 0; bi < g_map.num_brushes; bi++) {
        brush_t *br = &g_map.brushes[bi];
        vec3_t verts[128];
        int nv = brush_verts(br, verts, 128);
        unsigned long col = br->selected ? C_BRUSH_SEL : C_BRUSH;
        XSetForeground(g_dpy, g_gc, col);
        for (int a = 0; a < nv; a++)
        for (int b = a+1; b < nv; b++) {
            int x0,y0,x1,y1;
            proj3d(verts[a].x,verts[a].y,verts[a].z, vp,&x0,&y0);
            proj3d(verts[b].x,verts[b].y,verts[b].z, vp,&x1,&y1);
            XDrawLine(g_dpy, g_buf, g_gc, x0,y0,x1,y1);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Viewport render
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *view_label(int type) {
    switch (type) {
        case VIEW_TOP:   return "TOP (XY)";
        case VIEW_FRONT: return "FRONT (XZ)";
        case VIEW_SIDE:  return "SIDE (YZ)";
        case VIEW_3D:    return "3D WIRE";
        default:         return "?";
    }
}

static void render_viewport(const vport_t *vp) {
    dp_fill(vp->wx, vp->wy, vp->ww, vp->wh, C_BG);

    if (vp->type == VIEW_3D) {
        render_3d(vp);
    } else {
        render_grid(vp);
        render_bsp_2d(vp);

        for (int i = 0; i < g_map.num_brushes; i++)
            render_brush_2d(vp, &g_map.brushes[i],
                            g_map.brushes[i].selected ? C_BRUSH_SEL : C_BRUSH);

        render_entities_2d(vp);

        /* in-progress draw rect */
        if (g_ed.drawing) {
            int ax, ay, az;
            view_axes(vp->type, &ax, &ay, &az);
            float s[3]={g_ed.dx0,g_ed.dy0,g_ed.dz0};
            float e[3]={g_ed.dx1,g_ed.dy1,g_ed.dz1};
            int x0,y0,x1,y1;
            w2s(vp, s[ax],s[ay], &x0,&y0);
            w2s(vp, e[ax],e[ay], &x1,&y1);
            int rx=x0<x1?x0:x1, ry=y0<y1?y0:y1;
            int rw=abs(x1-x0), rh=abs(y1-y0);
            if (rw<1) rw=1;
            if (rh<1) rh=1;
            dp_rect(rx, ry, rw, rh, C_DRAW);
        }
    }

    /* border + label */
    dp_rect(vp->wx, vp->wy, vp->ww-1, vp->wh-1, C_BORDER);
    dp_fill(vp->wx+1, vp->wy+1, 148, 16, C_STATUSBG);
    char lbl[64];
    snprintf(lbl, sizeof(lbl), " %s  z:%.2f", view_label(vp->type), vp->zoom);
    dp_text(vp->wx+4, vp->wy+13, lbl, C_DIM);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Status bar
 * ═══════════════════════════════════════════════════════════════════════════ */

static void render_statusbar(void) {
    int sy = g_win_h - STATUS_H;
    dp_fill(0, sy, g_win_w, STATUS_H, C_STATUSBG);
    dp_line(0, sy, g_win_w, sy, C_BORDER);

    char row0[1024];
    snprintf(row0, sizeof(row0),
        "  [O]pen  [S]ave  [B]rush  [E]ntity  [Del]Delete  "
        "[T]ex=%-12s  [G]rid=%-3d  Mode=%-6s  "
        "Brushes:%-4d  Ents:%-3d  |  %s",
        g_ed.texture, (int)g_ed.grid,
        g_ed.mode==MODE_SELECT?"SELECT":
        g_ed.mode==MODE_DRAW  ?"DRAW":"ENTITY",
        g_map.num_brushes, g_map.num_entities,
        g_ed.status);
    dp_text(2, sy+14, row0, C_TEXT);

    const char *row1 =
        "  WASD/Arrows=Scroll  +/-=Zoom  Scroll=Zoom  "
        "RMB=Pan  [Tab]=Grid  [R]eset  [1/2/3/4]=View  [N]ew";
    dp_text(2, sy+29, row1, C_DIM);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Layout
 * ═══════════════════════════════════════════════════════════════════════════ */

static void layout(void) {
    int H = g_win_h - STATUS_H;
    int hw = g_win_w / 2, hh = H / 2;
    /* top-left */
    g_vp[0].wx=0;   g_vp[0].wy=0;   g_vp[0].ww=hw;        g_vp[0].wh=hh;
    /* top-right */
    g_vp[1].wx=hw;  g_vp[1].wy=0;   g_vp[1].ww=g_win_w-hw;g_vp[1].wh=hh;
    /* bottom-left */
    g_vp[2].wx=0;   g_vp[2].wy=hh;  g_vp[2].ww=hw;        g_vp[2].wh=H-hh;
    /* bottom-right */
    g_vp[3].wx=hw;  g_vp[3].wy=hh;  g_vp[3].ww=g_win_w-hw;g_vp[3].wh=H-hh;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Full render
 * ═══════════════════════════════════════════════════════════════════════════ */

static void render_all(void) {
    dp_fill(0, 0, g_win_w, g_win_h, C_BG);
    layout();
    for (int i = 0; i < 4; i++) render_viewport(&g_vp[i]);
    /* dividers */
    dp_line(g_win_w/2, 0, g_win_w/2, g_win_h-STATUS_H, C_BORDER);
    dp_line(0,(g_win_h-STATUS_H)/2, g_win_w,(g_win_h-STATUS_H)/2, C_BORDER);
    render_statusbar();
    XCopyArea(g_dpy, g_buf, g_win, g_gc, 0,0, (unsigned)g_win_w,(unsigned)g_win_h, 0,0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  File-name derivation helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Replace or append .map extension */
static void make_map_path(const char *src, char *dst, size_t dstlen) {
    strncpy(dst, src, dstlen-1); dst[dstlen-1]='\0';
    char *dot = strrchr(dst, '.');
    if (dot && (strcmp(dot,".bsp")==0 || strcmp(dot,".map")==0))
        strcpy(dot, ".map");
    else
        strncat(dst, ".map", dstlen - strlen(dst) - 1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Map initialisation
 * ═══════════════════════════════════════════════════════════════════════════ */

static void map_new(void) {
    g_map.num_brushes  = 0;
    g_map.num_entities = 1;
    memset(g_map.entities, 0, sizeof(g_map.entities[0]));
    strncpy(g_map.entities[0].pairs[0].key,   "classname", 255);
    strncpy(g_map.entities[0].pairs[0].value, "worldspawn", 255);
    g_map.entities[0].num_pairs = 1;
    g_map.filename[0] = '\0';
    bsp_free();
    g_ed.sel_brush = -1;
    snprintf(g_ed.status, sizeof(g_ed.status), "New map — ready.");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Selection
 * ═══════════════════════════════════════════════════════════════════════════ */

static void deselect_all(void) {
    for (int i = 0; i < g_map.num_brushes; i++)
        g_map.brushes[i].selected = 0;
    for (int i = 0; i < g_map.num_entities; i++)
        g_map.entities[i].selected = 0;
    g_ed.sel_brush = -1;
}

static void select_at(const vport_t *vp, int sx, int sy) {
    float wx, wy;
    s2w(vp, sx, sy, &wx, &wy);
    int ax, ay, az;
    view_axes(vp->type, &ax, &ay, &az);
    deselect_all();

    float best = 32.0f / (vp->zoom > 0.0f ? vp->zoom : 1.0f);
    int sel = -1;
    for (int i = 0; i < g_map.num_brushes; i++) {
        vec3_t verts[128];
        int nv = brush_verts(&g_map.brushes[i], verts, 128);
        if (nv == 0) continue;
        float sumx=0, sumy=0;
        for (int j=0;j<nv;j++){
            sumx+=fget3(verts[j],ax);
            sumy+=fget3(verts[j],ay);
        }
        float dx=wx-sumx/(float)nv, dy=wy-sumy/(float)nv;
        float d=sqrtf(dx*dx+dy*dy);
        if (d<best){ best=d; sel=i; }
    }
    if (sel >= 0) {
        g_map.brushes[sel].selected = 1;
        g_ed.sel_brush = sel;
        snprintf(g_ed.status, sizeof(g_ed.status), "Selected brush %d", sel);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Input state
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum { DRAG_NONE, DRAG_DRAW, DRAG_PAN, DRAG_MOVE } drag_t;
static drag_t   g_drag      = DRAG_NONE;
static vport_t *g_drag_vp   = NULL;
static float    g_drag_wx0, g_drag_wy0; /* world coords at drag start */
static float    g_drag_bwx, g_drag_bwy; /* brush origin at drag start */

static vport_t *vp_hit(int x, int y) {
    for (int i = 0; i < 4; i++) {
        vport_t *vp = &g_vp[i];
        if (x>=vp->wx && x<vp->wx+vp->ww &&
            y>=vp->wy && y<vp->wy+vp->wh) return vp;
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Keyboard
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_key(KeySym ks, unsigned int state) {
    static int grids[] = {4,8,16,32,64,128};
    static int gi = 3; /* default 32 */
    float spd = 32.0f / (g_vp[0].zoom > 0.0f ? g_vp[0].zoom : 1.0f);

    switch (ks) {
    /* ── mode ─────────────────────────────────────────────────────── */
    case XK_b: case XK_B:
        g_ed.mode = MODE_DRAW; g_ed.drawing = 0;
        snprintf(g_ed.status,sizeof(g_ed.status),
                 "DRAW — click-drag to stamp brush"); break;
    case XK_e: case XK_E:
        g_ed.mode = MODE_ENTITY;
        snprintf(g_ed.status,sizeof(g_ed.status),
                 "ENTITY — click to place info_player_start"); break;
    case XK_Escape:
        g_ed.mode = MODE_SELECT; g_ed.drawing = 0; g_drag = DRAG_NONE;
        snprintf(g_ed.status,sizeof(g_ed.status),"SELECT mode"); break;

    /* ── delete ───────────────────────────────────────────────────── */
    case XK_Delete:
        if (g_ed.sel_brush>=0 && g_ed.sel_brush<g_map.num_brushes) {
            int idx=g_ed.sel_brush;
            memmove(&g_map.brushes[idx],&g_map.brushes[idx+1],
                    (size_t)(g_map.num_brushes-idx-1)*sizeof(brush_t));
            g_map.num_brushes--;
            g_ed.sel_brush=-1;
            snprintf(g_ed.status,sizeof(g_ed.status),
                     "Deleted brush (%d remain)",g_map.num_brushes);
        } break;

    /* ── view type for pane 0 ─────────────────────────────────────── */
    case XK_1: g_vp[0].type=VIEW_TOP;   break;
    case XK_2: g_vp[0].type=VIEW_FRONT; break;
    case XK_3: g_vp[0].type=VIEW_SIDE;  break;
    case XK_4: g_vp[0].type=VIEW_3D;    break;

    /* ── scroll ───────────────────────────────────────────────────── */
    case XK_Left:  case XK_a: case XK_A:
        for(int i=0;i<4;i++) if(g_vp[i].type!=VIEW_3D) g_vp[i].sx-=spd;
        break;
    case XK_Right: case XK_d: case XK_D:
        for(int i=0;i<4;i++) if(g_vp[i].type!=VIEW_3D) g_vp[i].sx+=spd;
        break;
    case XK_Up:    case XK_w: case XK_W:
        for(int i=0;i<4;i++) if(g_vp[i].type!=VIEW_3D) g_vp[i].sy+=spd;
        break;
    case XK_Down:  case XK_s: case XK_S:
        if (!(state & ShiftMask))
            for(int i=0;i<4;i++) if(g_vp[i].type!=VIEW_3D) g_vp[i].sy-=spd;
        break;

    /* ── zoom ─────────────────────────────────────────────────────── */
    case XK_equal: case XK_plus:
        for(int i=0;i<4;i++) g_vp[i].zoom=fminf(g_vp[i].zoom*1.25f,64.0f);
        break;
    case XK_minus:
        for(int i=0;i<4;i++) g_vp[i].zoom=fmaxf(g_vp[i].zoom*0.8f,0.005f);
        break;

    /* ── grid ─────────────────────────────────────────────────────── */
    case XK_Tab:
        g_ed.show_grid = !g_ed.show_grid; break;
    case XK_g: case XK_G:
        gi=(gi+1)%6; g_ed.grid=(float)grids[gi];
        snprintf(g_ed.status,sizeof(g_ed.status),"Grid: %d",(int)g_ed.grid); break;

    /* ── reset ────────────────────────────────────────────────────── */
    case XK_r: case XK_R:
        for(int i=0;i<4;i++){g_vp[i].sx=0;g_vp[i].sy=0;g_vp[i].zoom=0.5f;}
        g_ed.cam_x=0; g_ed.cam_y=-600; g_ed.cam_z=128;
        g_ed.cam_yaw=0; g_ed.cam_pitch=0.3f;
        snprintf(g_ed.status,sizeof(g_ed.status),"Views reset"); break;

    /* ── new ──────────────────────────────────────────────────────── */
    case XK_n: case XK_N: map_new(); break;

    default: break;
    }
}

/* Read a line from terminal (blocking — the window stays visible but frozen) */
static int read_line_stdin(const char *prompt, char *out, size_t len) {
    printf("%s", prompt); fflush(stdout);
    if (!fgets(out, (int)len, stdin)) return 0;
    out[strcspn(out, "\r\n")] = '\0';
    return (int)strlen(out) > 0;
}

static void on_key_file(KeySym ks, unsigned int state) {
    /* file ops that block on terminal input */
    char fn[512];
    if (ks == XK_o || ks == XK_O) {
        snprintf(g_ed.status,sizeof(g_ed.status),
                 "Enter filename in terminal…");
        render_all();
        if (read_line_stdin("Open file (.bsp or .map): ", fn, sizeof(fn))) {
            size_t ln = strlen(fn);
            if (ln>4 && strcmp(fn+ln-4,".bsp")==0) bsp_load(fn);
            else                                     map_load(fn);
            XStoreName(g_dpy, g_win, fn);
        }
    } else if ((ks == XK_s || ks == XK_S) && (state & ShiftMask)) {
        snprintf(g_ed.status,sizeof(g_ed.status),
                 "Enter filename in terminal…");
        render_all();
        if (read_line_stdin("Save map as (.map): ", fn, sizeof(fn)) && fn[0]) {
            make_map_path(fn, g_map.filename, sizeof(g_map.filename));
            map_save(g_map.filename);
            XStoreName(g_dpy, g_win, g_map.filename);
        }
    } else if (ks == XK_s || ks == XK_S) {
        /* plain save */
        if (!g_map.filename[0]) {
            snprintf(g_ed.status,sizeof(g_ed.status),
                     "Enter filename in terminal…");
            render_all();
            if (!read_line_stdin("Save map as (.map): ", fn, sizeof(fn)) || !fn[0]) return;
            make_map_path(fn, g_map.filename, sizeof(g_map.filename));
        } else {
            make_map_path(g_map.filename, fn, sizeof(fn));
            strncpy(g_map.filename, fn, sizeof(g_map.filename)-1);
        }
        map_save(g_map.filename);
        XStoreName(g_dpy, g_win, g_map.filename);
    } else if (ks == XK_t || ks == XK_T) {
        snprintf(g_ed.status,sizeof(g_ed.status),
                 "Enter texture in terminal…");
        render_all();
        if (read_line_stdin("Texture name: ", fn, sizeof(fn)) && fn[0]) {
            strncpy(g_ed.texture, fn, sizeof(g_ed.texture)-1);
            snprintf(g_ed.status,sizeof(g_ed.status),
                     "Texture → %s", g_ed.texture);
        }
    } else {
        on_key(ks, state);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Mouse
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_button_press(XButtonEvent *ev) {
    vport_t *vp = vp_hit(ev->x, ev->y);
    if (!vp) return;

    /* scroll wheel → zoom this viewport only */
    if (ev->button == Button4) {
        vp->zoom = fminf(vp->zoom * 1.15f, 64.0f); return;
    }
    if (ev->button == Button5) {
        vp->zoom = fmaxf(vp->zoom * 0.87f, 0.005f); return;
    }

    /* RMB → pan */
    if (ev->button == Button3) {
        g_drag = DRAG_PAN; g_drag_vp = vp;
        s2w(vp, ev->x, ev->y, &g_drag_wx0, &g_drag_wy0); return;
    }

    if (ev->button != Button1) return;

    int ax, ay, az;
    view_axes(vp->type, &ax, &ay, &az);

    if (g_ed.mode == MODE_SELECT) {
        select_at(vp, ev->x, ev->y);
        if (g_ed.sel_brush >= 0) {
            /* start move */
            g_drag = DRAG_MOVE; g_drag_vp = vp;
            s2w(vp, ev->x, ev->y, &g_drag_wx0, &g_drag_wy0);
            /* record brush centroid as origin */
            vec3_t verts[128];
            int nv = brush_verts(&g_map.brushes[g_ed.sel_brush], verts, 128);
            float sx_=0, sy_=0;
            for (int i=0;i<nv;i++){sx_+=fget3(verts[i],ax);sy_+=fget3(verts[i],ay);}
            if (nv>0){g_drag_bwx=sx_/(float)nv; g_drag_bwy=sy_/(float)nv;}
        }
    } else if (g_ed.mode == MODE_DRAW) {
        float wx, wy;
        s2w(vp, ev->x, ev->y, &wx, &wy);
        wx = fsnap(wx, g_ed.grid);
        wy = fsnap(wy, g_ed.grid);
        float coords[3] = {0,0,0};
        coords[ax] = wx; coords[ay] = wy; coords[az] = -64.0f;
        g_ed.dx0=coords[0]; g_ed.dy0=coords[1]; g_ed.dz0=coords[2];
        coords[az] = 64.0f;
        g_ed.dx1=coords[0]; g_ed.dy1=coords[1]; g_ed.dz1=coords[2];
        g_ed.drawing = 1;
        g_drag = DRAG_DRAW; g_drag_vp = vp;
    } else if (g_ed.mode == MODE_ENTITY) {
        if (g_map.num_entities < MAX_ENTITIES) {
            float wx, wy;
            s2w(vp, ev->x, ev->y, &wx, &wy);
            entity_t *e = &g_map.entities[g_map.num_entities++];
            memset(e, 0, sizeof(*e));
            strncpy(e->pairs[0].key,   "classname",         255);
            strncpy(e->pairs[0].value, "info_player_start", 255);
            float oc[3]={0,0,0};
            oc[ax]=wx; oc[ay]=wy;
            char org[64];
            snprintf(org,sizeof(org),"%.0f %.0f %.0f",oc[0],oc[1],oc[2]);
            strncpy(e->pairs[1].key,   "origin", 255);
            strncpy(e->pairs[1].value, org,      255);
            e->num_pairs = 2;
            snprintf(g_ed.status,sizeof(g_ed.status),
                     "Entity placed at (%s)", org);
        }
    }
}

static void on_button_release(XButtonEvent *ev) {
    (void)ev;
    if (g_drag == DRAG_DRAW && g_ed.drawing) {
        g_ed.drawing = 0;
        if (g_map.num_brushes < MAX_BRUSHES) {
            brush_t *br = &g_map.brushes[g_map.num_brushes++];
            memset(br, 0, sizeof(*br));
            make_box_brush(br,
                g_ed.dx0, g_ed.dy0, g_ed.dz0,
                g_ed.dx1, g_ed.dy1, g_ed.dz1,
                g_ed.texture);
            if (g_map.num_entities > 0)
                g_map.entities[0].brush_count++;
            snprintf(g_ed.status,sizeof(g_ed.status),
                     "Brush created (%d total)", g_map.num_brushes);
        }
    }
    g_drag = DRAG_NONE; g_drag_vp = NULL;
}

static void on_motion(XMotionEvent *ev) {
    if (!g_drag_vp) return;
    vport_t *vp = g_drag_vp;

    if (g_drag == DRAG_PAN) {
        float wx, wy;
        s2w(vp, ev->x, ev->y, &wx, &wy);
        vp->sx += g_drag_wx0 - wx;
        vp->sy += g_drag_wy0 - wy;
        s2w(vp, ev->x, ev->y, &g_drag_wx0, &g_drag_wy0);
        return;
    }

    int ax, ay, az;
    view_axes(vp->type, &ax, &ay, &az);
    float wx, wy;
    s2w(vp, ev->x, ev->y, &wx, &wy);
    wx = fsnap(wx, g_ed.grid);
    wy = fsnap(wy, g_ed.grid);

    if (g_drag == DRAG_DRAW && g_ed.drawing) {
        float e[3] = {g_ed.dx1, g_ed.dy1, g_ed.dz1};
        e[ax] = wx; e[ay] = wy;
        g_ed.dx1=e[0]; g_ed.dy1=e[1]; g_ed.dz1=e[2];
    } else if (g_drag == DRAG_MOVE && g_ed.sel_brush >= 0) {
        float ddx[3]={0,0,0};
        ddx[ax] = wx - g_drag_wx0;
        ddx[ay] = wy - g_drag_wy0;
        brush_translate(&g_map.brushes[g_ed.sel_brush],
                        ddx[0], ddx[1], ddx[2]);
        g_drag_wx0 = wx; g_drag_wy0 = wy;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Editor init
 * ═══════════════════════════════════════════════════════════════════════════ */

static void editor_init(void) {
    memset(&g_map, 0, sizeof(g_map));
    memset(&g_bsp, 0, sizeof(g_bsp));
    memset(&g_ed,  0, sizeof(g_ed));

    g_ed.mode      = MODE_SELECT;
    g_ed.grid      = 32.0f;
    g_ed.show_grid = 1;
    g_ed.sel_brush = -1;
    g_ed.cam_x     = 0.0f;
    g_ed.cam_y     = -600.0f;
    g_ed.cam_z     = 128.0f;
    g_ed.cam_pitch = 0.3f;
    strncpy(g_ed.texture, "GROUND1", sizeof(g_ed.texture)-1);
    snprintf(g_ed.status,sizeof(g_ed.status),
             "Ready.  O=open  B=draw  S=save  ?=see status bar");

    g_vp[0].type = VIEW_TOP;
    g_vp[1].type = VIEW_FRONT;
    g_vp[2].type = VIEW_SIDE;
    g_vp[3].type = VIEW_3D;
    for (int i = 0; i < 4; i++) {
        g_vp[i].zoom = 0.5f;
        g_vp[i].sx   = 0.0f;
        g_vp[i].sy   = 0.0f;
    }

    /* worldspawn entity 0 */
    g_map.num_entities = 1;
    strncpy(g_map.entities[0].pairs[0].key,   "classname",  255);
    strncpy(g_map.entities[0].pairs[0].value, "worldspawn", 255);
    g_map.entities[0].num_pairs = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy) {
        fprintf(stderr, "vbsp: cannot open X display.\n");
        return 1;
    }
    g_screen = DefaultScreen(g_dpy);

    XSetWindowAttributes wa;
    wa.background_pixel = BlackPixel(g_dpy, g_screen);
    wa.event_mask = ExposureMask | KeyPressMask |
                    ButtonPressMask | ButtonReleaseMask |
                    PointerMotionMask | StructureNotifyMask;

    g_win = XCreateWindow(g_dpy, RootWindow(g_dpy, g_screen),
                          0, 0, (unsigned)WIN_W, (unsigned)WIN_H, 0,
                          DefaultDepth(g_dpy, g_screen), InputOutput,
                          DefaultVisual(g_dpy, g_screen),
                          CWBackPixel | CWEventMask, &wa);

    XStoreName(g_dpy, g_win, "vbsp :: WaspSrc Map Editor");
    XMapWindow(g_dpy, g_win);

    g_gc   = XCreateGC(g_dpy, g_win, 0, NULL);
    g_font = XLoadQueryFont(g_dpy, "fixed");
    if (g_font) XSetFont(g_dpy, g_gc, g_font->fid);

    init_colors();
    g_buf = XCreatePixmap(g_dpy, g_win,
                          (unsigned)WIN_W, (unsigned)WIN_H,
                          (unsigned)DefaultDepth(g_dpy, g_screen));

    editor_init();

    /* command-line file */
    if (argc > 1) {
        const char *fn = argv[1];
        size_t ln = strlen(fn);
        if (ln > 4 && strcmp(fn+ln-4, ".bsp") == 0) bsp_load(fn);
        else                                          map_load(fn);
        XStoreName(g_dpy, g_win, fn);
    }

    Atom wm_del = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &wm_del, 1);

    int dirty = 1;
    XEvent ev;
    struct timespec ts = { 0, 14000000 }; /* ~70 fps */

    while (g_running) {
        while (XPending(g_dpy)) {
            XNextEvent(g_dpy, &ev);
            switch (ev.type) {
            case Expose:
                dirty = 1; break;
            case ConfigureNotify:
                if (ev.xconfigure.width  != g_win_w ||
                    ev.xconfigure.height != g_win_h) {
                    g_win_w = ev.xconfigure.width;
                    g_win_h = ev.xconfigure.height;
                    XFreePixmap(g_dpy, g_buf);
                    g_buf = XCreatePixmap(g_dpy, g_win,
                                (unsigned)g_win_w, (unsigned)g_win_h,
                                (unsigned)DefaultDepth(g_dpy, g_screen));
                    dirty = 1;
                } break;
            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                on_key_file(ks, ev.xkey.state);
                dirty = 1;
                break; }
            case ButtonPress:
                on_button_press(&ev.xbutton); dirty=1; break;
            case ButtonRelease:
                on_button_release(&ev.xbutton); dirty=1; break;
            case MotionNotify:
                on_motion(&ev.xmotion); dirty=1; break;
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_del)
                    g_running = 0;
                break;
            }
        }
        if (dirty) { render_all(); dirty=0; }
        nanosleep(&ts, NULL);
    }

    bsp_free();
    XFreePixmap(g_dpy, g_buf);
    if (g_font) XFreeFont(g_dpy, g_font);
    XFreeGC(g_dpy, g_gc);
    XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
    return 0;
}
