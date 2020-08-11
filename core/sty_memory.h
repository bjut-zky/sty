
#ifndef __STY__MEMORY__H__
#define __STY__MEMORY__H__
#include "sty_types.h"
#ifdef  __cplusplus
extern "C" {
#endif

#define STY_ALLOC_FAILED_RETRY      5
#define STY_ALLOC_OOM               -1

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
STY_API void * STY_CDCEL STY_IMPORT
sty_alloc(int bytes);

/**
 * 此函数释放由sty_alloc分配得到的堆内存。若这块内存中包含了其他指针，则不保证其他指针指向指
 * 向的内存能够被正确地释放；和libc一样，这需要调用者来保证。
 * @author          bjut-zky
 * @brief           此函数释放一块堆内存，但不保证堆内存其中的内容被正确地释放。
 * @see             sty_alloc
 * @param ptr       由函数sty_alloc得到的一块堆内存。
 * @param size      这块堆内存的大小(字节)。
 */
STY_API void STY_CDCEL STY_IMPORT 
sty_free(void *ptr, int size);

#ifdef  __cplusplus
}
#endif
#endif
