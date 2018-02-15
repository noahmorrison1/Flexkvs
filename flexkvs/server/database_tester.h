#ifndef DATABASE_TESTER_H
#define DATABASE_TESTER_H


#include "database.h"
#include "tester.h"
#include <stdlib.h>
#include <stdio.h>
#include "cache_tester.h"
#include "iokvs.h"
#include "global.h"



void database_test_init(void);

void database_test_put(test_item* it);

void database_test_put_all(test_item** items, int n);

struct ssd_line* database_test_get(test_item* it);

void database_test_1(void);

void database_test_2(void);


void database_test_3(void);

void database_test_4(void);

void database_test_5(void);

void database_test_6(void);


void database_test_7(void);

void database_test_8(void);

#endif











