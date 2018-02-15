#ifndef CACHE_TESTER_H_
#define CACHE_TESTER_H_

#include "database.h"
#include "global.h"
#include "tester.h"
#include <stdlib.h>
#include <stdio.h>

#define tyepdef struct test_item test_item;


bool compare(test_item* t_it,struct cache_item* it);


/** Compares the key of item with the key, assuming the keylens are the same **/
//inline bool test_compare_item(struct cache_item* it, const void *key, size_t keylen, const void* val, size_t vallen);


void cache_test_init(void);


void test_put(test_item* it);


void test_put_all(test_item** items, int n);


struct ssd_line* test_get(test_item* it);

void test_compare(char* c, test_item* it,struct ssd_line* c_it,int n);

void test_compare_if_wrong(char* c, test_item* it,struct ssd_line* c_it,int n);

void test_compare_if_right(char* c, test_item* it,struct ssd_line* c_it,int n);


void cache_test_init(void);

void test1(void);

void test2(void);

void test3(void);

void test4(void);

void test5(void);

void test6(void);

void test7(void);

void test8(void);

void test9(void);

void test10(void);

void test11(void);

void test12(void);





#endif
