
#ifndef __STY__MEMORY__H__
#define __STY__MEMORY__H__
#include "sty_types.h"

#define STY_ALLOC_FAILED_RETRY          5
#define STY_ALLOC_OOM                   -1

#define MAX_BYTES                       128
#define DEFAULT_REFILL_BLOCKS           20
#define ALIGN                           8
#define FREELISTS                       ((MAX_BYTES) / (ALIGN))

/**
 * @brief   此联合体用于表示自由链表中的一个内存区块，代表一块固定大小的可用内存空间。
 * @author  zhangkeyang
 */
union sty_memblk 
{
    //这两个成员中，某一时刻只有其中一个成员是合法有效的。这样做是为了节省一个指针的内存。
    //(1) 如果此区块已经被分配给用户，则client_data是有效的，它指向实际可用的内存区块。
    //(2) 如果此区块正在自由链表当中，则next是有效的，表示其临接内存区块。
    unsigned char       client_data[1];   ///< 客户区可见的合法内存块起始地址, 写成数组是为了让成员成为常量。
    union sty_memblk    *next;            ///< 自由链表可见的临接内存块起始地址。
};

/**
 * @brief   内存池结构。
 * @author  zhangkeyang
 */
struct sty_mempool 
{
    int                 total_used;                 ///< 当前内存池总共分配的堆内存大小(字节)。
    int                 available;                  ///< 当前内存池中可用的堆内存大小(字节)。
    unsigned char       *pool_start;                ///< 当前内存池的起始地址。
    unsigned char       *pool_end;                  ///< 当前内存池的结束地址。
    union sty_memblk    *free_lists[FREELISTS];     ///< 当前内存池中维护的自由链表。
} sty_global_mempool;

/**
 * @brief 此函数是对malloc的简单封装，当内存分配失败时直接结束进程。
 * @param bytes     分配的字节数。
 * @return void*    分配到内存块的首地址。 
 */
STY_API void * STY_CDCEL STY_IMPORT
sty_alloc(int bytes);

/**
 * C99规定的malloc函数用来分配堆内存。实际使用中，这个函数存在一些问题，比如：
 * 1. malloc往往会额外分配一些字节来保存堆空间大小。虽然在x86_64操作系统中内存碎片不是问题，
 *    但是当频繁地在堆中分配少额的内存时仍然可能产生较大的浪费比。
 * 2. malloc可能会返回空指针(虽然在某些平台上不会返回空指针，但是这仍不可信)。所以调用者往往
 *    需要针对此情况给出额外的逻辑判断。
 * sty_mempool_alloc正是为此而设计；事实上，其内部维护了一个内存池，为减少内存浪费尽了最大
 * 努力；令一方面，sty_alloc保证不会返回空指针。就是说，它要么返回一段足够大且可用的连续内存
 * 空间，要么在多次尝试却失败后调用exit(STY_ALLOC_OOM)函数杀死当前进程，尝试的次数可以通过
 * 修改预定义宏STY_ALLOC_FAILED_RETRY来调试。
 * 
 * @note            sty_alloc是线程安全的，但并不是可重入的。
 * @note            sty_alloc获得的内存务必由sty_free释放。
 * @see             sty_mempool_free
 * @brief           此函数在堆上分配一大块连续的堆内存空间，并保证其大小足以容纳一定字节数。
 * @author          zhangkeyang
 * @param bytes     指定大小的字节数。
 * @return void*    连续内存空间的起始地址，其大小至少可以容纳bytes个字节。此函数不会返回NULL。
 */
STY_API void * STY_CDCEL STY_IMPORT
sty_mempool_alloc(struct sty_mempool *mempool, int bytes);

/**
 * @brief 此函数是对free的简单封装，目的是保持API命名的一致性，方便拓展。
 * @param bytes     分配的字节数。
 * @return void*    分配到内存块的首地址。 
 */
STY_API inline void * STY_CDCEL STY_IMPORT
sty_free(void *ptr);

/**
 * 此函数释放由sty_mempool_alloc分配得到的堆内存。若这块内存中包含了其他指针，则不保证其他指针指向指
 * 向的内存能够被正确地释放；和libc一样，这需要调用者来保证。
 * @author          bjut-zky
 * @brief           此函数释放一块堆内存，但不保证堆内存其中的内容被正确地释放。
 * @see             sty_alloc
 * @param ptr       由函数sty_alloc得到的一块堆内存。
 * @param size      这块堆内存的大小(字节)。
 */
STY_API void STY_CDCEL STY_IMPORT
sty_mempool_free(struct sty_mempool *mempool, void *ptr, int size);

#endif
