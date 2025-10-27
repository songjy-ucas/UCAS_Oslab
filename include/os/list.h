/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * Copyright (C) 2018 Institute of Computing
 * Technology, CAS Author : Han Shukai (email :
 * hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * Changelog: 2019-8 Reimplement queue.h.
 * Provide Linux-style doube-linked list instead of original
 * unextendable Queue implementation. Luming
 * Wang(wangluming@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * */

#ifndef INCLUDE_LIST_H_
#define INCLUDE_LIST_H_

#include <type.h>

// double-linked list
typedef struct list_node
{
    struct list_node *next, *prev;
} list_node_t;

typedef list_node_t list_head;

// LIST_HEAD is used to define the head of a list.
#define LIST_HEAD(name) struct list_node name = {&(name), &(name)}

/* TODO: [p2-task1] implement your own list API */
// Initializes a list head.
static inline void list_init(list_head *list)
{
    list->next = list;
    list->prev = list;
}

// Inserts a new node between two known consecutive nodes.
static inline void __list_add(struct list_node *new,
                              struct list_node *prev,
                              struct list_node *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

// Adds a new node at the head of the list.
static inline void list_add(struct list_node *new, list_head *head)
{
    __list_add(new, head, head->next);
}

// Adds a new node at the tail of the list.
static inline void list_add_tail(struct list_node *new, list_head *head)
{
    __list_add(new, head->prev, head);
}

// Deletes a node from the list.
static inline void __list_del(struct list_node *prev, struct list_node *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(struct list_node *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = entry;
    entry->prev = entry;
    // Optional: poison the pointers to catch use-after-free bugs
    // entry->next = entry->prev = NULL; 
}

// Checks if the list is empty.
static inline int list_is_empty(const list_head *head)
{
    return head->next == head;
}

// Gets the struct for this entry. 根据结构体中某个成员的地址，反向计算出这个结构体本身的起始地址。
// ptr: the &struct list_head pointer.
// type: the type of the struct this is embedded in.
// member: the name of the list_head within the struct.
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

#endif
