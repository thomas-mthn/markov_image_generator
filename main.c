#include "langext.h"
#include "main.h"
#include "tmath.h"
#include "vec3.h"

#ifdef __wasm__
#include <wasm_simd128.h>
#else
#include <immintrin.h>
#endif

#ifdef _WIN32
#include "kernel.h"
#endif

#if defined(_MSC_VER) && !defined(__POCC__)
int _fltused = 0;
#endif

static void shuffle(int* arr,int size){
    for(int i = size - 1;i > 0;i--){
        int j = tRnd() % (i + 1);

        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

static int bias(int value,int bias){
    return fixedDivR(value,(fixedMulR((fixedDivR(FIXED_ONE,bias) - FIXED_ONE * 2),(FIXED_ONE - value)) + FIXED_ONE));
}

void generateMipmaps(Texture* texture){
	int offset = 0;
    int size_width = texture->width;
    int size_height = texture->height;
	for(;size_width && size_height;){
		int mipmap_offset = size_width * size_height;
		for(int i = 0;i < (size_width >> 1) * (size_height >> 1);i++){
			int x = i % (size_width >> 1);
			int y = i / (size_width >> 1);
			int index = offset + mipmap_offset + x * (size_width >> 1) + y;

			Vec3 color = {0};

			vec3Add(&color,pixelColorToColor(texture->data[offset + (x * 2 + 0) * size_width + (y * 2 + 0)]));
			vec3Add(&color,pixelColorToColor(texture->data[offset + (x * 2 + 0) * size_width + (y * 2 + 1)]));
			vec3Add(&color,pixelColorToColor(texture->data[offset + (x * 2 + 1) * size_width + (y * 2 + 0)]));
			vec3Add(&color,pixelColorToColor(texture->data[offset + (x * 2 + 1) * size_width + (y * 2 + 1)]));

			vec3Shr(&color,2);

            int child[] = {
                (texture->data[offset + (x * 2 + 0) * size_width + (y * 2 + 0)] >> 24),
                (texture->data[offset + (x * 2 + 0) * size_width + (y * 2 + 1)] >> 24),
                (texture->data[offset + (x * 2 + 1) * size_width + (y * 2 + 0)] >> 24),
                (texture->data[offset + (x * 2 + 1) * size_width + (y * 2 + 1)] >> 24)
            };

			int alpha = child[0] & child[1] & child[2] & child[3];

			texture->data[index] = colorToPixelColor(color);
			texture->data[index] |= alpha << 24;
		}
		offset += mipmap_offset;
        size_width >>= 1;
        size_height >>= 1;
	}
}

structure(RGB){
    int red;
    int green;
    int blue;
};

structure(HSL){
    int hue;
    int saturation;
    int luminance;
};

static HSL rgb_to_hsv(int r,int g,int b){
    int max = tMax(tMax(r,g),b);
    int min = tMin(tMin(r,g),b);
    int delta = max - min;

    HSL hsl;
    if(delta == 0) 
        hsl.hue = 0;
    else if(max == r) 
        hsl.hue = fixedDivR(g - b,delta) + (g < b ? FIXED_ONE * 6 : 0);
    else if(max == g)
        hsl.hue = fixedDivR(b - r,delta) + FIXED_ONE * 2;
    else 
        hsl.hue = fixedDivR(r - g,delta) + FIXED_ONE * 4;

    fixedMul(&hsl.hue,FIXED_ONE * 60);
    if(hsl.hue < 0) 
        hsl.hue += FIXED_ONE * 360;

    hsl.saturation = max == 0 ? 0 : fixedDivR(delta,max);

    hsl.luminance = max;

    return hsl;
}

static RGB hsv_to_rgb(HSL hsl){
    int c = fixedMulR(hsl.luminance,hsl.saturation);
    int x = fixedMulR(c,(FIXED_ONE - tAbs((fixedDivR(hsl.hue,FIXED_ONE * 60) & FIXED_ONE * 2 - 1) - FIXED_ONE)));
    int m = hsl.luminance - c;

    int fr, fg, fb;
    if(hsl.hue < FIXED_ONE * 60){
        fr = c; fg = x; fb = 0; 
    }
    else if(hsl.hue < FIXED_ONE * 120){ 
        fr = x; fg = c; fb = 0; 
    }
    else if(hsl.hue < FIXED_ONE * 180){ 
        fr = 0; fg = c; fb = x; 
    }
    else if(hsl.hue < FIXED_ONE * 240){ 
        fr = 0; fg = x; fb = c; 
    }
    else if(hsl.hue < FIXED_ONE * 300){ 
        fr = x; fg = 0; fb = c; 
    }
    else{ 
        fr = c; fg = 0; fb = x; 
    }

    RGB rgb;

    rgb.red   = fr + m;
    rgb.green = fg + m;
    rgb.blue  = fb + m;

    return rgb;
}

void hueShift(int* red,int* green,int* blue,int shift){
    HSL hsl = rgb_to_hsv(*red,*green,*blue);
    hsl.hue += shift;
    if(hsl.hue >= FIXED_ONE * 360)
        hsl.hue -= FIXED_ONE * 360;
    if(hsl.hue < 0)
        hsl.hue += FIXED_ONE * 360;
    RGB rgb = hsv_to_rgb(hsl);
    *red   = rgb.red;
    *green = rgb.green;
    *blue  = rgb.blue;
}

#ifdef __wasm__
#include <wasm_simd128.h>
#endif

static int getProbabilityProbabilityDistance(Graph* graph,GraphNode* probability_1,GraphNode* probability_2){
#ifdef __wasm__
    int distance = 0;
    
    for(int k = 0;k < DIMENSIONS;k++){
        int rel = (probability_1->position[k]) - (probability_2->position[k]);
        distance += tAbs(rel) * graph->weights_inverse[k] >> 8;
    }
    return distance;
    
#else
    __m128i acc = _mm_setzero_si128();
    for(int k = 0;k < DIMENSIONS;k += 4){
        __m128i prob1 = _mm_cvtepu8_epi32(_mm_loadu_si128((__m128i*)(probability_1->position + k)));
        __m128i prob2 = _mm_cvtepu8_epi32(_mm_loadu_si128((__m128i*)(probability_2->position + k)));
        __m128i rel = _mm_abs_epi32(_mm_sub_epi32(prob1,prob2));
        rel = _mm_srai_epi32(_mm_mullo_epi32(rel,_mm_loadu_si128((__m128i*)(graph->weights_inverse + k))),8); 
        acc = _mm_add_epi32(acc,rel);
    }
    __m128i sum1 = _mm_hadd_epi32(acc, acc); 
    __m128i sum2 = _mm_hadd_epi32(sum1, sum1); 

    int distance = _mm_cvtsi128_si32(sum2);
    return distance;
#endif
}

static int getProbabilityDistance(Graph* graph,GraphNode* probability,uint8* position){
#ifdef __wasm__
    int distance = 0;
    for(int k = 0;k < DIMENSIONS;k++){
        int rel = (position[k]) - (probability->position[k]);
        distance += tAbs(rel) * graph->weights_inverse[k] >> 8;
    }
    return distance;
#else
    __m128i acc = _mm_setzero_si128();
    for(int k = 0;k < DIMENSIONS;k += 4){
        __m128i prob = _mm_cvtepu8_epi32(_mm_loadu_si128((__m128i*)(probability->position + k)));
        __m128i positions = _mm_cvtepu8_epi32(_mm_loadu_si128((__m128i*)(position + k)));
        __m128i rel = _mm_abs_epi32(_mm_sub_epi32(positions,prob));
        rel = _mm_srai_epi32(_mm_mullo_epi32(rel,_mm_loadu_si128((__m128i*)(graph->weights_inverse + k))),8);
        acc = _mm_add_epi32(acc,rel);
    }
    __m128i sum1 = _mm_hadd_epi32(acc, acc); 
    __m128i sum2 = _mm_hadd_epi32(sum1, sum1);

    int distance = _mm_cvtsi128_si32(sum2);
    return distance;
#endif
}

static bool probabilityCompare(Graph* graph,GraphNode* node,uint8* position){
    int difference = getProbabilityDistance(graph,node,position);
    return difference <= graph->n_difference_threshold;
}

static GraphNode* graphGet(Graph* graph,GraphNode* node,uint8* position){
    if(!node)
        return 0;
    for(;;){
        int distance = getProbabilityDistance(graph,node,position);

        int min_distance = INT_MAX;
        int min_index = -1;

        for(int i = 0;i < countof(node->neighbours);i++){
            if(node->neighbours[i] == -1)
                continue;
            int neighbour_distance = getProbabilityDistance(graph,graph->entry + node->neighbours[i],position);
            if(neighbour_distance < min_distance){
                min_distance = neighbour_distance;
                min_index = i;
            }
        }
        if(min_distance < distance && min_index != -1)
            node = graph->entry + node->neighbours[min_index];
        else
            return node;
    }
}

structure(Candidate){
    int distance;
    GraphNode* node;
};

structure(Heap){
	int amount;
    Candidate candidates[0xFF];
};

static void heapInsert(Heap* heap,Candidate value){
	int index;
	if(heap->amount == countof(heap->candidates)){
        int max_index = (countof(heap->candidates) + 1) / 2 - 1;
        for(int i = max_index + 1;i < heap->amount;i++){
            if(heap->candidates[i].distance > heap->candidates[max_index].distance)
                max_index = i;
        }
        index = max_index;
        if(heap->candidates[index].distance < value.distance)
            return;
    }
	else{
        index = heap->amount;
        heap->amount += 1;  
    }
    heap->candidates[index] = value;
	while(index > 0 && heap->candidates[(index - 1) / 2].distance > heap->candidates[index].distance){
		Candidate temp = heap->candidates[(index - 1) / 2];
		heap->candidates[(index - 1) / 2] = heap->candidates[index];
		heap->candidates[index] = temp;
		index = (index - 1) / 2;
    }   
}

static Candidate heapGet(Heap* heap){
	if(!heap->amount){
        Candidate candidate = {0};
		return candidate;
	}
	Candidate result = heap->candidates[0];
	heap->amount -= 1;
	heap->candidates[0] = heap->candidates[heap->amount];

	int index = 0;
    while(index < heap->amount){
        int max_index = index;
        int left = index * 2 + 1;
        int right = index * 2 + 2;

        if(left < heap->amount && heap->candidates[left].distance < heap->candidates[max_index].distance)
            max_index = left;
        if(right < heap->amount && heap->candidates[right].distance < heap->candidates[max_index].distance)
            max_index = right;
        if(max_index == index) 
            break; 
		
		Candidate temp = heap->candidates[index];
		heap->candidates[index] = heap->candidates[max_index];
		heap->candidates[max_index] = temp;
        index = max_index; 
    }
	return result;
}

static bool candidateInsert(Candidate* candidate_list,int n_candidate,Candidate candidate){
    for(int j = 0;j < n_candidate;j++){
        if(candidate_list[j].node == candidate.node)
            return false;
        if(!candidate_list[j].node || candidate_list[j].distance > candidate.distance){
            for(int k = n_candidate - 2;k >= j;k--)
                candidate_list[k + 1] = candidate_list[k];
            candidate_list[j] = candidate;
            return true;
        }
    }
    return false;
}

static void neighboursGet(Graph* graph,GraphNode* node,Candidate* candidate_list,int n_candidate,int n_search,uint8* position){
    if(!node || !graph->n_nodes)
        return;
    candidate_list[0].node = node;
    candidate_list[0].distance = getProbabilityDistance(graph,node,position);

    Heap heap = {0};

    GraphNode* visited_table[0x400] = {0};

    for(int counter = 0;counter < n_search;counter++){
        for(int i = 0;i < countof(node->neighbours);i++){
            if(node->neighbours[i] == -1)
                continue;
            
            if(visited_table[graph->entry[node->neighbours[i]].hash % countof(visited_table)] == graph->entry + node->neighbours[i])
                continue;
            
            int distance = getProbabilityDistance(graph,graph->entry + node->neighbours[i],position);
            Candidate candidate = {
                .distance = distance,
                .node = graph->entry + node->neighbours[i],
            };
            visited_table[candidate.node->hash % countof(visited_table)] = candidate.node;

            heapInsert(&heap,candidate);
        }
        Candidate candidate = heapGet(&heap);
        if(!candidate.node)
            break;
        candidateInsert(candidate_list,n_candidate,candidate);
        node = candidate.node;
    }
    for(;;){
        Candidate candidate = heapGet(&heap);
        if(!candidate.node)
            return;
        candidateInsert(candidate_list,n_candidate,candidate);
    }
}

static GraphNode* graphInference(Graph* graph,uint8* position){
    GraphNode* node = graphGet(graph,graph->entry,position);
    Candidate candidate_list[0x100] = {0};
    neighboursGet(graph,node,candidate_list,graph->n_candidates_inference,graph->n_search_inference,position);
    node = candidate_list[0].node;
    return node;
}

static void disconnectConnection(Graph* graph,GraphNode* node,GraphNode* neighbour){
    for(int m = 0;m < countof(node->neighbours);m++){
        if(neighbour->neighbours[m] == (int)(node - graph->entry)){
            neighbour->neighbours[m] = -1;
            return;
        }
    }
}

static int getNumberOfNeighbours(GraphNode* node){
    int n = 0;
    for(int i = 0;i < countof(node->neighbours);i++){
        if(node->neighbours[i] == -1)
            continue;
        n += 1;
    }
    return n;
}

static void connectToGraph(Graph* graph,GraphNode* node,uint8* position){
    GraphNode* near_node = graphGet(graph,graph->entry,position);
        
    Candidate candidate_list[0x100];

    for(int i = 0;i < graph->n_candidates_inference;i++){
        candidate_list[i].node = 0;
        candidate_list[i].distance = 0;
    }

    neighboursGet(graph,near_node,candidate_list,graph->n_candidates_inference,graph->n_search_inference,position);

    int neighbour_index = 0;

    for(int i = 0;i < graph->n_candidates_inference;i++){
        if(!candidate_list[i].node)
            break;
        bool reject = false;

        for(int j = 0;j < countof(node->neighbours);j++){
            if(node->neighbours[j] == -1)
                continue;
            
            int neighbour_distance = fixedMulR(getProbabilityProbabilityDistance(graph,candidate_list[i].node,graph->entry + node->neighbours[j]),FIXED_ONE + FIXED_ONE / 32 * 12);
            
            if(neighbour_distance < candidate_list[i].distance){
                reject = true;
                break;
            }
        }
        
        if(reject)
            continue;

        bool is_full = true;
        for(int j = 0;j < countof(node->neighbours);j++){
            if(candidate_list[i].node->neighbours[j] == -1){
                candidate_list[i].node->neighbours[j] = (int)(node - graph->entry);
                is_full = false;
                break;
            }
        }
        if(is_full){
            int max_distance = INT_MIN;
            int max_index = -1;
            for(int m = 0;m < countof(node->neighbours);m++){
                if(candidate_list[i].node->neighbours[m] == -1)
                    continue;
                int n_neighbour_neighbours = getNumberOfNeighbours(graph->entry + candidate_list[i].node->neighbours[m]);
                if(n_neighbour_neighbours == 1)
                    continue;
                int distance = getProbabilityProbabilityDistance(graph,candidate_list[i].node,graph->entry + candidate_list[i].node->neighbours[m]);
                if(distance > max_distance){
                    max_distance = distance;
                    max_index = m;
                }
            }
            if(max_index != -1 && candidate_list[i].distance < max_distance){
                disconnectConnection(graph,candidate_list[i].node,graph->entry + candidate_list[i].node->neighbours[max_index]);
                candidate_list[i].node->neighbours[max_index] = (int)(node - graph->entry);
            }
            else{
                reject = true;
            }
        }

        if(reject)
            continue;

        node->neighbours[neighbour_index] = (int)(candidate_list[i].node - graph->entry);

        neighbour_index += 1;
        if(neighbour_index >= countof(node->neighbours))
            break;
    }
}

static void setGraph(Graph* graph,GraphNode* node,uint8* position){
    if(graph->n_nodes == 1)
        return;
    connectToGraph(graph,node,position);
}

Graph graph_color[0x10];
unsigned g_vram[VRAM_SIZE * VRAM_SIZE];

#define MARGIN 4

void extractImageColor(int* data,int width,int height,int mipmap,int offset,int amount){
    int mipmap_offset = mipmapOffset(width,height,mipmap);
    int mipmap_width = width >> mipmap;
    int mipmap_height = height >> mipmap;

    for(int i = 0;i < amount;i++){
        int x = (offset + i) / mipmap_width;
        int y = (offset + i) % mipmap_height;

        int color = data[mipmap_offset + x * mipmap_width + y];
        int d_index = (color >> 4 & 0xF) << 0 | (color >> 12 & 0xF) << 4 | (color >> 20 & 0xF) << 8;

        graph_color[mipmap].n_color_distribution[d_index] += 1;

        if(x < MARGIN || x >= mipmap_height - MARGIN || y < MARGIN || y >= mipmap_width - MARGIN)
            continue;
        uint8 position[DIMENSIONS] = {0};

        for(int k = 0;k < countof(offsets);k++){
            int color = data[mipmap_offset + (x + offsets[k][0]) * mipmap_width + (y + offsets[k][1])];
            for(int c = 0;c < 3;c++)
                position[k * 3 + c] = (color >> c * 8 & 0xFF);
        }
        for(int k = 0;k < countof(offsets_up);k++){
            int color = data[mipmap_offset + (x / 2 + offsets_up[k][0]) * (mipmap_width / 2) + (y / 2 + offsets_up[k][1]) + mipmap_width * mipmap_height];
            for(int c = 0;c < 3;c++)
                position[countof(offsets) * 3 + k * 3 + c] = (color >> c * 8 & 0xFF);
        }
        GraphNode* graph_node = graphInference(graph_color + mipmap,position);
        if(!graph_node || !probabilityCompare(graph_color + mipmap,graph_node,position)){
            GraphNode* graph_node = graph_color[mipmap].entry + graph_color[mipmap].n_nodes++;
            for(int j = 0;j < DIMENSIONS;j++)
                graph_node->position[j] = position[j];

            graph_node->sample[0].color = data[mipmap_offset + x * mipmap_width + y];
            graph_node->sample[0].n_sample = 1;
            graph_node->hash = tRnd();

            for(int j = 0;j < countof(graph_node->neighbours);j++)
                graph_node->neighbours[j] = -1;

            setGraph(graph_color + mipmap,graph_node,position);
        }
        else{
            int color = data[mipmap_offset + x * mipmap_width + y];
            for(int j = 0;j < countof(graph_node->sample);j++){
                if(graph_node->sample[j].color == color || !graph_node->sample[j].n_sample){
                    graph_node->sample[j].color = color;
                    graph_node->sample[j].n_sample += 1;
                    break;
                }
            }
        }
    }
    int index = 0; 
    for(int i = 0;i < countof(graph_color[mipmap].n_color_distribution);i++){ 
        index += graph_color[mipmap].n_color_distribution[i]; 
        graph_color[mipmap].color_randindex[i] = index;
    } 
    graph_color[mipmap].n_color_randrange = index;
}

#if defined(__wasm__) || defined(__linux__)
int n_threads = 1;
#else
int n_threads;
#endif

structure(PixelFillThreadInfo){
    int* indices;
    int offset;
    int mipmap;
    int width;
    int height;
    Texture texture;
    int* data;  
    int amount;
    bool inpaint;
    bool brute_force;
};

static unsigned stdcall fillPixelsColor(void* arg){
    PixelFillThreadInfo* info = arg;

    int mipmap_offset = mipmapOffset(info->width,info->height,info->mipmap);
    int mipmap_width = info->width >> info->mipmap;
    int mipmap_size = mipmap_width * (info->height >> info->mipmap);

    Graph* graph = graph_color + info->mipmap;

    for(int i = 0;i < info->amount;i++){
        int layer_weight = (info->offset + i << 8) / ((mipmap_size)) << 8;

        int x = info->indices[i] / mipmap_width;
        int y = info->indices[i] % mipmap_width;

        if(info->texture.data && x < info->texture.height >> info->mipmap && y < info->texture.width >> info->mipmap)
            continue;

        if(info->inpaint && (info->data[mipmap_offset + x * mipmap_width + y] >> 24 & 0x01))
            continue;

        int luminance[3] = {0};
        GraphNode* graph_node;

        uint8 position[DIMENSIONS];
        for(int j = 0;j < DIMENSIONS;j++)
            position[j] = 0;

        for(int k = 0;k < countof(offsets);k++){
            int offset_x = (unsigned)(x + offsets[k][0]) % mipmap_width;
            int offset_y = (unsigned)(y + offsets[k][1]) % mipmap_width;

            int color = info->data[mipmap_offset + offset_x * mipmap_width + offset_y];

            if(color >> 24 & 0x02){
                for(int c = 0;c < 3;c++)
                    graph->weights_inverse[k * 3 + c] = 0;
            }
            else{
                for(int c = 0;c < 3;c++)
                    graph->weights_inverse[k * 3 + c] = fixedDivR(FIXED_ONE,weights[k]) >> 4;
            }
            for(int c = 0;c < 3;c++)
                position[k * 3 + c] = (color >> c * 8 & 0xFF);
        }
        for(int k = 0;k < countof(offsets_up);k++){
            int offset_x = (unsigned)(x / 2 + offsets_up[k][0]) % (mipmap_width / 2);
            int offset_y = (unsigned)(y / 2 + offsets_up[k][1]) % (mipmap_width / 2);

            int color = info->data[mipmap_offset + offset_x * (mipmap_width / 2) + offset_y + mipmap_size];

            for(int c = 0;c < 3;c++)
                position[countof(offsets) * 3 + k * 3 + c] = (color >> c * 8 & 0xFF);
        }

        int min_score = INT_MAX;
        int min_index;

        if(info->brute_force){
            int m_score = INT_MAX;
            graph_node = 0;

            for(int i = 0;i < graph->n_nodes;i++){
                GraphNode* node = graph->entry + i;
                int score = getProbabilityDistance(graph,node,position);
                if(score < m_score){
                    graph_node = node;
                    m_score = score;
                }
            }
        }
        else{
            graph_node = graphInference(graph,position);
        }
        
        if(!graph_node)
            continue;

        int n_sample = 0;

        for(int j = 0;j < countof(graph_node->sample);j++)
            n_sample += graph_node->sample[j].n_sample;

        if(!n_sample){
            info->data[mipmap_offset + x * mipmap_width + y] = 0x00FF00;
            continue;
        }

        for(int j = 0;j < countof(graph_node->sample);j++){
            if(tRnd() % n_sample < graph_node->sample[j].n_sample){
                int red   = (graph_node->sample[j].color >> 0  & 0xFF) << 16;
                int green = (graph_node->sample[j].color >> 8  & 0xFF) << 16;
                int blue  = (graph_node->sample[j].color >> 16 & 0xFF) << 16;
                //hueShift(&red,&green,&blue,-hue_shift);
                info->data[mipmap_offset + x * mipmap_width + y] = 0;
                info->data[mipmap_offset + x * mipmap_width + y] |= red   >> 16 << 0;
                info->data[mipmap_offset + x * mipmap_width + y] |= green >> 16 << 8;
                info->data[mipmap_offset + x * mipmap_width + y] |= blue  >> 16 << 16;
                //info->data[mipmap_offset + x * mipmap_size + y] = graph_node->sample[j].color;
                break;
            }
            n_sample -= graph_node->sample[j].n_sample;
        }
    }
    return 0;
}

static int binarySearch(int* A,int n,int T){
    int L = 0;
    int R = n - 1;
    while(L < R){
        int m = (L + R) >> 1;
        if(A[m] > T)
            R = m;
        else
            L = m + 1;
    }
    return L;
}

void generateColorInit(int* data,int mipmap,int width,int height){
    int mipmap_offset = mipmapOffset(width,height,mipmap);

    for(int i = 0;i < (width >> mipmap) * (height >> mipmap);i++){
        if(data[mipmap_offset + i] >> 24 & 0x01)
            continue;
        int r = tRnd() % tMax(graph_color[mipmap].n_color_randrange,1); 
        int color_index = binarySearch(graph_color[mipmap].color_randindex,countof(graph_color[mipmap].color_randindex),r); 
        int color = (color_index >> 0 & 0xF) << 4 | (color_index >> 4 & 0xF) << 12 | (color_index >> 8 & 0xF) << 20; 
        data[mipmap_offset + i] = color;
    }
}

void generateColorInitLayer(int* data,int* indices,int mipmap,int width,int height){
    int mipmap_width = width >> mipmap;
    int mipmap_height = height >> mipmap;
    int mipmap_size = mipmap_width * mipmap_height;
    if(!mipmap_width || !mipmap_height)
        return;
    int mipmap_offset = mipmapOffset(width,height,mipmap);

    for(int i = 0;i < mipmap_size;i++){
        int x = i / mipmap_width;
        int y = i % mipmap_width;
        if(data[mipmap_offset + i] >> 24 & 0x01)
            continue;
        data[mipmap_offset + i] = data[mipmap_offset + (x / 2) * (mipmap_width / 2) + y / 2 + mipmap_size] & 0xFFFFFF;
        data[mipmap_offset + i] |= 0x02000000;
    }
    for(int i = 0;i < mipmap_size;i++)
        indices[i] = i; 
    shuffle(indices,mipmap_size);
}

void generateColor(int* data,int* indices,int offset,int amount,int mipmap,int width,int height,bool inpaint,bool brute_force){
    indices += offset;
#if defined(__wasm__) || defined(__linux__)
    PixelFillThreadInfo info = {
        .indices = indices,
        .amount = amount,
        .mipmap = mipmap,
        .width = width,
        .height = height,
        .data = data,
        .inpaint = inpaint,
        .offset = offset,
        .brute_force = brute_force,
    };
    fillPixelsColor(&info);
#else
    PixelFillThreadInfo thread_info[0x40];
    void* threads[0x40];
    for(int j = 0;j < n_threads;j++){
        thread_info[j] = (PixelFillThreadInfo){
            .mipmap = mipmap,
            .width = width,
            .height = height,
            .data = data,
            .amount = amount / n_threads,
            .indices = indices + amount / n_threads * j,
            .inpaint = inpaint,
            .offset = offset,
        };
        threads[j] = CreateThread(0,0,fillPixelsColor,thread_info + j,0,0);
    }
    
    WaitForMultipleObjects(n_threads,threads,true,-1);
    for(int j = 0;j < n_threads;j++)
        CloseHandle(threads[j]);
#endif
}

int mipmapOffset(int width,int height,int mipmap){
    int offset = 0;
    for(int i = 0;i < mipmap;i++)
        offset += (width >> i) * (height >> i);
    return offset;
}
