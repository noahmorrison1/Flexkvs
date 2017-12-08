 
#include <stdlib.h>
#include <stdio.h>


#include <rte_prefetch.h>

#include "ram_cache.h"
#include "global.h"
#include <semaphore.h>

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
	RAM_GEN_LOG_WRITE("RAM CACHE INIT INITIALIZING \n");
	num_entries = RAM_CACHE_SIZE / LOG_BLOCK_SIZE;
	RAM_GEN_LOG_WRITE_2("NUM_ENTRIES: ",num_entries);

	// maybe multiply this later, cause will probably have more than NITEMS collisions
    ht_entry_num = (num_entries / CACHE_BUCKET_NITEMS);
    log_ = CALLOC(num_entries+1,LOG_BLOCK_SIZE,"RAM LOG");

    ht = CALLOC(ht_entry_num + 1, sizeof(struct ht_entry),"RAM HASH TABLE");

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
        rte_spinlock_init(&ht[i].barrier);
    }

    rte_spinlock_init(&evict_lock);
    rte_spinlock_init(&free_list.lock);
	rte_spinlock_init(&lru.lock);

    RAM_GEN_LOG_WRITE("RAM CACHE INIT INITIALIZING DONE\n");
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
	RAM_GEN_LOG_WRITE("RAM CACHE GET START \n");

    struct ht_entry* entry = ht + (hv % ht_entry_num);

    
    if(!entry->valid) return NULL;


    //lock the tnery from any other read or write temporarily
    RTE_LOCK(&entry->lock,"RAM BUCKET LOCK");

    struct cache_item* it = entry->it;


    //lock down the barrier so no one else can enter
    RTE_LOCK(&entry->barrier,"RAM CACHE BARRIER");
    //traverse bucket
    while (it != NULL) {
			if(it->hv == hv) 
			{
				RAM_GEN_LOG_WRITE("RAM CACHE GET HV EQUAL ");

				RTE_LOCK(&it->state_lock,"ITEM STATE LOCK");

				//if were in a modifiable state 
				if(it->state == READ_STATE)
				{
					it->readers++;
					RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");

					if(cache_item_key_matches(it,key,keylen))
					{	
						RAM_GEN_LOG_WRITE("RAM CACHE GET KEY MATCHES ");
						//move to head of LRU
						lru_update(it);

						goto done;
					}
					else
					{
						read_release(it);
					}

				}
				else //we can now go by the lock, if we get it
				{
					//lock means we will skip over things being evicted
					bool gotLock = RTE_TRYLOCK(&it->lock,"CAHCE ITEM LOCK");

					//we dont care about state changes now, if we got the lock, its ours/
					// if we didnt its means its being evicted
					RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");

					//if we dont get lock its being evicted
					if(gotLock)
					{	

						RAM_GEN_LOG_WRITE("RAM CACHE GET GOTLOCK ");

						// if we are using the item, keep lock up for returning of data later....
						if(cache_item_key_matches(it,key,keylen))
						{	
							RAM_GEN_LOG_WRITE("RAM CACHE GET KEY MATCHES ");

							RTE_LOCK(&it->state_lock,"ITEM STATE LOCK");
							it->state = READ_STATE;
							it->readers = 1;
							RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");


							//move to head of LRU
							lru_update(it);

							goto done;
						}

						RAM_GEN_LOG_WRITE("RAM CACHE GET KEY DOESNT MATCHE ");
						
						//only get here if key doesnt match
						RTE_UNLOCK(&it->lock,"CACHE ITEM LOCK");
						
					}
					else
					{
						//we want to keep looking but first let the eviction remove the item
						it = it->next;
						//no release the barrier for eviction, then continue
						RTE_UNLOCK(&entry->barrier,"RAM CACHE BARRIER")
						usleep(1);
						RTE_LOCK(&entry->barrier,"RAM CACHE BUCKET")
						continue;		

					}

				}

				
			}
			
			/*// important to note that even when evicting, the it->next should
			// still give us the next thing in the list
			if(it ==  h->it)
			{
				RAM_GEN_LOG_WRITE_2("THEY ARE SAME ",count);
				count = 0;
			}
			count++;*/
			it = it->next;
    }

done:

 	RTE_UNLOCK(&entry->barrier,"RAM CACHE BARRIER");
    RTE_UNLOCK(&entry->lock,"RAM CACHE BUCKET LOCK");


    RAM_GEN_LOG_WRITE("RAM CACHE GET DONE");
    
    
    return it;
}

inline void read_release(struct cache_item* it)
{
	/*RAM_GEN_LOG_WRITE("READ RELEASE START");

	RTE_LOCK(&it->state_lock,"ITEM STATE LOCK");
	it->readers--;
	if(it->readers == 0) 
	{*/
		it->state = NON_STATE;
		it->readers = 0;
		RTE_UNLOCK(&it->lock,"ITEM LOCK");
	/*}
	RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");

	RAM_GEN_LOG_WRITE("READ RELEASE DONE");*/

}




// right now when writing, no other writes or reads can come in
void cache_ht_set(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv,size_t version)
{
	GEN_LOG_WRITE("RAM CACHE SET START");


	struct ht_entry* entry = ht + (hv % ht_entry_num);
	
	
    RTE_LOCK(&entry->lock, "RAM CACHE BUCKET");


	// if entry invalid, write it, guaranteed no reads or writes will be added here. 
	if(!entry->valid)
	{
		GEN_LOG_WRITE("RAM CACHE SET FIRST ENTRY ");

		/* SHOULD NOT HAPPEN
	    if(entry >= ht + num_entries + 1){
			RAM_GEN_LOG_WRITE("TOO FAR ENTRY " );
			exit(0);
		}*/

	 	entry->it = write_entry(key,keylen,val,vallen,hv,version);
	 	lru_update(entry->it);
	 	entry->valid = true;

	 	goto done;
	}
	else // look for entry, if not there add it
	{
		GEN_LOG_WRITE("NON_EMPTY ENTRY ");

		// need the barrier to make sure no one is modifying the chain
		RTE_LOCK(&entry->barrier, "RAM CACHE BARRIER");

		struct cache_item* it = entry->it;
		while(it != NULL)
		{
			// if this is the same item, overwrite the value
			if(it->hv == hv) 
			{

				RTE_LOCK(&it->state_lock,"ITEM STATE LOCK");

				//if were in a modifiable state 
				if(it->state == READ_STATE)
				{
					RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");
					//release the barrier lock for evict and then regain it before continueing
					//no one else can get in cause we got entry lock
					RTE_UNLOCK(&entry->barrier, "RAM CACHE BARRIER");
					usleep(1);
					RTE_LOCK(&entry->barrier, "RAM CACHE BARRIER");
					continue;
				}

				//we are guaranteed here the state is nothing
				//so if we get the lock then its our, otherwise its being evicted


				//check to see if someone else has lock...
				bool gotLock = RTE_TRYLOCK(&it->lock,"CACHE ITEM LOCK");
				RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");

				//if we dont get the lock its being evicted or there is currently a get on the item
				if(gotLock)
				{
					

					if(cache_item_key_matches(it,key,keylen) )
					{	
						RTE_UNLOCK(&entry->barrier,"RAM CACHE ITEM LOCK");

						if(version < it->version) 
						{
							goto done;
						}

						RTE_LOCK(&it->state_lock,"ITEM STATE LOCK");
						it->state = WRITE_STATE;
						RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");

						overwrite(it,val,vallen,version);
						lru_update(it);

						RTE_LOCK(&it->state_lock,"ITEM STATE LOCK");
						it->state = NON_STATE;
						RTE_UNLOCK(&it->state_lock,"ITEM STATE LOCK");



						RTE_UNLOCK(&it->lock,"ITEM LOCK");
					    

						goto done;
					}

					
					RTE_UNLOCK(&it->lock,"RAM CACHE ITEM LOCK");
						
				}
				else  // we know that this item is being evicted
				{

					//we want to keep looking but first let the eviction remove the item
					it = it->next;
					//no release the barrier for eviction, then continue
					RTE_UNLOCK(&entry->barrier,"RAM CACHE BARRIER")
					usleep(1);
					RTE_LOCK(&entry->barrier,"RAM CACHE BUCKET")
					continue;					
				}
				

				
			}
			it = it->next;
		}
		
		RTE_UNLOCK(&entry->barrier,"ITEM CACHE BARRIER");
		GEN_LOG_WRITE("NEW WRITE ");

		//it did not match insert at head of chain	
		struct cache_item* new_it = write_entry(key,keylen,val,vallen,hv,version);


		//lock down the chain and add to it
		RTE_LOCK(&entry->barrier,"ITEM CACHE BARRIER");
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
		RTE_UNLOCK(&entry->barrier,"ITEM CACHE BARRIER");

		lru_update(new_it);

		goto done;
	}

    

done:
	
    RTE_UNLOCK(&entry->lock,"RAM HASH BUCKET ENTRY");

    GEN_LOG_WRITE("RAM CACHE SET DONE");

    return;
}




void lru_update(struct cache_item* it)
{	
	RAM_GEN_LOG_WRITE("LRU UPDATE START ");
	RAM_GEN_LOG_WRITE_2("LRU ITEM:",(size_t)it);

    RTE_LOCK(&lru.lock,"LRU LOCK");

    if(lru.head == NULL)
    {
    	RAM_GEN_LOG_WRITE("LRU: HEAD IS NULL");
    	lru.head = it;
    	lru.tail = it;
    }
    else if(it == lru.head)
    {
    	// do nothing
    	
    	RAM_GEN_LOG_WRITE("LRU: ITEM IS HEAD");

    }
    else if(it == lru.tail)
    {

    	// not the head
    	RAM_GEN_LOG_WRITE("LRU: ITEM IS TAIL");
    	struct cache_item* prev = it->lru_prev;
    	RAM_GEN_LOG_WRITE_2("LRU PREV: ",(size_t)prev);
    	prev->lru_next = NULL;
    	lru.tail = prev;
    	

    	it->lru_next = lru.head;
	   	lru.head->lru_prev = it;
	   	lru.head = it;
	   	it->lru_prev = NULL;

    }
    else {
    	RAM_GEN_LOG_WRITE("LRU: NORMAL");
		struct cache_item* next = it->lru_next;
		struct cache_item* prev = it->lru_prev;

		if(next != NULL) next->lru_prev = prev;
		if(prev != NULL) prev->lru_next = next;


	   	it->lru_next = lru.head;
	   	lru.head->lru_prev = it;
	   	lru.head = it;
	   	it->lru_prev = NULL;
   	}

   	if(lru.head == lru.tail)
	{
		RAM_GEN_LOG_WRITE_2("LRU UPDATE HEAD IS TAIL:",(size_t)lru.tail);
	}

    RTE_UNLOCK(&lru.lock,"LRU LOCK");

	RAM_GEN_LOG_WRITE("LRU UPDATE END ");
}







/*****      LOG Editing Methods    *************/






// will only be called if evict lock is held
// Evicts MIN_EVICT bytes
//Need to redo eviction algorithm
void evict()
{

	RAM_GEN_LOG_WRITE("EVICT START");

	//number of blocks to evict
	int64_t amount = MIN_EVICT;
	//+ 1 so dont do under the amount
	amount = (amount / LOG_BLOCK_SIZE)  + 1;

	int64_t freed_tot = 0;

	while(amount > 0)
	{
		RTE_LOCK(&lru.lock,"LRU LOCK");
		struct cache_item* tail = lru.tail;
		RTE_UNLOCK(&lru.lock,"LRU LOCK");


		RAM_GEN_LOG_WRITE_2("EVICTING TAIL",(size_t)tail);
		//wont need to unlock becasuse item will be deleted
		
		bool gotLock = RTE_TRYLOCK(&tail->lock,"LRU TAIL");
		

		//if got lock, evict
		//otherwise, wait for tail to change so you can evict
		if(gotLock)
		{
			remove_from_lru(tail);
			remove_from_chain(tail);			
			int freed = tail->size;
			struct fragment* new_tail = evict_item(tail);
			amount -= freed;
			freed_tot += freed;
			add_to_free(tail,new_tail);
			

			//dont release lock cause its been deleted;
		}
		else
		{
			//TODO: make it easier
			lru_update(tail);
			//usleep(1);
		}
	}


	RTE_LOCK(&free_list.lock,"FREE LIST");
	
	free_list.num_free_blocks += freed_tot;
	
	RTE_UNLOCK(&free_list.lock,"FREE LIST LOCK");


	RAM_GEN_LOG_WRITE("RAM EVICT DONE ");

}

// will have lock on item for this
//want to remove from bucket chain
void remove_from_chain(struct cache_item* it)
{
	RAM_GEN_LOG_WRITE("REMOVE FROM CHAIN START ");

	struct ht_entry* entry = ht + (it->hv % ht_entry_num);

	RTE_LOCK(&entry->barrier,"RAM BUCKET BARRIER");

	struct cache_item* prev = it->prev;
	struct cache_item* next = it->next;

	if(prev != NULL)
	{
		prev->next = next;
	}
	else
	{
		entry->it = next;
		if(next == NULL) entry->valid = false;
	}

	if(next != NULL) next->prev = prev;


	RTE_UNLOCK(&entry->barrier,"RAM BUCKET BARRIER");


	RAM_GEN_LOG_WRITE("REMOVE FROM CHAIN END");

}




// the only deals with the tail, which is only modified by the evict method, which 
// will only run single threaded
void remove_from_lru(struct cache_item* it)
{
	RAM_GEN_LOG_WRITE("REMOVE FROM LRU START ");
	
	//have to grab lock so that dont have a race 
	//condition with item writes and overwrites to the prev
	//baciscally need to make sure we actually grab the LRU tail
    RTE_LOCK(&lru.lock,"LRU LOCK");

	struct cache_item* next = it->lru_next;
	struct cache_item* prev = it->lru_prev;

	if(next != NULL) next->lru_prev = prev;
	if(prev != NULL) prev->lru_next = next;

	if(it == lru.tail)
	{ 
		RAM_GEN_LOG_WRITE_2("NEXT:",(size_t)next);
		RAM_GEN_LOG_WRITE_2("PREV:",(size_t)prev);
		RAM_GEN_LOG_WRITE_2("HEAD:",(size_t)lru.head);
		RAM_GEN_LOG_WRITE_2("TAIL:",(size_t)lru.tail);
		lru.tail = prev;
		


	}

	if(it == lru.head)
	{
		lru.head = next;
	}

	if(lru.head == lru.tail)
	{
		RAM_GEN_LOG_WRITE_2("REMOVE FROM LRU HEAD IS TAIL:",(size_t)lru.tail);
	}


    RTE_UNLOCK(&lru.lock,"LRU LOCK");

    RAM_GEN_LOG_WRITE("REMOVE FROM LRU END ");

}


// add to front of free list
void add_to_free(struct cache_item* it, struct fragment* last)
{
	RAM_GEN_LOG_WRITE("ADD TO FREE START ");

	RTE_LOCK(&free_list.lock,"FREE LIST");
	struct fragment* new_frag = (struct fragment*)it;
	struct fragment* old_head = (struct fragment* )free_list.head;
	last->nextFree = old_head;
	free_list.head = new_frag;
	RTE_UNLOCK(&free_list.lock,"FREE LIST");


	RAM_GEN_LOG_WRITE("ADD TO FREE END ");

}



struct fragment* evict_item(struct cache_item *it)
{
	RAM_GEN_LOG_WRITE("STARTING EVICT ITEM");
	struct fragment* last = (struct fragment*)it;
	struct fragment* first = last;


	//TESTING
	size_t* key = (size_t*)(it+1);
	RAM_GEN_LOG_WRITE_2("EVICTING ITEM: ",*key);

    struct fragment *frag = it->more_data;


    //shouldnt ahve to do this
    memset(it,0,sizeof(struct cache_item));

    
    //set up this item as a free list, when added to end of 
    last->nextFree = (struct log_entry*)frag;
    last->next = NULL;
    last->valid = 0;
	last->size = 0;

	first->size = 1;

    //evict all the fragments
    while(frag != NULL)
    {
    	// clear the frag and add to free list, started with the item head
    	struct fragment *next_frag = frag->next;
    	memset(frag,0,sizeof(struct cache_item));
    	frag->valid = 0;
    	frag->size = 0;
    	frag->nextFree = (struct log_entry*)next_frag;
    	frag->next = NULL;
    	//last should end up being last non-null frag
    	last = frag;
    	frag = next_frag;
    	first->size++;

    }

    RAM_GEN_LOG_WRITE("ENDING EVICT ITEM");

    return last;
}




/**

Get a free block. If there are too few left, go evict. If you try and evict
and someone else is already evicting, block, then check to see if you still need to evict before evicting more. 


 **/
void* getFree()
{
	RAM_GEN_LOG_WRITE("GET FREE STARTING ");

	if(free_list.num_free_blocks < EVICT_POINT)
	{
		
		RTE_LOCK(&evict_lock,"EVICT LOCK");
		
		if(free_list.num_free_blocks < EVICT_POINT){
			evict();
		}
		
		RTE_UNLOCK(&evict_lock,"EVICT LOCK");
	}

	//RTE_LOCK(&free_list.lock,"FREE LIST LOCK");
	rte_spinlock_lock(&free_list.lock);


	struct fragment *head = (struct fragment*)free_list.head;

	

	struct log_entry* next = head->nextFree;

	RAM_GEN_LOG_WRITE_2("GET FREE HEAD ",(size_t)head);
	RAM_GEN_LOG_WRITE_2("GET FREE NEXT ",(size_t)next);
	size_t end = (size_t)(log_ + num_entries+1);
	RAM_GEN_LOG_WRITE_2("LOG END ",end);

	//if we have no pointer to next, then it is simply the next block
	if(next == NULL) next = free_list.head+1;
	

	//clear out the block we are returning
	memset(head,0,sizeof(struct cache_item));

	free_list.head = next;
	free_list.num_free_blocks--;

	
	//RTE_UNLOCK(&free_list.lock,"FREE LIST LOCK");
	rte_spinlock_unlock(&free_list.lock);

	RAM_GEN_LOG_WRITE("GET FREE ENDING ");

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
	RAM_GEN_LOG_WRITE("WRITE_ENTRY START ");



	/** Next and prev item in the hash chain. */
	struct cache_item *next;	
	struct cache_item *prev;

	struct cache_item *lru_next;
    struct cache_item *lru_prev;




	//TESTING
	keyNUM++;

	struct cache_item* it = getFree();
	it->valid = true;
	it->hv = hv;
	it->size = 1;
	it->version = version;
	it->state = NON_STATE;
	it->readers = 0;
	it->next = NULL;
	it->prev = NULL;
	it->lru_next = NULL;
	it->lru_prev = NULL;
	rte_spinlock_init(&it->lock);
	rte_spinlock_init(&it->state_lock);

    it->keylen = keylen;
    it->vallen = vallen;

    char *src = (char *)key;
    char *dest = (char* )(it+1);
	struct fragment **more_data_ptr = &it->more_data;
	*more_data_ptr = NULL;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(struct cache_item);
    //copy key
	struct fragment* frag = cache_write(rest,src,dest,keylen, more_data_ptr,it);


	if(frag == NULL)
	{
		//if we didnt write into a new fragment, just move up our dest and rest
		RAM_GEN_LOG_WRITE("NO NEW FRAG");
		RAM_GEN_LOG_WRITE_2("NEW DATA PTR",(size_t)*more_data_ptr);
		dest += keylen;
		rest -= keylen;
	}
	else
	{
		// if we wrote into a fragment, move our dest
		// data_ptr, and rest up accordingly
		RAM_GEN_LOG_WRITE("NEW FRAG");
		more_data_ptr = &frag->next;
		dest=((char *)(frag+1));
		dest += frag->size;
		rest = LOG_BLOCK_SIZE - sizeof(struct fragment) - frag->size;
	}

	//write the value
    src = (char *)val;
    cache_write(rest,src,dest,vallen, more_data_ptr,it);


    RAM_GEN_LOG_WRITE("WRITE_ENTRY END ");

    return it;
}


#define COND_WR keyNUM >= 3589 
// writes out data and returns pointer to last written fragment
// if the fragment points to more fragemnts uses them, otherwise uses new block
struct fragment* cache_write(size_t rest,char *src, char *dest,int64_t length,struct fragment **more_data_ptr,struct cache_item* it)
{
	RAM_GEN_LOG_WRITE("WRITE START  ");

	struct fragment *frag = NULL;

	//if rest == 0, we have used up all the block
	// if we dont have a more data ptr,
	if(rest == 0)
    {
    	RAM_GEN_LOG_WRITE("WRITE REST RESET 0");
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
    		RAM_GEN_LOG_WRITE("OVERWRITE ACTIVATED at RESET 0");
    		frag = *more_data_ptr;
    	}
    	dest = (char *)(frag+1);
    	more_data_ptr  = &frag->next;
    	rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
    	frag->valid = true;   	
    }

	size_t amount = rest < length ? rest : length;

	char* end = ((char*)log_) + ((num_entries + 1)* LOG_BLOCK_SIZE);
	//TODO: check this
    if(dest + amount > end ){
		printf("TOO FAR \n" );
		exit(0);
	}

	//TESTING
	//display(src,dest,amount);
	RAM_GEN_LOG_WRITE_2("MEMSET:  ",(size_t) dest);
	RAM_GEN_LOG_WRITE_2("END: ",(size_t) end);
	MEMCPY(dest,src,amount,"CACHE WRITE");
	RAM_GEN_LOG_WRITE("MEMSET DONE");



	dest+=amount;

    // change vallen to new amount needed to copy
    length -= amount;

    // jump amount farther in val
    src += amount;
    rest -= amount;
    //TODO: FRAG should not equal NULL
    if(frag != NULL) frag->size = amount;

    // if need more space for key, continuously copy. 
    // will only enter this loop if previous block has been filled
    while(length > 0)
    {
    	//if here implies we are at start of a block
    	if(*more_data_ptr == NULL)
    	{
    		RAM_GEN_LOG_WRITE("MORE DATA IS NULL");
    		frag  = (struct fragment*)getFree();
    		*more_data_ptr = frag;
    		it->size +=1;
    	}
    	else
    	{
    		RAM_GEN_LOG_WRITE_2("MORE DATA EXISTS",(size_t)*more_data_ptr);
    		frag = *more_data_ptr;
    	}


    	dest = (char *) (frag+1);
    	rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
	    amount = rest < length ? rest : length;

	    RAM_GEN_LOG_WRITE_2("MEMSET:  ",(size_t) dest);
		RAM_GEN_LOG_WRITE_2("BEG: ",(size_t)log_ );
		MEMCPY(dest,src,amount,"CACHE WRITE");
		RAM_GEN_LOG_WRITE("MEMSET DONE");

	    length -= amount;

	    //reset more data ptr and set this block to valid
	    more_data_ptr  = &frag->next;
	    frag->valid = true;
	    frag->size = amount;

	    dest += amount;

    }


    //release extra space we are holding would be nice, but complicated so dont do it right now
    if(*more_data_ptr != NULL)
    {
    	struct cache_item* to_evict = (struct cache_item*)(*more_data_ptr);
    	struct fragment* to_evict_frag = *more_data_ptr;
    	struct fragment* next = to_evict_frag->next;
    	to_evict->more_data = next;
    	struct fragemnt* last = evict_item(to_evict);
    	add_to_free(to_evict,last);
    	RTE_LOCK(&free_list.lock,"FREE LIST LOCK");
    	free_list.num_free_blocks += ((struct fragment*)to_evict)->size;
    	RTE_UNLOCK(&free_list.lock,"FREE LIST LOCK");
    	*more_data_ptr = NULL;
    }



 	RAM_GEN_LOG_WRITE("WRITE END ");

    return frag;


}

//will never enter here if evicting block, and will never try and evict if entering here
void overwrite(struct cache_item* it, void* val, size_t vallen, size_t verison)
{
	RAM_GEN_LOG_WRITE("OVERWRITE START ");
	//lru_update(it);

	size_t keylen = it->keylen;
    it->size = 1;
    it->version = verison;
    it->vallen = vallen;
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
	    //not sure if this is right
	    it->size += 1;
	}

	cache_write(rest,val,dest,vallen, more_data_ptr,it);
	RAM_GEN_LOG_WRITE("OVERWRITE END ");
}



