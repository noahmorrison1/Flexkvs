#ifndef DATABASE_H_
#define DATABASE_H_

#include "ram_cache.h"
#include "ssd_ht.h"
#include "NVDIMM.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#include <unistd.h>

#include "global.h"

struct cache_item* database_get(void *key, size_t keylen, uint32_t hv,int t);

void database_set(void *key, size_t keylen, void *val, size_t vallen, uint32_t hv, int t);

void database_init();

#endif