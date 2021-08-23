#include <stdlib.h>
#include <memory.h>
#include "circular_list.h"

CAPI CircularList* CircularList_Create(unsigned int count, unsigned int element_size) {
    int i, next, prev, size = sizeof(CircularList) + sizeof(CircularListNode)*count + element_size*count;
    char *pool_ptr;
    CircularList* clist = (CircularList*)malloc(size);
    if (clist) {
        memset(clist, 0, size);
        clist->nodes = (CircularListNode*)(clist+1);
        clist->pool = (void*)(clist->nodes + count);
        clist->count = count;
        clist->element_size = element_size;

        pool_ptr = clist->pool;
        for (i=0; i<(int)count; i++, pool_ptr += element_size) {
            next = i+1;
            if (next == (int)count)
                next = 0;

            prev = i-1;
            if (prev < 0)
                prev = count-1;

            clist->nodes[i].data = (void*)pool_ptr;
            clist->nodes[i].prev = &(clist->nodes[prev]);
            clist->nodes[i].next = &(clist->nodes[next]);
            clist->nodes[i].index = i;
        }

    }
    return clist;
}

CAPI void CircularList_Destroy(CircularList *list) {
    if (list) free(list);
}

CAPI void CircularList_Reset(CircularList *list) {
    if (list) memset(list->pool, 0, list->element_size*list->count);
}
