#include "database.h" 


struct cache_item* database_get(void *key, size_t keylen, uint32_t hv, int t)
{
    TEST_PRINT_FINAL("BEFORE CACHE GET",t);
	struct cache_item* ret = cache_ht_get(key,keylen,hv);
	if(ret != NULL) return ret;
    TEST_PRINT_FINAL("BEFORE SSD GET",t);
	struct ssd_line* line = ssd_ht_get(key,keylen,hv);
	if(line == NULL) return NULL;
    TEST_PRINT_FINAL("BEFORE CACHE SET",t);
	cache_ht_set(line->key,line->keylen,line->val,line->vallen,hv,line->version);
	TEST_PRINT_FINAL("BEFORE CACHE SET",t);
	return cache_ht_get(key,keylen,hv);

}

void database_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv, int t)
{
    TEST_PRINT_FINAL("SSSD SET",t);
	size_t version = ssd_ht_set(key,keylen,val,vallen,hv);
	TEST_PRINT_FINAL("CACHE SET",t);
	cache_ht_set(key,keylen,val,vallen,hv,version);
}

void database_init()
{
	TEST_PRINT("DATABASE INITIALIZING \n");
	ssd_ht_init();
	cache_init();
	TEST_PRINT("DATABASE INITIALIZING DONE\n");
}