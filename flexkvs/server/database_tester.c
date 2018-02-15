

#include "database_tester.h"




rte_spinlock_t database_test_lock;


void database_test_put(test_item* it)
{
    size_t hv = jenkins_hash(it->key,it->keylen);
    cache_flush(it->key,it->keylen,hv);
    NVDIMM_write_entry(it->key, it->keylen,it->val, it->vallen, hv);
}

void database_test_put_all(test_item** items, int n)
{
    for(int i = 0; i < n ; i++)
    {
       database_test_put(items[i]);
    }
}

struct ssd_line* database_test_get(test_item* it)
{
    size_t hv = jenkins_hash(it->key,it->keylen);
    return database_get(it->key, it->keylen, hv);
}



void database_test_init()
{

    rte_spinlock_init(&database_test_lock);

    //database_test1();
    //database_test2();
    //database_test3();
    //database_test4();
    //database_test5();
    //database_test6();
    database_test8();
}

//single put
void database_test1()
{
    printf("TEST 1 STARTING \n");
    test_item* it = gen_reg_item(1024);

    database_test_put(it);
    struct ssd_line* c_it = database_test_get(it);



    if(!ssd_compare(it,c_it)) {
        printf("PUT DID NOT WORK!! \n");

        if(c_it != NULL) display(c_it->key,it->key,it->keylen + it->vallen);
    }
    else {printf("TEST1 PASSED \n");}

    free(it);
    printf("TEST 1 ENDING\n");
}

//single put
void database_test2()
{
    printf("TEST 2 STARTING \n");
    test_item* it = gen_reg_item(SSD_PAGE_SIZE *2);

    database_test_put(it);
    struct ssd_line* c_it = database_test_get(it);



    if(!ssd_compare(it,c_it)) {
        printf("PUT DID NOT WORK!! \n");

        if(c_it != NULL) display(c_it->key,it->key,it->keylen + it->vallen);
    }
    else {printf("TEST 2 PASSED \n");}

    free(it);
    printf("TEST 2 ENDING\n");
}


//put and read 1000 items
void database_test3()
{
    char * c = "TEST 3 ";
    printf("TEST 3 STARTING\n");

    int n = 100;
    size_t size = 4024;

    test_item** items = gen_n__reg_items(size, n);

    for(int i = 0; i < n ; i++)
    {

        //printf("ITEM: %d \n",i);
        database_test_put(items[i]);
    }
    

    for(int i = 0; i < n ; i++)
    {
        //printf("ITEM: %d \n",i);
        struct ssd_line* c_it = database_test_get(items[i]);
        ssd_test_compare_if_wrong(c,items[i],c_it,i);
        free(items[i]);
    }

    free(items);
    printf("TEST 3 ENDING\n");

}





//change valsize
void database_test4()
{
    printf("TEST 4 STARTING\n");
    char* c = "TEST 4";
    test_item* it = gen_reg_item(1024);

    database_test_put(it);
    struct ssd_line* c_it = database_test_get(it);
    ssd_test_compare(c,it,c_it,1);



    change_val(it);
    it = change_valsize(it,1500);

    database_test_put(it);
    c_it = database_test_get(it);
    ssd_test_compare(c,it,c_it,2);


    free(it);
    printf("TEST 4 ENDING\n");

}


//put a ton of data in
void database_test5()
{
    char * c = "TEST 5 ";
    printf("TEST 5 STARTING\n");

    size_t total = 1* (1ULL << 30);
    
    if(MULTI) total = total / 4;
    size_t size = 1ULL << 15;
    int n = total / size;
    printf("N: %d \n",n); 

    test_item** items = gen_n__reg_items(size, n);

    for(int i = 0; i < n ; i++)
    {

        printf("ITEM: %d \n",i);
        database_test_put(items[i]);
        struct ssd_line* c_it = database_test_get(items[i]);
        ssd_test_compare_if_wrong(c,items[i],c_it,i);
        if(c_it != NULL) free(c_it);
    }
    

    for(int i = 0; i < n ; i++)
    {
        printf("ITEM: %d \n",i);
        struct ssd_line* c_it = database_test_get(items[i]);
        ssd_test_compare_if_wrong(c,items[i],c_it,i);
        if(c_it != NULL) free(c_it);
        free(items[i]);
    }

    free(items);
    GEN_LOG_WRITE("TEST 5 ENDING");
    printf("TEST 5 ENDING\n");

}


void database_test6()
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
        
        if(i % 5 == 0) printf("I: %d   OPT: %d   :: %d\n",i,opt,rte_lcore_id());
        switch(opt){
            case  0 : ;            // generate an item and check for it
                    GEN_LOG_WRITE("TEST PUTTING START");
                    size = (rand() % maxsize) + sizeof(size_t);
                    items[num] = gen_reg_item(size);
                    //printf("PUTTING ITEM: %lu  :: %d \n",*((size_t*)(items[num]->key)),rte_lcore_id() );

                    database_test_put(items[num]);
                    GEN_LOG_WRITE("TEST PUT SUCCESSFUL, GETTING");
                    c_it = database_test_get(items[num]);
                    GEN_LOG_WRITE("TEST GETTING SUCCESSFUL, COMPARING");
                    ssd_test_compare_if_wrong(c,items[num],c_it,i);
                    GEN_LOG_WRITE("TEST COMPARING SUCESSFUL");
                    num++;
                    GEN_LOG_WRITE("TEST PUTTING END");
                    break;
            // search for a random item
            case 1 : ;
                    //TEST_PRINT_IF(i > 100,"C1 1");
                    ind = rand() % num;
                    c_it = database_test_get(items[ind]);
                    if(c_it != NULL)
                    {
                        ssd_test_compare_if_wrong(c,items[ind],c_it,i);
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
                    database_test_put(items[ind]);
                    //TEST_PRINT_IF(i > 100,"C2 4");
                    c_it = database_test_get(items[ind]);
                    //TEST_PRINT_IF(i > 100,"C2 5");
                    ssd_test_compare_if_wrong(c,items[ind],c_it,i);
                    break;
            // change size reput and test
            case 3 :  ;
                    
                    //TEST_PRINT_IF(i > 100,"C3 1");
                    ind = rand() % num;
                    size = (rand() % maxsize) + sizeof(size_t);
                    items[ind] = change_valsize(items[ind],1500);
                    database_test_put(items[ind]);
                    c_it = database_test_get(items[ind]);
                    ssd_test_compare_if_wrong(c,items[ind],c_it,i);
                    break;
        }
    }

    for(int i = 0 ; i < num; i++)
    {
        free(items[i]);

    }


    free(items);
    GEN_LOG_WRITE("TEST 6 END");
    printf("TEST 6 END \n");
}


//put and read 1000 items with random sizes
void database_test8()
{
    char * c = "TEST 8 ";
    printf("TEST 8 STARTING\n");

    srand(10);
    int maxsize = (1ULL << 30) / 100;
    size_t size = 100000;
    int n = maxsize / size;


    test_item** items = gen_n__reg_items(size, n);

    for(int i = 0; i < n ; i++)
    {

        printf("ITEM: %d \n",i);
        database_test_put(items[i]);
        struct ssd_line* c_it = database_test_get(items[i]);
        size_t key = *((size_t*)(items[i]->key));
        ssd_test_compare_if_wrong(c,items[i],c_it,key);
    }
    

    for(int i = 0; i < n ; i++)
    {
        printf("ITEM: %d \n",i);
        struct ssd_line* c_it = database_test_get(items[i]);
        ssd_test_compare_if_wrong(c,items[i],c_it,i);
        free(items[i]);
    }

    free(items);
    GEN_LOG_WRITE("TEST 8 END");
    printf("TEST 8 ENDING\n");

}
