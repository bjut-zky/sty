
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "sty_types.h"

#define ALIGN       8
#define MAX_BYTES   128
#define FREELISTS   (MAX_BYTES / ALIGN)

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
    int             total_used;     ///< 当前内存池总共分配的堆内存大小(字节)。
    int             available;      ///< 当前内存池中可用的堆内存大小(字节)。
    unsigned char   *pool_start;    ///< 当前内存池的起始地址。
    unsigned char   *pool_end;      ///< 当前内存池的结束地址。
} sty_global_mempool;

/**
 * @brief       将指定字节数调整到ALIGN的整数倍。
 * @param bytes 指定字节数。
 * @return int  指定字节数对齐到ALIGN倍数的结果。
 * @author      zhangkeyang
 */
static inline int STY_CDCEL
sty_mempool_bytes_round_up(int bytes) { return (bytes + ALIGN - 1) & ~(ALIGN - 1); }

/**
 * @brief       返回指定字节数的内存区块属于哪条自由链表。
 * @param bytes 指定字节数。
 * @return int  指定字节数的大小属于哪条自由链表管理。
 * @author      zhangkeyang
 */
static inline int STY_CDCEL
sty_mempool_freelist_index(int bytes) { return (bytes + ALIGN - 1) / ALIGN - 1; }

/**
 * @brief   此函数尝试从操作系统中分配一大块内存以填充内存池，并至少返回一个可用区块。
 * @note    如果对于操作系统而言nblocks个size字节的内存区块无法满足，则会适当降低nblocks的值，但至少会返回1个区块。
 * @author  zhangkeyang
 * @param   mempool             内存池指针。
 * @param   size                内存区块大小。
 * @param   nblocks             期望能够分配的区块数量。
 * @return  unsigned char*      内存区块的起始地址，其实际区块数量由nblocks指向的整数指定。
 */
static inline unsigned char * STY_CDCEL
sty_mempool_chunk_alloc_and_fill(struct sty_mempool *mempool, int size, int *nblocks) 
{
    unsigned char * result      = NULL; //保存返回值。 
    int             total_size  = 0;    //期望能够从系统中分配得到的字节数。
    int             bytes_left  = 0;    //当前内存池中的剩余字节数。
    
    //nblocks不能指向NULL。
    assert(nblocks != NULL);
    
    //初始化局部变量。
    total_size = size * (*nblocks);                         //初始状态下希望从系统中分配的字节数。这个值可能会被适当降低。
    bytes_left = mempool->pool_end - mempool->pool_start;   //当前内存池中的剩余可用字节数。

    //比较total_size和bytes_left两个值，根据其大小关系进行不同处理；
    if(bytes_left >= total_size) 
    {
        //如果流程走到这里，内存池中的剩余容量可以满足本次分配需要。
        //此时直接配置内存池并返回这块内存空间即可。
        result = mempool->pool_start;
        mempool->pool_start += total_size;
        return result;
    }
    else if(bytes_left < total_size >= size)
    {
        //如果流程走到这里，内存池中的剩余容量不能满足本次分配需要，但是至少包含了一个内存块大小的剩余空间。
        //此时适当调整nblocks的值，并返回这块空间。之所以此时不分配内存，是为了避免浪费，毕竟随后自由链表
        //可能会得到极大补充，或者是再也没有内存分配请求。
        *nblocks    = bytes_left / size;
        total_size  = size * (*nblocks);
        result      = mempool->pool_start;
        mempool->pool_start += total_size;
        return result;
    }
    else 
    {
        //如果流程走到这里，内存池中的剩余容量已经足够少，甚至连一个区块都分配不出来。此时存在以下情况：
        //(1)内存池中可能存在一些剩余字节，但是太少了，不足以提供一个内存区块。
        //(2)自由链表中尚有一些较大的区块；如果将这些区块拉回到内存池中，可能可以满足本次需求。
        //为了不进行无谓的内存分配工作，我们需要先尝试做以下工作来尽最大努力：
        //针对情况(1)，我们需要将此时内存池中的内存放到自由链表中，
    }
}   
