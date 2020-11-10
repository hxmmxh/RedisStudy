#include <stdio.h>
#include "xmendianconv.h"

int main(void) {
    char buf[32];

    sprintf(buf,"ciaoroma");
    memrev16(buf);
    printf("%s\n", buf);//应该输出icaoroma

    sprintf(buf,"ciaoroma");
    memrev32(buf);
    printf("%s\n", buf);//应该输出oaicroma

    sprintf(buf,"ciaoroma");
    memrev64(buf);
    printf("%s\n", buf);//应该输出amoroaic

    return 0;
}