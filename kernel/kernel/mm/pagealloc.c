/*
* Copyright (c) 2017 Pedro Falcato
* This file is part of Onyx, and is released under the terms of the MIT License
* check LICENSE at the root directory for more information
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>

#include <onyx/spinlock.h>
#include <onyx/page.h>
#include <onyx/vm.h>
#include <onyx/panic.h>
#include <onyx/copy.h>
#include <onyx/utils.h>

size_t page_memory_size;
size_t nr_global_pages;
atomic_size_t used_pages = 0;

static inline unsigned long pow2(int exp)
{
	return (1UL << (unsigned long) exp);
}

struct page_list 
{
	struct page *page;
	struct list_head list_node;
};

struct page_cpu
{
	struct page_arena *arenas;
	struct page_arena *first_to_search;
	struct page_cpu *next;
};

struct page_arena
{
	unsigned long free_pages;
	unsigned long nr_pages;
	void *start_arena;
	void *end_arena;
	struct list_head page_list;
	struct spinlock lock;
	struct page_arena *next;
};

static bool page_is_initialized = false;

struct page_cpu main_cpu = {0};

#define for_every_arena(cpu, starting_arena)	for(struct page_arena *arena = starting_arena; arena; \
	arena = arena->next)


#include <onyx/clock.h>

#define ADDRESS_4GB_MARK		0x100000000

struct page *page_alloc_one(unsigned long flags, struct page_arena *arena)
{
#if 0
	hrtime_t t0 = 0;
	hrtime_t t1 = 0;
	if(flags & 1 << 12)
	{
		struct clocksource *c = get_main_clock();
	 	t0 = c->get_ns();
	}
#endif
	
	struct page_list *p = container_of(list_first_element(&arena->page_list), struct page_list, list_node);
	
	list_remove(&p->list_node);

	page_ref(p->page);

	arena->free_pages--;

	spin_unlock(&arena->lock);
#if 0
	if(flags & 1 << 12)
	{
		struct clocksource *c = get_main_clock();
	 	t1 = c->get_ns();
		printk("Took %lu ns\n", t1 - t0);
	}
#endif

	return p->page;
}

struct page *page_alloc_from_arena(size_t nr_pages, unsigned long flags, struct page_arena *arena)
{
	spin_lock(&arena->lock);

	size_t found_pages = 0;
	uintptr_t base = 0;
	struct page_list *base_pg = NULL;
	bool found_base = false;

	if(arena->free_pages < nr_pages)
	{
		spin_unlock(&arena->lock);
		return NULL;
	}

	if(likely(nr_pages == 1))
		return page_alloc_one(flags, arena);

	/* Look for contiguous pages */
	list_for_every(&arena->page_list)
	{
		struct page_list *p = container_of(l, struct page_list, list_node);
		struct page_list *next = container_of(l->next, struct page_list, list_node);

		if(nr_pages != 1 && (uintptr_t) next - (uintptr_t) p > PAGE_SIZE)
		{
			found_pages = 0;
			found_base = false;
			continue;
		}
		else if(flags & PAGE_ALLOC_4GB_LIMIT && (uintptr_t) p > ADDRESS_4GB_MARK)
		{
			spin_unlock(&arena->lock);
			return NULL;
		}
		else
		{
			if(found_base == false)
			{
				base = (uintptr_t) p;
				found_base = true;
			}
			++found_pages;
		}

		if(found_pages == nr_pages)
			break;
	}

	/* If we haven't found nr_pages contiguous pages, continue the search */
	if(found_pages != nr_pages)
	{
		spin_unlock(&arena->lock);
		return NULL;
	}
	else
	{
		base_pg = (struct page_list *) base;
		struct list_head *prev = base_pg->list_node.prev;
		struct list_head *next = &base_pg->list_node;

		arena->free_pages -= found_pages;

		for(size_t i = 0; i < found_pages; i++)
			next = next->next;

		list_remove_bulk(prev, next);

		spin_unlock(&arena->lock);

		struct page *plist = NULL;
		struct page_list *pl = base_pg;
	
		for(size_t i = 0; i < nr_pages; i++)
		{
#ifdef PAGEALLOC_DEBUG
			if(pl->page == NULL)
			{
				struct page *p = phys_to_page((uintptr_t) pl - PHYS_BASE);
				printk("pl(%p)->ref: %lu\n", p->paddr, p->ref);
				panic("fark\n");
			}

			assert(pl->page->ref == 0);
#endif
			page_ref(pl->page);

			if(!plist)
			{
				plist = pl->page;
			}
			else
			{
				plist->next_un.next_allocation = pl->page;
				plist = pl->page;
			}

			pl = container_of(pl->list_node.next, struct page_list, list_node);
		}

		return base_pg->page;
	}
}

struct page *page_alloc(size_t nr_pages, unsigned long flags)
{
	struct page *pages = NULL;
	struct page_arena *last_with_pages = NULL;

	for_every_arena(&main_cpu, main_cpu.first_to_search)
	{
		if(flags & PAGE_ALLOC_4GB_LIMIT &&
			(unsigned long) arena->start_arena > ADDRESS_4GB_MARK)
		{
			return NULL;
		}

		if(arena->free_pages && !last_with_pages)
			last_with_pages = arena;

		if(unlikely(arena->free_pages < nr_pages))
			continue;

		if((pages = page_alloc_from_arena(nr_pages, flags, arena)) != NULL)
		{
			used_pages += nr_pages;

			if(last_with_pages == arena && !arena->free_pages)
			{
				/* Handle the case where we took it's last free page */
				if(arena->next)
					last_with_pages = arena->next;
				/* If it's the last arena, there's no problem. */
			}

			main_cpu.first_to_search = last_with_pages;

			return pages;
		}
	}

	return NULL;
}

void page_free_pages(struct page_arena *arena, void *_addr, size_t nr_pages)
{
	spin_lock(&arena->lock);

	unsigned long addr = (unsigned long) _addr;

	for(size_t i = 0; i < nr_pages; i++)
	{
		struct page_list *l = PHYS_TO_VIRT(addr);
		l->page = phys_to_page(addr);

		list_add_tail(&l->list_node, &arena->page_list);
	}

	arena->free_pages += nr_pages;

	spin_unlock(&arena->lock);
}

void page_free(size_t nr_pages, void *addr)
{
	bool freed = false;
	for_every_arena(&main_cpu, main_cpu.arenas)
	{
		if((uintptr_t) arena->start_arena <= (uintptr_t) addr && 
			(uintptr_t) arena->end_arena > (uintptr_t) addr)
		{
			page_free_pages(arena, addr, nr_pages);
			used_pages -= nr_pages;
			freed = true;
			break;
		}
	}

	assert(freed != false);
}

bool page_is_used(void *__page, struct bootmodule *modules);

static int page_add(struct page_arena *arena, void *__page,
	struct bootmodule *modules)
{
	if(page_is_used(__page, modules))
		return -1;
	nr_global_pages++;

	struct page_list *page = PHYS_TO_VIRT(__page);

	page->page = page_add_page(__page);
	list_add_tail(&page->list_node, &arena->page_list);

	return 0;
}

static void append_arena(struct page_cpu *cpu, struct page_arena *arena)
{
	struct page_arena **a = &cpu->arenas;

	while(*a)
		a = &(*a)->next;
	*a = arena;
}

static void page_add_region(uintptr_t base, size_t size, struct bootmodule *module)
{
	while(size)
	{
		size_t area_size = min(size, 0x200000);
		struct page_arena *arena = __ksbrk(sizeof(struct page_arena));
		assert(arena != NULL);
		memset_s(arena, 0, sizeof(struct page_arena));

		arena->free_pages = arena->nr_pages = area_size >> PAGE_SHIFT;
		arena->start_arena = (void*) base;
		arena->end_arena = (void*) (base + area_size);
		INIT_LIST_HEAD(&arena->page_list);

		for(size_t i = 0; i < area_size; i += PAGE_SIZE)
		{
			/* If the page is being used, decrement the free_pages counter */
			if(page_add(arena, (void*) (base + i), module) < 0)
				arena->free_pages--;
		}

		append_arena(&main_cpu, arena);

		size -= area_size;
		base += area_size;
	}

	if(!main_cpu.first_to_search)
		main_cpu.first_to_search = main_cpu.arenas;
}

void page_init(size_t memory_size, unsigned long maxpfn, void *(*get_phys_mem_region)(uintptr_t *base,
	uintptr_t *size, void *context), struct bootmodule *modules)
{
	uintptr_t region_base;
	uintptr_t region_size;
	void *context_cookie = NULL;

	printf("page: Memory size: %lu\n", memory_size);
	page_memory_size = memory_size;
	//nr_global_pages = vm_align_size_to_pages(memory_size);

	size_t nr_arenas = page_memory_size / 0x200000;
	if(page_memory_size % 0x200000)
		nr_arenas++;

	size_t needed_memory = nr_arenas *
		sizeof(struct page_arena) + 
		maxpfn * sizeof(struct page);
	void *ptr = alloc_boot_page(vm_align_size_to_pages(needed_memory), 0);
	if(!ptr)
	{
		halt();
	}

	__kbrk(PHYS_TO_VIRT(ptr), (void *)((unsigned long) PHYS_TO_VIRT(ptr) + needed_memory));
	page_allocate_pagemap(maxpfn);

	/* The context cookie is supposed to be used as a way for the
	 * get_phys_mem_region implementation to keep track of where it's at,
	 * without needing ugly global variables.
	*/

	/* Loop this call until the context cookie is NULL
	* (we must have reached the end)
	*/

	while((context_cookie = get_phys_mem_region(&region_base,
		&region_size, context_cookie)) != NULL)
	{
		/* page_add_region can't return an error value since it halts
		 * on failure
		*/
		page_add_region(region_base, region_size, modules);
	}

	page_is_initialized = true;
}

#include <onyx/pagecache.h>
#include <onyx/heap.h>

void page_get_stats(struct memstat *m)
{
	m->total_pages = nr_global_pages;
	m->allocated_pages = used_pages;
	m->page_cache_pages = pagecache_get_used_pages();
	m->kernel_heap_pages = heap_get_used_pages();
}

extern unsigned char kernel_end;

void *kernel_break = &kernel_end;
static void *kernel_break_limit = NULL;

__attribute__((malloc))
void *__ksbrk(long inc)
{
	void *ret = kernel_break;
	kernel_break = (char*) kernel_break + inc;

	assert((unsigned long) kernel_break <= (unsigned long) kernel_break_limit);
	return ret;
}

void __kbrk(void *break_, void *kbrk_limit)
{
	kernel_break = break_;
	kernel_break_limit = kbrk_limit;
}

void free_pages(struct page *pages)
{
	assert(pages != NULL);
	struct page *next = NULL;

	for(struct page *p = pages; p != NULL; p = next)
	{
		next = p->next_un.next_allocation;
		free_page(p);
	}
}

void free_page(struct page *p)
{
	assert(p != NULL);
	assert(p->ref != 0);

	if(page_unref(p) == 0)
	{
		p->next_un.next_allocation = NULL;
		page_free(1, page_to_phys(p));
	}
}

inline struct page *alloc_pages_nozero(size_t nr_pgs, unsigned long flags)
{
	return page_alloc(nr_pgs, flags);
}

struct page *__get_phys_pages(size_t nr_pgs, unsigned long flags)
{
	struct page *plist = NULL;
	struct page *ptail = NULL;
	off_t off = 0;

	for(size_t i = 0; i < nr_pgs; i++, off += PAGE_SIZE)
	{
		struct page *p = alloc_pages_nozero(1, flags);

		if(!p)
		{
			if(plist)
				free_pages(plist);

			return NULL;
		}

		if(page_should_zero(flags))
		{
			set_non_temporal(PAGE_TO_VIRT(p), 0, PAGE_SIZE);
		}

		if(!plist)
		{
			plist = ptail = p;
		}
		else
		{
			ptail->next_un.next_allocation = p;
			ptail = p;
		}
	}

	return plist;
}

struct page *do_alloc_pages_contiguous(size_t nr_pgs, unsigned long flags)
{
	struct page *p = alloc_pages_nozero(nr_pgs, flags);
	if(!p)
		return NULL;
	
	if(page_should_zero(flags))
	{
		set_non_temporal(PAGE_TO_VIRT(p), 0, nr_pgs << PAGE_SHIFT);
	}

	return p;
}

struct page *alloc_pages(size_t nr_pgs, unsigned long flags)
{
	//printf("alloc pages %lu %p\n", nr_pgs, __builtin_return_address(1));
	if(unlikely(flags & PAGE_ALLOC_CONTIGUOUS))
		return do_alloc_pages_contiguous(nr_pgs, flags);
	else
		return __get_phys_pages(nr_pgs, flags);
}

void __reclaim_page(struct page *new_page)
{
	__sync_add_and_fetch(&used_pages, 1);
	__sync_add_and_fetch(&nr_global_pages, 1);

	/* We need to set new_page->ref to 1 as free_page will decrement the ref as to
	 * free it
	*/
	new_page->ref = 1;
	free_page(new_page);
}