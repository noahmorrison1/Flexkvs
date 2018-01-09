#include "database.h" 


struct ssd_line* database_get(void *key, size_t keylen, uint32_t hv, int t)
{

	struct ssd_line* line =  NVDIMM_read(key, keylen,hv);
	if(line != NULL) return line;


	struct cache_item* ret = cache_ht_get(key,keylen,hv);
	if(ret != NULL) return ret;


	line = ssd_ht_get(key,keylen,hv);
	if(line == NULL) return NULL;


	cache_ht_set(line->key,line->keylen,line->val,line->vallen,hv,line->version);

	return line;

}

void database_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv, int t)



void database_(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv, int t)
{
    TEST_PRINT_FINAL("SSSD SET",t);
	size_t version = ssd_ht_set(key,keylen,val,vallen,hv);
	TEST_PRINT_FINAL("CACHE SET",t);
	cache_ht_set(key,keylen,val,vallen,hv,version);
}

void database_init()
{
	TEST_PRINT("DATABASE INITIALIZING \n");
	NVDIMM_init();
	ssd_ht_init();
	cache_init();	
	TEST_PRINT("DATABASE INITIALIZING DONE\n");
}