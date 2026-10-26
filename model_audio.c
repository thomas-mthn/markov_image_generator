#include "model_audio.h"
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

static int weights[] = {
    0x10000,0xA000,0x7000,0x4000,
    0x4000,0x7000,0x1000,0x10000,

    0x10000,0xA000,0x7000,0x4000,0x7000,0xA000,0x10000
};

static int offsets[] = {
    -4,-3,-2,-1,
    1,2,3,4
};

static int offsets_up[] = {
    -3,-2,-1,0,1,2,3
};

static void shuffle(int* arr,int size){
    for(int i = size - 1;i > 0;i--){
        int j = tRnd() % (i + 1);

        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

static int mipmapAudioOffset(int size,int mipmap){
    int offset = 0;
    for(int i = 0;i < mipmap;i++)
        offset += size >> i;
    return offset;
}

static int bias(int value,int bias){
    return fixedDivR(value,(fixedMulR((fixedDivR(FIXED_ONE,bias) - FIXED_ONE * 2),(FIXED_ONE - value)) + FIXED_ONE));
}

void audioMipmaps(int* data,int size){
    int offset = 0;
	for(;size;){
		for(int i = 0;i < size >> 1;i++){
			int x = i % (size >> 1);
			int index = offset + size + x;

            data[index] = (data[offset + x * 2 + 0] + data[offset + x * 2 + 1]) / 2;
		}
		offset += size;
        size >>= 1;
	}
}

void audioMipmaps16(int16* data,int size){
    int offset = 0;
	for(;size;){
		for(int i = 0;i < size >> 1;i++){
			int x = i % (size >> 1);
			int index = offset + size + x;

            data[index] = (data[offset + x * 2 + 0] + data[offset + x * 2 + 1]) / 2;
		}
		offset += size;
        size >>= 1;
	}
}

#ifdef __wasm__
#include <wasm_simd128.h>
#endif

static int getProbabilityProbabilityDistance(GraphAudio* graph,GraphAudioNode* probability_1,GraphAudioNode* probability_2){
#if 1
    int distance = 0;
    
    for(int k = 0;k < DIMENSIONS_AUDIO;k++){
        int rel = (probability_1->position[k]) - (probability_2->position[k]);
        distance += tAbs(rel) * graph->weights_inverse[k] >> 8;
    }
    return distance;
#else
    __m128i acc = _mm_setzero_si128();
    for(int k = 0;k < DIMENSIONS_AUDIO;k += 4){
        __m128i prob1 = _mm_cvtepi16_epi32(_mm_loadu_si128((__m128i*)(probability_1->position + k)));
        __m128i prob2 = _mm_cvtepi16_epi32(_mm_loadu_si128((__m128i*)(probability_2->position + k)));
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

static int getProbabilityDistance(GraphAudio* graph,GraphAudioNode* probability,int16* position){
#if 1
    int distance = 0;
    for(int k = 0;k < DIMENSIONS_AUDIO;k++){
        int rel = (position[k]) - (probability->position[k]);
        distance += tAbs(rel) * graph->weights_inverse[k] >> 8;
    }
    return distance;
#else
    __m128i acc = _mm_setzero_si128();
    for(int k = 0;k < DIMENSIONS_AUDIO;k += 4){
        __m128i prob = _mm_cvtepi16_epi32(_mm_loadu_si128((__m128i*)(probability->position + k)));
        __m128i positions = _mm_cvtepi16_epi32(_mm_loadu_si128((__m128i*)(position + k)));
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

static bool probabilityCompare(GraphAudio* graph,GraphAudioNode* node,int16* position){
    int difference = getProbabilityDistance(graph,node,position);
    return difference <= graph->n_difference_threshold;
}

static GraphAudioNode* graphGet(GraphAudio* graph,GraphAudioNode* node,int16* position){
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
    GraphAudioNode* node;
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

static void neighboursGet(GraphAudio* graph,GraphAudioNode* node,Candidate* candidate_list,int n_candidate,int n_search,int16* position){
    if(!node || !graph->n_nodes)
        return;
    candidate_list[0].node = node;
    candidate_list[0].distance = getProbabilityDistance(graph,node,position);

    Heap heap = {0};

    GraphAudioNode* visited_table[0x400] = {0};

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

static GraphAudioNode* graphInference(GraphAudio* graph,int16* position){
    GraphAudioNode* node = graphGet(graph,graph->entry,position);
    Candidate candidate_list[0x100] = {0};
    neighboursGet(graph,node,candidate_list,graph->n_candidates_inference,graph->n_search_inference,position);
    node = candidate_list[0].node;
    return node;
}

static void disconnectConnection(GraphAudio* graph,GraphAudioNode* node,GraphAudioNode* neighbour){
    for(int m = 0;m < countof(node->neighbours);m++){
        if(neighbour->neighbours[m] == (int)(node - graph->entry)){
            neighbour->neighbours[m] = -1;
            return;
        }
    }
}

static int getNumberOfNeighbours(GraphAudioNode* node){
    int n = 0;
    for(int i = 0;i < countof(node->neighbours);i++){
        if(node->neighbours[i] == -1)
            continue;
        n += 1;
    }
    return n;
}

static void connectToGraph(GraphAudio* graph,GraphAudioNode* node,int16* position){
    GraphAudioNode* near_node = graphGet(graph,graph->entry,position);
        
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

static void setGraph(GraphAudio* graph,GraphAudioNode* node,int16* position){
    if(graph->n_nodes == 1)
        return;
    connectToGraph(graph,node,position);
}

GraphAudio graph_audio[0x10];

#define MARGIN 8

#include "print.h"

void audioExtract(int16* data,int size,int mipmap,int offset,int amount){
    int mipmap_offset = mipmapAudioOffset(size,mipmap);
    int mipmap_width = size >> mipmap;

    for(int i = 0;i < amount;i++){
        int x = (offset + i);

        int color = data[mipmap_offset + x];
        int d_index = color >> 9;

        graph_audio[mipmap].n_color_distribution[d_index + 0x80] += 1;

        if(x < MARGIN || x >= mipmap_width - MARGIN)
            continue;
        int16 position[DIMENSIONS_AUDIO] = {0};

        for(int k = 0;k < countof(offsets);k++){
            int color = data[mipmap_offset + (x + offsets[k])];
            position[k] = color;
        }
        for(int k = 0;k < countof(offsets_up);k++){
            int color = data[mipmap_offset + (x / 2 + offsets_up[k]) + mipmap_width];
            position[countof(offsets) + k] = color;
        }
        GraphAudioNode* graph_node = graphInference(graph_audio + mipmap,position);
        if(!graph_node || !probabilityCompare(graph_audio + mipmap,graph_node,position)){
            GraphAudioNode* graph_node = graph_audio[mipmap].entry + graph_audio[mipmap].n_nodes++;
            for(int j = 0;j < DIMENSIONS_AUDIO;j++)
                graph_node->position[j] = position[j];

            graph_node->sample[0].color = data[mipmap_offset + x];
            graph_node->sample[0].n_sample = 1;
            graph_node->hash = tRnd();

            for(int j = 0;j < countof(graph_node->neighbours);j++)
                graph_node->neighbours[j] = -1;

            setGraph(graph_audio + mipmap,graph_node,position);
        }
        else{
            int color = data[mipmap_offset + x];
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
    for(int i = 0;i < countof(graph_audio[mipmap].n_color_distribution);i++){ 
        index += graph_audio[mipmap].n_color_distribution[i]; 
        graph_audio[mipmap].color_randindex[i] = index;
    } 
    graph_audio[mipmap].n_color_randrange = index;
}

structure(PixelFillThreadInfo){
    int* indices;
    int offset;
    int mipmap;
    int size;
    int* data;  
    int amount;
    bool inpaint;
    bool brute_force;
};

#include "print.h"

static unsigned stdcall fillPixelsColor(void* arg){
    PixelFillThreadInfo* info = arg;

    int mipmap_offset = mipmapAudioOffset(info->size,info->mipmap);
    int mipmap_size = info->size >> info->mipmap;

    GraphAudio* graph = graph_audio + info->mipmap;

    for(int i = 0;i < info->amount;i++){
        int x = info->indices[i];
#if 0
        if(info->texture.data && x < info->texture.height >> info->mipmap)
            continue;
#endif
        if(info->inpaint && (info->data[mipmap_offset + x] >> 16 & 0x01))
            continue;

        GraphAudioNode* graph_node;

        short position[DIMENSIONS_AUDIO] = {0};
        
        for(int k = 0;k < countof(offsets);k++){
            int offset_x = (unsigned)(x + offsets[k]) % mipmap_size;

            int color = info->data[mipmap_offset + offset_x];

            if(color >> 16 & 0x02)
                graph->weights_inverse[k] = 0;
            else
                graph->weights_inverse[k] = fixedDivR(FIXED_ONE,weights[k]) >> 4;
            
            position[k] = color & 0xFFFF;
        }
        for(int k = 0;k < countof(offsets_up);k++){
            int offset_x = (unsigned)(x / 2 + offsets_up[k]) % (mipmap_size / 2);

            int color = info->data[mipmap_offset + offset_x + mipmap_size];

            position[countof(offsets) + k] = color & 0xFFFF;
        }
        int min_score = INT_MAX;
        int min_index;

        if(info->brute_force){
            int m_score = INT_MAX;
            graph_node = 0;

            for(int i = 0;i < graph->n_nodes;i++){
                GraphAudioNode* node = graph->entry + i;
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
            info->data[mipmap_offset + x] = 0xFF00;
            continue;
        }

        for(int j = 0;j < countof(graph_node->sample);j++){
            if(tRnd() % n_sample < graph_node->sample[j].n_sample){
                info->data[mipmap_offset + x] = graph_node->sample[j].color;
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

void audioInitGenerate(int* data,int mipmap,int size){
    int mipmap_offset = mipmapAudioOffset(size,mipmap);

    for(int i = 0;i < size >> mipmap;i++){
        if(data[mipmap_offset + i] >> 16 & 0x01)
            continue;
        int r = tRnd() % tMax(graph_audio[mipmap].n_color_randrange,1); 
        int color_index = binarySearch(graph_audio[mipmap].color_randindex,countof(graph_audio[mipmap].color_randindex),r); 
        int color = (color_index - 0x80) << 8; 
        data[mipmap_offset + i] = color;

        data[mipmap_offset + i] = tRnd() & 0xFFFF;
    }
}

void audioInitLayerGenerate(int* data,int* indices,int mipmap,int size){
    int mipmap_size = size >> mipmap;
    if(!mipmap_size)
        return;
    int mipmap_offset = mipmapAudioOffset(size,mipmap);

    for(int i = 0;i < mipmap_size;i++){
        int x = i;
        if(data[mipmap_offset + i] >> 16 & 0x01)
            continue;
        data[mipmap_offset + i] = data[mipmap_offset + x / 2 + mipmap_size] & 0xFFFF;
        data[mipmap_offset + i] |= 0x020000;
    }
    for(int i = 0;i < mipmap_size;i++)
        indices[i] = i; 
    shuffle(indices,mipmap_size);
}

void audioGenerate(int* data,int* indices,int offset,int amount,int mipmap,int size,bool inpaint,bool brute_force){
    indices += offset;
#if defined(__wasm__) || defined(__linux__)
    PixelFillThreadInfo info = {
        .indices = indices,
        .amount = amount,
        .mipmap = mipmap,
        .size = size,
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

#include "memory.h"

void audioModelInit(void){
    for(int i = 0;i < 0x10;i++){
        if(!graph_audio[i].entry)
            graph_audio[i].entry = tMalloc(sizeof(GraphAudioNode) * 0x4000000 >> i);

        for(int j = 0;j < countof(weights);j++){
            graph_audio[i].weights_inverse[j] = fixedDivR(FIXED_ONE,weights[j]) >> 4;
        }
        graph_audio[i].n_candidates_inference = 0x100;
        graph_audio[i].n_search_inference = 16;
        graph_audio[i].n_difference_threshold = 0x100;
        graph_audio[i].n_neighbours = 0x10;
    }
}
