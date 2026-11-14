#include "AllocInterpose.h"

#include <malloc/malloc.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

/* Separate image so dyld applies these hooks to the test executable.
   The main executable's own __interpose section does not see its calls. */

static _Atomic long long mallocs;
static _Atomic long long callocs;
static _Atomic long long reallocs;
static _Atomic int armed;
static pthread_t watch;
static void* firstMalloc;
static void* firstCalloc;
static void* firstRealloc;

static int counting (void)
{
    return atomic_load_explicit (&armed, memory_order_relaxed)
        && pthread_equal (pthread_self(), watch);
}

static void* openeqMalloc (size_t size)
{
    if (counting())
    {
        if (atomic_fetch_add_explicit (&mallocs, 1, memory_order_relaxed) == 0)
            firstMalloc = __builtin_return_address (0);
    }

    malloc_zone_t* zone = malloc_default_zone();
    return zone->malloc (zone, size);
}

static void* openeqCalloc (size_t count, size_t size)
{
    if (counting())
    {
        if (atomic_fetch_add_explicit (&callocs, 1, memory_order_relaxed) == 0)
            firstCalloc = __builtin_return_address (0);
    }

    malloc_zone_t* zone = malloc_default_zone();
    return zone->calloc (zone, count, size);
}

static void* openeqRealloc (void* pointer, size_t size)
{
    if (counting())
    {
        if (atomic_fetch_add_explicit (&reallocs, 1, memory_order_relaxed) == 0)
            firstRealloc = __builtin_return_address (0);
    }

    malloc_zone_t* zone = malloc_default_zone();
    return zone->realloc (zone, pointer, size);
}

typedef struct DyldInterpose
{
    const void* replacement;
    const void* replacee;
} DyldInterpose;

__attribute__((used, section ("__DATA,__interpose")))
static const DyldInterpose openeqInterposers[] = {
    { (const void*) openeqMalloc,  (const void*) malloc },
    { (const void*) openeqCalloc,  (const void*) calloc },
    { (const void*) openeqRealloc, (const void*) realloc }
};

void openeqAllocReset (void)
{
    atomic_store (&mallocs, 0);
    atomic_store (&callocs, 0);
    atomic_store (&reallocs, 0);
    firstMalloc = 0;
    firstCalloc = 0;
    firstRealloc = 0;
    watch = pthread_self();
    (void) openeqInterposers;
}

void openeqAllocSetArmed (int isArmed)
{
    atomic_store (&armed, isArmed);
}

void openeqAllocRead (long long* outMallocs, long long* outCallocs, long long* outReallocs)
{
    if (outMallocs != 0)
        *outMallocs = atomic_load (&mallocs);
    if (outCallocs != 0)
        *outCallocs = atomic_load (&callocs);
    if (outReallocs != 0)
        *outReallocs = atomic_load (&reallocs);
}

void* openeqAllocFirstCaller (int which)
{
    if (which == 1)
        return firstCalloc;
    if (which == 2)
        return firstRealloc;
    return firstMalloc;
}

/* Legacy forwarders */
void godeqAllocReset (void) { openeqAllocReset(); }
void godeqAllocSetArmed (int isArmed) { openeqAllocSetArmed (isArmed); }
void godeqAllocRead (long long* m, long long* c, long long* r) { openeqAllocRead (m, c, r); }
void* godeqAllocFirstCaller (int which) { return openeqAllocFirstCaller (which); }
