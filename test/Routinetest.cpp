#include <iostream>
#include <stdint.h>

//#define LITTLE_ENDIAN 1234
//#define BYTE_ORDER LITTLE_ENDIAN

using namespace std;

struct A
{
    int v;
};

struct B
{
    A val;
};

void f(B &b)
{
    A a;
    a.v = 1;
    b.val = a;
}

int main()
{
    //if (BYTE_ORDER == LITTLE_ENDIAN)
       // cout << 2;
}