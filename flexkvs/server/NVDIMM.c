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

void NVDIMM_init(void)
{
	TEST_PRINT("NVDIMM INITIALIZING");
	circ_buffer.capacity = NVDIMM_SIZE;
	rte_spinlock_init(&circ_buffer.lock);
	circ_buffer.buffer = calloc(NVDIMM_SIZE,1);
	circ_buffer.head = circ_buffer.buffer;
	circ_buffer.tail = circ_buffer.head;
	circ_buffer.next = circ_buffer.head;
	buf_end = &circ_buffer.buffer[NVDIMM_SIZE -1];
	TEST_PRINT("NVDIMM INITIALIZING END");

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

	//GEN_LOG_WRITE_2("STATE: ",(size_t)item->state);

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


	//GEN_LOG_WRITE_2("STATE: ",(size_t)item->state);

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


	//GEN_LOG_WRITE_2(" STATE: ",(size_t)item->state);

}

inline bool contains_state(nv_line* item,uint8_t state)
{
	return item->state & state == state;
}


// is empty state means that this item has been written out to SSD 
// and should not be read more
inline bool NVDIMM_is_empty_state(nv_line* item)
{
	GEN_LOG_WRITE_2("ITEM STATE: ", (size_t)item->state);
	GEN_LOG_WRITE_2("IS EMPTY: ", (size_t)(item->state == NVD_EMPTY));
	GEN_LOG_WRITE_2("CONSUMED: ", (size_t)(contains_state(item,NVD_CONSUMED)));



	return (item->state == NVD_EMPTY || contains_state(item,NVD_CONSUMED));
}

inline bool NVDIMM_is_evictable_state(nv_line* item)
{
	return (item->state == NVD_EMPTY || item->state == NVD_CONSUMED);
}

//Whether the item is inside the buffer
inline bool NVDIMM_item_is_between_head_and_tail(nv_line* item)
{
	 char* dest = (char*)item;
	 bool forwards = (circ_buffer.tail > circ_buffer.head) && (dest < circ_buffer.tail) ;
	 bool backwards = circ_buffer.tail < circ_buffer.head;
	 bool behind =  dest < circ_buffer.tail;
	 bool ahead = dest > circ_buffer.head;

	 bool fits_in_backwards = backwards && ( behind || ahead);

	 return forwards || fits_in_backwards;
}


inline bool NVDIMM_compare_key(void* key, size_t keylen, nv_line* item)
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

	GEN_LOG_WRITE("NVDIMM WRITE ENTRY START");

	size_t totsize = sizeof(nv_line)+ keylen + vallen;
	if(totsize > NVDIMM_SIZE) return false;

	//find will put item in read state so it 
	// will not be inavlidated by another call to invalidate
	nv_line* old = NVDIMM_find(key,keylen,hv);
	if(old != NULL) {
		change_and_remove_state(old,NVD_READING,NVD_CONSUMED);
		NVDIMM_invalidate();
	}


	nv_line* item = NVDIMM_allocate(totsize);
	while(item == NULL)
	{
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
		memcpy(dest,src,rest);
		amount -= rest;
		dest = circ_buffer.buffer;
		src += rest;
		rest = amount;
	}

	memcpy(dest,src,amount);

	GEN_LOG_WRITE("NVDIMM WRITE TO END");


	return dest + amount;
}



nv_line* NVDIMM_allocate(size_t amount)
{
	GEN_LOG_WRITE("NVDIMM ALLOCATE START");

	nv_line* item = NULL;
	RTE_LOCK(&circ_buffer.lock,"CIRC BUFFER");


	size_t rest = (size_t)(buf_end - circ_buffer.tail);

	if(circ_buffer.capacity < amount ||  (   rest < sizeof(nv_line) && (circ_buffer.capacity - rest) < amount )   ) 
	{ 
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");
		return NULL;
	}

	char* dest = circ_buffer.tail;

	//first write out the nv_line
	if(rest < sizeof(nv_line))
	{
		item = (nv_line*) circ_buffer.tail;
	}
	else
	{
		circ_buffer.capacity = rest;
		dest = circ_buffer.buffer;
		item = (nv_line*)dest;	
		rest = amount;
	}


	item->readers = 0;
	item->state = NVD_WRITING_KEY;
	GEN_LOG_WRITE_2("ORIGINAL STATE: ",(size_t)item->state);
	rte_spinlock_init(&item->lock);
	amount -= sizeof(nv_line);
	dest += sizeof(nv_line);
	rest -= sizeof(nv_line);
	
	//now if we neeed to wrap around
	//allocate memory at the end of the buffer
	if(rest < amount )
	{
		amount -= rest;
		circ_buffer.capacity -= rest;
		dest = circ_buffer.buffer;
		rest = amount;
	}


	circ_buffer.tail = dest + amount;
	circ_buffer.capacity -= amount;



	RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");

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

	RTE_LOCK(&circ_buffer.lock,"NVDIMM BUFFER");


	nv_line* item = (nv_line*)circ_buffer.head;
	while( NVDIMM_item_is_between_head_and_tail(item))
	{
		GEN_LOG_WRITE("IS BETWEEN HEAD AND TAIL");
		RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
		// if its empty or has been written out
		// we dont return it even though it still may be here because someone else is 
		// reading or something
		if(!NVDIMM_is_empty_state(item)) {
			//cant read here

			GEN_LOG_WRITE("NOT EMPTY STATE");

			if(contains_state(item,NVD_WRITING_KEY) )
			{
				RTE_UNLOCK(&item->lock,"NVDIMM ITEM LOCK");
				usleep(1);
				RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
				continue;
			}


			GEN_LOG_WRITE("ABOUT TO COMPARE");

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

		// if this items ends in a wierd place the 
		// next item starts at the beggining of the buffer
		if( rest < sizeof(nv_line))
		{
			dest = circ_buffer.buffer;
		}

		item = (nv_line*)dest;
	}


	RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER");

	GEN_LOG_WRITE("NVDIMM FIND END");


	return NULL;
}



struct ssd_line* NVDIMM_read_item(nv_line* item)
{

	GEN_LOG_WRITE("NVDIMM READ ITEM START");


	size_t totsize = sizeof(struct ssd_line) + item->keylen + item->vallen;
	struct ssd_line* line = malloc(totsize);

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


	RTE_LOCK(&circ_buffer.lock,"CIRC BUFFER");

	char* dest = circ_buffer.next;
	nv_line* next = (nv_line*)dest;

	RTE_LOCK(&next->lock,"NVDIMM ITEM LOCK");
	/// if the next is not good 
	while(NVDIMM_is_empty_state(next))
	{
		RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");

		NVDIMM_invalidate();

		RTE_LOCK(&circ_buffer.lock,"CIRC BUFFER");
		RTE_LOCK(&next->lock,"NVDIMM ITEM LOCK");
	}
	//if its not done being written, we gotta wait
	if(next->state == NVD_WRITING_KEY || next->state == NVD_WRITING_VAL)
	{
		RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");
		RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");
		usleep(1);
		return NULL;
	}
	RTE_UNLOCK(&next->lock,"NVDIMM ITEM LOCK");

	change_and_remove_state(next,NVD_WRITING_TO_SSD,NVD_WAITING);

	size_t totsize = sizeof(nv_line) + next->keylen + next->vallen;
	size_t rest = (buf_end - dest);

	if(rest < totsize)
	{
		dest = circ_buffer.buffer;
		totsize -= rest;
	}

	circ_buffer.next = dest + totsize;

	RTE_UNLOCK(&circ_buffer.lock,"CIRC BUFFER");


	GEN_LOG_WRITE("NVDIMM CLAIM NEXT END");

	return next;

}


// it has already been claimed
bool NVDIMM_write_out(nv_line* item)
{
	GEN_LOG_WRITE("NVDIMM WRITE OUT START");

	struct ssd_line* ssd_item = NVDIMM_read_item(item);
	database_set(ssd_item->key,ssd_item->keylen,ssd_item->val,ssd_item->vallen, item->hv, 0);

	return true;

	GEN_LOG_WRITE("NVDIMM  WRITE OUT END");

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


	RTE_LOCK(&circ_buffer.lock,"NVDIMM BUFFER LOCK")
	if(circ_buffer.head == circ_buffer.tail) 
	{
		RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER LOCK")
		return;
	}

	char* dest = circ_buffer.head;
	nv_line* item = (nv_line*)dest;
	bool past_next = false;
	RTE_LOCK(&item->lock,"NVDIMM ITEM LOCK");
	while(NVDIMM_item_is_between_head_and_tail(item) &&  NVDIMM_is_evictable_state(item))
	{

		if(dest == circ_buffer.next) past_next = true;

		circ_buffer.capacity += sizeof(nv_line);
		dest = (char*)(item+1);
		size_t rest = buf_end - dest;
		size_t totsize = item->keylen+item->vallen;
		if(rest < totsize)
		{
			circ_buffer.capacity += rest;
			totsize -= rest;
			dest = circ_buffer.buffer;
		}

		dest += totsize;
		circ_buffer.capacity += totsize;
		rest = buf_end - dest;

		if(rest < sizeof(nv_line))
		{
			dest = circ_buffer.buffer;
			circ_buffer.capacity += rest;
		}

		item = (nv_line*)dest;
	}


	circ_buffer.head = item;
	if(past_next) circ_buffer.next = item;

	RTE_UNLOCK(&circ_buffer.lock,"NVDIMM BUFFER LOCK")

	GEN_LOG_WRITE("NVDIMM INVALIDATE END");

}


















