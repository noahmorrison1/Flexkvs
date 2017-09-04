#ifndef TESTER_H_
#define TESTER_H_

#include "global.h"
#include "database.h"
#include <stdlib.h>
#include <stdio.h>
#include <rte_spinlock.h>
#include <rte_lcore.h>


typedef struct {
	void *key;
	void *val;
	size_t vallen;
	size_t keylen;
	size_t id;
} test_item;


static int key_num = 1;

int get_key_num(void);

/** Generates a item with the key as the current key_num and the value of the given size **/
test_item* gen_reg_item(size_t vallen);




/**
Genreates an item with the given keylen and vallen
***/
test_item* gen_var_item(size_t keylen, size_t vallen);

// if keylen, = 0, then default keylen
test_item** gen_n_items(size_t keylen, size_t vallen, int n);

test_item** gen_n__reg_items(size_t vallen, int n);

void change_val(test_item* it);

test_item* change_valsize(test_item* it,size_t length);

void test_init(void);


void test_put(test_item* it);


void test_put_all(test_item** items, int n);

// fill void space with 1111
void write_ones(void* val, size_t vallen);


#include "database_tester.h"
#include "cache_tester.h"
#include "ssd_tester.h"
#endif
