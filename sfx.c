#include "main.h"
#include "tmath.h"
#include "vec3.h"
#include "sfx.h"
#include "memory.h"

#ifdef __wasm__
#include <wasm_simd128.h>
#else
#include <intrin.h>
#include "kernel.h"
#endif

int mipmapOffsetSfx(int size,int mipmap){
    int offset = 0;
    for(int i = 0;i < mipmap;i++)
        offset += (size >> i);
    return offset;
}

static void shuffle(int* arr,int size){
    for(int i = size - 1;i > 0;i--){
        int j = tRnd() % (i + 1);

        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

void generateMipmapsSfx(int16* data,int length){
	int offset = 0;
    int size = length;
	for(;size;){
		int mipmap_offset = size;
		for(int i = 0;i < size / 2;i++)
			data[offset + mipmap_offset + i] = data[offset + i * 2 + 0] + data[offset + i * 2 + 1] >> 1;
		offset += mipmap_offset;
        size >>= 1;
	}
}

static int getProbabilityProbabilityDistanceSfx(SfxGraph* graph,SfxGraphNode* probability_1,SfxGraphNode* probability_2){
    int distance = 0;
    
    for(int k = 0;k < SFX_DIMENSIONS;k++){
        int rel = probability_1->position[k] - probability_2->position[k];
        distance += tAbs(rel) * graph->weights_inverse[k] >> 4;
    }
    return distance;
    /*
#ifdef __wasm__
    int distance = 0;
    
    for(int k = 0;k < DIMENSIONS;k++){
        int rel = (probability_1->position[k]) - (probability_2->position[k]);
        distance += tAbs(rel) * graph->weights_inverse[k];
    }
    return distance;
#else
    __m128i acc = _mm_setzero_si128();
    for(int k = 0;k < DIMENSIONS;k += 4){
        __m128i prob1 = _mm_cvtepu8_epi32(_mm_loadu_si128(probability_1->position + k));
        __m128i prob2 = _mm_cvtepu8_epi32(_mm_loadu_si128(probability_2->position + k));
        __m128i rel = _mm_abs_epi32(_mm_sub_epi32(prob1,prob2));
        rel = _mm_srai_epi32(_mm_mullo_epi32(rel,_mm_loadu_si128(graph->weights_inverse + k)),8); 
        acc = _mm_add_epi32(acc,rel);
    }
    __m128i sum1 = _mm_hadd_epi32(acc, acc); 
    __m128i sum2 = _mm_hadd_epi32(sum1, sum1); 

    int distance = _mm_cvtsi128_si32(sum2);
    return distance;
#endif
    */
}

static int getProbabilityDistanceSfx(SfxGraph* graph,SfxGraphNode* probability,int16* position){
    int distance = 0;
    for(int k = 0;k < SFX_DIMENSIONS;k++){
        int rel = position[k] - probability->position[k];
        distance += tAbs(rel) * graph->weights_inverse[k] >> 4;
    }
    return distance;
    /*
#ifdef __wasm__
    int distance = 0;
    for(int k = 0;k < DIMENSIONS;k++){
        int rel = (position[k] >> 4) - (probability->position[k] << 4);
        distance += tAbs(rel) * graph->weights_inverse[k] >> 4;
    }
    return distance;
#else
    __m128i acc = _mm_setzero_si128();
    for(int k = 0;k < DIMENSIONS;k += 4){
        __m128i prob = _mm_cvtepu8_epi32(_mm_loadu_si128(probability->position + k));
        __m128i positions = _mm_cvtepu8_epi32(_mm_loadu_si128(position + k));
        __m128i rel = _mm_abs_epi32(_mm_sub_epi32(positions,prob));
        rel = _mm_srai_epi32(_mm_mullo_epi32(rel,_mm_loadu_si128(graph->weights_inverse + k)),8);
        acc = _mm_add_epi32(acc,rel);
    }
    __m128i sum1 = _mm_hadd_epi32(acc, acc); 
    __m128i sum2 = _mm_hadd_epi32(sum1, sum1);

    int distance = _mm_cvtsi128_si32(sum2);
    return distance;
#endif
    */
}

static bool probabilityCompareSfx(SfxGraph* graph,SfxGraphNode* node,int16* position){
    int difference = getProbabilityDistanceSfx(graph,node,position);
    return difference <= graph->n_difference_threshold;
}

static SfxGraphNode* graphGetSfx(SfxGraph* graph,SfxGraphNode* node,int16* position){
    if(!node)
        return 0;
    for(;;){
        int distance = getProbabilityDistanceSfx(graph,node,position);

        int min_distance = INT_MAX;
        int min_index = -1;

        for(int i = 0;i < countof(node->neighbours);i++){
            if(node->neighbours[i] == -1)
                continue;
            int neighbour_distance = getProbabilityDistanceSfx(graph,graph->entry + node->neighbours[i],position);
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

structure(SfxCandidate){
    int distance;
    SfxGraphNode* node;
};

structure(SfxHeap){
	int amount;
    SfxCandidate candidates[0xFF];
};

static void heapInsertSfx(SfxHeap* heap,SfxCandidate value){
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
		SfxCandidate temp = heap->candidates[(index - 1) / 2];
		heap->candidates[(index - 1) / 2] = heap->candidates[index];
		heap->candidates[index] = temp;
		index = (index - 1) / 2;
    }   
}

static SfxCandidate heapGetSfx(SfxHeap* heap){
	if(!heap->amount){
        SfxCandidate candidate = {0};
		return candidate;
	}
	SfxCandidate result = heap->candidates[0];
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
		
		SfxCandidate temp = heap->candidates[index];
		heap->candidates[index] = heap->candidates[max_index];
		heap->candidates[max_index] = temp;
        index = max_index; 
    }
	return result;
}

static bool candidateInsertSfx(SfxCandidate* candidate_list,int n_candidate,SfxCandidate candidate){
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

static void neighboursGetSfx(SfxGraph* graph,SfxGraphNode* node,SfxCandidate* candidate_list,int n_candidate,int n_search,int16* position){
    if(!node || !graph->n_nodes)
        return;
    candidate_list[0].node = node;
    candidate_list[0].distance = getProbabilityDistanceSfx(graph,node,position);

    SfxHeap heap = {0};

    SfxGraphNode* visited_table[0x100] = {0};

    for(int counter = 0;counter < n_search;counter++){
        for(int i = 0;i < countof(node->neighbours);i++){
            if(node->neighbours[i] == -1)
                continue;
            
            if(visited_table[graph->entry[node->neighbours[i]].hash % countof(visited_table)] == graph->entry + node->neighbours[i])
                continue;
            
            int distance = getProbabilityDistanceSfx(graph,graph->entry + node->neighbours[i],position);
            SfxCandidate candidate = {
                .distance = distance,
                .node = graph->entry + node->neighbours[i],
            };
            visited_table[candidate.node->hash % countof(visited_table)] = candidate.node;

            heapInsertSfx(&heap,candidate);
        }
        SfxCandidate candidate = heapGetSfx(&heap);
        if(!candidate.node)
            break;
        candidateInsertSfx(candidate_list,n_candidate,candidate);
        node = candidate.node;
    }
    for(;;){
        SfxCandidate candidate = heapGetSfx(&heap);
        if(!candidate.node)
            return;
        candidateInsertSfx(candidate_list,n_candidate,candidate);
    }
}

static SfxGraphNode* graphInferenceSfx(SfxGraph* graph,int16* position){
    SfxGraphNode* node = graphGetSfx(graph,graph->entry,position);
    SfxCandidate candidate_list[0x100] = {0};
    neighboursGetSfx(graph,node,candidate_list,graph->n_candidates_inference,graph->n_search_inference,position);
    node = candidate_list[0].node;
    return node;
}

static void disconnectConnectionSfx(SfxGraph* graph,SfxGraphNode* node,SfxGraphNode* neighbour){
    for(int m = 0;m < countof(node->neighbours);m++){
        if(neighbour->neighbours[m] == (int)(node - graph->entry)){
            neighbour->neighbours[m] = -1;
            return;
        }
    }
}

static int getNumberOfNeighboursSfx(SfxGraphNode* node){
    int n = 0;
    for(int i = 0;i < countof(node->neighbours);i++){
        if(node->neighbours[i] == -1)
            continue;
        n += 1;
    }
    return n;
}

static void connectToGraphSfx(SfxGraph* graph,SfxGraphNode* node,int16* position){
    SfxGraphNode* near_node = graphGetSfx(graph,graph->entry,position);
        
    SfxCandidate candidate_list[0x100];

    for(int i = 0;i < graph->n_candidates_inference;i++){
        candidate_list[i].node = 0;
        candidate_list[i].distance = 0;
    }

    neighboursGetSfx(graph,near_node,candidate_list,graph->n_candidates_inference,graph->n_search_inference,position);

    int neighbour_index = 0;

    for(int i = 0;i < graph->n_candidates_inference;i++){
        if(!candidate_list[i].node)
            break;
        bool reject = false;

        for(int j = 0;j < countof(node->neighbours);j++){
            if(node->neighbours[j] == -1)
                continue;
            
            int neighbour_distance = fixedMulR(getProbabilityProbabilityDistanceSfx(graph,candidate_list[i].node,graph->entry + node->neighbours[j]),FIXED_ONE + FIXED_ONE / 32 * 8);
            
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
                int n_neighbour_neighbours = getNumberOfNeighboursSfx(graph->entry + candidate_list[i].node->neighbours[m]);
                if(n_neighbour_neighbours == 1)
                    continue;
                int distance = getProbabilityDistanceSfx(graph,candidate_list[i].node,graph->entry + candidate_list[i].node->neighbours[m]);
                if(distance > max_distance){
                    max_distance = distance;
                    max_index = m;
                }
            }
            if(max_index != -1 && candidate_list[i].distance < max_distance){
                disconnectConnectionSfx(graph,candidate_list[i].node,graph->entry + candidate_list[i].node->neighbours[max_index]);
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

static void setGraphSfx(SfxGraph* graph,SfxGraphNode* node,int16* position){
    if(graph->n_nodes == 1)
        return;
    connectToGraphSfx(graph,node,position);
}

SfxGraph graph_sfx[0x10];

#define MARGIN 4

void extractImageColorSfx(int* data,int size,int mipmap,int offset,int amount){
    int mipmap_offset = mipmapOffsetSfx(size,mipmap);
    int mipmap_size = size >> mipmap;

    graph_sfx[mipmap].entry = tMalloc(sizeof(SfxGraphNode) * 0x400000);

    for(int i = 0;i < amount;i++){
        int x = (offset + i);
        if(x < MARGIN || x >= mipmap_size - MARGIN)
            continue;
        int16 position[SFX_DIMENSIONS] = {0};

        for(int k = 0;k < countof(sfx_offsets);k++){
            int color = data[mipmap_offset + (x + sfx_offsets[k])];
            position[k] = color;
        }
        for(int k = 0;k < countof(sfx_offsets_up);k++){
            int color = data[mipmap_offset + (x / 2 + sfx_offsets_up[k]) + mipmap_size];
            position[countof(sfx_offsets) + k] = color;
        }
        SfxGraphNode* graph_node = graphInferenceSfx(graph_sfx + mipmap,position);
        if(!graph_node || !probabilityCompareSfx(graph_sfx + mipmap,graph_node,position)){
            SfxGraphNode* graph_node = graph_sfx[mipmap].entry + graph_sfx[mipmap].n_nodes++;
            for(int j = 0;j < SFX_DIMENSIONS;j++)
                graph_node->position[j] = position[j];

            graph_node->sample[0].color = data[mipmap_offset + x];
            graph_node->sample[0].n_sample = 1;
            graph_node->hash = tRnd();

            for(int j = 0;j < countof(graph_node->neighbours);j++)
                graph_node->neighbours[j] = -1;

            setGraphSfx(graph_sfx + mipmap,graph_node,position);
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
}

void generateSfxInit(int16* data,int* indices,int mipmap,int image_size){
    int mipmap_size = image_size >> mipmap;
    if(!mipmap_size)
        return;
    int mipmap_offset = 0;
    for(int i = 0;i < mipmap;i++)
        mipmap_offset += (image_size >> i);
    for(int i = 0;i < mipmap_size;i++){
        int x = i;
        int weight = tRnd() % 2 ? FIXED_ONE : 0;
        int16 d = tRnd() % 8 ? data[mipmap_offset + (x / 2) + mipmap_size] : tRnd();
        data[mipmap_offset + i] = d;
    }
    for(int i = 0;i < mipmap_size;i++)
        indices[i] = i; 
    shuffle(indices,mipmap_size);
}