#define global __global
#define kernel __kernel
#define constant __constant

#define structure(NAME) typedef struct NAME NAME;struct NAME
#define enumeration(NAME) typedef enum NAME NAME;enum NAME

#define countof(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define repeat(AMOUNT) for(size_t I = AMOUNT;I--;)

#define loop for(;;)

#define FIXED_PRECISION 16
#define FIXED_ONE (1 << FIXED_PRECISION)

constant static int offsets[][2] = {
    {-1, 0},
    { 1, 0},
    { 0,-1},
    { 0, 1},
    
    {-1,-1},    
    {-1, 1},
    { 1,-1},
    { 1, 1},
};

constant static int offsets_up[][2] = {
    { 0, -1},
    { 1, 0},
    { 0, 1},
    { -1, 0},

    {-1, 0},
    { 1, 0},
    { 0,-1},
    { 0, 1},
};

#define DIMENSIONS ((((countof(offsets) + countof(offsets_up)) * 3) + 3) & ~3)

structure(GraphNode){
    uchar position[DIMENSIONS];
    int neighbours[0x10];
    int next;
    struct{
        int n_sample;
        int color;
    } sample[8];
    unsigned hash;
};

static int tAbs(int v){
    return v < 0 ? -v : v;
}

static int mipmapOffset(int width,int height,int mipmap){
    int offset = 0;
    for(int i = 0;i < mipmap;i++)
        offset += (width >> i) * (height >> i);
    return offset;
}

static unsigned tRnd(unsigned* rnd_state){
    *rnd_state ^= *rnd_state << 13;
    *rnd_state ^= *rnd_state >> 17;
    *rnd_state ^= *rnd_state << 5;
    return *rnd_state;
}

structure(Candidate){
    int distance;
    global GraphNode* node;
};

structure(Heap){
	int amount;
    Candidate candidates[0x1F];
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

constant int weights_inverse[] = {
    4096,4096,4096,
    4096,4096,4096,
    4096,4096,4096,
    4096,4096,4096,

    3413,3413,3413,
    3413,3413,3413,
    3413,3413,3413,
    3413,3413,3413,

    4096,4096,4096,
    4096,4096,4096,
    4096,4096,4096,
    4096,4096,4096,

    3413,3413,3413,
    3413,3413,3413,
    3413,3413,3413,
    3413,3413,3413,
};

static int getProbabilityDistance(global GraphNode* probability,uchar* position){
    int distance = 0;
    for(int k = 0;k < DIMENSIONS;k++){
        int rel = position[k] - probability->position[k];
        distance += tAbs(rel) * weights_inverse[k] >> 4;
    }
    return distance;
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

static void neighboursGet(global GraphNode* graph,global GraphNode* node,Candidate* candidate_list,int n_candidate,int n_search,uchar* position){
    if(!node)
        return;
    candidate_list[0].node = node;
    candidate_list[0].distance = getProbabilityDistance(node,position);

    Heap heap = {0};

    GraphNode* visited_table[0x40] = {0};

    for(int counter = 0;counter < n_search;counter++){
        for(int i = 0;i < countof(node->neighbours);i++){
            if(node->neighbours[i] == -1)
                continue;
            
            if(visited_table[graph[node->neighbours[i]].hash % countof(visited_table)] == graph + node->neighbours[i])
                continue;
            
            int distance = getProbabilityDistance(graph + node->neighbours[i],position);
            Candidate candidate = {
                .distance = distance,
                .node = graph + node->neighbours[i],
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

static global GraphNode* graphGet(global GraphNode* graph,global GraphNode* node,uchar* position){
    if(!node)
        return 0;
    for(;;){
        int distance = getProbabilityDistance(node,position);

        int min_distance = INT_MAX;
        int min_index = -1;

        for(int i = 0;i < countof(node->neighbours);i++){
            if(node->neighbours[i] == -1)
                continue;
            int neighbour_distance = getProbabilityDistance(graph + node->neighbours[i],position);
            if(neighbour_distance < min_distance){
                min_distance = neighbour_distance;
                min_index = i;
            }
        }
        if(min_distance < distance && min_index != -1)
            node = graph + node->neighbours[min_index];
        else
            return node;
    }
}

static global GraphNode* graphInference(global GraphNode* graph,uchar* position,int quality){
    global GraphNode* node = graphGet(graph,graph,position);
    Candidate candidate_list[0x100] = {0};
    neighboursGet(graph,node,candidate_list,quality,quality,position);
    node = candidate_list[0].node;
    return node;
}

kernel void inferenceBruteForce(global GraphNode* graph,global int* data,global int* indices,int mipmap,int image_size,int n_node){
    int mipmap_offset = mipmapOffset(image_size,image_size,mipmap);
    int mipmap_size = image_size >> mipmap;

    int x = indices[get_global_id(0)] / mipmap_size;
    int y = indices[get_global_id(0)] % mipmap_size;

    unsigned rnd_state = get_global_id(0) + 1;

    int luminance[3] = {0};
    global GraphNode* graph_node;
    
    uchar position[DIMENSIONS];
    for(int j = 0;j < DIMENSIONS;j++)
        position[j] = 0;

    for(int k = 0;k < countof(offsets);k++){
        int offset_x = (unsigned)(x + offsets[k][0]) % mipmap_size;
        int offset_y = (unsigned)(y + offsets[k][1]) % mipmap_size;

        int color = data[mipmap_offset + offset_x * mipmap_size + offset_y];

        for(int c = 0;c < 3;c++)
            position[k * 3 + c] = (color >> c * 8 & 0xFF);
    }
    for(int k = 0;k < countof(offsets_up);k++){
        int offset_x = (unsigned)(x / 2 + offsets_up[k][0]) % (mipmap_size / 2);
        int offset_y = (unsigned)(y / 2 + offsets_up[k][1]) % (mipmap_size / 2);

        int color = data[mipmap_offset + offset_x * (mipmap_size / 2) + offset_y + mipmap_size * mipmap_size];

        for(int c = 0;c < 3;c++)
            position[countof(offsets) * 3 + k * 3 + c] = (color >> c * 8 & 0xFF);
    }
    int m_score = INT_MAX;
    global GraphNode* selected = 0;

    for(int i = 0;i < n_node;i++){
        global GraphNode* node = graph + i;
        int score = getProbabilityDistance(node,position);
        if(score < m_score){
            selected = node;
            m_score = score;
        }
    }

    int min_score = INT_MAX;
    int min_index;
    
    graph_node = selected;

    if(!graph_node){
        data[mipmap_offset + x * mipmap_size + y] = 0xFF00FF;
        return;
    }
    
    int n_sample = 0;
   
    for(int j = 0;j < countof(graph_node->sample);j++)
        n_sample += graph_node->sample[j].n_sample;

    if(!n_sample){
        data[mipmap_offset + x * mipmap_size + y] = 0xFF00FF;
        return;
    }
   
    for(int j = 0;j < countof(graph_node->sample);j++){
        if(tRnd(&rnd_state) % n_sample < graph_node->sample[j].n_sample){
            data[mipmap_offset + x * mipmap_size + y] = graph_node->sample[j].color;
            return;
        }
        n_sample -= graph_node->sample[j].n_sample;
    }
    data[mipmap_offset + x * mipmap_size + y] = 0x00FF00;
}

kernel void inference(global GraphNode* graph,global int* data,global int* indices,int mipmap,int quality,int image_size,int n_node){
	int mipmap_offset = mipmapOffset(image_size,image_size,mipmap);
    int mipmap_size = image_size >> mipmap;

    int x = indices[get_global_id(0)] / mipmap_size;
    int y = indices[get_global_id(0)] % mipmap_size;

    unsigned rnd_state = get_global_id(0) + 1;

    int luminance[3] = {0};
    global GraphNode* graph_node;
    
    uchar position[DIMENSIONS];
    for(int j = 0;j < DIMENSIONS;j++)
        position[j] = 0;

    for(int k = 0;k < countof(offsets);k++){
        int offset_x = (unsigned)(x + offsets[k][0]) % mipmap_size;
        int offset_y = (unsigned)(y + offsets[k][1]) % mipmap_size;

        int color = data[mipmap_offset + offset_x * mipmap_size + offset_y];

        for(int c = 0;c < 3;c++)
            position[k * 3 + c] = (color >> c * 8 & 0xFF);
    }
    for(int k = 0;k < countof(offsets_up);k++){
        int offset_x = (unsigned)(x / 2 + offsets_up[k][0]) % (mipmap_size / 2);
        int offset_y = (unsigned)(y / 2 + offsets_up[k][1]) % (mipmap_size / 2);

        int color = data[mipmap_offset + offset_x * (mipmap_size / 2) + offset_y + mipmap_size * mipmap_size];

        for(int c = 0;c < 3;c++)
            position[countof(offsets) * 3 + k * 3 + c] = (color >> c * 8 & 0xFF);
    }
    graph_node = graphInference(graph,position,quality);

    if(!graph_node){
        data[mipmap_offset + x * mipmap_size + y] = 0xFF00FF;
        return;
    }
    
    int n_sample = 0;
   
    for(int j = 0;j < countof(graph_node->sample);j++)
        n_sample += graph_node->sample[j].n_sample;

    if(!n_sample){
        data[mipmap_offset + x * mipmap_size + y] = 0xFF00FF;
        return;
    }
   
    for(int j = 0;j < countof(graph_node->sample);j++){
        if(tRnd(&rnd_state) % n_sample < graph_node->sample[j].n_sample){
            data[mipmap_offset + x * mipmap_size + y] = graph_node->sample[j].color;
            return;
        }
        n_sample -= graph_node->sample[j].n_sample;
    }
    data[mipmap_offset + x * mipmap_size + y] = 0x00FF00;
}

constant static int sfx_offsets[] = {
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

constant static int sfx_offsets_up[] = {
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
    short position[SFX_DIMENSIONS];
    int neighbours[0x10];
    int next;
    struct{
        int n_sample;
        int color;
    } sample[8];
    unsigned hash;
};

constant static int sfx_weights[] = {
    256 / 6,
    256 / 5,
    256 / 4,
    256 / 3,
    256 / 2,
    256,
    256,
    256 / 2,
    256 / 3,
    256 / 4,
    256 / 5,
    256 / 6,

    256 / 6,
    256 / 5,
    256 / 4,
    256 / 3,
    256 / 2,
    256,
    256,
    256 / 2,
    256 / 3,
    256 / 4,
    256 / 5,
    256 / 6,
};

static int getProbabilityDistanceSfx(global SfxGraphNode* probability,short* position){
    int distance = 0;
    for(int k = 0;k < SFX_DIMENSIONS;k++){
        int rel = position[k] - probability->position[k];
        distance += tAbs(rel) * sfx_weights[k] >> 4;
    }
    return distance;
}

int mipmapOffsetSfx(int size,int mipmap){
    int offset = 0;
    for(int i = 0;i < mipmap;i++)
        offset += (size >> i);
    return offset;
}

kernel void inferenceBruteForceSfx(global SfxGraphNode* graph,global ushort* data,global int* indices,int mipmap,int image_size,int n_node){
    int mipmap_offset = mipmapOffsetSfx(image_size,mipmap);
    int mipmap_size = image_size >> mipmap;

    int x = indices[get_global_id(0)];

    unsigned rnd_state = get_global_id(0) + 1;

    int luminance[3] = {0};
    global SfxGraphNode* graph_node;
    
    short position[SFX_DIMENSIONS];
    for(int j = 0;j < SFX_DIMENSIONS;j++)
        position[j] = 0;

    for(int k = 0;k < countof(sfx_offsets);k++){
        int offset_x = (unsigned)(x + sfx_offsets[k]) % mipmap_size;

        position[k] = data[mipmap_offset + offset_x];
    }
    for(int k = 0;k < countof(sfx_offsets_up);k++){
        int offset_x = (unsigned)(x / 2 + sfx_offsets_up[k]) % (mipmap_size / 2);

        position[countof(sfx_offsets) + k] = data[mipmap_offset + offset_x + mipmap_size];
    }
    int m_score = INT_MAX;
    global SfxGraphNode* selected = 0;

    for(int i = 0;i < n_node;i++){
        global SfxGraphNode* node = graph + i;
        int score = getProbabilityDistanceSfx(node,position);
        if(score < m_score){
            selected = node;
            m_score = score;
        }
    }

    int min_score = INT_MAX;
    int min_index;
    
    graph_node = selected;

    if(!graph_node){
        data[mipmap_offset + x] = 0x00FF;
        return;
    }
    
    int n_sample = 0;
   
    for(int j = 0;j < countof(graph_node->sample);j++)
        n_sample += graph_node->sample[j].n_sample;

    if(!n_sample){
        data[mipmap_offset + x] = 0x00FF;
        return;
    }
   
    for(int j = 0;j < countof(graph_node->sample);j++){
        if(tRnd(&rnd_state) % n_sample < graph_node->sample[j].n_sample){
            data[mipmap_offset + x] = graph_node->sample[j].color;
            return;
        }
        n_sample -= graph_node->sample[j].n_sample;
    }
    data[mipmap_offset + x] = 0xFF00;
}