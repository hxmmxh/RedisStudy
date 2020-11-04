#include <iostream>

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
    B b;
    f(b);
    cout << b.val.v;
}