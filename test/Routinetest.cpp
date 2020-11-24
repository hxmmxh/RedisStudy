#include <iostream>
#include <stdint.h>

using namespace std;

void print(void *p)
{
    cout << p << endl;
}

int main()
{
    char *a = "hello",*b="hello",*c="hi";
    void *p = a;
    print(p);
    print(a);
    print(b);
    print(c);
}