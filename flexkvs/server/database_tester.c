

#include "database_tester.h"




rte_spinlock_t database_test_lock;

void database_test_init()
{
    rte_spinlock_init(&database_test_lock);
    database_test_3();
}

void database_test_put(test_item* it, int t)
{
    size_t hv = jenkins_hash(it->key,it->keylen);
    database_set(it->key, it->keylen,it->val, it->vallen, hv,t);
}

void database_test_put_all(test_item** items, int n, int t)
{
    for(int i = 0; i < n ; i++)
    {
       database_test_put(items[i],t);
    }
}

struct cache_item* database_test_get(test_item* it,  int t)
{
    size_t hv = jenkins_hash(it->key,it->keylen);
    return database_get(it->key, it->keylen, hv ,t);
}



//single put
void database_test_1()
{
    printf("TEST 1 STARTING \n");
    test_item* it = gen_reg_item(1024);
    int t = rand();
    database_test_put(it,t);
    struct cache_item* c_it = database_test_get(it,t);



    if(!compare(it,c_it)) {TEST_PRINT("PUT DID NOT WORK!! \n");}
    else {TEST_PRINT("TEST1 PASSED");}

    #ifndef NOHTLOCKS
        rte_spinlock_unlock(&c_it->lock);
    #endif
    free(it);
    TEST_PRINT("TEST 1 ENDING\n");
}

void database_test_2()
{
    char * c = "TEST 4 ";
    printf("TEST 2 \n");
    int t = rand();
    int n = 200;
    size_t size = 1024;
    
    test_item** items = gen_n__reg_items(size, n);
    for(int i = 0; i < n ; i++)
    {
        printf("ITEM: %d \n",i);
        database_test_put(items[i],t);
    }
    
    
    for(int i = 0; i < n ; i++)
    {
        struct cache_item* c_it = database_test_get(items[i],t);
        test_compare_if_wrong(c,items[i],c_it,i);
        free(items[i]);
    }
    
    free(items);
    printf("TEST 2 END \n");
    
}


void database_test_3()
{
    char * c = "TEST 7 ";
    printf("TEST 7 START \n");
    rte_spinlock_lock(&database_test_lock);
    int t = time(NULL);
    srand(t);
    t = rand();
    rte_spinlock_unlock(&database_test_lock);
    int n = 4000;
    test_item** items = calloc(n,sizeof(test_item*));
    int maxsize = RAM_CACHE_SIZE / 10;
    int num_opts = 4; 
    int num = 0;
    int made = 0;
    
    printf("MAXSIZE %d \n",maxsize);
    for(int i = 0 ; i < n ; i++)
    {
        if(i % 1 == 0) printf("I: %d       : %d \n",i,t);
        //TEST_PRINT_IF(COND7," BEFORE SWITCH \n");        
        int opt = rand() % num_opts; 
        struct cache_item* c_it = NULL;
        int size = 0;
        int ind= -1;
        //TEST_PRINT_IF(COND7," ENTER SWITCH \n");
        if( i < 4000) opt = 0;
        switch(opt){
            case  0 : ;            // generate an item and check for it
                    //TEST_PRINT_IF(COND7," SWTICH 0 \n");
                    size = (rand() % maxsize) + sizeof(size_t);
                    TEST_PRINT_FINAL("Before Gen ",t);
                    items[made] = gen_reg_item(size);
                    TEST_PRINT_FINAL("Before PUT ",t);
                    database_test_put(items[made],t);
                    TEST_PRINT_FINAL("Before GET ",t);
                    c_it = database_test_get(items[made],t);
                    TEST_PRINT_FINAL("Before COMPARE ",t);
                    test_compare_if_wrong(c,items[made],c_it,i);
                    TEST_PRINT_FINAL("AFTER COMPARE ",t);
                    made++;
                    break;
            // search for a random item
            case 1 : ;
                    //TEST_PRINT_IF(COND7," SWTICH 1 \n");
                    ind = rand() % num;
                    c_it = database_test_get(items[ind],t);
                    if(c_it != NULL)
                    {
                        test_compare_if_wrong(c,items[ind],c_it,i);
                    }
                    break;
            // change and reput and test 
            case 2 : ;
                    //TEST_PRINT_IF(COND7," SWTICH 2 \n");
                    ind = rand() % num;
                    change_val(items[ind]);
                    //items[ind] = change_valsize(items[i],1500);
                    database_test_put(items[ind],t);
                    c_it = database_test_get(items[ind],t);
                    test_compare_if_wrong(c,items[ind],c_it,i);
                    break;
            // change size reput and test
            case 3 :  ;
                    //TEST_PRINT_IF(COND7," SWTICH 3 \n");
                    ind = rand() % num;
                    size = (rand() % maxsize) + sizeof(size_t);
                    items[ind] = change_valsize(items[ind],1500);
                    database_test_put(items[ind],t);
                    c_it = database_test_get(items[ind],t);
                    test_compare_if_wrong(c,items[ind],c_it,i);
                    break;
        }
        num = made;
    }

    for(int i = 0 ; i < made; i++)
    {
        free(items[i]);

    }


    free(items);

    printf("TEST 7 END \n");
}

void database_test_4()
{
    char * c = "TEST 4 ";
    printf("TEST 4 \n");
    int t = rand();
    int n = 200;
    size_t size = 1024;
    
    test_item** items = gen_n__reg_items(size, n);
    for(int i = 0; i < n ; i++)
    {
        printf("ITEM: %d \n",i);
        database_test_put(items[i],t);
        struct cache_item* c_it = database_test_get(items[i],t);
        test_compare_if_wrong(c,items[i],c_it,i);
    }
    
    
    for(int i = 0; i < n ; i++)
    {
        free(items[i]);
    }
    
    free(items);
    printf("TEST 4 END \n");
    
}
