#ifndef SFX_H
#define SFX_H

#include "langext.h"
#include "vec3.h"

#define VRAM_SIZE (1 << 10)

static int sfx_offsets[] = {
    -6,
    -5,
    -4,
    -3,
    -2,
    -1,
    1,
    2,
    3,
    4,
    5,
    6,
};

static int sfx_offsets_up[] = {
    -6,
    -5,
    -4,
    -3,
    -2,
    -1,
    1,
    2,
    3,
    4,
    5,
    6,
};

#define SFX_DIMENSIONS (countof(sfx_offsets) + countof(sfx_offsets_up))

structure(SfxGraphNode){
    int16 position[SFX_DIMENSIONS];
    int neighbours[0x10];
    int next;
    struct{
        int n_sample;
        int color;
    } sample[8];
    unsigned hash;
};

structure(SfxGraph){
    int n_search_inference;
    int n_candidates_inference;
    int n_neighbours;
    int n_difference_threshold;

    int n_nodes;

    int weights[SFX_DIMENSIONS];
    int weights_inverse[SFX_DIMENSIONS];
    SfxGraphNode* entry;
};

extern SfxGraph graph_sfx[];
extern unsigned g_vram[];

void generateSfxInit(int16* data,int* indices,int mipmap,int image_size);
int mipmapOffsetSfx(int size,int mipmap);
void generateMipmapsSfx(int16* data,int length);
void extractImageColorSfx(int* data,int size,int mipmap,int offset,int amount);

#endif