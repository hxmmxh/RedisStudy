#include <assert.h>




#define redisAssertWithInfo(_c, _o, _e) ((_e) ? (void)0 : (_redisAssertWithInfo(_c, _o, #_e, __FILE__, __LINE__), _exit(1)))
#define redisAssert(_e) ((_e) ? (void)0 : (_redisAssert(#_e, __FILE__, __LINE__), _exit(1)))
#define redisPanic(_e) _redisPanic(#_e, __FILE__, __LINE__), _exit(1)