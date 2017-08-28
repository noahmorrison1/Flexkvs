#ifndef DATABASE_TESTER_H
#define DATABASE_TESTER_H


#include "database.h"
#include <stdlib.h>
#include <stdio.h>
#include "cache_tester.h"
#include "iokvs.h"
#include "global.h"



void database_test_init(void);

void database_test_put(test_item* it, int t);

void database_test_put_all(test_item** items, int n, int t);

struct cache_item* database_test_get(test_item* it, int t);

void database_test_1(void);

void database_test_2(void);


void database_test_3(void);

void database_test_4(void);

#endif











