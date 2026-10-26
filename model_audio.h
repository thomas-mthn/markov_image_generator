#ifndef MODEL_AUDIO_H
#define MODEL_AUDIO_H

#include "langext.h"
#include "vec3.h"

#define DIMENSIONS_AUDIO 0x10

structure(GraphAudioNode){
    int16 position[DIMENSIONS_AUDIO];
    int neighbours[0x10];
    int next;
    struct{
        int n_sample;
        int color;
    } sample[8];
    unsigned hash;
};

structure(GraphAudio){
    int n_search_inference;
    int n_candidates_inference;
    int n_neighbours;
    int n_difference_threshold;

    int n_nodes;

    int weights[DIMENSIONS_AUDIO];
    int weights_inverse[DIMENSIONS_AUDIO];
    GraphAudioNode* entry;

    int color_randindex[0x100];
    int n_color_distribution[0x100];
    int n_color_randrange;
};

extern GraphAudio graph_audio[];

void audioMipmaps(int* data,int size);
void audioMipmaps16(int16* data,int size);
void audioModelInit(void);
void audioInitGenerate(int* data,int mipmap,int size);
void audioInitLayerGenerate(int* data,int* indices,int mipmap,int size);
void audioGenerate(int* data,int* indices,int offset,int amount,int mipmap,int size,bool inpaint,bool brute_force);
void audioExtract(int16* data,int size,int mipmap,int offset,int amount);
#endif
