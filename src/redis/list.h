#ifndef __REDIS_LIST_H
#define __REDIS_LIST_H

#include "redis/object.h"

unsigned long listTypeLength(const robj *subject);
robj *listTypeGet(listTypeEntry *entry);
listTypeIterator *listTypeInitIterator(robj *subject, long index, unsigned char direction);
void listTypeReleaseIterator(listTypeIterator *li);
int listTypeNext(listTypeIterator *li, listTypeEntry *entry);
robj *listTypeGet(listTypeEntry *entry);

#endif
