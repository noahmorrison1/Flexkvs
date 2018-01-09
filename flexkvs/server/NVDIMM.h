#ifndef NVDIMM_H_
#define NVDIMM_H_

#ifndef NVDIMM_SIZE
#define NVDIMM_SIZE (1 << 20)
#endif


#ifndef NVD_WRITING_TO_SSD
#define NVD_WRITING_TO_SSD (uint8_t)(0x01)
#endif


#ifndef NVD_READING
#define NVD_READING (uint8_t)(0x02)
#endif

#ifndef NVD_CONSUMED
#define NVD_CONSUMED (uint8_t)(0x04)
#endif

#ifndef NVD_WRITING_KEY
#define NVD_WRITING_KEY (uint8_t)(0x08)
#endif

#ifndef NVD_WRITING_VAL
#define NVD_WRITING_VAL (uint8_t)(0x10)
#endif

#ifndef NVD_WAITING
#define NVD_WAITING (uint8_t)(0x20)
#endif


#ifndef NVD_EMPTY
#define NVD_EMPTY (uint8_t)(0x00)
#endif








#include "global.h"
#include "ssd_ht.h"
#include "database.h"


struct NVDIMM_line {
	size_t keylen;
	size_t vallen;
	uint8_t readers;
	uint8_t state;
	size_t hv;
	rte_spinlock_t lock;
};

typedef struct NVDIMM_line nv_line;


bool contains_state(nv_line* item,uint8_t state);

bool NVDIMM_is_empty_state(nv_line* item);

bool NVDIMM_is_evictable_state(nv_line* item);

bool NVDIMM_compare_key(void* key, size_t keylen, nv_line* item);

bool NVDIMM_item_is_between_head_and_tail(nv_line* item);


/*
Attempts to write to the NVDIMM, if there is not enough space
attempts to clear out NVDIMM. If the item is too big just returns false,
otherwise true
*/
bool NVDIMM_write_entry(void* key, size_t keylen, void* val, size_t vallen, size_t hv);

/*
write to an arbitrary place in the buffer, need to have preallocated all the memory
*/
void* NVDIMM_write_to(char* src, char* dest, size_t amount);

/*
	Reads from the NVDIMM. Returns null if the key does not exist.
*/
struct ssd_line* NVDIMM_read(void* key, size_t keylen,size_t hv);


/*
find the key in the NVDIMM
*/
nv_line* NVDIMM_find(void* key, size_t keylen,size_t hv);



/*
	Read the give NVDIMM item to an ssd_line
*/
struct ssd_line* NVDIMM_read_item(nv_line* item);


/*
	Attempt to write out the next thing in the buffer
*/
bool NVDIMM_write_out_next(void);


/*
	Claim you are gonna write out the next item in
	the buffer.
*/
nv_line* NVDIMM_claim_next(void);


/*
Write out the nv_line to ssd
*/
bool NVDIMM_write_out(nv_line* item);

/*
	You have written out this line. Clear this part of the buffer.
*/
void NVDIMM_consume(nv_line* item);


/*
	Allocate amount of space in the buffer to write to.
*/
nv_line* NVDIMM_allocate(size_t amount);




/*
	Initalize the NVDIMM
*/
void NVDIMM_init(void);




void NVDIMM_invalidate();




































#endif