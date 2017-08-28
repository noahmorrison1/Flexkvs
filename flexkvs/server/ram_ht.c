
#include <stdlib.h>
#include <stdio.h>


#include <rte_prefetch.h>

#include "ram_cache.h"
#include "global.h"

#define COND false
#define COND3 free_list.num_free_blocks < 20 
#define COND2 true

//Log is a contiguous array of log entries
static struct ht_entry* ht;
static struct log_entry* log_;
static size_t num_entries;
static size_t ht_entry_num;

static struct  {
	struct log_entry* head;
	struct log_entry* tail;
	size_t num_free_blocks;
	rte_spinlock_t lock;
} free_list;


static struct {
	struct cache_item* head;
	struct cache_item* tail;
	rte_spinlock_t lock;
} lru;

static rte_spinlock_t evict_lock;

static int keyNUM = 1;

void cache_init(void)
{
	TEST_PRINT("RAM CACHE INIT INITIALIZING \n");
	num_entries = RAM_CACHE_SIZE / LOG_BLOCK_SIZE;
	TEST_PRINT_2("NUM_ENTRIES: ",num_entries);
	// maybe multiply this later, cause will probably have more than NITEMS collisions
    ht_entry_num = (num_entries / CACHE_BUCKET_NITEMS);
    log_ = calloc(num_entries+1,LOG_BLOCK_SIZE);

    ht = calloc(ht_entry_num + 1, sizeof(struct ht_entry));

    if (ht == NULL) {
        perror("Allocating RAM_HT failed");
        abort();
    }

    if (log_ == NULL) {
        perror("Allocating cache log failed");
        abort();
    }

    // head and tail are end of log since all free
    free_list.head = log_;
    free_list.num_free_blocks = num_entries;
    free_list.tail = (log_ + free_list.num_free_blocks - 1); 

    //init locks
    for (int i = 0; i < ht_entry_num; i++) {
        rte_spinlock_init(&ht[i].lock);
    }

    rte_spinlock_init(&evict_lock);
    rte_spinlock_init(&free_list.lock);
	rte_spinlock_init(&lru.lock);
    TEST_PRINT("RAM CACHE INIT INITIALIZING DONE\n");
}


/** Compares the key of item with the key, assuming the keylens are the same **/
inline bool compare_item(struct cache_item* it, const void *key, size_t keylen)
{
	char *dest = (char *)(it +1);
	struct fragment **more_data_ptr = &it->more_data;
	char* src = (char* )key;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(struct cache_item);
    size_t amount = rest < keylen ? rest : keylen;
    // returns 0 if equal
    if(__builtin_memcmp(dest,src,amount) != 0) return false;

    keylen -=  amount;
    dest += amount;
    src += amount; 
    rest-=amount;
    //when it enters this loop, it means we have already compared all of the previous block
	while(keylen > 0)
	{
		struct fragment *frag = *more_data_ptr;
		dest = (char *)(frag +1);
		more_data_ptr = &frag->next;
	    rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
	    amount = rest < keylen ? rest : keylen;
	    if(__builtin_memcmp(dest,src,amount) != 0) return false;
	    keylen -=  amount;
	    dest += amount;
	    src += amount;
	}

	return true;
}

bool cache_item_key_matches(struct cache_item *it, const void *key,
        size_t klen)
{
    return it->valid && klen == it->keylen && compare_item(it, key, klen);
}

bool cache_item_hkey_matches(struct cache_item *it, const void *key,
        size_t klen, uint32_t hv)
{
    return it->valid && it->hv == hv && cache_item_key_matches(it, key, klen);
}



/*** Prefetches not current**/ 
void cache_hasht_prefetch1(uint32_t hv)
{
    rte_prefetch0(ht + (hv % ht_entry_num));
}

void cache_hasht_prefetch2(uint32_t hv)
{

    struct ht_entry* h = ht + (hv % ht_entry_num);
    struct cache_item* it;
    it = h->it;
    while (it != NULL) {
        if ( it->hv == hv) {
            rte_prefetch0(it);
        }
        it = it->next;
    }
}


struct cache_item* cache_ht_get(const void *key, size_t keylen, uint32_t hv)
{
	TEST_PRINT("RAM CACHE GET \n");
    struct ht_entry *h;

    h = ht + (hv % ht_entry_num);

    //TEST_PRINT_FINAL("START HT ENTRY: ",hv % ht_entry_num);
    
    if(!h->valid) return NULL;

    struct cache_item* it = h->it;
    int count = 0;
 	TEST_PRINT("BEFORE LOCK \n");
#ifndef NOHTLOCKS
    rte_spinlock_lock(&h->lock);
#endif
    TEST_PRINT("AFTER LOCK \n");
    while (it != NULL) {
			if(it->hv == hv) 
			{
				TEST_PRINT("RAM CACHE GET HV EQUAL \n");
				//lock means we will skip over things being evicted
				bool gotLock = true;
				#ifndef NOHTLOCKS
				    gotLock = rte_spinlock_trylock(&it->lock);
				#endif
				//if we dont get the lock its being evicted
				if(gotLock)
				{	

					TEST_PRINT("RAM CACHE GET GOTLOCK \n");
					// if we are using the item, keep lock up for returning of data later....
					if(cache_item_key_matches(it,key,keylen))
					{	
						TEST_PRINT("RAM CACHE GET KEY MATCHES \n");
						//move to head of LRU
						lru_update(it);

						goto done;
					}
					TEST_PRINT("RAM CACHE GET KEY DOESNT MATCHE \n");
					#ifndef NOHTLOCKS
					    rte_spinlock_unlock(&it->lock);
					#endif
				}

				
			}
			
			// important to note that even when evicting, the it->next should
			// still give us the next thing in the list
			if(it ==  h->it)
			{
				TEST_PRINT_2("THEY ARE SAME \n",count);
				count = 0;
			}
			count++;
			it = it->next;
    }

done:

#ifndef NOHTLOCKS
    rte_spinlock_unlock(&h->lock);
#endif
    TEST_PRINT("RAM CACHE GET DONE\n");
    
    //TEST_PRINT_FINAL("END HT ENTRY: ",hv % ht_entry_num);
    
    return it;
}


void lru_update(struct cache_item* it)
{	
	TEST_PRINT_IF(COND,"LRU UPDATE START \n");
	#ifndef NOHTLOCKS
    rte_spinlock_lock(&lru.lock);
	#endif

    TEST_PRINT("LRU_UPDATE 1 \n");
    if(lru.head == NULL)
    {
    	lru.head = it;
    	lru.tail = it;
    	TEST_PRINT("LRU_UPDATE 2 \n");
    }
    else if(it == lru.head)
    {
    	// do nothing
    	TEST_PRINT("LRU_UPDATE 3 \n");
    }
    else if(it == lru.tail)
    {
    	// not the head
    	TEST_PRINT("LRU_UPDATE 4.0 \n");
    	struct cache_item* prev = it->lru_prev;
    	prev->lru_next = NULL;
    	lru.tail = prev;

    	it->lru_next = lru.head;
	   	lru.head->lru_prev = it;
	   	lru.head = it;
	   	TEST_PRINT("LRU_UPDATE 4 \n");

    }
    else {
    	TEST_PRINT("LRU_UPDATE 5 \n");
		struct cache_item* next = it->lru_next;
		struct cache_item* prev = it->lru_prev;

		if(next != NULL) next->lru_prev = prev;
		if(prev != NULL) prev->lru_next = next;


	   	it->lru_next = lru.head;
	   	lru.head->lru_prev = it;
	   	lru.head = it;
	   	TEST_PRINT("LRU_UPDATE 6 \n");
   	}

	#ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru.lock);
	#endif

	TEST_PRINT("LRU UPDATE END \n");
}

void cache_ht_set(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv,size_t version)
{
	struct ht_entry* entry = ht + (hv % ht_entry_num);

	
	TEST_PRINT_IF(COND,"RAM CACHE SET\n");




#ifndef NOHTLOCKS
    rte_spinlock_lock(&entry->lock);
#endif

	// if entry invalid, write it, write will 
	if(!entry->valid)
	{
		TEST_PRINT_IF(COND2,"RAM CACHE SET FIRST ENTRY \n");
	    if(entry > ht + num_entries + 1){
			TEST_PRINT_IF(entry > ht + num_entries + 1 , "TOO FAR ENTRY \n" );
			exit(0);
		}
	 	entry->it = write_entry(key,keylen,val,vallen,hv,version);
	 	entry->valid = true;

	 	goto done;
	}
	else // look for entry, if not there add it
	{
		TEST_PRINT_IF(COND2,"NON_EMPTY ENTRY \n");
		struct cache_item* it = entry->it;
		while(it != NULL)
		{
			// if this is the same item, overwrite the value
			if(it->hv == hv) 
			{
				//this means we dont go here if its being evicted
				bool gotLock = true;
				#ifndef NOHTLOCKS
				    gotLock = rte_spinlock_trylock(&it->lock);
				#endif
				//if we dont get the lock its being evicted or there is currently a get on the item
				if(gotLock)
				{
					if(cache_item_key_matches(it,key,keylen) && version > it->version)
					{	
						TEST_PRINT_IF(COND2,"OVERWRITE \n");
						overwrite(it,val,vallen,version);
						lru_update(it);

						#ifndef NOHTLOCKS
					    	rte_spinlock_unlock(&it->lock);
						#endif

						goto done;
					}

					#ifndef NOHTLOCKS
					    rte_spinlock_unlock(&it->lock);
					#endif
				}

				
			}
			it = it->next;
		}
		

		TEST_PRINT_IF(COND2,"NEW WRITE \n");
		//it did not match insert at head of chain	
		struct cache_item* new_it = write_entry(key,keylen,val,vallen,hv,version);
		TEST_PRINT_IF(COND2,"NEW WRITE DONE \n");
		if(entry->it == NULL)
		{
			entry->it = new_it;
			entry->valid = true;
		}
		else
		{
			struct cache_item* old = entry->it;
			old->prev = new_it;
			new_it->next = old;
			entry->it = new_it;
		}
		goto done;
	}

    

done:
#ifndef NOHTLOCKS
    rte_spinlock_unlock(&entry->lock);
#endif

    TEST_PRINT_IF(COND,"RAM CACHE SET DONE\n");
    return;
}













/*****      LOG Editing Methods    *************/






// will only be called if evict lock is held
// Evicts MIN_EVICT bytes
void evict()
{
	//number of blocks to evict
	TEST_PRINT_IF(COND2,"RAM EVICT \n");
	TEST_PRINT_IF_2(COND2,"NUM FREE BLOCKS: ",free_list.num_free_blocks);
	int64_t amount = MIN_EVICT;
	amount = amount / LOG_BLOCK_SIZE;

	int64_t freed_tot = 0;

	while(amount > 0)
	{
		//TEST_PRINT_IF(COND,"RAM EVICT 0.25 \n");
		struct cache_item* tail = lru.tail;
		bool gotLock = true;
		//TEST_PRINT_IF(COND,"RAM EVICT 0.5 \n");
		//wont need to unlock becasuse item will be deleted
		#ifndef NOHTLOCKS
		gotLock = rte_spinlock_trylock(&tail->lock);
		#endif
		//TEST_PRINT_IF(COND,"RAM EVICT 1 \n");
		//if got lock, evict
		//otherwise, wait for tail to change so you can evict
		if(gotLock)
		{
			//TEST_PRINT_IF(COND,"RAM EVICT 2 \n");
			remove_from_chain(tail);
			//TEST_PRINT_IF(COND,"RAM EVICT 3 \n");
			remove_from_lru(tail);
			//TEST_PRINT_IF(COND,"RAM EVICT 4 \n");
			int freed = tail->size;
			struct fragment* new_tail = evict_item(tail);
			//TEST_PRINT_IF_2(COND,"SIZE: ",tail->size);
			//TEST_PRINT_IF_2(COND,"SIZE2: ",amount);
			//TEST_PRINT_IF(COND,"RAM EVICT 5 \n");
			amount -= freed;
			freed_tot += freed;
			add_to_free(tail,new_tail);
			
			//TEST_PRINT_IF(COND,"RAM EVICT 6 \n");

			//dont release lock cause its been deleted;
		}
	}

	//TEST_PRINT_IF(COND,"RAM EVICT 6.5 \n");
	#ifndef NOHTLOCKS
	rte_spinlock_lock(&free_list.lock);
	#endif

	//TEST_PRINT_IF(COND,"RAM EVICT 7 \n");
	//TEST_PRINT_IF_2(COND,"NUM FREE: ",free_list.num_free_blocks);
	TEST_PRINT_IF_2(COND2,"FREED TOT: ",freed_tot);
	free_list.num_free_blocks += freed_tot;

	//TEST_PRINT_IF(COND,"RAM EVICT 8\n");

	#ifndef NOHTLOCKS
	rte_spinlock_unlock(&free_list.lock);
	#endif

	TEST_PRINT_IF(COND2,"RAM EVICT DONE \n");

}

// will have lock on item for this
void remove_from_chain(struct cache_item* it)
{
	struct cache_item* prev = it->prev;
	struct cache_item* next = it->next;

	if(prev != NULL)
	{
		prev->next = next;
	}
	else
	{
		struct ht_entry* entry = ht + (it->hv % ht_entry_num);
		entry->it = next;
		if(next == NULL) entry->valid = false;
	}

	if(next != NULL) next->prev = prev;

}




// the only deals with the tail, which is only modified by the evict method, which 
// will only run single threaded
void remove_from_lru(struct cache_item* it)
{
	struct cache_item* prev = it->lru_prev;
	TEST_PRINT_IF_2(COND,"LRU PREV: ",(size_t)prev);
	//have to grab lock so that dont have a race 
	//condition with item writes and overwrites to the prev
	//baciscally need to make sure we actually grab the LRU tail
	#ifndef NOHTLOCKS
    rte_spinlock_lock(&lru.lock);
	#endif
	prev->lru_next = NULL;
	lru.tail = prev;
	#ifndef NOHTLOCKS
    rte_spinlock_unlock(&lru.lock);
	#endif
}


// free_list is guaranteed that head and tail will never touch
void add_to_free(struct cache_item* it, struct fragment* new_tail)
{
	struct fragment* new_frag = (struct fragment*)it;
	struct fragment* old_tail = (struct fragment* )free_list.tail;
	old_tail->nextFree = (struct log_entry*)new_frag;
	free_list.tail = (struct log_entry*)new_tail;
}

/*

struct cache_item {

	// whether this item is still valid
	bool valid;
	/** Next and prev item in the hash chain. 
	struct cache_item *next;	
	struct cache_item *prev;

	struct cache_item *lru_next;
    struct cache_item *lru_prev;

    // full hash value associated with this item. 
	uint32_t hv;
	/** Length of value in bytes 
	uint32_t vallen;
	/** Length of key in bytes 
	uint16_t keylen;

	// lock only used when overwriting or evicting an item
	rte_spinlock_t lock;
	// in blocks
	blocks size;

	size_t version;

	// ptr to where the rest of the data is stored
	struct fragment *more_data;
};


*/



struct fragment* evict_item(struct cache_item *it)
{
	struct fragment* last = (struct fragment*)it;


	size_t* key = (size_t*)(it+1);
	TEST_PRINT_IF_2(true,"EVICTING ITEM: ",*key);

    struct fragment *frag = it->more_data;
    memset(it,0,sizeof(struct cache_item));

    
    //set up this item as a free list, when added to end of 
    last->nextFree = (struct log_entry*)frag;
    last->next = NULL;
    last->valid = 0;
	last->size = 0;

    //evict all the fragments
    while(frag != NULL)
    {
    	// clear the frag and add to free list, started with the item head
    	struct fragment *next_frag = frag->next;
    	frag->valid = 0;
    	frag->size = 0;
    	frag->nextFree = (struct log_entry*)next_frag;
    	frag->next = NULL;
    	//last should end up being last non-null frag
    	last = frag;
    	frag = next_frag;

    }

    return last;
}

//never used
// returns number of blocks needed
/*blocks entrySize(size_t keylen, size_t vallen)
{
	size_t sum = keylen + vallen + sizeof(item);
	size_t adjusted_size = LOG_BLOCK_SIZE - sizeof(struct fragment);
	return = sum % adjusted_size != 0 ? sum / adjusted_size + 1 : sum / adjusted_size;
}

blocks entrySize(struct cache_item* it)
{
	return it->size;
}*/


/**

Get a free block. If there are too few left, go evict. If you try and evict
and someone else is already evicting, block, then check to see if you still need to evict before evicting more. 


 **/
void* getFree()
{
	TEST_PRINT_IF(COND3,"GET FREE STARTING \n");

	if(free_list.num_free_blocks < EVICT_POINT)
	{
		#ifndef NOHTLOCKS
		rte_spinlock_lock(&evict_lock);
		#endif
		if(free_list.num_free_blocks < EVICT_POINT){
			evict();
		}
		#ifndef NOHTLOCKS
		rte_spinlock_unlock(&evict_lock);
		#endif
	}

	#ifndef NOHTLOCKS
	rte_spinlock_lock(&free_list.lock);
	#endif

	//testing
	TEST_PRINT_IF_2(COND3,"FREE BLOCK: ", free_list.num_free_blocks);

	struct fragment *head = (struct fragment*)free_list.head;

	struct log_entry* next = head->nextFree;

	//if we have no pointer to next, then it is simply the next block
	if(next == NULL) next = free_list.head+1;
	else{ TEST_PRINT_IF(COND,"HEAD HAS NEXT \n");}

	free_list.head = next;
	free_list.num_free_blocks--;

	#ifndef NOHTLOCKS
	rte_spinlock_unlock(&free_list.lock);
	#endif

	//TEST_PRINT_IF(COND,"GET FREE ENDING \n");
	return (void *)head;

}

/** 

	writes a new entry to the cache. 
	guaranteed to have a lock on the entry when writing here.
	Basically gets memory, write the item, adds it to LRU, then returns it to be added
	to the Ht_entry chain.
	
**/
struct cache_item* write_entry(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv,size_t version)
{
	TEST_PRINT_IF(COND,"WRITE_ENTRY START \n");
	TEST_PRINT_IF_2(COND2,"NUM FREE BLOCKS WRITE : ",free_list.num_free_blocks);
	//TEST_PRINT_IF_2(COND2,"Keylen : ",keylen);
	//TEST_PRINT_IF_2(COND2,"Vallen : ",vallen);
	keyNUM++;
	struct cache_item* it = getFree();
	it->valid = true;
	it->hv = hv;
	it->size = 1;
	it->version = version;
	rte_spinlock_init(&it->lock);

    it->keylen = keylen;
    it->vallen = vallen;

    char *src = (char *)key;
    char *dest = (char* )(it+1);
	struct fragment **more_data_ptr = &it->more_data;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(struct cache_item);
    //copy key
	struct fragment* frag = cache_write(rest,src,dest,keylen, more_data_ptr,it);


	if(frag == NULL)
	{
		//if we didnt write into a new fragment, just move up our dest and rest
		dest += keylen;
		rest -= keylen;
	}
	else
	{
		// if we wrote into a fragment, move our dest
		// data_ptr, and rest up accordingly
		more_data_ptr = &frag->next;
		dest=((char *)(frag+1));
		dest += frag->size;
		rest = LOG_BLOCK_SIZE - sizeof(struct fragment) - frag->size;
	}

	//write the value
    src = (char *)val;
    cache_write(rest,src,dest,vallen, more_data_ptr,it);

    lru_update(it);


    return it;
}


#define COND_WR keyNUM >= 3589 
// writes out data and returns pointer to last written fragment
// if the fragment points to more fragemnts uses them, otherwise uses new block
struct fragment* cache_write(size_t rest,char *src, char *dest,int64_t length,struct fragment **more_data_ptr,struct cache_item* it)
{
	TEST_PRINT_IF_2(COND2,"WRITE START:  ",keyNUM);

	struct fragment *frag = NULL;

	if(rest == 0)
    {
    	TEST_PRINT_IF(COND2,"WRITE REST. + 0 \n");
    	// empty block then get new block
    	//otherwise use already allocated stuff
    	if(*more_data_ptr == NULL)
    	{
    		frag  = (struct fragment*)getFree();
    		*more_data_ptr = frag; 
    		it->size += 1;  		
    	}
    	else
    	{
    		TEST_PRINT_IF(COND2,"OVERWRITE ACTIVATED \n");
    		frag = *more_data_ptr;
    	}
    	dest = (char *)(frag+1);
    	more_data_ptr  = &frag->next;
    	rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
    	frag->valid = true;   	
    }

	size_t amount = rest < length ? rest : length;
    if(dest + amount > ((char*)log_) + ((num_entries + 1)* LOG_BLOCK_SIZE)){
		TEST_PRINT_IF(dest + amount > ((char*)log_) + ((num_entries + 1)* LOG_BLOCK_SIZE) , "TOO FAR 1 \n" );
		exit(0);
	}
	TEST_PRINT_IF_2(COND2,"WRITE Amount ",amount);
	display(src,dest,amount);
	memcpy(dest,src,amount);



	dest+=amount;

    // change vallen to new amount needed to copy
    length -= amount;

    // jump amount farther in val
    src += amount;
    rest -= amount;
    if(frag != NULL) frag->size = amount;
    int poop = 0;
    int poop2 = 0;
    // if need more space for key, continuously copy. 
    // will only enter this loop if previous block has been filled
    while(length > 0)
    {
    	TEST_PRINT_IF(COND,"WRITE MORE FRAGS\n");
    	//if here implies we are at start of a block
    	if(*more_data_ptr == NULL)
    	{
    		frag  = (struct fragment*)getFree();
    		*more_data_ptr = frag;
    		it->size +=1;
    		TEST_PRINT_IF(COND_WR && poop == 0,"WRITE NEW FRAG \n");
    		poop++;
    	}
    	else
    	{
    		TEST_PRINT_IF(COND_WR && poop2 == 0,"OVERWRITE ACTIVATED \n");
    		frag = *more_data_ptr;
    		poop2++;
    	}


    	dest = (char *) (frag+1);
    	rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
	    amount = rest < length ? rest : length;
		TEST_PRINT_IF_2(COND, "NEW FRAG AMOUNT ",amount );
	    memcpy(dest,src,amount);

	    length -= amount;

	    //reset more data ptr and set this block to valid
	    more_data_ptr  = &frag->next;
	    frag->valid = true;
	    frag->size = amount;

	    dest += amount;

    }


    //release extra space we are holding would be nice, but complicated so dont do it right now
    /*
    while(*more_data_ptr != NULL)
    {
    	add_to_free

    }*/



 	TEST_PRINT_IF(COND3,"WRITE END \n");

    return frag;


}

//will never enter here if evicting block, and will never try and evict if entering here
void overwrite(struct cache_item* it, void* val, size_t vallen, size_t verison)
{
	TEST_PRINT_IF(COND,"OVERWRITE START \n");
	lru_update(it);
	size_t keylen = it->keylen;
    it->size = 1;
    it->version = verison;
	char *dest = (char *)(it +1);
	struct fragment **more_data_ptr = &it->more_data;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(struct cache_item);
    size_t amount = rest < keylen ? rest : keylen;
    keylen -=  amount;
    dest += amount;
    rest -= amount;
    //jump over key
	while(keylen > 0)
	{
		struct fragment *frag = *more_data_ptr;
		dest = (char *)(frag +1);
		more_data_ptr = &frag->next;
	    rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
	    amount = rest < keylen ? rest : keylen;
	    keylen -=  amount;
	    dest += amount;
	    rest -= amount;
	    it->size += 1;
	}

	cache_write(rest,val,dest,vallen, more_data_ptr,it);
	TEST_PRINT_IF(COND,"OVERWRITE END \n");
}



