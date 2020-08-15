
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "sty_memory.h"

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
 * @brief           将一个内存区块放入内存池维护的某个自由链表中。
 * @param mempool   内存池指针。
 * @param index     内存池中自由链表的编号。
 * @param block     要添加到内存池中的内存区块指针。
 */
static inline void STY_CDCEL
sty_mempool_freelist_addblock(struct sty_mempool *mempool, int index, union sty_memblk *block)
{
    assert(mempool != NULL);
    assert(block != NULL);
    assert(index >= 0 && index < FREELISTS);

    block->next = mempool->free_lists[index];
    mempool->free_lists[index] = block;
}

/**
 * @brief           从内存池维护的某个自由链表中弹出一块内存空间。
 * @param mempool   内存池指针。
 * @param index     内存池中自由链表的编号。
 * @return          返回自由链表中的一块内存。
 */
static inline unsigned char * STY_CDCEL
sty_mempool_freelist_popblock(struct sty_mempool *mempool, int index)
{
    assert(mempool != NULL);
    assert(index >= 0 && index < FREELISTS);

    union sty_memblk *block = mempool->free_lists[index];
    if(block != NULL) 
        mempool->free_lists[index] = block->next;
    return block->client_data;
}

/**
 * @brief 此函数用于填充内存池中的某条自由链表。
 * @note  只有当某条自由链表中没有可用的区块时，此方法才可以被调用。
 * @param mempool   内存池指针。
 * @param size      内存区块的大小。
 * @param blocks    连续内存区块(一大块连续内存)的首地址。
 * @param nblocks   内存区块的数量。
 */
static inline void STY_CDCEL
sty_mempool_freelist_build(struct sty_mempool *mempool, int size, unsigned char *blocks, int nblocks)
{
    assert(mempool != NULL);
    assert(size % ALIGN == 0);
    assert(nblocks > 0);

    union sty_memblk *currentblk         = NULL;    //指向当前要链接的内存区块。
    union sty_memblk *nextblk            = NULL;    //指向下一个要链接的自由区块。

    int selected = sty_mempool_freelist_index(size);
    nextblk = (union sty_memblk *)(blocks);         //当前要处理的区块。
    mempool->free_lists[selected] = nextblk;        //设置当前自由链表的头结点(为第2个可用区块)。

    //【构建自由链表】以下将第2个区块和第3个区块链接、第3个区块和第4个区块链接。。。直到全部链接完毕。
    for(int i = 0; ;i++) 
    {
        currentblk = nextblk;
        nextblk    = (union sty_memblk *)((unsigned char *)nextblk + size);
        if(nblocks - 1 == i)            
        {
            //已经抵达了最后一个可用区块的首地址。作为单链表的结束，让其链接指针指向空，并退出循环。
            currentblk->next = NULL;
            break;
        }
        else 
        {
            //在这个区块后面仍然存在可用的区块，让当前区块的链接指针指向下一个区块的首地址。
            currentblk->next = nextblk;
        }
    }
}

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
    assert(mempool != NULL);
    assert(nblocks != NULL);
    assert(size % ALIGN == 0);
    
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
    else if(bytes_left >= size)
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
        //针对情况(1)，我们需要将此时内存池中的内存放到自由链表中；问题是，此时剩余区块的大小一定是ALIGN
        //的倍数吗？比如说，此时内存池中只剩下1个字节了，那么这1个字节的内存要被放到8字节的区块中吗？要解决
        //这个问题，我们必须保证剩余空间是ALIGN的倍数。因此，只要能保证每次向内存池中填充的字节数是ALIGN的
        //倍数，并且每次也从内存池中取出ALIGN的倍数的字节的话，就没有这个问题了。因此这里可以放心的写。
        //针对情况(2)，在绝大多数情况下，我们其实无需考虑；但是当系统中的实际可用内存实在没有可用内存时，这
        //部分的内存其实可以被利用起来：我们可以将这部分的内存块从自由链表中再调回内存池中，以满足本次内存分
        //配需要。某种程度上，这种操作也可以适当减少实际内存分配次数，达到节省少量内存的目的。
        if(bytes_left > 0) 
        {
            //流程走到这里，表示当前内存池中还有一些可用内存可以使用。先把这部分内存编入合适的自由链表中，
            int selected = sty_mempool_freelist_index(bytes_left); 
            sty_mempool_freelist_addblock(mempool, selected, (union sty_memblk *) mempool->pool_start);
        }

        //【尝试】从操作系统中申请一块内存空间。
        int bytes_to_alloc  = 2 * total_size + sty_mempool_bytes_round_up(mempool->total_used >> 4);
        mempool->pool_start = (unsigned char *) malloc(bytes_to_alloc);

        //下面检查内存分配工作是否成功完成，针对两种情况分别做出应对。
        if(mempool->pool_start == NULL)
        {
            //流程走到这里，表示此次堆内存申请失败。按照情况(1)的流程来处理。
            for(int i = size; i <= MAX_BYTES; i += ALIGN)
            {
                int selected = sty_mempool_freelist_index(i);
                union sty_memblk *p = (union sty_memblk *) sty_mempool_freelist_popblock(mempool, selected);
                if(p != NULL) 
                {
                    //流程走到这里，表示selected这条自由链表中的区块可以被放回到内存池中。
                    mempool->pool_start = p->client_data;
                    mempool->pool_end   = mempool->pool_start + i;

                    //此时分配了很多的内存，但是这会引发nblocks的值和返回值不对应。可以通过递归调用此函数的方式来修正这个值。
                    return sty_mempool_chunk_alloc_and_fill(mempool, size, nblocks);
                }
            }
            
            //流程走到这里，表示操作系统的内存资源已经山穷水尽了。我们直接结束进程。
            mempool->pool_end = NULL;
            exit(STY_ALLOC_OOM);
        }

        //配置堆内存。
        mempool->total_used += bytes_to_alloc;
        mempool->pool_end   =  mempool->pool_start + bytes_to_alloc;
        
        //此时分配了很多的内存，但是这会引发nblocks的值和返回值不对应。可以通过递归调用此函数的方式来修正这个值。
        return sty_mempool_chunk_alloc_and_fill(mempool, size, nblocks);
    }
}

/**
 * @brief 此函数至少返回一个固定大小的内存区块，并根据实际内存使用情况填充自由链表。
 * 
 * @param mempool           内存池指针。
 * @param size              内存区块大小。
 * @return unsigned char*   大小至少为size的内存区块的首地址。
 */
static inline unsigned char * STY_CDCEL
sty_mempool_refill(struct sty_mempool *mempool, int size) 
{
    assert(mempool != NULL);
    assert(size % ALIGN == 0);

    int             nblocks = DEFAULT_REFILL_BLOCKS; //实际分配得到的内存区块数量。
    unsigned char   *chunk  = sty_mempool_chunk_alloc_and_fill(mempool, size, &nblocks);

    //函数sty_mempool_chunk_alloc_and_fill可能会根据实际情况降低内存区块的分配数量。
    //根据实际得到的可用内存区块数量，做不同的处理；
    if(nblocks == 1) 
        return chunk;   //系统的内存紧张，仅得到了一个内存块。直接返回给调用者使用吧。
    
    //流程走到这里，表示至少分配了两个区块。那么多余的区块可以被编入到自由链表当中待用。
    sty_mempool_freelist_build(mempool, size, chunk + size, nblocks - 1);
    return chunk;
}

/**
 * C99规定的malloc函数用来分配堆内存。实际使用中，这个函数存在一些问题，比如：
 * 1. malloc往往会额外分配一些字节来保存堆空间大小。虽然在x86_64操作系统中内存碎片不是问题，
 *    但是当频繁地在堆中分配少额的内存时仍然可能产生较大的浪费比。
 * 2. malloc可能会返回空指针(虽然在某些平台上不会返回空指针，但是这仍不可信)。所以调用者往往
 *    需要针对此情况给出额外的逻辑判断。
 * sty_alloc正是为此而设计；事实上，其内部维护了一个内存池，为减少内存浪费尽了最大努力；令一
 * 方面，sty_alloc保证不会返回空指针。就是说，它要么返回一段足够大且可用的连续内存空间，要么
 * 在多次尝试却失败后调用exit(STY_ALLOC_OOM)函数杀死当前进程，尝试的次数可以通过修改预定义
 * 宏STY_ALLOC_FAILED_RETRY来调试。
 * 
 * @note            sty_alloc是线程安全的，但并不是可重入的。
 * @note            sty_alloc获得的内存务必由sty_free释放。
 * @see             sty_free
 * @brief           此函数在堆上分配一大块连续的堆内存空间，并保证其大小足以容纳一定字节数。
 * @author          bjut-zky
 * @param bytes     指定大小的字节数。
 * @return void*    连续内存空间的起始地址，其大小至少可以容纳bytes个字节。此函数不会返回NULL。
 */
void * STY_CDCEL STY_IMPORT
sty_mempool_alloc(struct sty_mempool *mempool, int bytes)
{
    assert(bytes >= 0);

    if(bytes == 0)
        bytes = 1;

    void *result = NULL;

    if(bytes > MAX_BYTES) 
    {
        result = malloc(bytes);
        if(result == NULL)
            exit(STY_ALLOC_OOM);
    }
    else 
    {
        int selected = sty_mempool_freelist_index(bytes);
        result = sty_mempool_freelist_popblock(&sty_global_mempool, selected);
        if(result == NULL)
        {
            int round_bytes = sty_mempool_bytes_round_up(bytes);
            result = sty_mempool_refill(&sty_global_mempool, round_bytes);
        }
    }
    return result;
}

/**
 * 此函数释放由sty_alloc分配得到的堆内存。若这块内存中包含了其他指针，则不保证其他指针指向指
 * 向的内存能够被正确地释放；和libc一样，这需要调用者来保证。
 * @author          bjut-zky
 * @brief           此函数释放一块堆内存，但不保证堆内存其中的内容被正确地释放。
 * @see             sty_alloc
 * @param ptr       由函数sty_alloc得到的一块堆内存。
 * @param size      这块堆内存的大小(字节)。
 */
void STY_CDCEL STY_IMPORT 
sty_mempool_free(struct sty_mempool *mempool, void *ptr, int size)
{
    assert(size >= 0);

    if(size > MAX_BYTES) 
    {
        free(ptr);
    }
    else
    {
        int selected = sty_mempool_freelist_index(size);
        sty_mempool_freelist_addblock(&sty_global_mempool, selected, (union sty_memblk *) ptr);
    }
}

/**
 * @brief 此函数是对malloc的简单封装，当内存分配失败时直接结束进程。
 * @param bytes     分配的字节数。
 * @return void*    分配到内存块的首地址。 
 */
STY_API void * STY_CDCEL STY_IMPORT
sty_alloc(int bytes)
{
    assert(bytes >= 0);

    void *ptr = malloc(bytes);
    if(ptr == NULL)
        exit(STY_ALLOC_OOM);
    return ptr;
}

/**
 * @brief 此函数是对free的简单封装，目的是保持API命名的一致性，方便拓展。
 * @param bytes     分配的字节数。
 * @return void*    分配到内存块的首地址。 
 */
STY_API inline void * STY_CDCEL STY_IMPORT
sty_free(void *ptr)
{
    free(ptr);
}
