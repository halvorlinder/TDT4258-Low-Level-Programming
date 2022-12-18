#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    dm,
    fa
} cache_map_t;
typedef enum
{
    uc,
    sc
} cache_org_t;
typedef enum
{
    instruction,
    data
} access_t;

typedef struct
{
    uint32_t address;
    access_t accesstype;
} mem_access_t;

typedef struct
{
    uint64_t accesses;
    uint64_t hits;
} cache_stat_t;

//direct mapped cache struct
typedef struct
{
    uint32_t *tags;
    uint32_t *valid;
} dm_cache;

//fully associative cache struct
typedef struct
{
    uint32_t *tags;
    uint32_t *valid;
    uint32_t tail;

} fa_cache;

uint32_t cache_size;
uint32_t block_size = 64;

uint32_t system_size = 32;

//numbers that describe cache accesses
uint32_t tag_size;
uint32_t block_index_size;
uint32_t block_offset_size;
uint32_t number_of_blocks;

cache_map_t cache_mapping;
cache_org_t cache_org;

//caches for different arguments
dm_cache dm_cache_u;
dm_cache dm_cache_i;
dm_cache dm_cache_d;

fa_cache fa_cache_u;
fa_cache fa_cache_i;
fa_cache fa_cache_d;

cache_stat_t cache_statistics;

//initialize direct mapped cache
//just need to keep track of validity and tag for each block
void dm_cache_init(dm_cache *cache)
{
    cache->tags = malloc(number_of_blocks * sizeof(uint32_t));
    cache->valid = calloc(number_of_blocks , sizeof(uint32_t));
}

//initialize fully associative cache
//just use a fifo queue to keep track of tags currently in cache as well as validity
void fa_cache_init(fa_cache *cache)
{
    cache->tags = malloc(number_of_blocks * sizeof(uint32_t));
    cache->valid = calloc(number_of_blocks , sizeof(uint32_t));
    cache->tail = 0;
}

//return the index part of an address
uint32_t get_index(uint32_t address)
{
    return (address & (((1 << (block_index_size)) - 1) << block_offset_size)) >> block_offset_size;
}

//return tag part of an address
uint32_t get_tag(uint32_t address)
{
    return (address & (((1 << (tag_size)) - 1) << (block_offset_size + block_index_size))) >> (block_offset_size + block_index_size);
}

//register an access to direct mapped cache
//returns 1 in the case of a hit and 0 otherwise
uint32_t access_dm(dm_cache *cache, uint32_t address)
{
    uint32_t index = get_index(address);
    uint32_t tag = get_tag(address);
    //check if tag is stored at index and is valid
    if (cache->tags[index] == tag && cache->valid[index])
    {
        return 1; //hit
    }
    //place tag at index and make valid
    cache->tags[index] = tag;
    cache->valid[index] = 1;
    return 0; // miss
}

//register an access to a fully associative cache
//returns 1 in the case of a hit and 0 otherwise
uint32_t access_fa(fa_cache *cache, uint32_t address){
    uint32_t tag = get_tag(address);
    //iterate over queue and look for tag
    for (int i =0; i<number_of_blocks; i++){
        if (cache->tags[i]==tag && cache->valid[i]){
            return 1; //hit
        }
    }
    //place tag at the tail of the queue and move tail
    cache->tags[cache->tail] = tag;
    cache->valid[cache->tail] = 1;
    cache->tail = (cache->tail + 1) % number_of_blocks;
    return 0; //miss
}

/* Reads a memory access from the trace file and returns
 * 1) access type (instruction or data access
 * 2) memory address
 */
mem_access_t read_transaction(FILE *ptr_file)
{
    char buf[1000];
    char *token;
    char *string = buf;
    mem_access_t access;

    if (fgets(buf, 1000, ptr_file) != NULL)
    {
        /* Get the access type */
        token = strsep(&string, " \n");
        if (strcmp(token, "I") == 0)
        {
            access.accesstype = instruction;
        }
        else if (strcmp(token, "D") == 0)
        {
            access.accesstype = data;
        }
        else
        {
            printf("Unkown access type\n");
            exit(0);
        }

        /* Get the access type */
        token = strsep(&string, " \n");
        access.address = (uint32_t)strtol(token, NULL, 16);

        return access;
    }

    /* If there are no more entries in the file,
     * return an address 0 that will terminate the infinite loop in main
     */
    access.address = 0;
    return access;
}

// return the log base 2 of a power of 2
uint32_t log2_of_power_of_2(uint32_t n)
{
    int l = 0;
    while (n >>= 1)
    {
        ++l;
    }
    return l;
}

int main(int argc, char **argv)
{
    // Reset statistics:
    memset(&cache_statistics, 0, sizeof(cache_stat_t));

    /* Read command-line parameters and initialize:
     * cache_size, cache_mapping and cache_org variables
     */
    if (argc != 4)
    { /* argc should be 2 for correct execution */
        printf(
                "Usage: ./cache_sim [cache size: 128-4096] [cache mapping: dm|fa] "
                "[cache organization: uc|sc]\n");
        exit(0);
    }
    else
    {
        /* argv[0] is program name, parameters start with argv[1] */

        /* Set cache size */
        cache_size = atoi(argv[1]);

        /* Set Cache Mapping */
        if (strcmp(argv[2], "dm") == 0)
        {
            cache_mapping = dm;
        }
        else if (strcmp(argv[2], "fa") == 0)
        {
            cache_mapping = fa;
        }
        else
        {
            printf("Unknown cache mapping\n");
            exit(0);
        }

        /* Set Cache Organization */
        if (strcmp(argv[3], "uc") == 0)
        {
            cache_org = uc;
        }
        else if (strcmp(argv[3], "sc") == 0)
        {
            cache_org = sc;
        }
        else
        {
            printf("Unknown cache organization\n");
            exit(0);
        }
    }

    /* Open the file mem_trace.txt to read memory accesses */
    FILE *ptr_file;
    ptr_file = fopen("mem_trace.txt", "r");
    if (!ptr_file)
    {
        printf("Unable to open the trace file\n");
        exit(1);
    }

    // in the case of a split cache, each cache is half the size
    if (cache_org == sc)
    {
        cache_size = cache_size / 2;
    }

    // Set offset numbers
    number_of_blocks = cache_size / block_size;
    block_offset_size = log2_of_power_of_2(block_size);
    block_index_size = log2_of_power_of_2(number_of_blocks);
    tag_size = system_size - block_index_size - block_offset_size;

    // if the cache is fully associative, the index bits of the address must be used in the tag
    if (cache_mapping==fa){
        tag_size += block_index_size;
        block_index_size = 0;
    }

    // initialize the correct cache(s)
    if (cache_mapping == dm)
    {
        if (cache_org == uc){
            dm_cache_init(&dm_cache_u); //unified
        }
        else {
            //data
            dm_cache_init(&dm_cache_d); //data
            //instruction
            dm_cache_init(&dm_cache_i); //instruction
        }
    }
    else
    {
        if (cache_org == uc){
            fa_cache_init(&fa_cache_u);
        }
        else {
            fa_cache_init(&fa_cache_d);
            fa_cache_init(&fa_cache_i);
        }
    }


    /* Loop until whole trace file has been read */
    mem_access_t access;
    while (1)
    {
        access = read_transaction(ptr_file);
        // If no transactions left, break out of loop
        if (access.address == 0)
            break;
        cache_statistics.accesses++;
        // need to use the correct cache(s) and access functions
        if (cache_mapping == dm)
        {
            if (cache_org == uc){
                cache_statistics.hits+=access_dm(&dm_cache_u, access.address);
            }
            else {
                if(access.accesstype==data){
                    cache_statistics.hits+=access_dm(&dm_cache_d, access.address);
                }
                else{
                    cache_statistics.hits+=access_dm(&dm_cache_i, access.address);
                }
            }
        }
        else
        {
            if (cache_org == uc){
                cache_statistics.hits+= access_fa(&fa_cache_u, access.address);
            }
            else {
                if(access.accesstype==data){
                    cache_statistics.hits+=access_fa(&fa_cache_d, access.address);
                }
                else{
                    cache_statistics.hits+=access_fa(&fa_cache_i, access.address);
                }
            }
        }
    }

    /* Print the statistics */
    printf("\nCache Statistics\n");
    printf("-----------------\n\n");
    printf("Accesses: %ld\n", cache_statistics.accesses);
    printf("Hits:     %ld\n", cache_statistics.hits);
    printf("Hit Rate: %.4f\n",
           (double)cache_statistics.hits / cache_statistics.accesses);

    /* Close the trace file */
    fclose(ptr_file);
}