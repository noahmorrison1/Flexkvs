
#include "NVDIMM_tester.h"
#include "iokvs.h"



void NVDIMM_test_put(test_item* it)
{
	size_t hv = jenkins_hash(it->key,it->keylen);
	NVDIMM_write_entry(it->key, it->keylen,it->val, it->vallen, hv );
}

void NVDIMM_test_put_all(test_item** items, int n)
{
	for(int i = 0; i < n ; i++)
	{
		NVDIMM_test_put(items[i]);
	}
}

struct ssd_line* NVDIMM_test_get(test_item* it)
{
	size_t hv = jenkins_hash(it->key,it->keylen);
	return NVDIMM_read(it->key, it->keylen, hv );
}

void NVDIMM_test_compare(char* c, test_item* it,struct ssd_line* c_it,int n)
{
	if(!NVDIMM_compare(it,c_it)) printf("%s: FAILED at %d \n",c,n);
	else printf("%s: PASSED at %d \n",c,n);
}

void NVDIMM_test_compare_if_wrong(char* c, test_item* it,struct ssd_line* c_it,int n)
{
	if(!NVDIMM_compare(it,c_it)){
	    if(c_it != NULL) display(it->val,c_it->val,it->vallen);
	    printf("%s: FAILED at %d  :: %d\n",c,n,rte_lcore_id());
	    sleep(1);
	    exit(0);
	    
	}
}

void NVDIMM_test_compare_if_right(char* c, test_item* it,struct ssd_line* c_it,int n)
{
	if(NVDIMM_compare(it,c_it)){ printf("%s: FAILED at %d \n",c,n);}
}

bool NVDIMM_compare(test_item* t_it,struct ssd_line* it)
{
    //printf("TESTING Key: %lu   Val: %lu \n",*(size_t*)t_it->key ,*(size_t*)t_it->val);
	if(it == NULL){
	 	printf("NULL KEY \n");
	 	return it == (struct ssd_line*)t_it;
	}

	if(!(it->keylen == t_it->keylen && it->vallen == t_it->vallen)) 
	{
		printf("DIFF LENGTHS \n");
		printf("IT->KEYLEN: % d  IT->VALLEN: %d  T_IT->KEYLEN: % d  T_IT->VALLEN: %d \n",it->keylen,it->vallen,t_it->keylen,t_it->vallen );
		return false;
	}
    //printf("Val 1:  %lu  Val 2: %lu   :: %d\n",*(size_t*)it->val ,*(size_t*)t_it->val,rte_lcore_id());
	return  __builtin_memcmp(it->key,t_it->key, t_it->keylen + t_it->vallen) == 0;
}

void NVDIMM_test_init()
{
	//NVDIMM_test1();
	//NVDIMM_test2();
	//NVDIMM_test3();
	//NVDIMM_test4();
	//NVDIMM_test5();
	//NVDIMM_test6();
	//NVDIMM_test7();
	//NVDIMM_test8();
	//NVDIMM_test_multi_1();
	//NVDIMM_test_multi_2();
}

//single put
void NVDIMM_test1()
{
	printf("TEST 1 STARTING \n");
	test_item* it = gen_reg_item(1024);

	TEST_PRINT("PUTING \n");
	NVDIMM_test_put(it);

	TEST_PRINT("GETTING \n");
	struct ssd_line* c_it = NVDIMM_test_get(it);


	TEST_PRINT("COMPARING\n");
	if(!NVDIMM_compare(it,c_it)) {
		printf("PUT DID NOT WORK!! \n");

		if(c_it != NULL) display(c_it->key,it->key,it->keylen + it->vallen);
	}
	else {printf("TEST1 PASSED \n");}
	usleep(1);
	//free(it);
	printf("TEST 1 ENDING\n");
}




void NVDIMM_test3()
{
	char * c = "TEST 3 ";
	printf("TEST 3 STARTING\n");

	int n = 16;
	size_t size = NVDIMM_SIZE;
	size = size/20;

	//TEST_PRINT_2("GENNING: ",NVDIMM_SIZE);
	//TEST_PRINT_2("GENNING: ",size);
	test_item** items = gen_n__reg_items(size, n);

	TEST_PRINT("PUTTING \n");
	for(int i = 0; i < n ; i++)
	{

		printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
	}
	

	TEST_PRINT("GETTING\n");
	for(int i = 0; i < n ; i++)
	{
		printf("ITEM: %d \n",i);
		printf("KEYLEN: %d   VALLEN: %d  \n",items[i]->keylen,items[i]->vallen);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		if(c_it != NULL) free(c_it);
		free(items[i]);
	}

	//free(items);
	printf("TEST 3 ENDING\n");

}




void NVDIMM_test4()
{
	char * c = "TEST 4 ";
	printf("TEST 4 STARTING\n");

	int n = 21;
	size_t size = NVDIMM_SIZE;
	size = size/20;

	//TEST_PRINT_2("GENNING: ",NVDIMM_SIZE);
	//TEST_PRINT_2("GENNING: ",size);
	test_item** items = gen_n__reg_items(size, n);

	TEST_PRINT("PUTTING \n");
	for(int i = 0; i < n ; i++)
	{

		printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
	}
	
	for(int i = 0; i < 2; i++)
	{
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_right(c,items[i],c_it,i);
		free(items[i]);
	}

	TEST_PRINT("GETTING\n");
	for(int i = 2; i < n ; i++)
	{
		printf("ITEM: %d \n",i);
		printf("KEYLEN: %d   VALLEN: %d  \n",items[i]->keylen,items[i]->vallen);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		if(c_it != NULL) free(c_it);
		free(items[i]);
	}

	//free(items);
	printf("TEST 4 ENDING\n");

}






//PUT THEN CHANGE SIZE
void NVDIMM_test5()
{
	char * c = "TEST 5 ";
	printf("TEST 5 STARTING\n");

	int n = 15;
	size_t size = NVDIMM_SIZE;
	size = size/20;

	//TEST_PRINT_2("GENNING: ",NVDIMM_SIZE);
	//TEST_PRINT_2("GENNING: ",size);
	test_item** items = gen_n__reg_items(size, n);

	TEST_PRINT("PUTTING \n");
	for(int i = 0; i < n ; i++)
	{

		printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		if(c_it != NULL) free(c_it);
	}
	

	TEST_PRINT("CHANGING\n");
	for(int i = 0; i < n ; i++)
	{
		printf("ITEM: %d \n",i);
		GEN_LOG_WRITE("INSERTING CHAGED VAL");
        change_val(items[i]);
        NVDIMM_test_put(items[i]);
        GEN_LOG_WRITE("GETTING");
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		free(items[i]);
	}

	//free(items);
	printf("TEST 5 ENDING\n");

}


//fill, then fill again,
// then change size
void NVDIMM_test6()
{
	char * c = "TEST 4 ";
	printf("TEST 6 STARTING\n");

	int n = 40;
	size_t size = NVDIMM_SIZE;
	size = size/20;


	test_item** items = gen_n__reg_items(size, n);

	TEST_PRINT("PUTTING \n");
	for(int i = 0; i < n ; i++)
	{

		printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		if(c_it != NULL) free(c_it);
	}
	

	printf("CHANGING \n");
	for(int i = 25; i < 28; i++)
	{
		printf("ITEM: %d \n",i);
		GEN_LOG_WRITE("INSERTING CHAGED VAL");
        change_val(items[i]);
        NVDIMM_test_put(items[i]);
        GEN_LOG_WRITE("GETTING");
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
	}


	for(int i = 0; i < 24; i++)
	{
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_right(c,items[i],c_it,i);
		free(items[i]);
	}


	TEST_PRINT("GETTING\n");
	for(int i = 24; i < n ; i++)
	{
		printf("ITEM: %d \n",i);
		//printf("KEYLEN: %d   VALLEN: %d  \n",items[i]->keylen,items[i]->vallen);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		if(c_it != NULL) free(c_it);
		free(items[i]);
	}

	//free(items);
	printf("TEST 6 ENDING\n");

}


void NVDIMM_test7()
{
    char * c = "TEST 7 ";
    printf("TEST 7 START \n");
    srand(time(NULL));
    int n = 400;
    test_item** items = calloc(n,sizeof(test_item*));
    int maxsize = 4096*10;//(1ULL << 30) / 100;
    int num_opts = 2; 
    int num = 0;
    printf("MAXSIZE %d \n",maxsize);
    for(int i = 0 ; i < n ; i++)
    {
        //if(i % 1 == 0) printf("I: %d  \n",i);
        int opt = rand() % num_opts; 
        struct ssd_line* c_it = NULL;
        int size = 0;
        int ind = -1;
        //TEST_PRINT_IF(SSD_COND7," ENTER SWITCH \n");
        if( i < 10) opt = 0;
        
       // if(i % 50 == 0) printf("I: %d   OPT: %d   :: %d\n",i,opt,rte_lcore_id());
        switch(opt){
            case  0 : ;            // generate an item and check for it
                    TEST_PRINT_2("0 I: ",i);
                    size = (rand() % maxsize) + sizeof(size_t);
                    items[num] = gen_reg_item(size);
                    //printf("PUTTING ITEM: %lu  :: %d \n",*((size_t*)(items[num]->key)),rte_lcore_id() );
					NVDIMM_test_put(items[num]);
					c_it = NVDIMM_test_get(items[num]);
					NVDIMM_test_compare_if_wrong(c,items[num],c_it,i);
					if(c_it != NULL) free(c_it);
					num++;
                    break;
            // search for a random item
            case 1 : ;
                    TEST_PRINT_2("1 I: ",i);
                    ind = rand() % num;
			        change_val(items[ind]);
			        NVDIMM_test_put(items[ind]);
					c_it = NVDIMM_test_get(items[ind]);
					NVDIMM_test_compare_if_wrong(c,items[ind],c_it,i);
					if(c_it != NULL) free(c_it);
                    break;
        }
    }

    for(int i = 0 ; i < num; i++)
    {
        free(items[i]);

    }


    free(items);

    printf("TEST 7 END \n");
}


/** MULTITHREADING TEST **/

//put and read 1000 items with random sizes
void NVDIMM_test8()
{
	char * c = "TEST 3 ";
	printf("TEST 3 STARTING\n");

	int n = 20;
	size_t size = NVDIMM_SIZE;
	size = size/20;

	//TEST_PRINT_2("GENNING: ",NVDIMM_SIZE);
	//TEST_PRINT_2("GENNING: ",size);
	test_item** items = gen_n__reg_items(size, n);

	TEST_PRINT("PUTTING \n");
	for(int i = 0; i < n ; i++)
	{

		printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		if(c_it != NULL) {
		 NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		 free(c_it);
		}
		free(items[i]);

	}

	free(items);
	printf("TEST 3 ENDING\n");

}



static test_item** shared_items;
static test_item* shared_item;

static rte_spinlock_t shared_lock;
static bool shared_start  = false;




void NVDIMM_test_multi()
{

	TEST_PRINT("MULTI TEST 1 START \n");
	int n = 20;
	srand(time(0));
	if(rte_lcore_id() == 1)
	{
		rte_spinlock_init(&shared_lock);
		RTE_LOCK(&shared_lock,"NVDIMM TEST LOCK");
		shared_items = gen_n__reg_items(1024, n);		
		TEST_PRINT("ABOUT TO PUT\n");
		for(int i = 0; i < n ; i++)
		{
			NVDIMM_test_put(shared_items[i]);
		}
		shared_start = true;
		RTE_UNLOCK(&shared_lock,"NVDIMM TEST LOCK");
	}
	else
	{
		TEST_PRINT("ABOUT TO WHILE \n");
		while(!shared_start)
		{
			TEST_PRINT("POOP \n");
		}
		TEST_PRINT("DONE WITH WHILE \n");
	}

	TEST_PRINT_2("past first stage ",shared_start);
	usleep(1);

	int x = 1000;

	for(int i = 0 ; i < x; i++)
	{
		printf("TRY: %d \n",i);	
		int ind = rand() % n;
		struct test_item*  item = shared_items[ind];
		struct ssd_line* c_it = NVDIMM_test_get(item);
		NVDIMM_test_compare_if_wrong("TEST MULTI 1",item,c_it,i);
		free(c_it);

	}

	//free(shared_item);


	TEST_PRINT("MULTI TEST 1 END \n");




}




void NVDIMM_test_multi_2()
{

	TEST_PRINT("MULTI TEST 2 START \n");
	int n = 20;
	srand(time(0));
	if(rte_lcore_id() == 1)
	{
		rte_spinlock_init(&shared_lock);
		RTE_LOCK(&shared_lock,"NVDIMM TEST LOCK");
		shared_item = gen_reg_item(10000);		
		TEST_PRINT("ABOUT TO PUT\n");
		NVDIMM_test_put(shared_item);		
		shared_start = true;
		RTE_UNLOCK(&shared_lock,"NVDIMM TEST LOCK");
	}
	else
	{
		TEST_PRINT("ABOUT TO WHILE \n");
		while(!shared_start)
		{
			TEST_PRINT("POOP \n");
		}
		TEST_PRINT("DONE WITH WHILE \n");
	}

	TEST_PRINT_2("past first stage ",shared_start);
	usleep(1);

	int x = 10;
	size_t size = sizeof(test_item) + shared_item->vallen + shared_item->keylen;
	struct test_item* my_item = malloc(size);
	memcpy(my_item,shared_item,size);

	for(int i = 0 ; i < x; i++)
	{
		printf("TRY: %d \n",i);	
		change_val(my_item);
		NVDIMM_test_put(my_item);		
		struct ssd_line* c_it = NVDIMM_test_get(my_item);
		if(c_it == NULL) 
		{
			TEST_PRINT("FAILED \n");
			exit(0);
		}
		free(c_it);

	}

	//free(shared_item);


	TEST_PRINT("MULTI TEST 2 END \n");




}


