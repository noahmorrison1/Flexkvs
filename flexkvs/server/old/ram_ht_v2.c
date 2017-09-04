
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

rte_spinlock_t evict_lock;



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

    rte_spinlock_init(&evict_lock);
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



// Wrting something new
// Write item to log, add to head of LRU
// Add to head of LRU
// really 2 critical sections, LRU critical section and
// write to item
// so overwrite knows what to do,
// will have to look at locked out log_entry
// then second one will find
// ok so write locks item, but doesnt matter...
item* write_entry(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv)
{
	entry_blocks  = entrySize(keylen,vallen);
	// if not enough space evict, first get lock, then make sure nothing was evicted. 
	if(free_list.num_free_blocks < entry_blocks)
	{
	   #ifndef NOHTLOCKS
       rte_spinlock_lock(evict_lock);
       #endif

       if(free_list.num_free_blocks < entry_blocks) evict(entry_blocks); 

       #ifndef NOHTLOCKS
       rte_spinlock_unlock(evict_lock);
       #endif
	}

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
	fragement **more_data_ptr = &item->more_data;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(it);

    //copy key
	fragment* frag = write(rest,src,dest,keyllen, more_data_ptr,it);

	
	if(frag == NULL)
	{
		//if we didnt write into a new fragment, just move up our dest and rest
		dest += keylen;
		rest += keylen;
	}
	else
	{
		// if we wrote into a fragment, move our dest
		// data_ptr, and rest up accordingly
		more_data_ptr = &frag->next;
		dest=(char *)((frag+1) + frag->size);
		rest = LOG_BLOCK_SIZE - sizeof(frag) - frag->size;
	}

	//write the value
    src = (char *)key;
    write(rest,src,dest,vallen, more_data_ptr,it);

}

// writes out data and returns pointer to last written fragment
// if the fragment points to more fragemnts uses them, otherwise uses new block
fragment* write(size_t rest,char *src, char *dest,size_t length, fragement **more_data_ptr, item* it)
{
	fragment *frag = NULL;
	if(rest == 0)
    {
    	// empty block then get new block
    	//otherwise use already allocated stuff
    	if(*more_data_ptr == NULL)
    	{
    		frag  = (fragment*)getFree();
    		*more_data_ptr = frag;
    		it->size +=1;
    	}
    	else
    	{
    		freg = *more_data_ptr;
    	}
    	dest = (char *)frag+1;
    	more_data_ptr  = frag->next;
    	rest = LOG_BLOCK_SIZE - sizeof(fragment);
    	frag->valid = true;   	
    }
	
	amount = rest < length ? rest : length;
	memcpy(src,dest,amount);

    // change vallen to new amount needed to copy
    length -= amount;

    // jump amount farther in val
    src += amount;
    if(frag != NULL) frag->size = amount;


    // if need more space for key, continuously copy. 
    while(length > 0)
    {
    	//if here implies we are at start of a block

    	if(*more_data_ptr == NULL)
    	{
    		frag  = (fragment*)getFree();
    		*more_data_ptr = frag;
    		it->size +=1;
    	}
    	else
    	{
    		freg = *more_data_ptr;
    	}
    	dest = (char *) frag+1;
    	rest = LOG_BLOCK_SIZE - sizeof(fragment);
	    amount = rest < length ? rest : keylen;
	    memcpy(src,dest,amount);
	    length -= amount;
	
	    //reset more data ptr and set this block to valid
	    more_data_ptr  = frag->next;
	    frag->valid = true;
	    frag->size = amount;
    }

    return ret;


}

//will never enter here if evicting block, and will never try and evict if entering here
void overwrite(struct item* it, void* val, size_t vallen, void* key, size_t keylen)
{

    if(!gotLock)
    {
    	write_entry()
    }

	#ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru->lock);
	#endif

    //add to front of LRU
    it->prev = lru->head;
    lru->head-> = it;
    lru_head = it;

    //no need to hold up LRU
    #ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru->lock);
	#endif

	char *dest = (char *)(it +1);
	fragement **more_data_ptr = &item->more_data;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(it);
    size_t amount = rest < keylen ? rest : keylen;
    keylen -=  amount;
    dest += amount;
    rest -= amount;
    //jump over key
	while(keylen > 0)
	{
		fragment *frag = *more_data_ptr;
		dest = (char *)(frag +1);
		more_data_ptr = &frag->next;
	    rest = LOG_BLOCK_SIZE - sizeof(fragment);
	    amount = rest < keylen ? rest : keylen;
	    keylen -=  amount;
	    dest += amount;
	    rest -= amount;
	}

	write(rest,val,dest,vallen, more_data_ptr,it);
}



