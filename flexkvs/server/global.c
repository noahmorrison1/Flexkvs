#include "global.h"



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
	
	char* base_names[4] = { "page_log:" , "write_out_log:" , "read_log:" , "gen_log:" };
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
