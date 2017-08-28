#ifndef SSD_TESTER_H_
#define SSD_TESTER_H_

#include "database.h"
#include <stdlib.h>
#include <stdio.h>
#include "tester.h"


bool ssd_compare(test_item* t_it,struct ssd_line* it);


void ssd_test_init(void);


void ssd_test_put(test_item* it);


void ssd_test_put_all(test_item** items, int n);


struct ssd_line* ssd_test_get(test_item* it);

void ssd_test_compare(char* c, test_item* it,struct ssd_line* c_it,int n);


void ssd_test_compare_if_wrong(char* c, test_item* it,struct ssd_line* c_it,int n);


void ssd_test1(void);

void ssd_test2(void);


void ssd_test3(void);

void ssd_test4(void);

void ssd_test5(void);

void ssd_test6(void);

void ssd_test8(void);



#endif
