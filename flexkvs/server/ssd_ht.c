#include "ssd_ht.h"
#include "global.h"


// number of possible entires that can be stored in the hash table
static size_t ssd_num_entries;
// number of slots in the hashtable, the above / by collisions
static size_t ssd_num_ht_entries;
//point to head of ssh hash table, hashtable stores pointers to items
static struct ssd_ht_entry* ssd_ht;
// the log storing all the items for the hashtable
static struct ssd_item* ssd_log;
// file descriptor for the 
static int fd;
//pointer to the beggining of the file, which is the backend of the DB
static void* ssd;
//the number of pages in the circular page buffer.buffer
// This buffer is used to store pages before being staged to SSD/HD
static uint16_t page_buf_size;


/*

Keeps track of where there are free items available for allocation
in the SSD hash table. If the current head has item->next == NULL
Then the next free page is the page consecutive to the head. Deleted 
items are put at the front of the queue.

*/
struct {
	struct ssd_item* head;
	struct ssd_item* tail;
	size_t num_free_slots;
	rte_spinlock_t lock;
} ssd_free_log;



struct {
    size_t last_written[10000];
    uint32_t head;
    rte_spinlock_t lock;
} written_counter;

/*

Holds the current page in the page buffer that
is being written to as well as the page_num of where in
SSD the page should be written. The offset is how 
far into the page has been write so far.

----
The main reason for this is I want to seperate the logic of getting a new page
And accessing what page we are writing to, could put in same struct as page buffer

*/
struct {
	size_t page_num;
	uint16_t offset;
	struct page* cur_page;
	rte_spinlock_t lock;
} ssd_free_pages;


/*

Circular buffer of the pages to be staged to SSD.
Each core can write up to PAGES_PER_CORE pages. In the 
buffer at one time. The buffer has more than enough pages
so it will never run into problems.

*/
struct {
	//buffer
	struct page* pages;

	//indices
	uint8_t tail;
	int8_t head;

	rte_spinlock_t lock;

} page_buffer;




void ssd_init()
{
	fd = open("output.dat",O_CREAT | O_RDWR);
    ssd = mmap(0, SSD_SIZE, PROT_READ | PROT_WRITE , MAP_SHARED, fd, 0);
    if(ssd == NULL) TEST_PRINT("SSD IS NULL\n");
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
	rte_spinlock_init(&page_buffer.lock);
	rte_spinlock_init(&written_counter.lock);
	written_counter.head = 0;
	for(int i = 0; i < 10000; i++)
	{
	    written_counter.last_written[i] = 0;
	}

    //clear the page buffer
    page_buffer.tail = 0;
    page_buffer.head = 0;


    //over allocate buffer
    page_buf_size = (rte_lcore_count() +1) * PAGES_PER_CORE + 5;
    page_buffer.pages = calloc(page_buf_size,sizeof(struct page));
    
    //init locks for page buffer
    for(int i = 0; i < page_buf_size; i++)
    {
    	struct free_page_header* p = (struct free_page_header*)&page_buffer.pages[i];
    	rte_spinlock_init(&p->lock);
    	//this shouldnt matter, but do it for safety
        p->written_out = true;
    }


	ssd_free_pages.page_num = 0;
    swap_current_page();

    
    ssd_init();
}


struct ssd_line* ssd_ht_get( void* key, size_t keylen,uint32_t hv)
{
    
	struct ssd_ht_entry* entry = ssd_ht + (hv % ssd_num_ht_entries);
	
    // for testing
    size_t full_key = *((size_t*)key);
    
    
	struct ssd_line *current = NULL;
	
	    RTE_LOCK(&entry->lock,"ENTRY");
	


		// if entry invalid, then dont have key
		if(!entry->valid)
		{
		 	goto done;
		}
		else // look for entry
		{
			struct ssd_item* it = entry->it;
			// go through chain
			while(it != NULL)
			{
				
				if(it->hv == hv) 
				{
					//only checks shortened key					
					if(ssd_item_key_matches(it,key,keylen))
					{
						// malloc the memory then read whole item from ssd
					    current = malloc(sizeof(struct ssd_line) + keylen + it->vallen);
						ssd_read(it,current);

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

	RTE_UNLOCK(&entry->lock,"ENTRY");
	    
	return current;
}


size_t ssd_ht_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv)
{
	struct ssd_ht_entry* entry = ssd_ht + (hv % ssd_num_ht_entries);

	size_t version = -1;

	    RTE_LOCK(&entry->lock,"ENTRY");

        size_t full_key = *((size_t*)key);

		// if entry invalid, write a new one
		if(!entry->valid)
		{
		 	entry->it = ssd_write_entry(key,keylen,val,vallen,hv);
		 	entry->valid = true;
		 	//version is for syncing with cache
		 	version = entry->it->version;
		 	
		 	goto done;
		}
		else // look for key, if not there add it
		{
			struct ssd_item* it = entry->it;
			while(it != NULL)
			{
				
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

			// will go here if the key doesnt exist
			struct ssd_item* old = entry->it;
			//writes new kv to ssd then creates new item and returns it
			struct ssd_item* new_it = ssd_write_entry(key,keylen,val,vallen,hv);
			
			// potential problems here but cant see any
			// feels like something that is normally locked
			old->prev = new_it;
			new_it->next = old;
			entry->it = new_it;
			version = it->version;

			goto done;
		}

	    

	done:
	
	    RTE_UNLOCK(&entry->lock,"ENTRY");

	    return version;
}



bool ssd_key_matches(struct ssd_line* current, void* key, size_t keylen)
{
    //TEST_PRINT_FINAL("CHecking Key: ", *((size_t*)key) );
    //TEST_PRINT_FINAL("READING KEY: ", *((size_t*)current->key) )
    
    //display only outputs differences
	display(current->key,key,keylen);
	
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
	
	//indicates the first page needs to be set
	it->first_page = -1;

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
	it->num_headers = 0;
	it->keylen = keylen;
	it->vallen = vallen;
	it->version = 0;
	//indicates the first page needs to be set
	it->first_page = -1;
	
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
	//Can have written up to PAGES PER CORE before staging some to disk
	
	//# of pages that have been written
	int wo_index = 0;
	//The pages that have been written to
	struct page* to_write_out[PAGES_PER_CORE];
	

	size_t totsize = 0;
	for(int i = 0; i < num_srcs; i++)
	{
		totsize +=sizes[i];
	}

    size_t key = *((size_t*)srcs[0]);
	// dest to write to
	char* dest = NULL;
	//current page of the SSD we are writing out stuff to
	size_t page_num = 0;
	//The offset within this page to write to
	uint16_t offset = 0;
	//A ptr the actual allocated memory for the current page
	struct page* cur_page = NULL;
	//how much more memory is available to us on this page
	uint16_t rest = 0;
	//The place in the previous header that points to 
	//the continuation of data. 
	size_t* next_ptr = NULL;
	//same as above but for offset into that page
	uint16_t* next_offset = 0;
	//the last page we wrote to
	struct page* old_page = NULL;
	
	
	//used to check if we deadlock in our loop
	// since this is the most common place I have found deadlock
	int deadlock = 0;



	for(int i = 0; i < num_srcs; i++)
	{
		char* src = srcs[i];
		size_t size = sizes[i];
		
		while(size > 0)
		{
		    //enter here once we have run out of room in our
		    // current page
			if(rest == 0)
			{
			
			        
				    RTE_LOCK(&ssd_free_pages.lock,"SSD_FREE_PAGES");
				


				    //get where we need to write to
				    old_page = cur_page;
			    	cur_page = ssd_free_pages.cur_page;




			    	struct free_page_header* cur_header = (struct free_page_header*)cur_page; 
			    	
			    	//we need to make sure that nobody else is writing out this page
			    	// already when we want to try and add outseles to the writers
			    	RTE_LOCK(&cur_header->lock,"CUR_HEADER");
			    	    if(!cur_header->written_out){
			    		    cur_header->writers++;
			    	    }
			    	    else
			    	    {
			    	        //otherwise we need a new page
			    	        RTE_UNLOCK(&cur_header->lock,"CUR_HEADER");
			    	        
			    	        //note we have ssd_free_page_lock
			    	        swap_current_page();
                            
                            //reset here so when we continue our old_page is 
                            //set correcctly
                            cur_page = old_page;
                            
                            RTE_UNLOCK(&ssd_free_pages.lock,"SSD_FREE_PAGES");
                            
                            
			    	        deadlock++;
			   
			    	        if(deadlock > 100) printf("LOCKED IN CUR HEADER \n");
			    	        
			    	        continue;
			    	        
			    	    }
			    	RTE_UNLOCK(&cur_header->lock,"CUR_HEADER");
                

                    //we now write to current ssd_free_page
			    	page_num = ssd_free_pages.page_num;
			    	offset = ssd_free_pages.offset;
			    	dest = ((char*)cur_page) + offset;

            
			    	if(it->first_page == -1)
			    	{
			    	 it->first_page = page_num;
			    	 it->offset = offset - sizeof(struct free_page_header);
			    	}

			    	//claim header memory in rest before we write to it
			    	//offset counts extra free_page header
			    	rest = SSD_PAGE_SIZE - offset - sizeof(struct ssd_header) + sizeof(struct free_page_header);


			    	// if we write to the end of this page, make a new one
				    if(totsize > rest || rest - totsize < SSD_MIN_ENTRY_SIZE)
				    {	
				    	cur_header->consumed = true;
				    	swap_current_page();
				    }
				    else //we should change the offset so others can write
				    {
				    	ssd_free_pages.offset += totsize + sizeof(struct ssd_header);
				    }

                    //set the last ptrs and release them, 
                    //might possibly move up to more restrictd section of code
                    if(next_ptr != NULL)
                    {
                        *next_ptr = page_num;
                        //dont count free_page_header in offset because not going to be written
                        *next_offset = offset - sizeof(struct free_page_header);
                        struct free_page_header* old_header = (struct free_page_header*)old_page; 
                        
                        //done writing to old page
                        RTE_LOCK(&old_header->lock,"OLD_HEADER");
                            old_header->writers--;
                        RTE_UNLOCK(&old_header->lock,"OLD_HEADER");        
                    }

                    

                    RTE_UNLOCK(&ssd_free_pages.lock,"SSD_FREE_PAGES");
                    
                    



				  if(wo_index >= PAGES_PER_CORE -1) 
                  {
                      wo_index = write_out_all(wo_index,to_write_out);
                  }   






				//add this as one of the pages im responsible for
		    	to_write_out[wo_index] = cur_page;
		    	wo_index++;

                if(offset > sizeof(struct free_page_header))
                {
                    printf("Writing into middle of page: %lu  page :: %d  amount :: %d   KEY :: %lu    ID :: %d \n",offset - sizeof(struct free_page_header),((char*)cur_page - (char*)page_buffer.pages)/sizeof(struct page),size, key, rte_lcore_id() );
                }
                
				struct ssd_header* new_header = (struct ssd_header*)dest;
				new_header->size = totsize > rest ? rest : totsize;
				new_header->next = 0;
				next_ptr = &(new_header->next);
				next_offset = &(new_header->offset);
				it->num_headers++;
				dest += sizeof(struct ssd_header);
				
				
			}

			uint16_t amount = rest > size ? size : rest;
			
			WRITE_TO_BUFFER(dest,src,amount,page_buffer.pages);
			
			
			
			struct free_page_header* cur_header = (struct free_page_header*)cur_page; 
			
			//if here is not getting written out because we have not 
			// decreased number of writers
	    	RTE_LOCK(&cur_header->lock,"CUR_HEADER");
	    		cur_header->timestamp = time(NULL);
	    	RTE_UNLOCK(&cur_header->lock,"CUR_HEADER");


			rest -= amount;
			dest += amount;
			src += amount;
			size -= amount;
			totsize -= amount;

		}

	}


	//free last page to be written out
	struct free_page_header* cur_header = (struct free_page_header*)cur_page; 
	
	
	RTE_LOCK(&cur_header->lock,"CUR_HEADER");
		cur_header->writers--;
	RTE_UNLOCK(&cur_header->lock,"CUR_HEADER");	
	
	
	
	

	//write out all the pages for which we are responsible
	forced_write_out_all(wo_index,to_write_out);
}


// not implemented yet
size_t ssd_delete(void *key, size_t keylen, uint32_t hv)
{
	return 0;
}



void swap_current_page()
{
    ssd_free_pages.offset = sizeof(struct free_page_header);
    ssd_free_pages.page_num++;
    ssd_free_pages.cur_page = get_new_page(ssd_free_pages.page_num);
}



struct ssd_item* getFreeLogSlot()
{
	    RTE_LOCK(&ssd_free_log.lock,"SSD_FREE_LOG");
	    
	    if(ssd_free_log.head == ssd_free_log.tail){
	     printf("Ran out of SSD HT Items");
	     return NULL;
	 	}
	   	struct ssd_item* head = ssd_free_log.head;
	   	ssd_free_log.head = head->next == NULL ? head+1 : head->next;

	    RTE_UNLOCK(&ssd_free_log.lock,"SSD_FREE_LOG");
	    
	return head;
}


bool can_write_out(struct free_page_header* p)
{
	bool timed_out =(time(NULL) -  p->timestamp) >= TIMEOUT;
	bool other =  p->writers <= 0 && (p->consumed || timed_out);
	if( other && time_out){ printf("TIMED OUT at :: %lu    ID:: %d \n",((char*)p - (Char*)page_buffer.pages)/sizeof(struct page) 
				     ,rte_lcore_id());}
	return other;
}

int write_out_all(int wo_count,struct page** pages)
{
	int front = 0;
	
	    
	if(TESTING_FINAL) {    
	    printf("WRITING OUT PAGES : ");
        for(int i = 0 ; i < wo_count; i ++ )
        {
            struct free_page_header* header = (struct free_page_header*)pages[i];
            printf(" - %lu - ",header->num);
            
        }
        printf(" ::  %d   \n",rte_lcore_id());
	}
 
	
	do 
	{
	    //write what we can and shift down in the array what we cant
		front = 0;
		for(int i = 0; i < wo_count; i++)
		{

			struct free_page_header* header = (struct free_page_header*)pages[i];


			bool gotLock = RTE_TRYLOCK(&header->lock,"WO_HEADER 2");
			bool headLock = gotLock;
			bool freeLock = false;
			
			

			if(pages[i] == ssd_free_pages.cur_page)
			{
				freeLock = RTE_TRYLOCK(&ssd_free_pages.lock,"SSD_FREE_PAGE");
				gotLock = gotLock && freeLock;
			}

			//if we can write out page do so, otherwise, if we need to move overwrite head(front), do so 
			if(header->written_out) {

				pages[i] = NULL;
			}
			else if(gotLock && can_write_out(header) )
			{

				if(freeLock)
				{
					swap_current_page();	
				}
				
				write_out(header);
                pages[i] = NULL;
			}
			else if(front == i)
			{ 
				front++;
			}
			else { // move forward this header ptr and then front

				pages[front] = pages[i];
				pages[i] = NULL;
				// go while page is either written here or by someone else, and less than i
				while( pages[front] != NULL && front < i) front++;
			}	

			if(headLock) RTE_UNLOCK(&header->lock,"WO_HEADER 2");	
			if(freeLock) RTE_UNLOCK(&ssd_free_pages.lock,"SSD_FREE_PAGE");
		}
		//note this could if I write bad code lead to infinite loop

	}while(front == wo_count);

	return pages[front] == NULL  ? front : front + 1;	

}



//@deprecated
int write_out_all_with_lock(int wo_count,struct page** pages)
{
	int front = 0;
	do 
	{
		front = 0;
		for(int i = 0; i < wo_count; i++)
		{

			struct free_page_header* header = (struct free_page_header*)pages[i];


			bool gotLock = RTE_TRYLOCK(&header->lock,"WO_HEADER 1");

			//if we can write out page do so
			if(header->written_out) {

				pages[i] = NULL;
			}
			else if(gotLock && can_write_out(header) )
			{
				write_out(header);
				pages[i] = NULL;
			}
			else if(front == i)
			{ 
				front++;
			}
			else { // move forward this header ptr and then front

				pages[front] = pages[i];
				pages[i] = NULL;
				// go while page is either written here or by someone else, and less than i
				while( pages[front] != NULL && front < i) front++;
			}	

			if(gotLock) RTE_UNLOCK(&header->lock,"WO_HEADER 1");	
		}
		//note this could if I write bad code lead to infinite loop

	}while(front == wo_count);


	return pages[front] == NULL ? front : front + 1;	

}

void forced_write_out_all(int wo_count,struct page** pages)
{
	int left = wo_count;
	while(left > 0)
	{
		left = write_out_all(left,pages);

		if(left != 0){ 
		    //TEST_PRINT_FINAL("LEFTOVER ",left);
		    usleep(1);
		    
		}
	}
}


//must have pagg_header lock to enter here
void write_out(struct free_page_header* p)
{
	char* dest = ssd;

	dest += p->num * SSD_PAGE_SIZE; 
	
	char* src = (char*)(p+1);
	
	
	
	
   /* struct {
        size_t last_written[10000];
        uint32_t head;
        rte_spinlock_t lock;
        
    } written_counter;*/
    
    if(TESTING) {
    RTE_LOCK(&written_counter.lock, "WRITTEN COUNTER");
        
        for(int i = 0; i < 10000; i++)
        {
            if(written_counter.last_written[i] == 0) break;
            
            if(p->num == written_counter.last_written[i])
            {
                printf("PAGE: %lu  Already Written To :: %d", p->num,rte_lcore_id());
                exit(0);
            }
        }
        
        written_counter.last_written[written_counter.head] = p->num;
        written_counter.head = written_counter.head >= 10000 - 1 ? 0 : written_counter.head + 1;
        
    RTE_UNLOCK(&written_counter.lock,"WRITTEN COUNTER");
    }

    
    


    TEST_PRINT_IF_2(p->num > 10000,"Writing to page: ",p->num);

    WRITE_TO_SSD(dest,src,SSD_PAGE_SIZE,page_buffer.pages,ssd);
	
	if( (size_t)(dest - (char*)ssd) % 4096 != 0 ) printf("DEST not page aligned: %d \n",dest - (char*)ssd );
	

	msync(dest,SSD_PAGE_SIZE, MS_SYNC);
	
	p->written_out = true;
	dest += sizeof(struct ssd_header);
	clear_buffer(p);
}


//not necessary, only for sanity check
void clear_buffer(struct free_page_header* p)
{

	

	struct page* head = &(page_buffer.pages[page_buffer.head]);
	struct free_page_header* head_header = (struct free_page_header*)head;
	//if we didnt free head not our problem
	if(head_header != p)
	{
	    return;
	}
	
	//for the off chance that the tail gets changed dring this...
	RTE_LOCK(&page_buffer.lock,"PAGE_BUFFER 2");
	
	if(head_header != p)
    {
        RTE_UNLOCK(&page_buffer.lock,"PAGE_BUFFER 2.5");
        return;
    }
	
	//no clear pages until we reach the tail.
	bool stop = page_buffer.head == page_buffer.tail; //|| (page_buffer.tail == 0 && page_buffer.head == page_buf_size - 1);
	while( !stop  && head_header->written_out)
	{
		page_buffer.head = page_buffer.head >= page_buf_size - 1 ? 0 : page_buffer.head + 1;
		head = &(page_buffer.pages[page_buffer.head]);
		head_header = (struct free_page_header*)head;
		stop = page_buffer.head == page_buffer.tail; //|| (page_buffer.tail == 0 && page_buffer.head == page_buf_size - 1);
	}


    //TESTING CODE
    if(1)
    {
        uint8_t* overwrite = (uint8_t*)p;
        for(int i = 0; i < sizeof(struct page);i++)
        {
            overwrite[i] = 0;
        }
    }
    buff_too_close();
	RTE_UNLOCK(&page_buffer.lock,"PAGE_BUFFER 2");
}

//also only for sanity check
inline void buff_too_close()
{
    int diff = page_buffer.tail < page_buffer.head ? page_buffer.head - page_buffer.tail : page_buf_size - page_buffer.tail + page_buffer.head;
    if(diff < 5) printf("DIFF BETWEEN HEAD AND TAIL:  %d  \n",diff);
}


void* get_new_page(int num)
{


    RTE_LOCK(&page_buffer.lock,"PAGE_BUFFER 1");

    struct page* p = &(page_buffer.pages[page_buffer.tail]);

    //circular buffer
    page_buffer.tail = page_buffer.tail >= page_buf_size - 1 ? 0 : page_buffer.tail + 1;
    //once popped off page, release page buffer lock

    RTE_UNLOCK(&page_buffer.lock,"PAGE_BUFFER 1");
    

    struct free_page_header* header = (struct free_page_header*)p;
    //capacity not currently used
    header->capacity = SSD_PAGE_SIZE;
    header->num = num;
    header->timestamp = time(NULL);
    header->written_out = false;
    header->writers = 0;
    header->consumed = false;
    return (void*)(p);
}


/** also uses the mmapped file **/ 
struct ssd_line* ssd_read(struct ssd_item* it,void* memory)
{
    
    size_t full_key =  it->key;
    TEST_PRINT_IF(full_key > 100, "START READ")
    
	struct ssd_line* header = memory;
	header->vallen = it->vallen;
	header->keylen = it->keylen;
	header->version = it->version;

    
	char* dest = (char *)(header+1);
	header->key = dest;
	size_t len = header->keylen;
	char* src = ssd;

	src += it->first_page*SSD_PAGE_SIZE + it->offset;
	struct ssd_header* ssd_head = (struct ssd_header* )(src );
	

	src+= sizeof(struct ssd_header);
	size_t rest = (SSD_PAGE_SIZE - it->offset - sizeof(struct ssd_header) ) ;


	while(len > 0)
	{
		size_t amount = len < rest ? len : rest;

		memcpy(dest,src,amount);
		len -= amount;
		rest -= amount;
		dest +=amount;
		src += amount;
		if(rest == 0)
		{
			src = ssd;
			src += ssd_head->next*SSD_PAGE_SIZE + ssd_head->offset;
			rest = (SSD_PAGE_SIZE - ssd_head->offset - sizeof(struct ssd_header) ) ;
			ssd_head = (struct ssd_header* )(src );
			src+= sizeof(struct ssd_header);

		}
	}

	len = it->vallen;
	header->val = dest;

	while(len > 0)
	{
		size_t amount = len < rest ? len : rest;
		memcpy(dest,src,amount);
		len -= amount;
		rest -= amount;
		dest +=amount;
		src += amount;
		if(rest == 0)
		{
			src = ssd;
			src += ssd_head->next*SSD_PAGE_SIZE + ssd_head->offset;
			rest = (SSD_PAGE_SIZE - ssd_head->offset - sizeof(struct ssd_header) ) ;		
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
	src += it->first_page*SSD_PAGE_SIZE + it->offset;
	struct ssd_header* ssd_head = (struct ssd_header* )(src );
	src+= sizeof(struct ssd_header);
	size_t rest = (SSD_PAGE_SIZE - it->offset - sizeof(struct ssd_header) ) ;


	while(len > 0)
	{
		size_t amount = len < rest ? len : rest;

		memcpy(dest,src,amount);
		len -= amount;
		rest -= amount;
		dest +=amount;
		src += amount;
		if(rest == 0)
		{
			src = ssd;
			src += ssd_head->next*SSD_PAGE_SIZE + ssd_head->offset;
			rest = (SSD_PAGE_SIZE - ssd_head->offset - sizeof(struct ssd_header) ) ;
			ssd_head = (struct ssd_header* )(src );
			src+= sizeof(struct ssd_header);

		}
	}

	return header;
}



