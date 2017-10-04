#include "global.h"

struct calloc_log* valg;
uint8_t valg_index = 0;

void global_init()
{
	
	logs_init();
	return;
}

void logs_init()
{
	
	PRINT("LOG INIT START");
	//hacky but whatever for now
	rte_spinlock_init(&logs.lock);
	logs.count = 0;
	
	char* base_names[4] = { "page_log:" , "write_out_log:" , "read_log:" , "gen_log:" , "valgrind_log"};
	//logs.base_names = bn;
	
	for(int l = 0; l < NUMLOGS; l++)
	{
		logs.fptrs[l] = malloc(sizeof(int)*rte_lcore_count());
		for(int i = 0; i < rte_lcore_count(); i++)
		{
			int length = strlen(base_names[l]);
			char str[length+2];
			for(int j = 0; j < length; j++) {
				str[j] = base_names[l][j];
			}
			str[length] = (char)((int)'0'+ i);
			str[length+1] = '\0';
			logs.fptrs[l][i] = fopen(str, "w");
		}
	}
	
	PRINT("LOG INIT END");
}

void valgrind_init()
{
	valg = calloc(VALG_SIZE,sizeof(struct calloc_log));
	add_to_valg(valg,VALG_SIZE,sizeof(struct calloc_log),"VALGRIND");
}

void* new_calloc(size_t num, size_t size,char* name)
{

	void* c = calloc(num,size);
	add_to_valg(c,num,size,name);
	return c;
}

void* add_to_valg(char* start, size_t num, size_t size,char* name)
{
	struct calloc_log* v = &valg[valg_index++];
	v->start = start;
	v->size = size;
	v->count = num;
	v->name = malloc(20);
	uint8_t i = 0;
	while(name[i] != '\0')
	{
		v->name[i] = name[i];
	}

	v->name[i] = name[i];

}

void* new_memcpy(char* src, char* dest, size_t amount, char* name)
{
	bool found = 0;
	for(int i = 0; i < valg_index; i++)
	{
		struct calloc_log* v = &valg[i];
		if(dest >= v->start && dest <= (v->start + (v->count*v->size) ) )
		{
			found = 1;
			int j = (dest - v->start)/v->size;
			//if it passes over a boundary we have problems
			if(v->start + j*v->size < (dest + amount))
			{
				VALG_LOG_WRITE(name);
				VALG_LOG_WRITE("WROTE OVER BOUNDS");
				VALG_LOG_WRITE_2(name,(size_t)dest);
				VALG_LOG_WRITE_2(name,amount);
				VALG_LOG_WRITE_2(name,j);
				VALG_LOG_WRITE_2(name,v->size);
				VALG_LOG_WRITE_2(name,(size_t)((dest + amount) - (v->start + j*v->size))  );
				LOG_BREAK(VALG_LOG_I);
			}

			break;
		}
	}

	if(!found)
	{
		VALG_LOG_WRITE(name);
		VALG_LOG_WRITE("WRITE OUT OF BOUNDS");
		VALG_LOG_WRITE_2(name,(size_t)dest);
		VALG_LOG_WRITE_2(name,amount);
		LOG_BREAK(VALG_LOG_I);
	}


	memcpy(src,dest,amount);
}

void display(char * src,char *dest,size_t amount)
{
    int count = 0;
    size_t key = *((size_t* )src);
    printf("Starting Display \n");
	if(1)
	{
		size_t i = 0;
		
		while(i < amount)
		{
			//if(count > 50) break;
			uint8_t s = (uint8_t) *(src + i);
			uint8_t d = (uint8_t) *(dest + i);
			if(s != d)
			{
				count++;
				if(count < 50) printf("SRC: %d  DEST: %d I: %d   PAGE :: %d    Key :: %lu    ID :: %d \n",s,d,i % 4096,i/4096,key,rte_lcore_id());
			}
			i++;
		}
		printf("Total PAGES: %lu \n",i/4096);
		printf("\n");
	}
}
