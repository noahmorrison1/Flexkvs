#include "ssd_ht.h"
#include "global.h"


static size_t ssd_num_entries;
static size_t ssd_num_ht_entries;
static struct ssd_ht_entry* ssd_ht;
static struct ssd_item* ssd_log;
static int fd;
static void* ssd;

struct {
	struct ssd_item* head;
	struct ssd_item* tail;
	size_t num_free_slots;
	rte_spinlock_t lock;
} ssd_free_log;



struct {
	size_t page_num;
	uint16_t offset;
	void* cur_page;
	rte_spinlock_t lock;
} ssd_free_pages;




void ssd_init()
{
	fd = open("db",O_CREAT | O_RDWR);
    ssd = mmap(0, SSD_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

void ssd_ht_init(void) {


	ssd_num_entries = SSD_HT_SIZE/sizeof(struct ssd_item);
	ssd_num_ht_entries = ssd_num_entries/SSD_NUM_COLLISIONS;
    ssd_log = calloc(ssd_num_entries, sizeof(struct ssd_item));
    ssd_ht = calloc(ssd_num_ht_entries,sizeof(struct ssd_ht_entry));
    ssd_free_log.num_free_slots = ssd_num_entries;

    if (ssd_log == NULL || ssd_ht == NULL) {
        perror("Allocating ssd_ht item hash table failed");
        abort();
    }



    //init locks
    for (int i = 0; i < ssd_num_entries/SSD_NUM_COLLISIONS; i++) {
        rte_spinlock_init(&ssd_ht[i].lock);
    }

    ssd_free_log.head = ssd_log;
    ssd_free_log.tail = ssd_log + ssd_num_entries -1;

    rte_spinlock_init(&ssd_free_log.lock);
    rte_spinlock_init(&ssd_free_pages.lock);
    // cur_page is the cur_page to be evicted to SSD
    // The page is just a log and evicted when full for now. 
    // Or a timeout
    // 0 is a NULL page
    ssd_free_pages.cur_page = make_page(1);
    
    ssd_init();
}


struct ssd_line* ssd_ht_get( void* key, size_t keylen,uint32_t hv)
{
	struct ssd_ht_entry* entry = ssd_ht + (hv % ssd_num_ht_entries);

	struct ssd_line *current = NULL;
	#ifndef NOHTLOCKS
	    rte_spinlock_lock(&entry->lock);
	#endif

		// if entry invalid, write it, write will 
		if(!entry->valid)
		{
		 	goto done;
		}
		else // look for entry, if not there add it
		{
			struct ssd_item* it = entry->it;
			while(it != NULL)
			{
				// if this is the same item, overwrite the value
				if(it->hv == hv) 
				{
					//only checks shortened key					
					if(ssd_item_key_matches(it,key,keylen))
					{

						//this whole thing will be calloced and will be 
						// ssd_line then key then value
					    struct ssd_line *current = malloc(sizeof(struct ssd_line) + keylen + it->vallen);
						ssd_read(it,current);
						TEST_PRINT("ITEM KEY MATCHES \n");
						//check if the full key matches
						if(ssd_key_matches(current,key,keylen)){							
							goto done;
						}
						//if it doesnt free this memory
						free(current);
						current = NULL;
					}					
				}
				it = it->next;
			}
		}
	    

	done:
	#ifndef NOHTLOCKS
	    rte_spinlock_unlock(&entry->lock);
	#endif
	
	return current;
}


size_t ssd_ht_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv)
{
	struct ssd_ht_entry* entry = ssd_ht + (hv % ssd_num_ht_entries);

	size_t version = -1;
	#ifndef NOHTLOCKS
	    rte_spinlock_lock(&entry->lock);
	#endif

		// if entry invalid, write it, write will 
		if(!entry->valid)
		{
			TEST_PRINT("NEW ENTRY \n");
		 	entry->it = ssd_write_entry(key,keylen,val,vallen,hv);
		 	entry->valid = true;
		 	version = entry->it->version;
		 	goto done;
		}
		else // look for entry, if not there add it
		{
			TEST_PRINT("ENTRY FILLED \n");
			struct ssd_item* it = entry->it;
			while(it != NULL)
			{
				// if this is the same item, overwrite the value
				if(it->hv == hv) 
				{
					//only checks shortened key					
					if(ssd_item_key_matches(it,key,keylen))
					{

						//this whole thing will be calloced, only read in keylen
						struct ssd_line *current = malloc(it->num_headers * sizeof(struct ssd_header) + keylen + it->vallen);
						ssd_read_key(it,current);
						//check if the full key matches
						if(ssd_key_matches(current,key,keylen)){							
							ssd_overwrite(it,val,vallen,key,keylen);
							//note the version will have been overwritten so we can do this
							version = it->version;
							goto done;
						}
						free(current);
					}					
				}
				it = it->next;
			}

			//will go here if the key doesnt exist
			struct ssd_item* old = entry->it;
			//writes new kv to ssd then creates new item and returns it
			struct ssd_item* new_it = ssd_write_entry(key,keylen,val,vallen,hv);
			old->prev = new_it;
			new_it->next = old;
			entry->it = new_it;
			version = it->version;
			goto done;
		}

	    

	done:
	#ifndef NOHTLOCKS
	    rte_spinlock_unlock(&entry->lock);
	#endif
	    return version;
}

bool ssd_key_matches(struct ssd_line* current, void* key, size_t keylen)
{
	return __builtin_memcmp(current->key,key,keylen) == 0;
}

bool ssd_item_key_matches(struct ssd_item* it, void* key,size_t keylen)
{
	size_t size = keylen < sizeof(it->key) ? keylen : sizeof(it->key);
	return it->keylen == keylen && __builtin_memcmp(&it->key,key,size) == 0;
}


void ssd_overwrite(struct ssd_item *it, void* val, size_t vallen,void* key, size_t keylen)
{
	it->valid = true;
	it->num_headers = 1;
	it->keylen = keylen;
	it->vallen = vallen;

	void* srcs[2] = {key,val};
	size_t sizes[2] = {keylen,vallen};

	ssd_write(srcs,sizes,2,it);
	it->version++;
}



struct ssd_item* ssd_write_entry(void *key, size_t keylen, void* val, size_t vallen, uint32_t hv)
{
	struct ssd_item* it = getFreeLogSlot();
	it->valid = true;
	it->hv = hv;
	it->num_headers = 1;
	it->keylen = keylen;
	it->vallen = vallen;
	it->version = 0;
	it->start_page = -1;
	//copy truncated key
	size_t size = keylen < sizeof(it->key) ? keylen : sizeof(it->key);
	memcpy(&it->key,key,size);
	void* srcs[2] = {key,val};
	size_t sizes[2] = {keylen,vallen};

	ssd_write(srcs,sizes,2,it);
	return it;

}

void ssd_write(void** srcs, size_t *sizes, uint16_t num_srcs, struct ssd_item* it )
{
	//write out up to 10 pages
	int wo_index = 0;
	void* to_write_out[10];

	size_t totsize = 0;
	for(int i = 0; i < num_srcs; i++)
	{
		totsize +=sizes[i];
	}

	// dest to write to
	char* dest = NULL;
	//current page of the SSD we are writing out stuff to
	size_t page_num = 0;
	//The offset within this page to write to
	uint16_t offset = 0;
	//A ptr the actual allocated memory for the current page
	void* cur_page = NULL;
	//how much more memory is available to us on this page
	uint16_t rest = 0;
	//The place in the previous header that points to 
	//the continuation of data. 
	size_t* next_ptr = NULL;
	//same as above but for offset into that page
	uint16_t* next_offset = 0;

	size_t consumed = 0;

	for(int i = 0; i < num_srcs; i++)
	{
		char* src = srcs[i];
		size_t size = sizes[i];
		while(size > 0)
		{
			if(rest == 0)
			{
				#ifndef NOHTLOCKS
				    rte_spinlock_lock(&ssd_free_pages.lock);
				#endif

				    //get where we need to write to
				    void* old_page = cur_page;
			    	cur_page = ssd_free_pages.cur_page;
			    	page_num = ssd_free_pages.page_num;
			    	offset = ssd_free_pages.offset;
			    	dest = cur_page + offset;

			    	
			    	if(it->start_page == -1) it->start_page;
			    	//claim header memory in rest before we write to it
			    	rest = SSD_PAGE_SIZE - offset - sizeof(struct ssd_header);

			    	// if we write to the end of this page, make a new one
				    if(totsize > rest || rest - totsize < SSD_MIN_ENTRY_SIZE)
				    {	
				    	if(wo_index >= 10) 
				    	{
				    		printf("Trying to write out too many pages...");
				    		#ifndef NOHTLOCKS
							    rte_spinlock_unlock(&ssd_free_pages.lock);
							#endif
							return;
				    	} 
				    	ssd_free_pages.offset = sizeof(struct free_page_header);
				    	ssd_free_pages.page_num++;
				    	ssd_free_pages.cur_page = make_page(ssd_free_pages.page_num);			    	
				    }

				#ifndef NOHTLOCKS
				    rte_spinlock_unlock(&ssd_free_pages.lock);
				#endif


				if(next_ptr != NULL)
				{
					*next_ptr = page_num;
					*next_offset = offset;
					//we no longer care if old page is written out
					// so consume the data we used in it
					if(consume(old_page,consumed))
					{
				    	//if we finished the page, we need to write it out.
				    	to_write_out[wo_index] = old_page;
				    	wo_index++;
					}
				}

				struct ssd_header* new_header = (struct ssd_header*)dest;
				new_header->size = totsize > rest ? rest : totsize;
				new_header->next = 0;
				next_ptr = &(new_header->next);
				next_offset = &(new_header->offset);
				it->num_headers++;
				dest+= sizeof(struct ssd_header);
				


				consumed = sizeof(struct ssd_header);
			}

			uint16_t amount = rest > size ? size : rest;
			memcpy(src,dest,amount);
			rest -= amount;
			dest += amount;
			size -= amount;
			totsize -= amount;
			consumed+= amount;

		}

	}

	//consume the last stuff we wrote out
	if(consumed != 0) {
		if(consume(cur_page,consumed))
		{
	    	//if we finished the page, we need to write it out.
	    	to_write_out[wo_index] = cur_page;
	    	wo_index++;
		}
	}
	//write out all the pages for which we are responsible
	write_out_all(wo_index,to_write_out);
}


size_t ssd_delete(void *key, size_t keylen, uint32_t hv)
{
	return 0;
}

bool consume(void* curr_page,size_t amount)
{
	struct free_page_header* page = (struct free_page_header*)curr_page;
	#ifndef NOHTLOCKS
	    rte_spinlock_lock(&page->lock);
	#endif

	    page->capacity -= amount;
	    bool ret = page->capacity == 0;

	#ifndef NOHTLOCKS
	    rte_spinlock_unlock(&page->lock);
	#endif
	return ret;    	
}

void* make_page(size_t num)
{
	struct free_page_header* page = calloc(SSD_PAGE_SIZE+sizeof(struct free_page_header),1);
	rte_spinlock_init(&page->lock);
	page->capacity = SSD_PAGE_SIZE;
	page->num = num;
	return (void*)(page);
}


struct ssd_item* getFreeLogSlot()
{
	#ifndef NOHTLOCKS
	    rte_spinlock_lock(&ssd_free_log.lock);
	#endif
	    if(ssd_free_log.head == ssd_free_log.tail){
	     printf("Ran out of SSD HT Items");
	     return NULL;
	 	}
	   	struct ssd_item* head = ssd_free_log.head;
	   	ssd_free_log.head = head->next == NULL ? head+1 : head->next;

	#ifndef NOHTLOCKS
	    rte_spinlock_unlock(&ssd_free_log.lock);
	#endif
	return head;
}


/**           NOT IMPLEMENTED YET            **/



void write_out_all(int wo_count,void** pages)
{

	for(int i = 0; i < wo_count; i++)
	{
		write_out((struct free_page_header*)pages[i]);
	}	

}


/**Do last... Probably just mmap a file and write it out to disk, need to think long and hard about this **/
void write_out(struct free_page_header* page)
{
	char* dest = ssd;
	dest += page->num * SSD_PAGE_SIZE; 
	memcpy(page+1,dest,SSD_PAGE_SIZE);
	msync(dest,SSD_PAGE_SIZE, MS_SYNC);
	free(page);
}

/** also uses the mmapped file **/ 
struct ssd_line* ssd_read(struct ssd_item* it,void* memory)
{
	struct ssd_line* header = memory;
	header->vallen = it->vallen;
	header->keylen = it->keylen;
	header->version = it->version;

	char* dest = (char *)(header+1);
	header->key = dest;
	size_t len = header->keylen;
	char* src = ssd;
	src += it->page*SSD_PAGE_SIZE + it->offset;
	struct ssd_header* ssd_head = (struct ssd_header* )(src );
	src+= sizeof(struct ssd_header);
	size_t rest = len > SSD_PAGE_SIZE - it->offset - sizeof(struct ssd_header);

	while(len > 0)
	{
		size_t amount = len < rest ? len : rest;
		memcpy(src,dest,amount);
		len -= amount;
		rest -= amount;
		dest +=amount;
		src += amount;
		if(rest == 0)
		{
			char* src = ssd;
			src += ssd_head->next*SSD_PAGE_SIZE + ssd_head->offset;
			ssd_head = (struct ssd_header* )(src );
			src+= sizeof(struct ssd_header);
		}
	}

	len = it->vallen;
	header->key = dest;

	while(len > 0)
	{
		size_t amount = len < rest ? len : rest;
		memcpy(src,dest,amount);
		len -= amount;
		rest -= amount;
		dest +=amount;
		src += amount;
		if(rest == 0)
		{
			char* src = ssd;
			src += ssd_head->next*SSD_PAGE_SIZE + ssd_head->offset;
			ssd_head = (struct ssd_header* )(src );
			src+= sizeof(struct ssd_header);
		}
	}

	return header;
}

struct ssd_line* ssd_read_key(struct ssd_item* it,void* memory)
{
	struct ssd_line* header = memory;
	header->vallen = it->vallen;
	header->keylen = it->keylen;
	header->version = it->version;

	char* dest = (char *)(header+1);
	header->key = dest;
	size_t len = header->keylen;
	char* src = ssd;
	src += it->page*SSD_PAGE_SIZE + it->offset;
	struct ssd_header* ssd_head = (struct ssd_header* )(src );
	src+= sizeof(struct ssd_header);
	size_t rest = len > SSD_PAGE_SIZE - it->offset - sizeof(struct ssd_header);

	while(len > 0)
	{
		size_t amount = len < rest ? len : rest;
		memcpy(src,dest,amount);
		len -= amount;
		rest -= amount;
		dest +=amount;
		src += amount;
		if(rest == 0)
		{
			char* src = ssd;
			src += ssd_head->next*SSD_PAGE_SIZE + ssd_head->offset;
			ssd_head = (struct ssd_header* )(src );
			src+= sizeof(struct ssd_header);
		}
	}

	len = it->vallen;
	header->key = dest;

	return header;
}



