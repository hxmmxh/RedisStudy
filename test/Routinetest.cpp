#include <iostream>
#include <stdint.h>
#include <stdio.h>

using namespace std;

static unsigned long rev(unsigned long v)
{
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0)
    {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

int main()
{
    uint16_t i = UINT16_MAX;
    i+=2;
    cout << i;
}