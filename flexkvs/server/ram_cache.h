#ifndef RCACHE_H_
#define RCACHE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <rte_spinlock.h>
#include <stdio.h>
#include <string.h>
#include "global.h"

#ifndef RAM_CACHE_SIZE
#define RAM_CACHE_SIZE (1ULL << 30) // 1GB
#endif 

#ifndef SSD_SIZE
#define SSD_SIZE (1ULL << 40)  - (1ULL << 30) // 1TB - 1GB
#endif 

#ifndef MIN_EVICT
#define MIN_EVICT 1ULL << 20  // at least evict 1 MB from cache 
#endif 

#ifndef LOG_BLOCK_SIZE
#define LOG_BLOCK_SIZE 1024
#endif 

#ifndef MIN_KEY_LEN
#define MIN_KEY_LEN 32
#endif 


#ifndef MIN_VAL_LEN
#define MIN_VAL_LEN 32
#endif 

#ifndef SSD_HT_SIZE
#define SSD_HT_SIZE 1ULL << 31 
#endif 

#ifndef MAX_KEY_LEN
#define MAX_KEY_LEN 32
#endif 

#ifndef STORAGE_SIZE
#define STORAGE_SIZE 1ULL << 40
#endif 

#ifndef EVICT_POINT
#define EVICT_POINT 10
#endif 

#ifndef CACHE_BUCKET_NITEMS
#define CACHE_BUCKET_NITEMS 5
#endif 

#ifndef HASHTABLE_POWER
#define HASHTABLE_POWER 15
#endif 

#ifndef TABLESZ
#define TABLESZ(p,s) (1ULL << (p))
#endif 

#ifndef NON_STATE
#define NON_STATE '-'
#endif 

#ifndef WRITE_STATE
#define WRITE_STATE 'w'
#endif  

#ifndef READ_STATE
#define READ_STATE 'r'
#endif  
//static_assert(sizeof(rte_spinlock_t) == 4, "Bad spinlock size");




typedef uint32_t blocks;





//size = 2^7
struct cache_item {

	// whether this item is still valid
	bool valid;

	char state;
	rte_spinlock_t state_lock;
	uint8_t readers;
	/** Next and prev item in the hash chain. */
	struct cache_item *next;	
	struct cache_item *prev;

	struct cache_item *lru_next;
    struct cache_item *lru_prev;

    // full hash value associated with this item. 
	uint32_t hv;
	/** Length of value in bytes */
	uint32_t vallen;
	/** Length of key in bytes */
	uint16_t keylen;

	// lock only used when overwriting or evicting an item
	rte_spinlock_t lock;
	// in blocks
	blocks size;

	size_t version;

	// ptr to where the rest of the data is stored
	struct fragment *more_data;
};



struct fragment {
	bool valid;
	uint32_t size;
	uint32_t poop;
	struct fragment *next;
	struct log_entry *nextFree;
};

typedef struct cache_item item_t;
typedef struct fragment frag_t;

struct ht_entry {
	struct cache_item *it;
	rte_spinlock_t lock;
	rte_spinlock_t barrier;
	bool valid;
};

struct log_entry {
	uint8_t block[LOG_BLOCK_SIZE];
};

/**************      RAM HT Methods    *************/


/** Initalize the in-memory cache **/
void cache_init(void);


bool cache_item_key_matches(struct cache_item *it, const void *key, size_t klen);

bool cache_item_hkey_matches(struct cache_item *it, const void *key, size_t klen, uint32_t hv);

/** Compares the key of item it, and the passed in key **/
//static inline bool compare_item(struct cache_item* it, const void *key, size_t klen);

/*** Prefetches not current**/ 
void cache_hasht_prefetch1(uint32_t hv);

void cache_hasht_prefetch2(uint32_t hv);



/**  Looks for the key in the cache, if its finds it, returns the item , otherwise null_ptr **/
struct ssd_line* cache_ht_get(const void *key, size_t keylen, uint32_t hv);


struct ssd_line* cache_item_to_line(struct cache_item* it);


void cache_ht_set(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv,size_t version);

void lru_update(struct cache_item* it);

/**************      LOG Editing Methods    *************/




/** Evicts MIN_EVICT bytes**/
void evict();

/** Evict the item it **/
struct fragment* evict_item(struct cache_item *it);

// will have lock on item for this
void remove_from_chain(struct cache_item* it);


// the only deals with the tail, which is only modified by the evict method, which 
// will only run single threaded
void remove_from_lru(struct cache_item* it);


// free_list is guaranteed that head and tail will never touch
void add_to_free(struct cache_item* it, struct fragment* new_tail);

/** Returns the number of blocks needed to write out the kv pair **/
//blocks entrySize(size_t keylen, size_t vallen);


/** Gets the size in block sof the item **/
//blocks entrySize(struct cache_item* it);

/**

Creates a new key-value pair item and writes out the key-value pair. Returns a ptr the newly allocated item. 

**/
struct cache_item* write_entry(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv,size_t version);


/**

General Method that writes length bytes from source to dest. 
If the more_data_ptr is not null, will follow it to write out data. 
otherwise, it will get a new cache block to write to. 

**/
struct fragment* cache_write(size_t rest,char *src, char *dest,int64_t length,struct fragment **more_data_ptr,struct cache_item* it);


/** 

This overwrites the value for a given key. Retains the old item. 
Guaranteed that will only be called if the item is not being evicted
and will not evicted after this method is called. 

The version is used to make sure only most recent SSD writer, overwrites this key-value in the cache. 

**/
void overwrite(struct cache_item* it, void* val, size_t vallen, size_t verison);


/** Flushes the specific key from the cache**/
void cache_flush(void *key, size_t keylen, uint32_t hv);


void cache_flush_item(struct cache_item* item);







#endif // ndef IOKVS_H_