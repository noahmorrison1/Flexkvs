#include "tester.h"
#include "iokvs.h"



/**
struct test_item {
	void *key;
	void *val;
	size_t vallen;
	size_t keylen;
	size_t id;
}
**/

rte_spinlock_t test_lock;

/** Generates a item with the key as the current key_num and the value of the given size **/
test_item* gen_reg_item(size_t vallen)
{
	GEN_LOG_WRITE("GEN REG ITEM START");
	//item is header + key_num + value
	test_item* t_it = calloc(sizeof(test_item) + vallen + sizeof(size_t) + 1,1);
	GEN_LOG_WRITE_2("ITEM IS : ",(size_t)t_it);

	GEN_LOG_WRITE("GEN REG ITEM START 1");
	rte_spinlock_lock(&test_lock);
	t_it->id = key_num++;
	rte_spinlock_unlock(&test_lock);
	
	GEN_LOG_WRITE("GEN REG ITEM START 2");

	size_t* key = (size_t *)(t_it + 1);
	t_it->key =  key;
	*key = t_it->id;
	size_t* val = key + 1;

	GEN_LOG_WRITE("GEN REG ITEM START 3");

	*val = t_it->id;
	t_it->val = val;
	t_it->vallen = vallen;
	t_it->keylen = sizeof(size_t);

	GEN_LOG_WRITE("GEN REG ITEM START 4");

	write_ones(t_it->val,vallen);



	GEN_LOG_WRITE("GEN REG ITEM END");

	return t_it;
}

test_item* gen_item(size_t id, size_t keylen, size_t vallen)
{
	test_item* t_it = calloc(1,sizeof(test_item) + vallen + sizeof(keylen) + 1);
	t_it->id = id;
	size_t* key = (size_t *)(t_it + 1);
	t_it->key =  key;
	*key = t_it->id;
	size_t* val = key + 1;
	*val = t_it->id;
	t_it->val = val;
	t_it->vallen = vallen;
	t_it->keylen = keylen;
	write_ones(t_it->val,vallen);
	return t_it;
}


/**
Genreates an item with the given keylen and vallen
***/
test_item* gen_var_item(size_t keylen, size_t vallen)
{
	//item is header + key_num + value
	test_item* t_it = calloc(1,sizeof(test_item) + vallen + keylen + 1);
	
	rte_spinlock_lock(&test_lock);
	t_it->id = key_num++;
	rte_spinlock_unlock(&test_lock);
	
	t_it->key = (t_it + 1);
	size_t* key = t_it->key;
	*key = t_it->id;
	t_it->val = (((char*)key) + keylen);
	size_t* val = t_it->val;
	*val = t_it->id;
	t_it->vallen = vallen;
	t_it->keylen = keylen;
	write_ones(t_it->val,vallen);
	return t_it;
}

void write_ones(void* val, size_t vallen)
{
    size_t* start = (size_t*)val;
    start++;
    uint8_t* space = (uint8_t*)start;
    vallen -= sizeof(size_t);


    while(vallen > 0)
    {
	    //TEST_PRINT_2("SPACE: ",(size_t)space);

        *space = 0xff;
        space++;
        vallen--;
    }
}

void change_val(test_item* it)
{
	size_t* dest = it->val;
	
	rte_spinlock_lock(&test_lock);
	*dest = key_num++;
	rte_spinlock_unlock(&test_lock);
}

int get_key_num()
{
	return key_num;
}

test_item* change_valsize(test_item* it,size_t length)
{
	test_item* new_it = gen_item(it->id,it->keylen,length);
	free(it);
	return new_it;
}

// if keylen, = 0, then default keylen
test_item** gen_n_items(size_t keylen, size_t vallen, int n)
{
	test_item** items = calloc(n+1,sizeof(test_item *));
	for(int i = 0 ; i < n; i ++)
	{
		items[i] = gen_var_item(keylen,vallen);
	}
	return items;
}

// if keylen, = 0, then default keylen
test_item** gen_n__reg_items(size_t vallen, int n)
{
	test_item** items = calloc(n+1,sizeof(test_item *));
	for(int i = 0 ; i < n; i ++)
	{
		items[i] = gen_reg_item(vallen);
	}
	return items;
}






void test_init()
{
  rte_spinlock_init(&test_lock);
	//cache_test_init();
	//ssd_test_init();
	//database_test_init();
	//NVDIMM_test_init();
	database_test_init();
}


