
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
		return false;
	}
    //printf("Val 1:  %lu  Val 2: %lu   :: %d\n",*(size_t*)it->val ,*(size_t*)t_it->val,rte_lcore_id());
	return  __builtin_memcmp(it->key,t_it->key, t_it->keylen + t_it->vallen) == 0;
}

void NVDIMM_test_init()
{
	NVDIMM_test3();
	//NVDIMM_test2();
	//NVDIMM_test3();
	//NVDIMM_test4();
	//NVDIMM_test5();
	//NVDIMM_test6();
	//NVDIMM_test8();
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




//put and read 1000 items
void NVDIMM_test3()
{
	char * c = "TEST 3 ";
	printf("TEST 3 STARTING\n");

	int n = 15;
	size_t size = NVDIMM_SIZE/20;

	TEST_PRINT("GENNING\n");
	test_item** items = gen_n__reg_items(size, n);

	TEST_PRINT("PUTTING \n");
	for(int i = 0; i < n ; i++)
	{

		//printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
	}
	

	TEST_PRINT("GETTING\n");
	for(int i = 0; i < n ; i++)
	{
		//printf("ITEM: %d \n",i);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		//free(items[i]);
	}

	//free(items);
	printf("TEST 3 ENDING\n");

}





//change valsize
void NVDIMM_test4()
{
	printf("TEST 4 STARTING\n");
	char* c = "TEST 4";
	test_item* it = gen_reg_item(1024);

	NVDIMM_test_put(it);
	struct ssd_line* c_it = NVDIMM_test_get(it);
	NVDIMM_test_compare(c,it,c_it,1);



	change_val(it);
	it = change_valsize(it,1500);

	NVDIMM_test_put(it);
	c_it = NVDIMM_test_get(it);
	NVDIMM_test_compare(c,it,c_it,2);


	free(it);
	printf("TEST 4 ENDING\n");

}


//put a ton of data in
void NVDIMM_test5()
{
	char * c = "TEST 5 ";
	printf("TEST 5 STARTING\n");

	size_t total = 1* (1ULL << 30);
	int n = 200; //10000 
	size_t size = total / n;

	test_item** items = gen_n__reg_items(size, n);

	for(int i = 0; i < n ; i++)
	{

		printf("ITEM: %d \n",i);
		NVDIMM_test_put(items[i]);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
	}
	

	for(int i = 0; i < n ; i++)
	{
		printf("ITEM: %d \n",i);
		struct ssd_line* c_it = NVDIMM_test_get(items[i]);
		NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
		free(items[i]);
	}

	free(items);
	printf("TEST 5 ENDING\n");

}


void NVDIMM_test6()
{
    char * c = "TEST 6 ";
    printf("TEST 6 START \n");
    srand(10);
    int n = 1000;
    test_item** items = calloc(n,sizeof(test_item*));
    int maxsize = 4096*10;//(1ULL << 30) / 100;
    int num_opts = 1; 
    int num = 0;
    printf("MAXSIZE %d \n",maxsize);
    for(int i = 0 ; i < n ; i++)
    {
        //if(i % 1 == 0) printf("I: %d  \n",i);
        //TEST_PRINT_IF(SSD_COND7," BEFORE SWITCH \n");        
        int opt = rand() % num_opts; 
        struct ssd_line* c_it = NULL;
        int size = 0;
        int ind = -1;
        //TEST_PRINT_IF(SSD_COND7," ENTER SWITCH \n");
        if( i < 100) opt = 0;
        
        if(i % 50 == 0) printf("I: %d   OPT: %d   :: %d\n",i,opt,rte_lcore_id());
        switch(opt){
            case  0 : ;            // generate an item and check for it
					GEN_LOG_WRITE("TEST PUTTING START");
                    size = (rand() % maxsize) + sizeof(size_t);
                    items[num] = gen_reg_item(size);
                    //printf("PUTTING ITEM: %lu  :: %d \n",*((size_t*)(items[num]->key)),rte_lcore_id() );

                    NVDIMM_test_put(items[num]);
					GEN_LOG_WRITE("TEST PUT SUCCESSFUL, GETTING");
                    c_it = NVDIMM_test_get(items[num]);
					GEN_LOG_WRITE("TEST GETTING SUCCESSFUL, COMPARING");
                    NVDIMM_test_compare_if_wrong(c,items[num],c_it,i);
					GEN_LOG_WRITE("TEST COMPARING SUCESSFUL");
                    num++;
					GEN_LOG_WRITE("TEST PUTTING END");
                    break;
            // search for a random item
            case 1 : ;
                    //TEST_PRINT_IF(i > 100,"C1 1");
                    ind = rand() % num;
                    c_it = NVDIMM_test_get(items[ind]);
                    if(c_it != NULL)
                    {
                        NVDIMM_test_compare_if_wrong(c,items[ind],c_it,i);
                    }
                    break;
            // change and reput and test 
            case 2 : ;
                    //TEST_PRINT_IF(COND7," SWTICH 2 \n");
                    //TEST_PRINT_IF(i > 100,"C2 0.5");
                    ind = rand() % num;
                    //TEST_PRINT_IF(i > 100,"C2 1");
                    change_val(items[ind]);
                    //TEST_PRINT_IF(i > 100,"C2 2");
                    items[ind] = change_valsize(items[ind],1500);
                    //TEST_PRINT_IF(i > 100,"C2 3");
                    NVDIMM_test_put(items[ind]);
                    //TEST_PRINT_IF(i > 100,"C2 4");
                    c_it = NVDIMM_test_get(items[ind]);
                    //TEST_PRINT_IF(i > 100,"C2 5");
                    NVDIMM_test_compare_if_wrong(c,items[ind],c_it,i);
                    break;
            // change size reput and test
            case 3 :  ;
                    
                    //TEST_PRINT_IF(i > 100,"C3 1");
                    ind = rand() % num;
                    size = (rand() % maxsize) + sizeof(size_t);
                    items[ind] = change_valsize(items[ind],1500);
                    NVDIMM_test_put(items[ind]);
                    c_it = NVDIMM_test_get(items[ind]);
                    NVDIMM_test_compare_if_wrong(c,items[ind],c_it,i);
                    break;
        }
    }

    for(int i = 0 ; i < num; i++)
    {
        free(items[i]);

    }


    free(items);

    printf("TEST 6 END \n");
}


//put and read 1000 items with random sizes
void NVDIMM_test8()
{
    char * c = "TEST 8 ";
    printf("TEST 8 STARTING\n");

    int n = 100;
    srand(10);
    int maxsize = (1ULL << 30) / 100;
    size_t size = 4024;

    test_item** items = gen_n__reg_items(size, n);

    for(int i = 0; i < n ; i++)
    {

        printf("ITEM: %d \n",i);
        NVDIMM_test_put(items[i]);
        struct ssd_line* c_it = NVDIMM_test_get(items[i]);
        size_t key = *((size_t*)(items[i]->key));
        NVDIMM_test_compare_if_wrong(c,items[i],c_it,key);
    }
    

    for(int i = 0; i < n ; i++)
    {
        printf("ITEM: %d \n",i);
        struct ssd_line* c_it = NVDIMM_test_get(items[i]);
        NVDIMM_test_compare_if_wrong(c,items[i],c_it,i);
        free(items[i]);
    }

    free(items);
    printf("TEST 8 ENDING\n");

}

