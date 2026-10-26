#ifndef MAIN_H
#define MAIN_H

#include "langext.h"
#include "vec3.h"

#define VRAM_SIZE (1 << 10)

static int offsets[][2] = {
    {-1, 0},
    { 1, 0},
    { 0,-1},
    { 0, 1},
    
    {-1,-1},    
    {-1, 1},
    { 1,-1},
    { 1, 1},

    {-2, 0},
    { 2, 0},
    { 0,-2},
    { 0, 2},
};

static int offsets_up[][2] = {
    { 0, 0},
    { 1, 0},
    { 0, 1},
    { -1, 0},
    /*
    {-1,-1},    
    {-1, 1},
    { 1,-1},
    { 1, 1},
    */
};

static int weights[] = {
    FIXED_ONE / 2,FIXED_ONE / 2,FIXED_ONE / 2,FIXED_ONE / 2,
    FIXED_ONE - FIXED_ONE / 10 * 2,FIXED_ONE - FIXED_ONE / 10 * 2,FIXED_ONE - FIXED_ONE / 10 * 2,FIXED_ONE - FIXED_ONE / 10 * 2,
    FIXED_ONE,FIXED_ONE,FIXED_ONE,FIXED_ONE,
    FIXED_ONE / 4,FIXED_ONE,FIXED_ONE,FIXED_ONE,
    /*
    FIXED_ONE * 8,FIXED_ONE * 8,FIXED_ONE * 8,FIXED_ONE * 8,
    /*
    FIXED_ONE / 2,FIXED_ONE / 2,FIXED_ONE / 2,FIXED_ONE / 2,
    FIXED_ONE - FIXED_ONE / 10 * 2,FIXED_ONE - FIXED_ONE / 10 * 2,FIXED_ONE - FIXED_ONE / 10 * 2,FIXED_ONE - FIXED_ONE / 10 * 2,
    */
};

#define DIMENSIONS ((((countof(offsets) + countof(offsets_up)) * 3) + 3) & ~3)
#define COLOR_RANGE 0x100

structure(Texture){
    int width;
    int height;
    int* data;
};

structure(GraphNode){
    uint8 position[DIMENSIONS];
    int neighbours[0x10];
    int next;
    struct{
        int n_sample;
        int color;
    } sample[8];
    unsigned hash;
};

structure(Graph){
    int n_search_inference;
    int n_candidates_inference;
    int n_neighbours;
    int n_difference_threshold;

    int n_nodes;

    int weights[DIMENSIONS];
    int weights_inverse[DIMENSIONS];
    GraphNode* entry;

    int color_randindex[4096];
    int n_color_distribution[4096];
    int n_color_randrange;
};

extern Graph graph_color[];
extern unsigned g_vram[];
extern int n_threads;

static int colorToPixelColor(Vec3 color){
	return tClamp(color.x >> 12,0,0xFF) | tClamp(color.y >> 12,0,0xFF) << 8 | tClamp(color.z >> 12,0,0xFF) << 16;
}

static Vec3 pixelColorToColor(int color){
	return (Vec3){(color >> 0 & 0xFF) << 12,(color >> 8 & 0xFF) << 12,(color >> 16 & 0xFF) << 12};
}

void generateColorInit(int* data,int mipmap,int width,int height);
void generateColorInitLayer(int* data,int* indices,int mipmap,int width,int height);
void generateColor(int* data,int* indices,int offset,int amount,int mipmap,int width,int height,bool inpaint,bool brute_force);
void generateMipmaps(Texture* texture);
void extractImageColor(int* data,int width,int height,int mipmap,int offset,int amount);

int mipmapOffset(int width,int height,int mipmap);
void hueShift(int* red,int* green,int* blue,int shift);

#endif
