#ifndef NVDIMM_TESTER_H_
#define NVDIMM_TESTER_H_

#include "database.h"
#include "NVDIMM.h"
#include <stdio.h>
#include "tester.h"


bool NVDIMM_compare(test_item* t_it,struct NVDIMM_line* it);


void NVDIMM_test_init(void);


void NVDIMM_test_put(test_item* it);


void NVDIMM_test_put_all(test_item** items, int n);


struct ssd_line* NVDIMM_test_get(test_item* it);

void NVDIMM_test_compare(char* c, test_item* it,struct NVDIMM_line* c_it,int n);


void NVDIMM_test_compare_if_wrong(char* c, test_item* it,struct NVDIMM_line* c_it,int n);


void NVDIMM_test1(void);

void NVDIMM_test2(void);


void NVDIMM_test3(void);

void NVDIMM_test4(void);

void NVDIMM_test5(void);

void NVDIMM_test6(void);

void NVDIMM_test8(void);



#endif
