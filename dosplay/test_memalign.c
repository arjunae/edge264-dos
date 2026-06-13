#include <malloc.h>
#include <stdlib.h>
int main() {
    void *p = memalign(64, 1024);
    free(p);
    return 0;
}
