#include "xmskiplist.h"
#include "xmdict.h"

// 删除所有分值在给定范围之内的节点。节点不仅会从跳跃表中删除，而且会从相应的字典中删除。返回值为被删除节点的数量
unsigned long zslDeleteRangeByScore(zskiplist *zsl, zrangespec *range, dict *dict);