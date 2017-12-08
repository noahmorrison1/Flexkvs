#include "cache_tester.h"
#include "iokvs.h"
#include "global.h"



/** Compares the key of item with the key, assuming the keylens are the same **/
inline bool test_compare_item(struct cache_item* it, const void *key, size_t keylen, const void* val, size_t vallen)
{
	TEST_PRINT_IF(true,"STARTING COMPARE: \n");
	int count = 0;
	char *dest = (char *)(it +1);
	char* src = (char * )key;
	struct fragment **more_data_ptr = &it->more_data;
	// amount left in block
    size_t rest = LOG_BLOCK_SIZE - sizeof(struct cache_item);
    size_t amount = rest < keylen ? rest : keylen;
    // returns 0 if equal
    if(__builtin_memcmp(dest,src,amount) != 0) 
	{
		TEST_PRINT_2("COUNT FIRST KEY: ",count);
		display(dest,src,amount);
		return false;
	}

    keylen -=  amount;
    dest += amount;
    src += amount;
    rest-=amount;
    count = 1;
    //when it enters this loop, it means we have already compared all of the previous block
	while(keylen > 0)
	{

		struct fragment *frag = *more_data_ptr;
		dest = (char *)(frag +1);
		more_data_ptr = &frag->next;
	    rest = LOG_BLOCK_SIZE - sizeof(frag);
	    amount = rest < keylen ? rest : keylen;
	    if(__builtin_memcmp(dest,src,amount) != 0) 
		{
			TEST_PRINT_2("COUNT KEY: ",count);
			display(src,dest,amount);
			return false;
		}
	    keylen -=  amount;
	    dest += amount;
	    src += amount;
	    count++;
	}

	src = (char*)val;

	if(rest != 0)
	{
	    amount = rest < vallen ? rest : vallen;
	    // returns 0 if equal
	    if(__builtin_memcmp(dest,src,amount) != 0) 
		{
			TEST_PRINT_2("COUNT VAL FIRST: ",count);
			display(src,dest,amount);
			return false;
		}

	    vallen -=  amount;
	    dest += amount;
	    src += amount;
	  
	}

	while(vallen > 0)
	{

		struct fragment *frag = *more_data_ptr;
		dest = (char *)(frag +1);
		count++;
		more_data_ptr = &frag->next;
	    rest = LOG_BLOCK_SIZE - sizeof(struct fragment);
	    amount = rest < vallen ? rest : vallen;
	    
	    if(__builtin_memcmp(dest,src,amount) != 0) 
		{
			TEST_PRINT_2("COUNT VAL: ",count);
			display(src,dest,amount);
			return false;
		}
	    vallen -=  amount;
	    dest += amount;
	    src += amount;
	}
	
	


	return true;
}

bool compare(test_item* t_it,struct cache_item* it)
{
	if(it == NULL)
	{
		//TEST_PRINT("COMPARED ITEM IS NULL\n");
	 	return it == t_it;
	 	//return true;
	}

	return it->keylen == t_it->keylen && it->vallen == t_it->vallen && test_compare_item(it, t_it->key,t_it->keylen,t_it->val,t_it->vallen);
}



static int w_id = 0;
static rte_spinlock_t id_lock;


void test_put(test_item* it)
{
	size_t hv = jenkins_hash(it->key,it->keylen);
	rte_spinlock_lock(&id_lock);
	int x = w_id++;
	rte_spinlock_unlock(&id_lock);
	cache_ht_set(it->key, it->keylen,it->val, it->vallen, hv ,x);
}

void test_put_all(test_item** items, int n)
{
	for(int i = 0; i < n ; i++)
	{
		test_put(items[i]);
	}
}


struct cache_item* test_get(test_item* it)
{

	size_t hv = jenkins_hash(it->key,it->keylen);
	return cache_ht_get(it->key, it->keylen, hv );
}




void cache_test_init()
{
	//do nothing for now
	rte_spinlock_init(&id_lock);
	//test1();
	//test2();
	//test3();
	//test4();
	//test5();
	//test6();
	test7();

}

void test_compare(char* c, test_item* it,struct cache_item* c_it,int n)
{
	if(!compare(it,c_it)) printf("%s: FAILED at %d \n",c,n);
	else printf("%s: PASSED at %d \n",c,n);

	read_release(c_it);
}

void test_compare_if_wrong(char* c, test_item* it,struct cache_item* c_it,int n)
{
	if(!compare(it,c_it)){ printf("%s: FAILED at %d \n",c,n); exit(0);}

	if(c_it != NULL){
	    read_release(c_it);
	}
}

void test_compare_if_right(char* c, test_item* it,struct cache_item* c_it,int n)
{
	if(compare(it,c_it)){ printf("%s: FAILED at %d \n",c,n); exit(0);}

	if(c_it != NULL){
	    read_release(c_it);
	}
}

//single put
void test1()
{
	TEST_PRINT("TEST 1 STARTING \n");
	test_item* it = gen_reg_item(1024);

	test_put(it);
	struct cache_item* c_it = test_get(it);



	if(!compare(it,c_it)) {TEST_PRINT("PUT DID NOT WORK!! \n");}
	else {TEST_PRINT("TEST1 PASSED \n");}

	#ifndef NOHTLOCKS
	    rte_spinlock_unlock(&c_it->lock);
	#endif
	free(it);
	TEST_PRINT("TEST 1 ENDING \n");
}

//put then overwrite
void test2()
{

	TEST_PRINT("TEST 2 STARTING \n");
	test_item* it = gen_reg_item(1024);

	test_put(it);
	struct cache_item* c_it = test_get(it);
	test_compare("TEST2",it,c_it,1);



	change_val(it);
	test_put(it);
	c_it = test_get(it);
	test_compare("TEST2",it,c_it,2);


	free(it);
	TEST_PRINT("TEST 2 ENDING\n");
}



//change valsize
void test3()
{
	char * c = "TEST 3 ";
	TEST_PRINT_2(c,0UL);
	test_item* it = gen_reg_item(1024);

	test_put(it);
	struct cache_item* c_it = test_get(it);
	test_compare(c,it,c_it,1);



	change_val(it);
	it = change_valsize(it,1500);

	test_put(it);
	c_it = test_get(it);
	test_compare(c,it,c_it,2);


	free(it);
	TEST_PRINT_2(c,10UL);

}


//put and read 1000 items
void test4()
{
	char * c = "TEST 4 ";
	//TEST_PRINT_2(c,0UL);
    printf("TEST 4 STARTING \n");
	int n = 1000;
	size_t size = 1024;

	test_item** items = gen_n__reg_items(size, n);
	for(int i = 0; i < n ; i++)
	{
		//printf("ITEM: %d \n",i);
		test_put(items[i]);
	}
	

	for(int i = 0; i < n ; i++)
	{
		struct cache_item* c_it = test_get(items[i]);
		//printf("ITEM 2 : %d \n",i);
		test_compare_if_wrong(c,items[i],c_it,i);
		free(items[i]);
	}

	free(items);
	//TEST_PRINT_2(c,10UL);
    printf("TEST 4 ENDING \n");
}





//put read then change and read 1000 items
void test5()
{

	char * c = "TEST 5 ";
	TEST_PRINT("TEST 5 START \n");

	int n = 1000;
	size_t size = 1024;

	test_item** items = gen_n__reg_items(size, n);
	for(int i = 0; i < n ; i++)
	{
		//TEST_PRINT_2("ITEM: ",i);
		test_put(items[i]);
	}
	
	TEST_PRINT("TEST 5 PART 1 \n");
	for(int i = 0; i < n ; i++)
	{
		struct cache_item* c_it = test_get(items[i]);
		test_compare_if_wrong(c,items[i],c_it,i);
	}

	TEST_PRINT("TEST 5 PART 2\n");

	for(int i = 0; i < n ; i++)
	{
		//TEST_PRINT_2("ITEM: ",i);
		change_val(items[i]);
		items[i] = change_valsize(items[i],1500);
		test_put(items[i]);
	}
	
	TEST_PRINT("TEST 5 PART 3\n");

	for(int i = 0; i < n ; i++)
	{
		struct cache_item* c_it = test_get(items[i]);
		test_compare_if_wrong(c,items[i],c_it,i);
		free(items[i]);
	}

	free(items);
	TEST_PRINT("TEST 5 END \n");

}

//evict stuff
//also really big stuff
void test6()
{
	char * c = "TEST 6 ";

    printf("TEST 6 STARTING \n");

	int n = 24;
	size_t size = RAM_CACHE_SIZE / 20;

	if(MULTI) n = 10;

	test_item** items = gen_n__reg_items(size, n);
	for(int i = 0; i < n ; i++)
	{
		test_put(items[i]);
		printf("ITEMS: %d \n",i);
		struct cache_item* c_it = test_get(items[i]);
		test_compare_if_wrong(c,items[i],c_it,i);

	}

    //sleep(1);
	for(int i = 0; i < n ; i++)
	{
			struct cache_item* c_it = test_get(items[i]);
			printf("Items 2: %d \n",i);
			
			if(MULTI)
			{
			    if(c_it != NULL)
			    {
			        if(!compare(items[i],c_it))
			        {
			            printf("FAILED AT: %d \n",i);
			            printf("Vallen: %d,%d     Keylen: %d,%d   \n",items[i]->vallen,c_it->vallen,items[i]->keylen,c_it->keylen);
			            exit(0);
			        }
			        rte_spinlock_unlock(&c_it->lock);
			    }
			}
			else {
    			if(c_it != NULL){
    				//printf("Keylen1: %lu, Keylen2: %lu Vallen1: %lu Vallen2 %lu \n",c_it->keylen, items[i]->keylen, c_it->vallen, items[i]->vallen);
    				
    			}

    			if(i < 4 )
    			{
    				test_compare_if_right(c,items[i],c_it,i);
    
    			}
    			else if(i == 4)
    			{

    			}
    			else
    			{
    				test_compare_if_wrong(c,items[i],c_it,i);
    			}
			}
			
			free(items[i]);
	}


	free(items);

	//TEST_PRINT_2(c,15UL);
    printf("TEST 6 ENDING \n");
}


#define COND7 true
//random action
void test7()
{
	char * c = "TEST 7 ";
	printf("TEST 7 START \n");
	int t = time(0);
	//t = time(NULL);
	srand(t);
	int n = MULTI ? 3500 : 10000;
	test_item** items = calloc(n,sizeof(test_item*));
	int maxsize = MULTI ? RAM_CACHE_SIZE/40 : RAM_CACHE_SIZE / 20;
	int num_opts = 4; 
	int num = 0;
	printf("MAXSIZE %d \n",maxsize);
	int max = MULTI ? 100 : 20;
	for(int i = 0 ; i < n ; i++)
	{
		if(i % 1 == 0) printf("I: %d  \n",i);
		//TEST_PRINT_IF(COND7," BEFORE SWITCH \n");		
		int opt = rand() % num_opts; 
		struct cache_item* c_it = NULL;
		int size = 0;
		int ind= -1;
		//TEST_PRINT_IF(COND7," ENTER SWITCH \n");
		if( i < max) opt = 0;
		//TEST_PRINT_FINAL("SWITCH: ",opt);
		switch(opt){
			case  0 : ;			// generate an item and check for it
					GEN_LOG_WRITE(" SWTICH 0 ");
					size = (rand() % maxsize) + sizeof(size_t);
					items[num] = gen_reg_item(size);
					test_put(items[num]);
					c_it = test_get(items[num]);
					test_compare_if_wrong(c,items[num],c_it,i);

					num++;
					RAM_GEN_LOG_WRITE(" SWTICH 0 END");
					break;
			// search for a random item
			case 1 : ;
					GEN_LOG_WRITE(" SWTICH 1 ");
					ind = rand() % num;
					c_it = test_get(items[ind]);
					if(c_it != NULL)
					{
						test_compare_if_wrong(c,items[ind],c_it,i);
					}

					RAM_GEN_LOG_WRITE(" SWTICH 1 END");

					break;
			// change and reput and test 
			case 2 : ;
					GEN_LOG_WRITE(" SWTICH 2 ");
					ind = rand() % num;
					change_val(items[ind]);
					//items[ind] = change_valsize(items[i],1500);
					test_put(items[ind]);
					c_it = test_get(items[ind]);
					test_compare_if_wrong(c,items[ind],c_it,i);
					RAM_GEN_LOG_WRITE(" SWTICH 2 END");

					break;
			// change size reput and test
			case 3 :  ;
					GEN_LOG_WRITE(" SWTICH 3 ");
					ind = rand() % num;
					size = (rand() % maxsize) + sizeof(size_t);
					items[ind] = change_valsize(items[ind],1500);
					test_put(items[ind]);
					c_it = test_get(items[ind]);
					test_compare_if_wrong(c,items[ind],c_it,i);
					RAM_GEN_LOG_WRITE(" SWTICH 3 END");

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







void test8()
{

}

void test9(){}

void test10(){}

void test11(){}

void test12(){}

