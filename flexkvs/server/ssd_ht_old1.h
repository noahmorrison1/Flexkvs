#ifndef SSD_HT_H_
#define SSD_HT_H_


#ifndef SSD_SIZE
#define SSD_SIZE ((1ULL << 40)  - (1ULL << 30)) // 1TB - 1GB
#endif 

#ifndef SSD_PAGE_SIZE
#define SSD_PAGE_SIZE (1 << 12)
#endif 

#ifndef SSD_BLOCK_SIZE
#define SSD_BLOCK_SIZE (6ULL *PAGE_SIZE)
#endif 

#ifndef SSD_HT_SIZE 
#define SSD_HT_SIZE (1ULL << 30)
#endif 

#ifndef NUM_PAGES
#define NUM_PAGES (SSD_SIZE / PAGE_SIZE)
#endif 

#ifndef NUM_BRICKS_PER_PAGE
#define NUM_BRICKS_PER_PAGE (PAGE_SIZE / SSD_BRICK_SIZE)
#endif 

#ifndef SSD_NUM_COLLISIONS
#define SSD_NUM_COLLISIONS 5
#endif 

//bricks
#ifndef SSD_BRICK_SIZE
#define SSD_BRICK_SIZE (1 << 9)
#endif 


#ifndef SSD_MIN_ENTRY_SIZE
#define SSD_MIN_ENTRY_SIZE (SSD_PAGE_SIZE / 5ULL)
#endif 


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <rte_spinlock.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "global.h"

//static_assert(sizeof(rte_spinlock_t) == 4, "Bad spinlock size");

typedef uint32_t bricks;




//size = 2^7
struct ssd_item {

	 /** Next item in the hash chain. */
	bool valid;
	struct ssd_item *next;	
	struct ssd_item *prev;

	uint16_t pages;
	int8_t start_page;

	uint32_t hv;

	//this is a shortened key if the key is longer than this
	uint64_t key;

	size_t version;

	/** Length of value in bytes */
	uint32_t vallen;
	/** Length of key in bytes */
	uint16_t keylen;
	// in blocks
	bricks size;
	// number of headers
	uint16_t num_headers;

	size_t page;
	uint16_t offset;
};



struct ssd_ht_entry {
	struct ssd_item *it;
	rte_spinlock_t lock;
	bool valid;
};



struct free_brick {
	struct item* next;
};

struct ssd_header {
	bricks size;
	/**Next Page with Info**/
	size_t next;
	// offest into next page
	uint16_t offset;
};

struct ssd_line {
	void* key;
	void* val;
	size_t vallen;
	size_t keylen;
	size_t version;
};


struct free_page_header {
	uint16_t capacity;
	size_t num;
	rte_spinlock_t lock;
};


void ssd_init();

/**Initalizes the SSD HT **/
void ssd_ht_init(void);

/**

Returns a ptr to a malloced area containting the relavent Kv pair
if the key isnt there, return null

**/
struct ssd_line* ssd_ht_get( void* key, size_t keylen,uint32_t hv);

/** 
Sets the value in the SSD_HT by redirecting the HT if neccesary and writing out
value to a new page. Returns version number of write. 
**/
size_t ssd_ht_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv);

// only overwrites if keys match
// increases version number by 1
void ssd_overwrite(struct ssd_item *it, void* val, size_t vallen,void* key, size_t keylen);

/**
 Same as overwrite but gets a new slot in the log and inserts
the newly allocated item at head of ht_entry
**/
struct ssd_item* ssd_write_entry(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv);

/** 
Write in log format, from each src in srcs. Associate the head of this writing with it.
If consume the last bytes of the page, then eventuially write that page out to SSD. A process and 
have up to 10 pages to write out at a time. If the kv pair is bigger than that, an error occurs. 
\**/
void ssd_write(void** srcs, size_t *sizes, uint16_t num_srcs,struct  ssd_item* it );

/**
 Consumes bytes from the current page to be writtent to ssd, returns
true IFF this consume sets the number of aavailable bytes to 0
**/
bool consume(void* curr_page,size_t amount);

/** 
Makes a new SSD Page to be written out later. Note that it allocates extra room for the free_page header
This is reflected in the offset
**/
void* make_page(size_t num);

/** Gets the next free slot for the SSD HT entry log**/
struct ssd_item* getFreeLogSlot();




/*** Not yet Implemented ***/


/** **/
void write_out_all(int wo_count,void** pages);


/**Do last... Probably just mmap a file and write it out to disk, need to think long and hard about this **/
void write_out(struct free_page_header* page);

/** also uses the mmapped file, reads in all data for item to memory, returns a ssd_line* at memory **/ 
struct ssd_line* ssd_read(struct ssd_item* it,void* memory);

/** same as above but just reads in key **/ 
struct ssd_line* ssd_read_key(struct ssd_item* it,void* memory);

bool ssd_key_matches(struct ssd_line* it, void* key, size_t keylen);

bool ssd_item_key_matches(struct ssd_item* current, void* key,size_t keylen);

size_t ssd_delete(void *key, size_t keylen, uint32_t hv);








#endif 