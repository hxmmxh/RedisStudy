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
    size_t t = sizeof(unsigned long);
    unsigned long mask = 0xf;
    unsigned long i = 0;
    do
    {
        i |= ~mask;
        i = rev(i);
        i++;
        i = rev(i);
        cout << i << ' ';
    } while (i != 0);
    cout << endl;
    i = 1;
    do
    {
        i |= ~mask;
        i = rev(i);
        i++;
        i = rev(i);
        cout << i << ' ';
    } while (i != 0);
    cout << endl;
    i = 1;
    mask = 0x3f;
    do
    {
        i |= ~mask;
        i = rev(i);
        i++;
        i = rev(i);
        cout << i << ' ';
    } while (i != 0);
    /*
    i |= ~mask;
    printf("%lx ", i);
    i = rev(i);
    printf("%lx ", i);
    i++;
    printf("%lx ", i);
    i = rev(i);
    printf("%lx", i);
    */
}