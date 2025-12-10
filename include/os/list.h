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
// 初始化链表头
static inline void list_init(list_head *head)
{
    head->next = head;
    head->prev = head;
}

// 内部函数：在两个节点之间插入一个新节点
static inline void __list_add(list_node_t *new, list_node_t *prev, list_node_t *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

// 在链表头之后插入新节点 (add to head)
static inline void list_add(list_node_t *new, list_head *head)
{
    __list_add(new, head, head->next);
}

// 在链表末尾插入新节点 (add to tail)
static inline void list_add_tail(list_node_t *new, list_head *head)
{
    __list_add(new, head->prev, head);
}

// 内部函数：删除两个节点之间的某个节点
static inline void __list_del(list_node_t * prev, list_node_t * next)
{
    next->prev = prev;
    prev->next = next;
}

// 从链表中删除一个节点
static inline void list_del(list_node_t *entry)
{
    __list_del(entry->prev, entry->next);
}

// 检查链表是否为空
static inline int list_empty(const list_head *head)
{
    return head->next == head;
}
#define offsetof(type, member) __builtin_offsetof(type, member)
// 获取包含该链表节点的结构体的指针
// ptr: 指向 list_node 成员的指针
// type: 包含 list_node 的结构体的类型 (例如 pcb_t)
// member: list_node 成员在结构体中的名字 (例如 list)
#define list_entry(ptr, type, member) \
    (type *)((char *)(ptr) - offsetof(type, member))

// #define list_for_each_safe(pos, n, head) \
//     for (pos = (head)->next, n = pos->next; pos != (head); \
//          pos = n, n = pos->next)

#endif
