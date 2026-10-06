// Global operator new/delete override -> mimalloc (drop-in allocator).
// Combined with third_party/mimalloc/src/alloc-override.c (which covers the
// C malloc family), every heap allocation in the binary routes to mimalloc.
#include <mimalloc-new-delete.h>
