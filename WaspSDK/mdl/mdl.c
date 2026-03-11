#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#pragma pack(push, 1)
typedef struct { float x, y, z; } vec3_t;

typedef struct {
    char     id[4];
    uint32_t version;
    vec3_t   scale;
    vec3_t   origin;
    float    radius;
    vec3_t   offsets;
    uint32_t num_skins;
    uint32_t skin_width;
    uint32_t skin_height;
    uint32_t num_verts;
    uint32_t num_triangles;
    uint32_t num_frames;
    uint32_t sync_type;
    uint32_t flags;
    float    size;
} mdl_header_t;

typedef struct { uint32_t on_seam, s, t; } st_vert_t;
typedef struct { uint32_t front, vertex[3]; } triangle_t;
typedef struct { uint8_t v[3], normal; } vert_t;

typedef struct {
    uint32_t type;
    vert_t   min, max;
    char     name[16];
} frame_t;
#pragma pack(pop)

void write_mdl(const char *filename, float *v_data, int num_v, uint32_t *f_data, int num_f, float *uv_data) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;

    mdl_header_t header = {0};
    memcpy(header.id, "IDPO", 4);
    header.version = 6;
    header.num_verts = num_v;
    header.num_triangles = num_f;
    header.num_frames = 1;
    header.num_skins = 1;
    header.skin_width = 256;
    header.skin_height = 256;

    float min[3] = {1e9, 1e9, 1e9}, max[3] = {-1e9, -1e9, -1e9};
    for (int i = 0; i < num_v * 3; i += 3) {
        for (int j = 0; j < 3; j++) {
            if (v_data[i+j] < min[j]) min[j] = v_data[i+j];
            if (v_data[i+j] > max[j]) max[j] = v_data[i+j];
        }
    }

    header.origin.x = min[0]; header.origin.y = min[1]; header.origin.z = min[2];
    header.scale.x = (max[0] - min[0]) / 255.0f;
    header.scale.y = (max[1] - min[1]) / 255.0f;
    header.scale.z = (max[2] - min[2]) / 255.0f;

    fwrite(&header, sizeof(mdl_header_t), 1, f);

    uint32_t skin_type = 0;
    fwrite(&skin_type, 4, 1, f);
    uint8_t *dummy_skin = calloc(1, header.skin_width * header.skin_height);
    fwrite(dummy_skin, 1, header.skin_width * header.skin_height, f);
    free(dummy_skin);

    for (int i = 0; i < num_v; i++) {
        st_vert_t st = { 0, (uint32_t)(uv_data[i*2] * header.skin_width), 
                            (uint32_t)((1.0f - uv_data[i*2+1]) * header.skin_height) };
        fwrite(&st, sizeof(st_vert_t), 1, f);
    }

    for (int i = 0; i < num_f; i++) {
        triangle_t tri = { 1, { f_data[i*3], f_data[i*3+1], f_data[i*3+2] } };
        fwrite(&tri, sizeof(triangle_t), 1, f);
    }

    uint32_t frame_type = 0;
    fwrite(&frame_type, 4, 1, f);
    vert_t frame_info[2] = {0}; 
    fwrite(frame_info, sizeof(vert_t), 2, f); 
    char frame_name[16] = "frame1";
    fwrite(frame_name, 1, 16, f);

    for (int i = 0; i < num_v; i++) {
        vert_t v;
        v.v[0] = (uint8_t)((v_data[i*3] - header.origin.x) / (header.scale.x ? header.scale.x : 1));
        v.v[1] = (uint8_t)((v_data[i*3+1] - header.origin.y) / (header.scale.y ? header.scale.y : 1));
        v.v[2] = (uint8_t)((v_data[i*3+2] - header.origin.z) / (header.scale.z ? header.scale.z : 1));
        v.normal = 0;
        fwrite(&v, sizeof(vert_t), 1, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <file.mdl|file.obj>\n", argv[0]);
        return 1;
    }

    char *ext = strrchr(argv[1], '.');
    if (ext && strcmp(ext, ".obj") == 0) {
        // --- OBJ TO MDL ---
        FILE *f = fopen(argv[1], "r");
        float *v = malloc(sizeof(float)*3000), *vt = malloc(sizeof(float)*2000);
        uint32_t *idx = malloc(sizeof(uint32_t)*6000);
        int vc=0, vtc=0, ic=0;
        char line[256];
        while(fgets(line, 256, f)) {
            if(line[0]=='v' && line[1]==' ') { sscanf(line, "v %f %f %f", &v[vc*3], &v[vc*3+1], &v[vc*3+2]); vc++; }
            if(line[0]=='v' && line[1]=='t') { sscanf(line, "vt %f %f", &vt[vtc*2], &vt[vtc*2+1]); vtc++; }
            if(line[0]=='f') { 
                sscanf(line, "f %u/%*u %u/%*u %u/%*u", &idx[ic*3], &idx[ic*3+1], &idx[ic*3+2]);
                idx[ic*3]--; idx[ic*3+1]--; idx[ic*3+2]--; ic++; 
            }
        }
        char out[256]; snprintf(out, 256, "output.mdl");
        write_mdl(out, v, vc, idx, ic, vt);
        fclose(f);
    } else {
        // --- MDL TO OBJ (Existing Logic) ---
        FILE *f = fopen(argv[1], "rb");
        if (!f) { perror("Error"); return 1; }
        mdl_header_t mdl;
        fread(&mdl, sizeof(mdl), 1, f);
        fseek(f, mdl.num_skins * (mdl.skin_width * mdl.skin_height + 4), SEEK_CUR); // Simplified skip
        st_vert_t *st = malloc(sizeof(st_vert_t) * mdl.num_verts);
        triangle_t *tri = malloc(sizeof(triangle_t) * mdl.num_triangles);
        fread(st, sizeof(st_vert_t), mdl.num_verts, f);
        fread(tri, sizeof(triangle_t), mdl.num_triangles, f);
        fseek(f, sizeof(frame_t), SEEK_CUR);
        vert_t *v = malloc(sizeof(vert_t) * mdl.num_verts);
        fread(v, sizeof(vert_t), mdl.num_verts, f);
        
        FILE *obj = fopen("out.obj", "w");
        for(int i=0; i<mdl.num_verts; i++) 
            fprintf(obj, "v %f %f %f\n", v[i].v[0]*mdl.scale.x+mdl.origin.x, v[i].v[1]*mdl.scale.y+mdl.origin.y, v[i].v[2]*mdl.scale.z+mdl.origin.z);
        for(int i=0; i<mdl.num_triangles; i++)
            fprintf(obj, "f %u %u %u\n", tri[i].vertex[0]+1, tri[i].vertex[2]+1, tri[i].vertex[1]+1);
        fclose(obj); fclose(f);
    }
    return 0;
}
