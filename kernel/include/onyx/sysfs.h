/*
 * Copyright (c) 2017 - 2021 Pedro Falcato
 * This file is part of Onyx, and is released under the terms of the MIT License
 * check LICENSE at the root directory for more information
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _ONYX_SYSFS_H
#define _ONYX_SYSFS_H

#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <onyx/list.h>
#include <onyx/object.h>

/* Each sysfs entry is a sysfs property */
struct sysfs_object
{
    struct object obj;
    char *name;

    struct list_head dentry_node;

    struct spinlock dentry_lock;

    struct list_head dentries;

    struct sysfs_object *parent;

    ino_t inode;
    int type;
    mode_t perms;
    void *priv;
    ssize_t (*write)(void *buffer, size_t size, off_t off);
    ssize_t (*read)(void *buffer, size_t size, off_t off);
};

void sysfs_init(void);
int sysfs_object_init(const char *name, struct sysfs_object *obj);
void sysfs_add(struct sysfs_object *obj, struct sysfs_object *parent);
int sysfs_init_and_add(const char *name, struct sysfs_object *obj, struct sysfs_object *parent);
void sysfs_mount(void);

#endif
