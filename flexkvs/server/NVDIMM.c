#include "NVDIMM.h"




struct {	
	// the head of the list that has not been fully consumed
	char* head;
	// the next item to write out
	char* next;
	char* tail;
	size_t capacity;
	char* buffer;
	rte_spinlock_t lock;
} circ_buffer;

static char* buf_end;

void NVDIMM_init()
{
	TEST_PRINT("NVDIMM INITIALIZING\n");
	circ_buffer.capacity = NVDIMM_SIZE ;
	rte_spinlock_init(&circ_buffer.lock);
	circ_buffer.buffer = CALLOC(1,NVDIMM_SIZE + 4098,"NVDIMM");
	circ_buffer.head = circ_buffer.buffer;
	circ_buffer.tail = circ_buffer.head;
	circ_buffer.next = circ_buffer.head;
	buf_end = (circ_buffer.buffer + NVDIMM_SIZE );



	TEST_PRINT_2("BUFFER START: ",(size_t)circ_buffer.buffer);
	TEST_PRINT_2("BUFFER END: ",(size_t)buf_end);
	TEST_PRINT_2("BUFFER END 2: ",(size_t)(&(circ_buffer.buffer[NVDIMM_SIZE ])) );
	//uint8_t* end = buf_end;
	//*end = 0;
	TEST_PRINT("NVDIMM INITIALIZING END\n");

}





/** TOOLS **/

inline void change_state(nv_line* item, uint8_t new_state)
{
	RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
	if(new_state == NVD_READING)
	{
		item->readers++;
	}
	item->state = item->state | new_state;
	RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");


}

// never is reading called here
inline void change_and_remove_state(nv_line* item, uint8_t new_state,uint8_t old_state)
{
	RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
	if(new_state == NVD_READING)
	{
		item->readers++;
	}
	item->state = item->state | new_state;

	if(old_state == NVD_READING)
	{
		item->readers--;
		if(item->readers == 0)
		{
			item->state = item->state & ~old_state;
		}
	}
	else
	{
		item->state = item->state & ~old_state;
	}
	RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");



}


inline void remove_state(nv_line* item, uint8_t old_state)
{
	RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
	if(old_state == NVD_READING)
	{
		item->readers--;
		if(item->readers == 0)
		{
			item->state = item->state & ~old_state;
		}
	}
	else
	{
		item->state = item->state & ~old_state;
	}
	RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");

}

bool contains_state(nv_line* item,uint8_t state)
{
	return (item->state & state) == state;
}


// is empty state means that this item has been written out to SSD 
// and should not be read more
bool NVDIMM_is_empty_state(nv_line* item)
{
	return (item->state == NVD_EMPTY || contains_state(item,NVD_CONSUMED));
}

bool NVDIMM_is_evictable_state(nv_line* item)
{
	return (item->state == NVD_EMPTY || item->state == NVD_CONSUMED);
}

//Whether the item is inside the buffer
bool NVDIMM_item_is_between_head_and_tail(nv_line* item)
{
	 char* dest = (char*)item;
	 bool forwards = (circ_buffer.tail > circ_buffer.head) && (dest < circ_buffer.tail && dest >= circ_buffer.head);
	 bool backwards = circ_buffer.tail < circ_buffer.head;
	 bool behind =  dest < circ_buffer.tail;
	 bool ahead = dest >= circ_buffer.head;

	 bool fits_in_backwards = backwards && ( behind || ahead);

	/*GEN_LOG_WRITE_2("IS BETWEEN HEAD AND TAIL: ",(size_t)(forwards || fits_in_backwards));

	 GEN_LOG_WRITE_2("HEAD: ",circ_buffer.head - circ_buffer.buffer);
	 GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
	 GEN_LOG_WRITE_2("THIS: ",dest- circ_buffer.buffer); 
	 GEN_LOG_WRITE_2("CAPACITY: ",circ_buffer.capacity);*/


	 return forwards || fits_in_backwards;
}


bool NVDIMM_compare_key(void* key, size_t keylen, nv_line* item)
{

	char* dest = (char*)(item + 1);
	char* src = key;

	size_t rest = buf_end - dest; 

	if(rest < keylen)
	{
		if(__builtin_memcmp(src,dest,rest) != 0) return false;

		keylen -= rest;
		dest = circ_buffer.buffer;
		src += rest;
	}

	bool comp = __builtin_memcmp(src,dest,keylen) == 0;
	GEN_LOG_WRITE_2("NVDIMM COMPARE: ",(size_t)comp);
	return comp;
}





/** WRITE STUFF **/

bool NVDIMM_write_entry(void* key, size_t keylen, void* val, size_t vallen, size_t hv)
{

	GEN_LOG_WRITE_2("WE HASH VALUE: ",hv);

	GEN_LOG_WRITE("NVDIMM WRITE ENTRY START");

	size_t totsize = sizeof(nv_line)+ keylen + vallen;

	GEN_LOG_WRITE_2("TOTSIZE: ",totsize);
	if(totsize > NVDIMM_SIZE) return false;

	//find will put item in read state so it 
	// will not be inavlidated by another call to invalidate
	nv_line* old = NVDIMM_find(key,keylen,hv);
	if(old != NULL) {
		change_and_remove_state(old,NVD_CONSUMED, NVD_WAITING);
		remove_state(old,NVD_READING);
		NVDIMM_invalidate();
	}


	//static volatile int c_uont = 0;
	nv_line* item = NVDIMM_allocate(totsize);

	if(item == NULL)
	{
		printf("Need to clear NVDIMM %d \n",rte_lcore_id());
	}

	while(item == NULL)
	{
		//__sync_fetch_and_add(&c_uont, 1);
		//if(c_uont % 500 == 0) printf("Having to force eviction \n");
		NVDIMM_write_out_next();
		//allocate returns the item in the
		// NVD_WRITING_KEY state
		item = NVDIMM_allocate(totsize);
	}

	
	item->hv = hv;
	item->keylen = keylen;
	item->vallen = vallen;
	char* dest = (char*)(item+1);
	dest = NVDIMM_write_to(key,dest,keylen);
	change_and_remove_state(item,NVD_WRITING_VAL,NVD_WRITING_KEY);


	dest = NVDIMM_write_to(val,dest,vallen);
	change_and_remove_state(item,NVD_WAITING,NVD_WRITING_VAL);


	GEN_LOG_WRITE_2("LOCK STATE: ",rte_spinlock_is_locked(&item->lock));

	GEN_LOG_WRITE("NVDIMM WRITE ENTRY END");


	return true;

}



void* NVDIMM_write_to(char* src, char* dest, size_t amount)
{

	GEN_LOG_WRITE("NVDIMM WRITE TO START");

	size_t rest = (size_t)(buf_end - dest);

	//if we need to wrap over, write here first
	if(rest < amount)
	{
		MEMCPY(dest,src,rest,"NVDIMM WRITE TO");
		GEN_LOG_WRITE("WRAPPING AROUND");
		amount -= rest;
		dest = circ_buffer.buffer;
		src += rest;
		rest = amount;
		if(dest < circ_buffer.head && dest + rest >= circ_buffer.head)
		{
			GEN_LOG_WRITE_2("HEAD: ",circ_buffer.head - circ_buffer.buffer);
			GEN_LOG_WRITE_2("NEXT: ",circ_buffer.next - circ_buffer.buffer);
			GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
			GEN_LOG_WRITE_2("START: ",dest - circ_buffer.buffer);
			GEN_LOG_WRITE_2("END: ",(dest + amount) - circ_buffer.buffer);
			exit(0);
		}
	}

	/*GEN_LOG_WRITE_2("HEAD: ",circ_buffer.head - circ_buffer.buffer);
	GEN_LOG_WRITE_2("NEXT: ",circ_buffer.next - circ_buffer.buffer);
	GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
	GEN_LOG_WRITE_2("START: ",dest - circ_buffer.buffer);
	GEN_LOG_WRITE_2("END: ",(dest + amount) - circ_buffer.buffer);*/

	if( dest < circ_buffer.head && dest + amount >= circ_buffer.head)
	{
		GEN_LOG_WRITE_2("HEAD: ",circ_buffer.head - circ_buffer.buffer);
		GEN_LOG_WRITE_2("NEXT: ",circ_buffer.next - circ_buffer.buffer);
		GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
		GEN_LOG_WRITE_2("START: ",dest - circ_buffer.buffer);
		GEN_LOG_WRITE_2("END: ",(dest + amount) - circ_buffer.buffer);
		exit(0);
	}




	MEMCPY(dest,src,amount,"NVDIMM WRITE TO 2");

	GEN_LOG_WRITE("NVDIMM WRITE TO END");


	return dest + amount;
}



nv_line* NVDIMM_allocate(size_t amount)
{



	GEN_LOG_WRITE("NVDIMM ALLOCATE START");


	/*GEN_LOG_WRITE_2("HEAD: ",circ_buffer.head - circ_buffer.buffer);
	GEN_LOG_WRITE_2("NEXT: ",circ_buffer.next - circ_buffer.buffer);
	GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
	GEN_LOG_WRITE_2("CAPACITY: ",circ_buffer.capacity);
	GEN_LOG_WRITE_2("AMOUNT: ",amount);*/

	size_t cap = circ_buffer.capacity;

	///GEN_LOG_WRITE_2("START: ",dest - circ_buffer.buffer);
	//GEN_LOG_WRITE_2("END: ",(dest + amount) - circ_buffer.buffer);

	char* first_tail = circ_buffer.tail;

	nv_line* item = NULL;

	GEN_LOG_WRITE("NVDIMM ALLOCATE START 0.5");

	RTE_LOCK(&circ_buffer.lock,"CIRC BUFFER");


	size_t rest = (size_t)(buf_end - circ_buffer.tail);
 
	GEN_LOG_WRITE("NVDIMM ALLOCATE START 1");


	//return null if we dont have enough space
	if(circ_buffer.capacity <= amount ||  (   rest <= sizeof(nv_line) && (circ_buffer.capacity - rest) <= amount )   ) 
	{ 
		GEN_LOG_WRITE("QUITTING EARLY");
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");
		return NULL;
	}

	char* dest = circ_buffer.tail;


	GEN_LOG_WRITE("NVDIMM ALLOCATE 2");


	//first write out the nv_line
	//we know if were here we have enough capacity
	if(rest >= sizeof(nv_line))
	{
		item = (nv_line*) circ_buffer.tail;
	}
	else
	{
		GEN_LOG_WRITE("WRAPPING AROUND 1");
		circ_buffer.capacity -= rest;
		dest = circ_buffer.buffer;
		item = (nv_line*)dest;	
		rest = amount;
	}

	GEN_LOG_WRITE("NVDIMM ALLOCATE 3");



	item->readers = 0;
	item->state = NVD_WRITING_KEY;
	//GEN_LOG_WRITE_2("ORIGINAL STATE: ",(size_t)item->state);
	rte_spinlock_init(&item->lock);
	amount -= sizeof(nv_line);
	circ_buffer.capacity -= sizeof(nv_line);
	dest += sizeof(nv_line);
	rest -= sizeof(nv_line);
	
	GEN_LOG_WRITE("NVDIMM ALLOCATE 4");


	//now if we neeed to wrap around
	//allocate memory at the end of the buffer
	if(rest < amount )
	{
		GEN_LOG_WRITE("WRAPPING AROUND 2");
		amount -= rest;
		circ_buffer.capacity -= rest;
		dest = circ_buffer.buffer; 
	}


	GEN_LOG_WRITE("NVDIMM ALLOCATE 5");


	circ_buffer.tail = dest + amount;
	circ_buffer.capacity -= amount;



	RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");


	GEN_LOG_WRITE("NVDIMM ALLOCATE 6");



	if(circ_buffer.tail < first_tail )
	{
		GEN_LOG_WRITE("WRAPPING AROUND");
		GEN_LOG_WRITE_2("OLD TAIL: ",(first_tail - circ_buffer.buffer));
		GEN_LOG_WRITE_2("NEW TAIL: ",(circ_buffer.tail - circ_buffer.buffer));
		GEN_LOG_WRITE_2("END: ",(buf_end - circ_buffer.buffer));
		GEN_LOG_WRITE_2("CAPACITY: ",(circ_buffer.capacity));


	}

	GEN_LOG_WRITE_2("LOST CAP: ",cap - circ_buffer.capacity);
	GEN_LOG_WRITE("NVDIMM ALLOCATE END");



	return item;
}








/** READ STUFF **/


struct ssd_line* NVDIMM_read(void* key, size_t keylen,size_t hv)
{
	GEN_LOG_WRITE("NVDIMM READ START");


	nv_line* item = NVDIMM_find(key,keylen,hv);
	if(item == NULL) return NULL;

	struct ssd_line* line = NVDIMM_read_item(item);

	remove_state(item,NVD_READING);
	NVDIMM_invalidate();

	GEN_LOG_WRITE("NVDIMM READ END");

	

	return line;
}


//guaranteed to have buffer lock when this is called
nv_line* NVDIMM_find(void* key, size_t keylen,size_t hv)
{
	GEN_LOG_WRITE("NVDIMM FIND START");

	GEN_LOG_WRITE_2("FIND HASH VALUE: ",hv);


	RTE_LOCK(&circ_buffer.lock,"NVDIMM BUFFER");

	//GEN_LOG_WRITE("GOT CIRC BUFFER LOCK");

	nv_line* item = (nv_line*)circ_buffer.head;
	GEN_LOG_WRITE_2("ABOUT TO CHECK ITEM: ",(char*)item - circ_buffer.buffer);

	while( NVDIMM_item_is_between_head_and_tail(item))
	{
		GEN_LOG_WRITE_2("ITEM HV: ",item->hv);
		GEN_LOG_WRITE_2("ITEM: ",  buf_end - (char*)item );
		GEN_LOG_WRITE_2("END: ",buf_end - circ_buffer.buffer);
		GEN_LOG_WRITE_2("HEAD: ",buf_end - circ_buffer.head);
		GEN_LOG_WRITE_2("TAIL: ",buf_end - circ_buffer.tail);
		GEN_LOG_WRITE_2("NEXT: ",buf_end - circ_buffer.next);

		//GEN_LOG_WRITE("IS BETWEEN HEAD AND TAIL");
		//usleep(1);

		RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
		// if its empty or has been written out
		// we dont return it even though it still may be here because someone else is 
		// reading or something
		if(!NVDIMM_is_empty_state(item)) {		//cant read here

			//GEN_LOG_WRITE("NOT EMPTY STATE");

			if(contains_state(item,NVD_WRITING_KEY) )
			{
				RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");
				usleep(1);
				//RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
				continue;
			}


			//GEN_LOG_WRITE_2("ABOUT TO COMPARE HV: ",item->hv);

			if(hv == item->hv && NVDIMM_compare_key(key,keylen,item))
			{
				item->readers++;
				item->state = item->state | NVD_READING;
				// we can unlock because we have declared we are reading
				RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");
				RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER");
				return item;
			}
		}

		RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");


		size_t totsize = sizeof(nv_line)+ item->keylen + item->vallen;



		char* dest = (char*)item;

		
		size_t rest = buf_end - dest;

		//check to see if we wrap around
		if( rest < totsize)
		{
			dest = circ_buffer.buffer;
			totsize -= rest;
		}

		dest += totsize;
		rest = buf_end - dest;

		GEN_LOG_WRITE_2("DEST: ",dest);
		GEN_LOG_WRITE_2("BUF END: ",buf_end);
		GEN_LOG_WRITE_2("REST: ",rest);
		GEN_LOG_WRITE_2("SIZE: ",sizeof(nv_line));
		// if this items ends in a wierd place the 
		// next item starts at the beggining of the buffer
		if( rest < sizeof(nv_line))
		{
			GEN_LOG_WRITE("RESET");
			dest = circ_buffer.buffer;
		}

		item = (nv_line*)dest;
	}


	GEN_LOG_WRITE("ABOUT TO FREE CIRC BUFFER");

	RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER");

	GEN_LOG_WRITE("NVDIMM FIND END");


	return NULL;
}



struct ssd_line* NVDIMM_read_item(nv_line* item)
{

	GEN_LOG_WRITE("NVDIMM READ ITEM START");





	size_t totsize = sizeof(struct ssd_line) + item->keylen + item->vallen;


	struct ssd_line* line = malloc(totsize + 100);



	char* dest = (char*)(line+1);
	line->key = dest;
	line->val = dest + item->keylen;
	line->keylen = item->keylen;
	line->vallen = item->vallen;
	line->version = -1;
	char* src = (char*)(item+1);

	size_t rest = buf_end - src;

	if(rest < totsize)
	{
		GEN_LOG_WRITE("READ WRAPPING AROUND");
		memcpy(dest,src,rest);
		src = circ_buffer.buffer;
		dest += rest;
		totsize -= rest;
	}

	memcpy(dest,src,totsize);

	GEN_LOG_WRITE("NVDIMM READ ITEM END");


	return line;

}



/** WRITING OUT STUFF **/


bool NVDIMM_write_out_next()
{
	GEN_LOG_WRITE("NVDIMM WRITE OUT NEXT START");


	nv_line* item = NVDIMM_claim_next();
	if(item == NULL) return false;

	bool written =  NVDIMM_write_out(item);
	if(written = false) return false;

	NVDIMM_consume(item);


	GEN_LOG_WRITE("NVDIMM WRITE OUT NEXT END");


	return true;
}





nv_line* NVDIMM_claim_next(void)
{

	GEN_LOG_WRITE("NVDIMM CLAIM NEXT START");


	GEN_LOG_WRITE_2("END: ",buf_end - circ_buffer.buffer);
	GEN_LOG_WRITE_2("HEAD: ", circ_buffer.head - circ_buffer.buffer);
	GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
	GEN_LOG_WRITE_2("NEXT: ", circ_buffer.next - circ_buffer.buffer);
	

	RTE_LOCK(&circ_buffer.lock,"CIRC BUFFER");

	GEN_LOG_WRITE_2("NEXT: ",circ_buffer.next - circ_buffer.buffer);

	char* dest = circ_buffer.next;
	nv_line* next = (nv_line*)dest;



	bool gotLock = RTE_TRYLOCK(&next->lock,"NVDIMM ITEM LOCK");
	if(!gotLock) {


		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");
		return NULL;
	}

	GEN_LOG_WRITE("NEXT 0.5");
	/// if the next is not good 
	if( !NVDIMM_item_is_between_head_and_tail(next))
	{
		RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");

		GEN_LOG_WRITE("NEXT IS NOT BETEWEEN HEAD AND TAIL");
		NVDIMM_invalidate();
		return NULL;
	}

	if(NVDIMM_is_empty_state(next))
	{
		RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");

		GEN_LOG_WRITE_2("NEXT IS EVICTABLE STATE: ",next->state);
		NVDIMM_invalidate();
		return NULL;
	}




	GEN_LOG_WRITE("NEXT 0.75");

	//if its not done being written, we gotta wait
	if(next->state == NVD_WRITING_KEY || next->state == NVD_WRITING_VAL)
	{
		GEN_LOG_WRITE("NEXT 1");
		RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");
		usleep(1);
		return NULL;
	}
	RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");



	GEN_LOG_WRITE("NEXT 2");

	change_and_remove_state(next,NVD_WRITING_TO_SSD,NVD_WAITING);
	change_state(next,NVD_READING);


	size_t totsize = sizeof(nv_line) + next->keylen + next->vallen;
	size_t rest = (buf_end - dest);

	GEN_LOG_WRITE("NEXT 3");


	if(rest < totsize)
	{
		GEN_LOG_WRITE("NEXT 4");
		dest = circ_buffer.buffer;
		totsize -= rest;
	}


	dest = dest + totsize;

	rest = buf_end - dest;

	if(rest < sizeof(nv_line))
	{
		dest = circ_buffer.buffer;
	}
	GEN_LOG_WRITE("NEXT 5");

	circ_buffer.next = dest;

	RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");


	GEN_LOG_WRITE("NVDIMM CLAIM NEXT END");

	GEN_LOG_WRITE_2("CLAIMING NEXT: ",(char*)next - circ_buffer.buffer);
	GEN_LOG_WRITE_2("HEAD IS STILL: ",circ_buffer.head -  circ_buffer.buffer);



	if(!NVDIMM_item_is_between_head_and_tail(circ_buffer.next) && circ_buffer.next != circ_buffer.tail)
	{
			GEN_LOG_WRITE_2("END: ",buf_end - circ_buffer.buffer);
			GEN_LOG_WRITE_2("HEAD: ", circ_buffer.head - circ_buffer.buffer);
			GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
			GEN_LOG_WRITE_2("NEXT: ", circ_buffer.next - circ_buffer.buffer);
			TEST_PRINT("NEXT IS BAD 1! \n");
			exit(0);
	}


	return next;

}


// it has already been claimed
bool NVDIMM_write_out(nv_line* item)
{
	GEN_LOG_WRITE("NVDIMM WRITE OUT START");

	struct ssd_line* ssd_item = NVDIMM_read_item(item);
	remove_state(item,NVD_READING);
	database_set(ssd_item->key,ssd_item->keylen,ssd_item->val,ssd_item->vallen, item->hv);

	GEN_LOG_WRITE("NVDIMM  WRITE OUT END");


	return true;


}

void NVDIMM_consume(nv_line* item)
{
	GEN_LOG_WRITE("NVDIMM CONSUME START");

	//TODO: problem here
	change_and_remove_state(item,NVD_CONSUMED,NVD_WRITING_TO_SSD);
	NVDIMM_invalidate();

	GEN_LOG_WRITE("NVDIMM CONSUME END");

}



void NVDIMM_invalidate()
{

	GEN_LOG_WRITE("NVDIMM INVALIDATE START");

	size_t cap = circ_buffer.capacity;

	RTE_LOCK(&circ_buffer.lock,"NVDIMM BUFFER LOCK")
	if(circ_buffer.head == circ_buffer.tail) 
	{
		RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER LOCK")
		return;
	}

	char* dest = circ_buffer.head;
	nv_line* item = (nv_line*)dest;
	bool past_next = false;
	GEN_LOG_WRITE("ACTUALLY INVALIDATING");
	GEN_LOG_WRITE_2("EVICTING: ",dest - circ_buffer.buffer);
	RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
	GEN_LOG_WRITE("GOT ITEM LOCK");
	GEN_LOG_WRITE_2("ITEM STATE: ",item->state);
	while(NVDIMM_item_is_between_head_and_tail(item) &&  NVDIMM_is_evictable_state(item))
	{

		GEN_LOG_WRITE("ACTUALLY EVICTING");
		if(dest == circ_buffer.next) past_next = true;

		circ_buffer.capacity += sizeof(nv_line);


		dest = (char*)(item+1);
		size_t rest = buf_end - dest;
		size_t totsize = item->keylen+item->vallen;


		if(rest < totsize)
		{
			GEN_LOG_WRITE("INVALIDATE WRAPPING");
			circ_buffer.capacity += rest;
			totsize -= rest;
			dest = circ_buffer.buffer;
		}

		dest += totsize;
		circ_buffer.capacity += totsize;
		rest = buf_end - dest;

		if(rest < sizeof(nv_line))
		{
			GEN_LOG_WRITE_2("INVALIDATE ADDING EXTRA: ",rest);
			dest = circ_buffer.buffer;
			circ_buffer.capacity += rest;
		}

		RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");

		item = (nv_line*)dest;

		if(NVDIMM_item_is_between_head_and_tail(item)){
			RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
		}
	}

	if(NVDIMM_item_is_between_head_and_tail(item)) RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");


	circ_buffer.head = dest;
	if(past_next) circ_buffer.next = dest;

	if(!NVDIMM_item_is_between_head_and_tail(circ_buffer.next) && circ_buffer.next != circ_buffer.tail)
	{
			GEN_LOG_WRITE_2("END: ",buf_end - circ_buffer.buffer);
			GEN_LOG_WRITE_2("HEAD: ", circ_buffer.head - circ_buffer.buffer);
			GEN_LOG_WRITE_2("TAIL: ",circ_buffer.tail - circ_buffer.buffer);
			GEN_LOG_WRITE_2("NEXT: ", circ_buffer.next - circ_buffer.buffer);
			GEN_LOG_WRITE("MY B");
			TEST_PRINT("NEXT IS BAD 2! \n");
			exit(0);
	}

	RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER LOCK")

	GEN_LOG_WRITE_2("GAINED CAP: ", circ_buffer.capacity - cap);

	GEN_LOG_WRITE("NVDIMM INVALIDATE END");

}


















