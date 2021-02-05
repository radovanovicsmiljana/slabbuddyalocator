#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "slab.h"
#include "buddy.h"

#define SLAB_SIZE (2*BLOCK_SIZE)
#define CACHE_NAMELEN (20)

int blockNum;
void* spaceS;
char* cache_list;
char* sizeN_list;
CRITICAL_SECTION slabMutex;

struct kmem_cache_s {
	char* slabs_full;
	char* slabs_partial;
	char* slabs_free;
	int objsize;
	int num;
	int colour;
	int slabsCreated;
	int expand;
	void (*Ctor)(void*);
	void (*Dtor)(void*);
	char name[CACHE_NAMELEN];
	char* next;
	int pos;
};

typedef struct cache_sizes {
	size_t cs_size;
	kmem_cache_t* cs_cachep;
} cache_sizes_t;

typedef struct slab_s {
	char* next;
	char* s_mem;
	unsigned long colouroff;
	unsigned int inuse;
	int free[SLAB_SIZE];
} slab_t;

void kmem_init(void* space, int block_num) {
	buddy_init(space, block_num);
	cache_list = NULL;
	spaceS = space;
	blockNum = block_num;
	sizeN_list = (char*)(buddy_alloc(spaceS, BLOCK_SIZE));
	for (int i = 0; i < 13; i++) {
		char* sizeN = sizeN_list + i * sizeof(cache_sizes_t);
		int powTwo = i + 5;
		((cache_sizes_t*)sizeN)->cs_size = pow(2, powTwo);
		((cache_sizes_t*)sizeN)->cs_cachep = NULL;
	}
	printf("koliko keseva ima u blocku %d\n", BLOCK_SIZE/sizeof(kmem_cache_t));
	InitializeCriticalSection(&slabMutex);
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*)) {
	EnterCriticalSection(&slabMutex);

	if (size <= 0 || (ctor == NULL && dtor != NULL)) {
		printf("Wrong parameters for cache create!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	char* tek = cache_list;
	char* prev = NULL;

	kmem_cache_t cache;
	cache.slabs_full = NULL;
	cache.slabs_partial = NULL;
	cache.slabs_free = NULL;
	cache.objsize = size;
	if (name != NULL) strcpy_s(cache.name, 20, name);
	else name = NULL;
	cache.num = SLAB_SIZE / size;
	cache.colour = (SLAB_SIZE % size) / CACHE_L1_LINE_SIZE;
	cache.slabsCreated = 0;
	cache.expand = 0;
	cache.next = NULL;
	if (ctor != NULL) cache.Ctor = ctor;
	else cache.Ctor = NULL;
	if (dtor != NULL) cache.Dtor = dtor;
	else cache.Dtor = NULL;

	if (cache_list == NULL) {
		cache.pos = 0;
		cache_list = (char*)(buddy_alloc(spaceS, BLOCK_SIZE));
		memcpy(cache_list, &cache, sizeof(kmem_cache_t));
		tek = cache_list;
	}
	else {
		char* help = tek;
		while (((kmem_cache_t*)help)->next != NULL) {
			prev = tek;
			tek = ((kmem_cache_t*)help)->next;
			help = tek;
		}
		prev = tek;
		cache.pos = ((kmem_cache_t*)prev)->pos + 1;
		if (cache.pos != BLOCK_SIZE / sizeof(kmem_cache_t)) {
			tek += sizeof(kmem_cache_t);
			((kmem_cache_t*)prev)->next = tek;
			memcpy(tek, &cache, sizeof(kmem_cache_t));
		}
		else {
			cache.pos = 0;
			tek = (char*)(buddy_alloc(spaceS, BLOCK_SIZE));
			((kmem_cache_t*)prev)->next = tek;
			memcpy(tek, &cache, sizeof(kmem_cache_t));
		}
	}
	printf("Napravljen kes sa imenom %s\n", ((kmem_cache_t*)tek)->name);
	LeaveCriticalSection(&slabMutex);
	return (kmem_cache_t*)tek;
}

void* kmem_cache_alloc(kmem_cache_t* cachep) {
	EnterCriticalSection(&slabMutex);
	
	if (cachep == NULL) {
		printf("Can't allocate cache that is not initialized!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	char* temp = cache_list;
	int gotIt = 0;

	if (cachep->name == NULL) {
		while (((kmem_cache_t*)temp) != NULL) {
			if ((((kmem_cache_t*)temp)->objsize == cachep->objsize) && (((kmem_cache_t*)temp)->name == NULL)) { gotIt = 1; break; }
			temp = ((kmem_cache_t*)temp)->next;
		}
	}
	else {
		while (((kmem_cache_t*)temp) != NULL) {
			if (((kmem_cache_t*)temp)->name == cachep->name && ((kmem_cache_t*)temp)->objsize == cachep->objsize) { gotIt = 1; break; }
			temp = ((kmem_cache_t*)temp)->next;
		}
	}


	if (gotIt == 0) {
		printf("Can't allocate object that does not have a cache\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	if (((kmem_cache_t*)temp)->slabs_partial != NULL) {
		//printf("Ima delimicno popunjenih ploca\n");
		char* help = ((kmem_cache_t*)temp)->slabs_partial;
		int i = 0;
		for (i = 0; i < ((kmem_cache_t*)temp)->num; i++) {
			if (((slab_t*)help)->free[i] == 0) break;
		}
		char* ret = (char*)(((slab_t*)help)->s_mem + ((slab_t*)help)->colouroff + i * ((kmem_cache_t*)temp)->objsize);
		((slab_t*)help)->inuse++;
		((slab_t*)help)->free[i] = 1;
		if (((kmem_cache_t*)temp)->Ctor != NULL) ((kmem_cache_t*)temp)->Ctor(ret);
		//printf("Alociramo element na lokaciji %d\n", ret);
		if (((slab_t*)help)->inuse == ((kmem_cache_t*)temp)->num) {
			printf("Popunili smo jedan delimican slab pa ga prebacujemo u listu punih slabova\n");
			if (((slab_t*)help)->next != NULL) ((kmem_cache_t*)temp)->slabs_partial = ((slab_t*)help)->next;
			else ((kmem_cache_t*)temp)->slabs_partial = NULL;
			if (((kmem_cache_t*)temp)->slabs_full != NULL) ((slab_t*)help)->next = ((kmem_cache_t*)temp)->slabs_full;
			else ((slab_t*)help)->next = NULL;
			((kmem_cache_t*)temp)->slabs_full = help;
		}
		LeaveCriticalSection(&slabMutex);
		return ret;
	}
	else if (((kmem_cache_t*)temp)->slabs_free != NULL) {
		printf("Ima slobodnih ploca\n");
		char* help = ((kmem_cache_t*)temp)->slabs_free;
		char* ret = ((slab_t*)help)->s_mem = ((slab_t*)help)->colouroff;
		((slab_t*)help)->inuse++;
		((slab_t*)help)->free[0] = 1;
		if (((slab_t*)help)->next != NULL) ((kmem_cache_t*)temp)->slabs_free = ((slab_t*)help)->next;
		else ((kmem_cache_t*)temp)->slabs_free = NULL;
		if (((kmem_cache_t*)temp)->slabs_partial != NULL) ((slab_t*)help)->next = ((kmem_cache_t*)temp)->slabs_partial;
		else ((slab_t*)help)->next = NULL;
		((kmem_cache_t*)temp)->slabs_partial = help;
		LeaveCriticalSection(&slabMutex);
		return ret;
	}
	else if ((((kmem_cache_t*)temp)->slabs_free == NULL) && (((kmem_cache_t*)temp)->slabs_partial == NULL)) {
		printf("Nema nista slobodno - moramo da pravimo novi slab\n");
		((kmem_cache_t*)temp)->slabs_partial = (char*)(buddy_alloc(spaceS, sizeof(slab_t)));
		char* initSlabManager = ((kmem_cache_t*)temp)->slabs_partial;
		((slab_t*)initSlabManager)->next = NULL;
		for (int i = 0; i < ((kmem_cache_t*)temp)->num; i++) ((slab_t*)initSlabManager)->free[i] = 0;
		((slab_t*)initSlabManager)->inuse = 1;
		if (cachep->colour != 0) {
			((slab_t*)initSlabManager)->colouroff = (cachep->slabsCreated % cachep->colour) * CACHE_L1_LINE_SIZE;
		}
		else ((slab_t*)initSlabManager)->colouroff = 0;
		
		((slab_t*)initSlabManager)->s_mem = (char*)(buddy_alloc(spaceS, SLAB_SIZE));
		void* ObjectStart = (void*)(((slab_t*)initSlabManager)->s_mem + ((slab_t*)initSlabManager)->colouroff);
		if (((kmem_cache_t*)temp)->Ctor != NULL) ((kmem_cache_t*)temp)->Ctor(ObjectStart);

		/*if (((kmem_cache_t*)temp)->Ctor != NULL) {
			for (int i = 0; i < ((kmem_cache_t*)temp)->num; i++) {
				void* ObjectStart = (void*)(((slab_t*)initSlabManager)->s_mem + ((slab_t*)initSlabManager)->colouroff + i* ((kmem_cache_t*)temp)->objsize);
				((kmem_cache_t*)temp)->Ctor(ObjectStart);
			}
		}**/
		((slab_t*)initSlabManager)->free[0] = 1;
		((kmem_cache_t*)temp)->slabsCreated++;
		if (((kmem_cache_t*)temp)->expand != 0) ((kmem_cache_t*)temp)->expand = 2;
		printf("Napravljen novi slab, kes sa imenom %s ima ukupno %d slabova\n", ((kmem_cache_t*)temp)->name, ((kmem_cache_t*)temp)->slabsCreated);
		char* ret = ((slab_t*)initSlabManager)->s_mem + ((slab_t*)initSlabManager)->colouroff;
		if (((slab_t*)initSlabManager)->inuse == ((kmem_cache_t*)temp)->num) {
			printf("Popunili smo jedan delimican slab pa ga prebacujemo u listu punih slabova\n");
			if (((slab_t*)initSlabManager)->next != NULL) ((kmem_cache_t*)temp)->slabs_partial = ((slab_t*)initSlabManager)->next;
			else ((kmem_cache_t*)temp)->slabs_partial = NULL;
			if (((kmem_cache_t*)temp)->slabs_full != NULL) ((slab_t*)initSlabManager)->next = ((kmem_cache_t*)temp)->slabs_full;
			else ((slab_t*)initSlabManager)->next = NULL;
			((kmem_cache_t*)temp)->slabs_full = initSlabManager;
		}
		LeaveCriticalSection(&slabMutex);
		return ret;
	}
	LeaveCriticalSection(&slabMutex);
	return NULL;
}

void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
	EnterCriticalSection(&slabMutex);

	if (cachep == NULL || objp == NULL) {
		printf("Can't deallocate cache that is not allocated!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	char* temp = cache_list;
	int gotIt = 0;

	if (cachep->name == NULL) {
		while (((kmem_cache_t*)temp) != NULL) {
			if ((((kmem_cache_t*)temp)->objsize == cachep->objsize) && (((kmem_cache_t*)temp)->name == NULL)) { gotIt = 1; break; }
			temp = ((kmem_cache_t*)temp)->next;
		}
	}
	else {
		while (((kmem_cache_t*)temp) != NULL) {
			if (((kmem_cache_t*)temp)->name == cachep->name && ((kmem_cache_t*)temp)->objsize == cachep->objsize) { gotIt = 1; break; }
			temp = ((kmem_cache_t*)temp)->next;
		}
	}

	if (gotIt == 0) {
		printf("Can't deallocate object that does not have a cache\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	if (((kmem_cache_t*)temp)->slabs_full != NULL) {
		char* help = ((kmem_cache_t*)temp)->slabs_full;
		char* prev = NULL;
		int found = 0;
		while (((slab_t*)help) != NULL) {
			char* deObj = NULL;
			for (int i = 0; i < ((kmem_cache_t*)temp)->num; i++) {
				deObj = ((slab_t*)help)->s_mem + ((slab_t*)help)->colouroff + ((kmem_cache_t*)temp)->objsize * i;
				if (deObj == objp) {
					found = 1;
					((slab_t*)help)->free[i] = 0;
					((slab_t*)help)->inuse--;
					break;
				}
			}
			if (found == 1) break;
			prev = help;
			help = ((slab_t*)help)->next;
		}
		if (found == 1) {
			if (((slab_t*)help)->inuse == 0) {
				if (prev == NULL)((kmem_cache_t*)temp)->slabs_partial = ((slab_t*)help)->next;
				else if (((slab_t*)help)->next != NULL) ((slab_t*)prev)->next = ((slab_t*)help)->next;
				else ((slab_t*)prev)->next = NULL;
				if (((kmem_cache_t*)temp)->slabs_free != NULL) ((slab_t*)help)->next = ((kmem_cache_t*)temp)->slabs_free;
				else ((slab_t*)help)->next = NULL;
				((kmem_cache_t*)temp)->slabs_free = ((slab_t*)help);
				printf("Partial slab je prebacen u free listu\n");
				LeaveCriticalSection(&slabMutex);
				return;
			}
			if (prev == NULL)((kmem_cache_t*)temp)->slabs_full = ((slab_t*)help)->next;
			else if (((slab_t*)help)->next != NULL) ((slab_t*)prev)->next = ((slab_t*)help)->next;
			else ((slab_t*)prev)->next = NULL;
			if (((kmem_cache_t*)temp)->slabs_partial != NULL) ((slab_t*)help)->next = ((kmem_cache_t*)temp)->slabs_partial;
			else ((slab_t*)help)->next = NULL;
			((kmem_cache_t*)temp)->slabs_partial = ((slab_t*)help);
			printf("Dealociran objekat iz full liste\n");
			LeaveCriticalSection(&slabMutex);
			return;
		}
	}
	if (((kmem_cache_t*)temp)->slabs_partial != NULL) {
		char* help = ((kmem_cache_t*)temp)->slabs_partial;
		char* prev = NULL;
		int found = 0;
		char* deObj = NULL;
		while (((slab_t*)help) != NULL) {
			for (int i = 0; i < ((kmem_cache_t*)temp)->num; i++) {
				deObj = ((slab_t*)help)->s_mem + ((slab_t*)help)->colouroff + ((kmem_cache_t*)temp)->objsize * i;
				if (deObj == objp && ((slab_t*)help)->free[i] == 1) {
					found = 1;
					((slab_t*)help)->free[i] = 0;
					((slab_t*)help)->inuse--;
					break;
				}
			}
			if (found == 1) break;
			prev = help;
			help = ((slab_t*)help)->next;
		}
		if (found == 1) {
			//printf("Dealociran objekat iz partial liste!\n");
			if (((slab_t*)help)->inuse == 0) {
				if (prev == NULL)((kmem_cache_t*)temp)->slabs_partial = ((slab_t*)help)->next;
				else if (((slab_t*)help)->next != NULL) ((slab_t*)prev)->next = ((slab_t*)help)->next;
				else ((slab_t*)prev)->next = NULL;
				if (((kmem_cache_t*)temp)->slabs_free != NULL) ((slab_t*)help)->next = ((kmem_cache_t*)temp)->slabs_free;
				else ((slab_t*)help)->next = NULL;
				((kmem_cache_t*)temp)->slabs_free = ((slab_t*)help);
				printf("Partial slab je prebacen u free listu\n");
				LeaveCriticalSection(&slabMutex);
				return;
			}
		}
		else {
			printf("Deallocation not possible!\n");
			LeaveCriticalSection(&slabMutex);
			exit(1);
		}
	}
	else {
		printf("No allocated objects in this cache!\n");
		LeaveCriticalSection(&slabMutex);
		return;
	}
	LeaveCriticalSection(&slabMutex);
}

void* kmalloc(size_t size) {
	EnterCriticalSection(&slabMutex);
	printf("E ");
	char* str = NULL;
	int i = -1;
	if (size > 0 && size <= 32) i = 0;
	else if (size > 32 && size <= 64) i = 1;
	else if (size > 64 && size <= 128) i = 2;
	else if (size > 128 && size <= 256) i = 3;
	else if (size > 256 && size <= 512) i = 4;
	else if (size > 512 && size <= 1024) i = 5;
	else if (size > 1024 && size <= 2048) i = 6;
	else if (size > 2048 && size <= 4096) i = 7;
	else if (size > 4196 && size <= 8192) i = 8;
	else if (size > 8192 && size <= 16384) i = 9;
	else if (size > 16384 && size <= 32768) i = 10;
	else if (size > 32768 && size <= 65536) i = 11;
	else if (size > 65536 && size <= 131072) i = 12;
	else {
		printf("That size can not be allocated with small memory buffers!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	str = sizeN_list + i * sizeof(cache_sizes_t);

	if (((cache_sizes_t*)str)->cs_cachep == NULL) {
		((cache_sizes_t*)str)->cs_cachep = kmem_cache_create(NULL, size, NULL, NULL);
	}
	void* ret = kmem_cache_alloc(((cache_sizes_t*)str)->cs_cachep);
	printf("L\n");
	LeaveCriticalSection(&slabMutex);
	return ret;
}

void kfree(const void* objp) {
	EnterCriticalSection(&slabMutex);

	kmem_cache_t* cache = cache_list;
	char* buffer = sizeN_list;
	if (cache == NULL || buffer == NULL || objp == NULL) {
		printf("Wrong parameters!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	while (cache != NULL) {
		for (int i = 0; i < 13; i++) {
			buffer = sizeN_list + i * sizeof(cache_sizes_t);
			if (((cache_sizes_t*)buffer)->cs_cachep == cache) {
				char* objFreeF = ((cache_sizes_t*)buffer)->cs_cachep->slabs_full;
				while (objFreeF != 0) {
					for (int i = 0; i < ((cache_sizes_t*)buffer)->cs_cachep->num; i++) {
						objFreeF = ((slab_t*)(((cache_sizes_t*)buffer)->cs_cachep->slabs_full))->s_mem + ((slab_t*)(((cache_sizes_t*)buffer)->cs_cachep->slabs_full))->colouroff + i * ((cache_sizes_t*)buffer)->cs_cachep->objsize;
						if (objFreeF = objp) {
							kmem_cache_free(((cache_sizes_t*)buffer)->cs_cachep, objp);
							LeaveCriticalSection(&slabMutex);
							return;
						}
					}
					objFreeF = ((slab_t*)(((cache_sizes_t*)buffer)->cs_cachep->slabs_full))->next;
				}
				objFreeF = ((cache_sizes_t*)buffer)->cs_cachep->slabs_partial;
				while (objFreeF != 0) {
					for (int i = 0; i < ((cache_sizes_t*)buffer)->cs_cachep->num; i++) {
						objFreeF = ((slab_t*)(((cache_sizes_t*)buffer)->cs_cachep->slabs_partial))->s_mem + ((slab_t*)(((cache_sizes_t*)buffer)->cs_cachep->slabs_partial))->colouroff + i * ((cache_sizes_t*)buffer)->cs_cachep->objsize;
						if (objFreeF = objp) {
							kmem_cache_free(((cache_sizes_t*)buffer)->cs_cachep, objp);
							LeaveCriticalSection(&slabMutex);
							return;
						}
					}
					objFreeF = ((slab_t*)(((cache_sizes_t*)buffer)->cs_cachep->slabs_partial))->next;
				}
			}
		}
		cache = cache->next;
	}
	LeaveCriticalSection(&slabMutex);
}

int kmem_cache_shrink(kmem_cache_t* cachep) {
	EnterCriticalSection(&slabMutex);

	if (cachep == NULL) {
		printf("Can't shrink cache that does not exist!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	char* temp = cache_list;
	int gotIt = 0;

	if (cachep->name == NULL) {
		while (((kmem_cache_t*)temp) != NULL) {
			if ((((kmem_cache_t*)temp)->objsize == cachep->objsize) && (((kmem_cache_t*)temp)->name == NULL)) { gotIt = 1; break; }
			temp = ((kmem_cache_t*)temp)->next;
		}
	}
	else {
		while (((kmem_cache_t*)temp) != NULL) {
			if (((kmem_cache_t*)temp)->name == cachep->name && ((kmem_cache_t*)temp)->objsize == cachep->objsize) { gotIt = 1; break; }
			temp = ((kmem_cache_t*)temp)->next;
		}
	}

	if (gotIt == 0) {
		printf("Can't shrink cache that is not initialized!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	if (((kmem_cache_t*)temp)->slabs_free == NULL) return 0;
	else if (((kmem_cache_t*)temp)->expand == 0 || ((kmem_cache_t*)temp)->expand == 1) {
		if (((kmem_cache_t*)temp)->expand == 0) ((kmem_cache_t*)temp)->expand = 1;
		char* help = ((kmem_cache_t*)temp)->slabs_free;
		char* deObj = NULL;
		int ret = 0;
		while (help != NULL) {
			for (int i = 0; i < ((kmem_cache_t*)temp)->num; i++) {
				deObj = ((slab_t*)help)->s_mem + ((slab_t*)help)->colouroff + ((kmem_cache_t*)temp)->objsize * i;
				if (((kmem_cache_t*)temp)->Dtor != NULL) ((kmem_cache_t*)temp)->Dtor(deObj);
			}
			buddy_dealloc(spaceS, blockNum, ((slab_t*)help)->s_mem);
			deObj = help;
			help = ((slab_t*)help)->next;
			buddy_dealloc(spaceS, blockNum, deObj);
			ret += SLAB_SIZE / BLOCK_SIZE;
		}
		((kmem_cache_t*)temp)->slabs_free = NULL;
		printf("Obrisana free lista!\n");
		LeaveCriticalSection(&slabMutex);
		return ret;
	}
	LeaveCriticalSection(&slabMutex);
	return 0;
}

void kmem_cache_destroy(kmem_cache_t* cachep) {
	EnterCriticalSection(&slabMutex);

	kmem_cache_t* cache = cache_list;
	kmem_cache_t* prevCache = NULL;
	if (cachep == NULL) {
		printf("Wrong parameters for destroying cache!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}
	int gotIt = 0;
	while (cache != NULL) {
		if (cache == cachep) {
			gotIt = 1;
			break;
		}
		prevCache = cache;
		cache = cache->next;
	}
	if (gotIt == 0) {
		printf("No such cache to destroy!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	slab_t* free = cache->slabs_free;
	slab_t* prev = NULL;
	while (free != NULL) {
		buddy_dealloc(spaceS, blockNum, free->s_mem);
		prev = free;
		free = free->next;
		buddy_dealloc(spaceS, blockNum, prev);
	}
	cache->slabs_free = NULL;

	free = cache->slabs_partial;
	prev = NULL;
	while (free != NULL) {
		buddy_dealloc(spaceS, blockNum, free->s_mem);
		prev = free;
		free = free->next;
		buddy_dealloc(spaceS, blockNum, prev);
	}
	cache->slabs_partial = NULL;

	free = cache->slabs_full;
	prev = NULL;
	while (free != NULL) {
		buddy_dealloc(spaceS, blockNum, free->s_mem);
		prev = free;
		free = free->next;
		buddy_dealloc(spaceS, blockNum, prev);
	}
	cache->slabs_full = NULL;

	if (cache->next != NULL && prevCache != NULL) prevCache->next = cache->next;
	else if(cache->next == NULL && prevCache != NULL) prevCache->next = NULL;
	if (cache_list == cache && cache->next == NULL) cache_list = NULL;
	else if (cache_list == cache && cache->next != NULL) cache_list = cache->next;
	cache->next = NULL;
	//buddy_dealloc(spaceS, blockNum, cache);
	printf("Cache on %d deallocated\n", cache);
	cache = NULL;
	LeaveCriticalSection(&slabMutex);
}

void kmem_cache_info(kmem_cache_t* cachep) {
	EnterCriticalSection(&slabMutex);

	if (cachep == NULL) {
		printf("Wrong parameters for cache info!\n");
		LeaveCriticalSection(&slabMutex);
		exit(1);
	}

	printf("\nCACHE INFORMATION: \n\tCache name is \"%s\"\n", cachep->name);
	printf("\tSize of one object in cache is %d\n", cachep->objsize);
	int slabs = 0;
	int inUse = 0;
	if (cachep->slabs_full != NULL) {
		slab_t* help = cachep->slabs_full;
		while (help != NULL) {
			slabs++;
			help = help->next;
		}
	}
	inUse += slabs * cachep->num;
	if (cachep->slabs_partial != NULL) {
		slab_t* help = cachep->slabs_partial;
		while (help != NULL) {
			slabs++;
			inUse += help->inuse;
			help = help->next;
		}
	}
	if (cachep->slabs_free != NULL) {
		slab_t* help = cachep->slabs_free;
		while (help != NULL) {
			slabs++;
			help = help->next;
		}
	}
	int ret = 0;
	if (slabs != 0) {
		ret = SLAB_SIZE / BLOCK_SIZE;
		ret *= slabs;
	}
	printf("\tSize of cache in blocks is %d blocks\n", ret);
	printf("\tNumber of slabs in cache is %d\n", slabs);
	printf("\tNumber of objects in one slab is %d\n", cachep->num);
	printf("\tProcent of used cache is %lf \n\n", ((double)inUse / (cachep->num * slabs))*100);
	LeaveCriticalSection(&slabMutex);
}

int kmem_cache_error(kmem_cache_t* cachep) {
	EnterCriticalSection(&slabMutex);

	if (cachep == NULL) { LeaveCriticalSection(&slabMutex); return 0; }

	kmem_cache_t* tmp = cache_list;
	int gotIt = 0;
	while (tmp != 0) {
		if (tmp == cachep)
			gotIt = 1;
		tmp = tmp->next;
	}
	if (gotIt == 0) { LeaveCriticalSection(&slabMutex); return 0; }
	else { LeaveCriticalSection(&slabMutex); return 1; }
}