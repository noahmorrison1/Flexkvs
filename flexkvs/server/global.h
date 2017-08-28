#ifndef GLOBAL_H_
#define GLOBAL_H_


#include <stdlib.h>
#include <stdint.h> 
#include "tester.h"
#include <rte_lcore.h>

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


#ifndef WRITE_TO_BUFFER
#define WRITE_TO_BUFFER(dest,src,amount,start) memcpy(dest,src,amount); if(TESTING_FINAL) printf("Writing to Buffer %lu - %lu  :: %d \n",(char*)dest - (char*)start,(char*)dest - (char*)start + amount ,rte_lcore_id());
#endif

#ifndef WRITE_TO_SSD
#define WRITE_TO_SSD(dest,src,amount,pages,ssd) memmove(dest,src,SSD_PAGE_SIZE); if(TESTING_FINAL) { for(int u = 32; u < amount - SSD_MIN_ENTRY_SIZE -1 ; u++) {if(src[u] == 0) printf("WRITING 0: %d  ::   page :: %lu  ID :: %d  \n ",u,((char*)src - (char*)pages)/sizeof(struct page),rte_lcore_id()); } }
//if(TESTING_FINAL) printf("Writing From BUFFER: %lu  to  SSD Page: %lu  :: %d \n",(char*)src - (char*)pages, ((char*)dest - (char*)ssd)/4096,rte_lcore_id() );
#endif


#include <unistd.h>
#include <stdio.h>


void global_init(void);

void display(char * src,char *dest,size_t amount);



#endif