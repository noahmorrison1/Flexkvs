
#include <stdlib.h>
#include <stdio.h>


#include <rte_prefetch.h>

#include "ram_cache.h"






//Log is a contiguous array of log entries
static struct ht_entry* ht;
static struct log_entry* log_;
static struct item* free_head;
static size_t num_entries;
static size_t num_free_blocks;

static stuct  {
	struct log_entry* head;
	struct log_entry* tail;
	uint32_t num_free_blocks;
} free_list;


static struct {
	struct item* lru_head;
	struct item* lru_tail;
	rte_spinlock_t lock;
} lru;



void cache_init(void)
{
	num_entries = RAM_CACHE_SIZE / LOG_BLOCK_SIZE;
	// maybe multiply this later, cause will probably have more than NITEMS collisions
    size_t ht_size = (num_entries * sizeof(ht_entry) / CACHE_BUCKET_NITEMS);
    log_ = calloc(RAM_CACHE_SIZE);
    ht = calloc(ht_size);
    if (log_entries == NULL || ht == NULL) {
        perror("Allocating cache item hash table failed");
        abort();
    }

    // head and tail are end of log since all free
    free_list.head = log_;
    free_list.num_free_blocks = RAM_CACHE_SIZE / LOG_BLOCK_SIZE;
    free_list.tail = (log_ + num_free_blocks - 1); 


    //init locks
    for (i = 0; i < ht_size; i++) {
        rte_spinlock_init(&ht[i].lock);
    }
}


static inline bool cache_item_key_matches(struct item *it, const void *key,
        size_t klen)
{
    return klen == it->keylen && !__builtin_memcmp(item_key(it), key, klen);
}

static inline bool cache_item_hkey_matches(struct item *it, const void *key,
        size_t klen, uint32_t hv)
{
    return it->hv == hv && item_key_matches(it, key, klen);
}


void cache_hasht_prefetch1(uint32_t hv)
{
    rte_prefetch0(log_ + (hv % num_entries));
}

void cache_hasht_prefetch2(uint32_t hv)
{
    struct log_entry *l;
    size_t i;

    l = log_ + (hv % num_entries);
    struct it*;
    it = &l->item;
    while (it != NULL) {
        if ( l->hv == hv) {
            rte_prefetch0(l);
        }
        l = l->next;
    }
}


struct item* cache_hasht_get(const void *key, size_t klen, uint32_t hv)
{
    struct log_entry *l;
    size_t i;


    l = log_ + (hv % num_entries);
    struct it*;
    it = &l->item;
#ifndef NOHTLOCKS
    rte_spinlock_lock(l->lock);
#endif
    while (it != NULL) {
        if ( l->hv == hv && item_key_matches(it, key, klen)) {
            goto done;
        }
        l = l->next;
    }

done:

#ifndef NOHTLOCKS
    rte_spinlock_unlock(&b->lock);
#endif
    return it;
}


void hasht_put(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv)
{
	struct ht_entry* entry = ht + (hv % num_entries);


#ifndef NOHTLOCKS
    rte_spinlock_lock(&entry->lock);
#endif

	// if entry invalid, write it, write will 
	if(!entry->valid)
	{
	 	entry->it = write(key,keylen,val,vallen,hv);
	 	entry->valid = true;
	 	goto done;
	}
	else // look for entry, if not there add it
	{
		struct item* it = entry->it;
		while(it != NULL)
		{
			// if this is the same item, overwrite the value
			if(it->hv == hv && item_key_matches(it,key,keylen)) 
			{
				overwrite(it,val,vallen);
				goto done;
			}
			it = it->next;
		}
		//it did not match insert at head of chain
		struct item* old = entry->it;
		struct item* new_it = write(key,keylen,val,vallen,hv);
		old->prev = new_it;
		new_it->next = old;
		entry->it = new_it;
		goto done;
	}

    

done:
#ifndef NOHTLOCKS
    rte_spinlock_unlock(&entry->lock);
#endif
    return;
}

//evict at least n bytes
void evict(blocks n)
{


}


// returns number of blocks needed
blocks entrySize(size_t keylen, size_t vallen)
{
	size_t sum = keylen + vallen + sizeof(item);
	size_t adjusted_size = LOG_BLOCK_SIZE - sizeof(fragment);
	return = sum % adjusted_size != 0 ? sum / adjusted_size + 1 : sum / adjusted_size;
}

blocks entrySize(struct item* it)
{
	return it->size;
}

size_t evictNext()
{

}


/**
struct item {


	bool valid;
	struct item *next;	
	struct item *prev;
	struct log_entry* head; //might not need
	uint32_t hv;

	union {
		struct item *lru_next;
		struct log_entry *nextFree;
	}
	union {
		struct item *lru_prev;
		struct log_entry *lastFree;
	}

	uint32_t vallen;

	uint16_t keylen;

	uint16_t flags;

	// in blocks
	blocks size;

	fragment *more_data;
}

**/

// Wrting something new
// Write item to log, add to head of LRU
// Add to head of LRU
// really 2 critical sections, LRU critical section and
// write to item
// so overwrite knows what to do,
// will have to look at locked out log_entry
// then second one will find
// ok so write locks item, but doesnt matter...

item* write(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv)
{
	entry_blocks  = entrySize(keylen,vallen);
	// if not enough space evict
	if(free_list.num_free_blocks < entry_blocks) evict(entry_blocks);

	item* it = getFree();
	it->valid = true;
	it->hv = hv;
	it->size = 1;
	rte_spinlock_init(&it.lock);

	#ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru->lock);
    //might not need to lock item here
    //rte_spinlock_lock(&it->lock); 
	#endif

    //add to front of LRU
    it->prev = lru->head;
    lru->head-> = it;
    lru_head = it;

    //no need to hold up LRU
    #ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru->lock);
	#endif

    it->keylen = keylen;
    it->vallen = vallen;

    char *src = (char *)key;
    char *dest = (char* )(it+1);

    //copy key

    // amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(it);
    // amount to copy to this block
    size_t amount = rest < keylen ? rest : keylen;

    //copy key to rest of block
    memcpy(src,dest,amount);

    // change keylen to new amount needed to copy
    keylen -= amount;

    // copy this amount more from the key;
    src += amount;

    fragement **more_data_ptr = &item->more_data;

    // if dont need another block then just incrase dest
    // will be overwritten if I need more space. 
    dest += amount;
    rest = rest - amount;

    // if need more space for key, continuously copy. 
    while(keylen > 0)
    {
    	//if here implies we are at start of a block

    	//first get the new block
    	fragement *frag  = (fragment*)getFree();
    	//link it
    	*more_data_ptr = frag;
    	dest = (char *) frag+1;
    	rest = LOG_BLOCK_SIZE - sizeof(fragment);
	    amount = rest < keylen ? rest : keylen;
	    memcpy(src,dest,amount);
	    keylen -= amount;
	    it->size +=1;

	    //reset more data ptr and set this block to valid
	    more_data_ptr  = frag->next;
	    frag->valid = true;
	    // how much data is stored here
	    // somehwat unnessesary cause know keylength
	    frag->size = amount;

	    //for if we dont finish block
	    dest += amount;
	    rest = rest - amount;
    }

    // need to know where in block I finished wrting, also where fragment head is

    //copy value

    // if finishing off key finished block then get new one
    if(rest == 0)
    {
    	fragement *frag  = (fragment*)getFree();
    	dest = (char *)frag;
    	more_data_ptr  = frag->next;

    }


    // if need more space for key, continuously copy. 
    while(vallen > 0)
    {
    	//if here implies we are at start of a block

    	//first get the new block
    	fragement *frag  = (fragment*)getFree();
    	//link it
    	*more_data_ptr = frag;
    	dest = (char *) frag+1;
    	rest = LOG_BLOCK_SIZE - sizeof(fragment);
	    amount = rest < keylen ? rest : keylen;
	    memcpy(src,dest,amount);
	    keylen -= amount;
	    it->size +=1;

	    //reset more data ptr and set this block to valid
	    more_data_ptr  = frag->next;
	    frag->valid = true;
    }


    #ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru->lock);
	#endif



	 #ifndef NOHTLOCKS
    rte_spinlock_unlock(&it->lock);
	#endif

}

void overwrite(struct item* it, void* val, size_t vallen)
{
	
}



