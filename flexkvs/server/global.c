#include "global.h"



void global_init()
{
	

	return;
}

void display(char * src,char *dest,size_t amount)
{
    int count = 0;
    size_t key = *((size_t* )src);
    printf("Starting Display \n");
	if(1)
	{
		int i = 0;
		
		while(i < amount)
		{
			if(count > 50) return;
			uint8_t s = (uint8_t) *(src + i);
			uint8_t d = (uint8_t) *(dest + i);
			if(s != d)
			{
				count++;
				printf("SRC: %d  DEST: %d I: %d    Key :: %lu    ID :: %d \n",s,d,i % 4096,key,rte_lcore_id());
			}
			i++;
		}
		printf("\n");
	}
}
