/* C-only startup; no constructors are used in this firmware. */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
extern char _heap_start, _heap_end;
void *_sbrk(ptrdiff_t increment) {
    static uintptr_t current;
    if (!current) current = (uintptr_t)&_heap_start;
    if (increment < 0 || (uintptr_t)increment > (uintptr_t)&_heap_end - current) {
        errno = ENOMEM;
        return (void *)-1;
    }
    void *previous = (void *)current;
    current += (uintptr_t)increment;
    return previous;
}
void __libc_init_array(void) {}
void _init(void) {}
void _fini(void) {}
