#include "database.h" 


struct ssd_line* database_get(void *key, size_t keylen, uint32_t hv)
{


	struct ssd_line* line;

#if NVD_ON	
	line = NVDIMM_read(key, keylen,hv);
	if(line != NULL) return line;
#endif



	line = cache_ht_get(key,keylen,hv);
	if(line != NULL) return line;


	line = ssd_ht_get(key,keylen,hv);
	if(line == NULL) return NULL;


	cache_ht_set(line->key,line->keylen,line->val,line->vallen,hv,line->version);

	return line;

}




// this is not including NVDIMM SET, this method is called by the NVDIMM CODE
void database_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv)
{

#if !NVD_ON
	cache_flush(key,keylen,hv);
#endif

	size_t version = ssd_ht_set(key,keylen,val,vallen,hv);

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