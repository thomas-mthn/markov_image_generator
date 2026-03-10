#ifdef __wasm__
#include "main.h"
#include "memory.h"

#define TRAIN_SIZE 1024
#define GENERATE_SIZE 2048

int g_train[TRAIN_SIZE * TRAIN_SIZE * 2];
int g_generate[GENERATE_SIZE * GENERATE_SIZE * 2];
int g_indices[GENERATE_SIZE * GENERATE_SIZE];

void wasmTrainInit(int width,int height,int quality){
	for(int i = 0;i < 0x10;i++){
        graph_color[i].entry = tMalloc(sizeof(GraphNode) * 0x40000 >> i * 2);
        for(int j = 0;j < countof(weights);j++){
            for(int c = 0;c < 3;c++)
                graph_color[i].weights_inverse[j * 3 + c] = fixedDivR(FIXED_ONE,weights[j]) >> 4;
        }
        graph_color[i].n_candidates_inference = 0x100;
        graph_color[i].n_search_inference = quality;
        graph_color[i].n_difference_threshold = 0x100;
        graph_color[i].n_neighbours = 0x10;
    }
	Texture texture = {.data = g_train,.width = width,.height = height};
	generateMipmaps(&texture);
}

void wasmTrain(int width,int height,int mipmap,int offset,int amount){
	extractImageColor(g_train,width,height,mipmap,offset,amount);
}

void wasmGenerateInit(int quality,int size,int mipmap){
    for(int i = 0;i < 0x10;i++){
        graph_color[i].n_candidates_inference = 0x100;
        graph_color[i].n_search_inference = quality;
    }
    generateColorInit(g_generate,mipmap,size,size);
}

void wasmGenerateMipmapInit(int width,int height,int mipmap){
    generateColorInitLayer(g_generate,g_indices,mipmap,width,height);
}

void wasmGenerate(int size,int mipmap,int offset,int amount){
    generateColor(g_generate,g_indices,offset,amount,mipmap,size,size,false);
}

void wasmInpaintInit(int quality,int width,int height,int mipmap){
    generateMipmaps(&(Texture){.width = width,.height = height,.data = g_generate});
    for(int i = 0;i < 0x10;i++){
        graph_color[i].n_candidates_inference = 0x100;
        graph_color[i].n_search_inference = quality;
    }
    generateColorInit(g_generate,mipmap,width,height);
}

void wasmInpaint(int width,int height,int mipmap,int offset,int amount){
    generateColor(g_generate,g_indices,offset,amount,mipmap,width,height,true);
}

void wasmRemixInit(int quality,int size){
    for(int i = 0;i < 0x10;i++){
        graph_color[i].n_candidates_inference = 0x100;
        graph_color[i].n_search_inference = quality;
    }
    Texture texture = {.data = g_generate,.width = size,.height = size};
    generateMipmaps(&texture);
}

#endif