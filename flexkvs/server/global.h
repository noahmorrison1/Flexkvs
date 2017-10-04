#ifndef GLOBAL_H_
#define GLOBAL_H_


#include <stdlib.h>
#include <stdint.h> 
#include <string.h>
#include "tester.h"
#include <rte_lcore.h>
#include <rte_spinlock.h>

#ifndef TESTING 
#define TESTING true
#endif

#ifndef TESTING_FINAL
#define TESTING_FINAL false
#endif

#define STOP false

#ifndef TEST_PRINT
#define TEST_PRINT(c) if(TESTING) printf(c);
#endif 

#ifndef MULTI
#define MULTI true
#endif


#ifndef TEST_PRINT_FINAL
#define TEST_PRINT_FINAL(c,d) if(TESTING_FINAL) printf("%s : %lu    :: %d \n",c,d,rte_lcore_id());
#endif 

#ifndef TEST_PRINT_FINAL_1
#define TEST_PRINT_FINAL_1(c,d,e) if(TESTING_FINAL) printf("%s : %lu   : %lu  ::%d \n",c,d,e,rte_lcore_id());
#endif 


#ifndef TEST_PRINT_FINAL_2
#define TEST_PRINT_FINAL_2(c,d,e,f) if(TESTING_FINAL) printf("%s : %lu  -  %lu : %lu  :: %d\n",c,d,e,f,rte_lcore_id());
#endif 

#ifndef TEST_PRINT_FINAL_4
#define TEST_PRINT_FINAL_4(c,d,e,f,g,i) if(TESTING_FINAL) printf("%s : %lu  -  %lu : %s : %lu -  %lu  :: %d \n",c,d,e,f,g,i,rte_lcore_id());
#endif 



#ifndef TEST_PRINT_V2_4
#define TEST_PRINT_V2_4(c,d,e,f,g,i) if(false) printf("%s : %lu  -  %lu : %s : %lu -  %lu   :: %d \n",c,d,e,f,g,i,rte_lcore_id());
#endif 


#ifndef TEST_PRINT_2
#define TEST_PRINT_2(c,n) if(TESTING) printf("%s %lu  :: %d\n",c,n, rte_lcore_id());
#endif 

#ifndef TEST_PRINT_IF
#define TEST_PRINT_IF(b,c) if(b && STOP) printf("%s    %d \n",c,rte_lcore_id());
#endif

#ifndef TEST_PRINT_IF_2
#define TEST_PRINT_IF_2(b,c,n) if(b && STOP) printf("%s %lu  :: %d\n",c,n,rte_lcore_id());
#endif




#ifndef TEST_DATA
#define TEST_DATA(src,size,c)  if(TESTING) {for(int i =0 ; i < size; i++) {	uint8_t s = *(src + i); if(s != 0){  printf("S %s: %d \n",c,s); printf("I %s: %d \n",c,i); } }}
#endif

#ifndef LOCK_TEST
#define LOCK_TEST false
#endif

#ifndef NOHTLOCKS
#define RTE_LOCK(l,c) rte_spinlock_lock(l); if(LOCK_TEST) printf("LOCK AT %s  :: %d \n",c,rte_lcore_id());
#else
#define RTE_LOCK(l,c) 
#endif


#ifndef NOHTLOCKS
#define RTE_UNLOCK(l,c) rte_spinlock_unlock(l); if(LOCK_TEST) printf("UNLOCK AT %s  :: %d \n",c,rte_lcore_id());
#else
#define RTE_UNLOCK(l,c) 
#endif

#ifndef NOHTLOCKS
#define RTE_TRYLOCK(l,c) rte_spinlock_trylock(l); if(LOCK_TEST) printf("TRYLOCK AT %s :: %d\n",c,rte_lcore_id());
#else
#define RTE_TRYLOCK(l,c) 
#endif


//if(TESTING_FINAL) printf("Writing to Buffer %lu - %lu  :: %d \n",(char*)dest - (char*)start,(char*)dest - (char*)start + amount ,rte_lcore_id());


#ifndef WRITE_TO_SSD
#define WRITE_TO_SSD(dest,src,amount,pages,ssd) memmove(dest,src,SSD_PAGE_SIZE); if(TESTING_FINAL) { for(int u = 32; u < amount - SSD_MIN_ENTRY_SIZE -1 ; u++) {if(src[u] == 0) printf("WRITING 0: %d  ::   page :: %lu  ID :: %d  \n ",u,((char*)src - (char*)pages)/sizeof(struct page),rte_lcore_id()); } }
//if(TESTING_FINAL) printf("Writing From BUFFER: %lu  to  SSD Page: %lu  :: %d \n",(char*)src - (char*)pages, ((char*)dest - (char*)ssd)/4096,rte_lcore_id() );
#endif

#ifndef PRINT
#define PRINT(c) printf("%s \n",c)
#endif


#ifndef LOG
#define LOG(l,n) RTE_LOCK(&logs.lock,"LOGS"); logs.count++; fprintf(logs.fptrs[l][rte_lcore_id()],"%d :: %d \n",n,logs.count); RTE_UNLOCK(&logs.lock,"LOGS"); fflush(logs.fptrs[l][rte_lcore_id()])
#endif

#ifndef LOGC
#define LOGC(l,c) RTE_LOCK(&logs.lock,"LOGS"); logs.count++; fprintf(logs.fptrs[l][rte_lcore_id()],"%s :: %d \n",c,logs.count); RTE_UNLOCK(&logs.lock,"LOGS"); fflush(logs.fptrs[l][rte_lcore_id()])
#endif

#ifndef LOGCN
#define LOGCN(l,c,n) RTE_LOCK(&logs.lock,"LOGS"); logs.count++; fprintf(logs.fptrs[l][rte_lcore_id()],"%s %lu :: %d \n",c,n,logs.count); RTE_UNLOCK(&logs.lock,"LOGS"); fflush(logs.fptrs[l][rte_lcore_id()])
#endif

#ifndef NUMLOGS
#define NUMLOGS 4
#endif

#ifndef PAGE_LOG_I
#define PAGE_LOG_I 0
#endif

#ifndef WRITE_OUT_LOG_I
#define WRITE_OUT_LOG_I 1
#endif


#ifndef READ_LOG_I
#define READ_LOG_I 2
#endif

#ifndef GEN_LOG_I
#define GEN_LOG_I 3
#endif

#ifndef VALG_LOG_I
#define VALG_LOG_I 3
#endif

#ifndef GEN_LOG_WRITE
#define GEN_LOG_WRITE(c) LOGC(GEN_LOG_I,c)
#endif

#ifndef GEN_LOG_WRITE_2
#define GEN_LOG_WRITE_2(c,n) LOGCN(GEN_LOG_I,c,n)
#endif

#ifndef VALG_LOG_WRITE
#define VALG_LOG_WRITE(c) LOGC(VALG_LOG_I,c)
#endif

#ifndef VALG_LOG_WRITE_2
#define VALG_LOG_WRITE_2(c,n) LOGCN(VALG_LOG_I,c,n)
#endif

#ifndef LOG_BREAK
#define LOG_BREAK(l) LOGC(l,"")
#endif

#ifndef VALG_SIZE
#define VALG_SIZE 20
#endif

#ifndef CALLOC 
#define CALLOC(num,size,name) new_calloc(num,size,name)
#endif


struct {
	char** base_names;
	int* fptrs[NUMLOGS];
	rte_spinlock_t lock;
	size_t count;
} logs;

struct calloc_log{
	char* start;
	size_t size;
	size_t count;
	char* name;
};




#include <unistd.h>
#include <stdio.h>


void global_init(void);

void logs_init(void);

void valgrind_init(void);

void* new_calloc(size_t num, size_t size, char* name);

void* add_to_valg(char* start, size_t num, size_t , char* name);

void* new_memcpy(char* src, char* dest, size_t amount, char* name);

void display(char * src,char *dest,size_t amount);



#endif