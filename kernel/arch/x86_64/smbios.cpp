/*
 * Copyright (c) 2016 - 2021 Pedro Falcato
 * This file is part of Onyx, and is released under the terms of the MIT License
 * check LICENSE at the root directory for more information
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include <onyx/compiler.h>
#include <onyx/init.h>
#include <onyx/log.h>
#include <onyx/smbios.h>
#include <onyx/vm.h>

static struct smbios_table *tables = NULL;
static size_t nr_structs = 0;

static inline void *__find_phys_mem(void *lower_boundary, void *upper_boundary, int alignment,
                                    const char *s)
{
    for (size_t i = 0; i < ((uintptr_t) upper_boundary - (uintptr_t) lower_boundary) / alignment;
         i++)
    {
        if (!memcmp((void *) (((uintptr_t) lower_boundary + PHYS_BASE) + i * alignment), s,
                    strlen(s)))
        {
            return (void *) ((uintptr_t) lower_boundary + i * alignment);
        }
    }
    return NULL;
}

/* Finds the 32-bit entry point */
struct smbios_entrypoint32 *smbios_find_entry32()
{
    return (smbios_entrypoint32 *) __find_phys_mem((void *) 0xF0000, (void *) 0xFFFFF, 16, "_SM_");
}

/* Finds the 64-bit entrypoint */
struct smbios_entrypoint64 *smbios_find_entry64()
{
    return (smbios_entrypoint64 *) __find_phys_mem((void *) 0xF0000, (void *) 0xFFFFF, 16, "_SM3_");
}

extern bool efi64_present;

/* Finds the SMBIOS tables, independently of the entry point */
smbios_table *smbios_find_tables(void)
{
    /* These tables can't be found like that in non BIOS systems */
    if (efi64_present)
        return NULL;
    struct smbios_entrypoint32 *entry32 = smbios_find_entry32();
    struct smbios_entrypoint64 *entry64 = smbios_find_entry64();
    if (entry64)
    {
        LOG("smbios", "64-bit table: %p\n", entry64);

        entry64 = (struct smbios_entrypoint64 *) ((char *) entry64 + PHYS_BASE);

        return (smbios_table *) PHYS_TO_VIRT(entry64->addr);
    }

    if (entry32)
    {
        LOG("smbios", "32-bit table: %p\n", entry32);

        /* Find the address and the size of the tables */

        entry32 = (struct smbios_entrypoint32 *) ((char *) entry32 + PHYS_BASE);

        return (smbios_table *) PHYS_TO_VIRT(entry32->addr);
    }

    return NULL;
}

struct smbios_table *smbios_get_table(int type)
{
    if (!tables)
        return NULL;
    struct smbios_table *tab = tables;
    for (size_t i = 0; i < nr_structs; i++)
    {
        if (tab->type == type)
            return tab;
        char *a = (char *) tab + tab->len;
        uint16_t zero = 0;
        while (memcmp(a, &zero, 2))
        {
            a++;
        }
        a += 2;
        tab = (struct smbios_table *) a;
    }
    return NULL;
}

char *smbios_get_string(struct smbios_table *t, uint8_t strndx)
{
    char *strtab = ((char *) t + t->len);
    uint8_t i = 0;
    while (i != strndx - 1)
    {
        strtab += strlen(strtab) + 1;
        i++;
    }
    return strtab;
}

/* Initializes the smbios */
void smbios_init(void)
{
    LOG("smbios", "Initializing!\n");

    tables = smbios_find_tables();
    if (!tables)
        return;

    struct smbios_table_bios_info *info =
        (struct smbios_table_bios_info *) smbios_get_table(SMBIOS_TYPE_BIOS_INFO);

    if (info)
    {
        INFO("smbios", "BIOS Vendor: %s\n", smbios_get_string(&info->header, info->vendor));
        INFO("smbios", "BIOS Date: %s\n",
             smbios_get_string(&info->header, info->bios_release_date));
    }
}

INIT_LEVEL_CORE_AFTER_SCHED_ENTRY(smbios_init);
