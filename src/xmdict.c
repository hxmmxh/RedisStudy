
#include <stdlib.h>
#include <time.h>
#include <xmmalloc.h>
#include <xmdict.h>

/* 通过 dictEnableResize() 和 dictDisableResize() 两个函数，
 * 程序可以手动地允许或阻止哈希表进行 rehash ，
 * 这在 Redis 使用子进程进行保存操作时，可以有效地利用 copy-on-write 机制。
 * 需要注意的是，并非所有 rehash 都会被 dictDisableResize 阻止
 * 如果已使用节点的数量和字典大小之间的比率，
 * 大于字典强制 rehash 比率 dict_force_resize_ratio ，
 * 那么 rehash 仍然会（强制）进行。
 */
// 指示字典是否启用 rehash 的标识
static int dict_can_resize = 1;
// 强制 rehash 的比率
static unsigned int dict_force_resize_ratio = 5;

//开启自动rehash
void dictEnableResize(void)
{
    dict_can_resize = 1;
}

//关闭自动rehash
void dictDisableResize(void)
{
    dict_can_resize = 0;
}

dict *dictCreate(dictType *type, void *privDataPtr)
{
    dict *d;
    if ((d = xm_malloc(sizeof(*d))) == NULL)
        return NULL;
    _dictInit(d, type, privDataPtr);
    return d;
}

//初始化字典
static int _dictInit(dict *d, dictType *type, void *privDataPtr)
{
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;
    return DICT_OK;
}

//重置、初始化哈希表
static int _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
    return DICT_OK;
}

// 计算第一个大于等于 size 的 2 的 N 次方，用作哈希表的值
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if (size >= LONG_MAX)
        return LONG_MAX;
    while (1)
    {
        if (i >= size)
            return i;
        i *= 2;
    }
}

static int _dictExpandIfNeeded(dict *d)
{
    // 渐进式 rehash 已经在进行了，直接返回
    if (dictIsRehashing(d))
        return DICT_OK;
    // 如果字典（的 0 号哈希表）为空，那么创建并返回初始化大小的 0 号哈希表
    // T = O(1)
    if (d->ht[0].size == 0)
        return dictExpand(d, DICT_HT_INITIAL_SIZE);
    // 一下两个条件之一为真时，对字典进行扩展
    // 1）字典已使用节点数和字典大小之间的比率大于等于1并且 dict_can_resize 为真
    // 2）已使用节点数和字典大小之间的比率超过 dict_force_resize_ratio
    if ((d->ht[0].used >= d->ht[0].size && dict_can_resize) ||
        d->ht[0].used / d->ht[0].size > dict_force_resize_ratio)
    {
        // 新哈希表的大小至少是目前已使用节点数的两倍
        return dictExpand(d, d->ht[0].used * 2);
    }
    return DICT_OK;
}

int dictExpand(dict *d, unsigned long size)
{
    /* 这里不需要为n分配动态空间，即
     * dictht *n;
     * n = xm_malloc(sizeof(*n));
     * 只在函数栈上分配的原因是下面会通过赋值=，将n复制到已分配好空间的d中的ht[0]或ht[1]中
    */
    dictht n;
    unsigned long realsize = _dictNextPower(size);
    // 有两种出错情况
    // 1.字典正在 rehash
    // 2.size 的值小于 0 号哈希表的当前已使用节点
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;
    n.size = realsize;
    n.sizemask = realsize - 1;
    // 分配realsize个指针
    n.table = xm_calloc(realsize * sizeof(dictEntry *));
    n.used = 0;
    // ht[0]为空则初始化
    if (d->ht[0].table == NULL)
    {
        d->ht[0] = n;
    }
    // 不为空则rehash
    // ?不需要考虑不能rehash的情况？？
    // 是否需要和dictResize一样判断？
    else
    {
        d->ht[1] = n;
        d->rehashidx = 0;
    }
    return DICT_OK;
}

int dictResize(dict *d)
{
    int minimal;
    // 不能在关闭 rehash 或者正在 rehash 的时候调用
    if (!dict_can_resize || dictIsRehashing(d))
        return DICT_ERR;
    // 计算让比率接近 1：1 所需要的最少节点数量
    minimal = d->ht[0].used;
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;
    // 调整字典的大小
    return dictExpand(d, minimal);
}

int dictAdd(dict *d, void *key, void *val)
{
    dictEntry *entry = dictAddRaw(d, key);
    if (entry == NULL)
        return DICT_ERR;
    dictSetVal(d, entry, val);
    return DICT_OK;
}

dictEntry *dictAddRaw(dict *d, void *key)
{
    int index;
    dictEntry *entry;
    dictht *ht;

    //如果在rehash过程中，进行一次单步的rehash
    if (dictIsRehashing(d))
        _dictRehashStep(d);
    //如果返回的索引为 -1 ，那么表示键已经存在，返回NULL。表示无法插入
    if ((index = _dictKeyIndex(d, key)) == -1)
        return NULL;
    // 如果字典正在 rehash ，那么将新键添加到 1 号哈希表，_dictKeyIndex保证了返回的索引一定是1号的
    // 否则，将新键添加到 0 号哈希表
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    //为新节点分配空间并插入到链表中
    if ((entry = xm_malloc(sizeof(*entry))) == NULL)
        return NULL;
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ++ht->used;
    dictSetKey(d, entry, key);
    return entry;
}

int dictReplace(dict *d, void *key, void *val)
{
    //先尝试能否add成功
    if (dictAdd(d, key, val) == DICT_OK)
        return 1;
    //如果失败了，说明这个键已经存在，将其找出
    dictEntry *entry, auxentry;
    entry = dictFind(d, key);
    //必须先设置新值再释放旧值，因为新值和旧值可能是相同的
    auxentry = *entry;
    dictSetVal(d, entry, val);
    dictFreeVal(d, &auxentry);
    return 0;
}

dictEntry *dictReplaceRaw(dict *d, void *key)
{
    // 使用 key 在字典中查找节点
    dictEntry *entry = dictFind(d, key);
    // 如果节点找到了直接返回节点，否则添加并返回一个新节点
    return entry ? entry : dictAddRaw(d, key);
}

// 参数 nofree 决定是否调用键和值的释放函数,0 表示调用，1 表示不调用
// Generic:一般的
static int dictGenericDelete(dict *d, const void *key, int nofree)
{
    //如果字典为空，肯定是失败了
    if (d->ht[0].size == 0)
        return DICT_ERR;
    //来一次rehash
    if (dictIsRehashing(d))
        _dictRehashStep(d);
    unsigned int h, table, idx;
    dictEntry *he, *prevHe;
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; ++table)
    {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        prevHe = NULL;
        while (he)
        {
            if (dictCompareKeys(d, key, he->key))
            {
                if (prevHe)
                    prevHe->next = he->next;
                else
                    d->ht[table].table[idx] = he->next;
                if (!nofree)
                {
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                }
                xm_free(he);
                --d->ht[table].used;
                return DICT_OK;
            }
            prevHe = he;
            he = he->next;
        }
        if (!dictIsRehashing(d))
            break;
    }
    return DICT_ERR;
}

int dictDelete(dict *ht, const void *key)
{
    return dictGenericDelete(ht, key, 0);
}

int dictDeleteNoFree(dict *ht, const void *key)
{
    return dictGenericDelete(ht, key, 1);
}

//删除哈希表上的所有节点，并重置哈希表的各项属性
//callback没看出来有什么用
static int _dictClear(dict *d, dictht *ht, void(callback)(void *))
{
    unsigned long i;
    for (i = 0; i < ht->size && ht->used > 0; ++i)
    {
        //65535=1111 1111 1111 1111
        //i&65535==0，相当于判断i是65535的整数倍
        //不知道用途
        if (callback && (i & 65535) == 0)
            callback(d->privdata);
        dictEntry *he, *nextHe;
        if ((he = ht->table[i]) == NULL)
            continue;
        while (he)
        {
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            xm_free(he);
            --ht->used;
            he = nextHe;
        }
    }
    xm_free(ht->table);
    _dictReset(ht);
    return DICT_OK;
}

void dictRelease(dict *d)
{
    // 先删除并清空两个哈希表
    _dictClear(d, &d->ht[0], NULL);
    _dictClear(d, &d->ht[1], NULL);
    // 再释放自身节点结构
    xm_free(d);
}

dictEntry *dictFind(dict *d, const void *key)
{
    // 如果字典为空，直接返回
    if (d->ht[0].size == 0)
        return NULL;
    //如果在rehash，进行一个单步rehash
    if (dictIsRehashing(d))
        _dictRehashStep(d);
    unsigned int h, table, idx;
    dictEntry *he;
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++)
    {
        // 计算索引值
        idx = h & d->ht[table].sizemask;
        // 遍历给定索引上的链表的所有节点，查找 key
        he = d->ht[table].table[idx];
        while (he)
        {
            if (dictCompareKeys(d, key, he->key))
                return he;
            he = he->next;
        }
        // 如果程序遍历完 0 号哈希表，仍然没找到指定的键的节点
        // 那么程序会检查字典是否在进行 rehash,然后才决定是直接返回 NULL ，还是继续查找 1 号哈希表
        if (!dictIsRehashing(d))
            return NULL;
    }
    // 进行到这里时，说明两个哈希表都没找到
    return NULL;
}

void *dictFetchValue(dict *d, const void *key)
{
    dictEntry *he;
    he = dictFind(d, key);
    return he ? dictGetVal(he) : NULL;
}

static void _dictRehashStep(dict *d)
{
    //只有在字典不存在安全迭代器的情况下，才能对字典进行单步 rehash
    if (d->iterators == 0)
        dictRehash(d, 1);
}

int dictRehash(dict *d, int n)
{
    if (!dictIsRehashing(d))
        return 0;
    // 进行 N 步迁移,每步 rehash 都是以一个哈希表索引（桶）作为单位的
    // 一个桶里可能会有多个节点，被 rehash 的桶里的所有节点都会被移动到新哈希表。
    while (n--)
    {
        dictEntry *de, *nextde;
        // 如果 0 号哈希表为空，那么表示 rehash 执行完毕,可以提前退出
        if (d->ht[0].used == 0)
        {
            //释放0号哈希表
            xm_free(d->ht[0].table);
            d->ht[0] = d->ht[1];
            //只需重置，不用释放1号哈希表
            //此时不能释放1号，因为0号指向的空间就是之前1号分配的
            _dictReset(&d->ht[1]);
            // 关闭 rehash 标识
            d->rehashidx = -1;
            // 返回 0 ，向调用者表示 rehash 已经完成
            return 0;
        }
        //找到数组中的下一个非空索引
        while (d->ht[0].table[d->rehashidx] == NULL)
            ++d->rehashidx;
        de = d->ht[0].table[d->rehashidx];
        while (de)
        {

            nextde = de->next;
            unsigned int h;
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;
            --d->ht[0].used;
            ++d->ht[1].used;
            de = nextde;
        }
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }
    return 1;
}

//返回以毫秒为单位的 UNIX 时间戳
/*struct timeval
{
  long tv_sec;  //秒
  long tv_usec; //微秒
};
*/
static long long timeInMilliseconds(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

//返回rehash的次数
int dictRehashMilliseconds(dict *d, int ms)
{
    // 记录开始时间
    long long start = timeInMilliseconds();
    int rehashes = 0;
    while (dictRehash(d, 100))
    {
        rehashes += 100;
        // 如果时间已过，跳出
        if (timeInMilliseconds() - start > ms)
            break;
    }
    return rehashes;
}

// 返回可以将 key 插入到哈希表的索引位置,如果 key 已经存在于哈希表，那么返回 -1
static int _dictKeyIndex(dict *d, const void *key)
{
    //先扩展一下
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;
    unsigned int h, table, idx;
    dictEntry *he;
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; ++table)
    {
        idx = h & d->ht[table].sizemask;
        //然后要在链中搜查
        he = d->ht[table].table[idx];
        while (he)
        {
            if (dictCompareKeys(d, key, he->key))
                return -1;
            he = he->next;
        }
        // 如果运行到这里时，说明 0 号哈希表中所有节点都不包含 key
        // 如果这时不在rehahs，则不需要再对 1 号哈希表进行搜索
        // 而如果在rehash,idx会被修改成ht[1]中的索引，保证了新的索引一定在ht[1]中
        if (!dictIsRehashing(d))
            break;
    }
    return idx;
}

long long dictFingerprint(dict *d)
{
    long long integers[6], hash = 0;
    int j;
    //记录两个哈希表的特征
    integers[0] = (long)d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long)d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;
    //对上面6个特征值进行Hash
    //This way the same set of integers in a different order will (likely) hash to a different number.
    //Result = hash(hash(hash(int1)+int2)+int3) ...
    for (j = 0; j < 6; j++)
    {
        hash += integers[j];
        // For the hashing step we use Tomas Wang's 64 bit integer hash.
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter;
    if ((iter = xm_malloc(sizeof(*iter))) == NULL)
        return NULL;
    iter->d = d;
    iter->table = 0;
    //索引一开始设为-1
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

dictIterator *dictGetSafeIterator(dict *d)
{
    dictIterator *i = dictGetIterator(d);
    // 设置安全迭代器标识
    i->safe = 1;
    return i;
}

dictEntry *dictNext(dictIterator *iter)
{
    dictht *ht;
    while (1)
    {
        // 有两种可能：
        // 这是迭代器第一次运行
        // 当前索引链表中的节点已经迭代完（NULL 为链表的表尾）
        if (iter->entry == NULL)
        {
            // 指向被迭代的哈希表
            ht = &iter->d->ht[iter->table];
            // 如果是初次迭代
            if (iter->index == -1 && iter->table == 0)
            {
                // 如果是安全迭代器，那么更新安全迭代器计数器
                if (iter->safe)
                    iter->d->iterators++;
                // 如果是不安全迭代器，那么计算指纹
                else
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            //更新索引，往下移一格
            ++iter->index;
            // 如果迭代器的当前索引大于当前被迭代的哈希表的大小
            // 那么说明这个哈希表已经迭代完毕
            if (iter->index >= (signed)ht->size)
            {
                // 如果正在 rehash 的话，那么说明 1 号哈希表也正在使用中
                // 那么继续对 1 号哈希表进行迭代
                if (dictIsRehashing(iter->d) && iter->table == 0)
                {
                    ++iter->table;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                }
                else
                    // 如果没有 rehash ，那么说明迭代已经完成,跳出循环
                    break;
            }
            // 如果进行到这里，说明这个哈希表并未迭代完
            // 更新节点指针，指向下个索引链表的表头节点
            iter->entry = ht->table[iter->index];
        }
        else
        {
            // 执行到这里，说明程序正在迭代某个链表
            // 将节点指针指向链表的下个节点
            iter->entry = iter->nextEntry;
        }

        // 如果当前节点不为空，那么也记录下该节点的下个节点
        // 因为安全迭代器有可能会将迭代器返回的当前节点删除
        if (iter->entry)
        {
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}

void dictReleaseIterator(dictIterator *iter)
{
    //如果已经使用了
    if (!(iter->index == -1 && iter->table == 0))
    {
        // 释放安全迭代器时，安全迭代器计数器减一
        if (iter->safe)
            iter->d->iterators--;
        // 释放不安全迭代器时，验证指纹是否有变化
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    xm_free(iter);
}

//其实每个节点被选中的概率是不一样的
//只有假设每个桶的节点数量是一样的，这样每个节点的概率一样
dictEntry *dictGetRandomKey(dict *d)
{
    // 字典为空
    if (dictSize(d) == 0)
        return NULL;
    // 先进行单步 rehash
    if (dictIsRehashing(d))
        _dictRehashStep(d);
    dictEntry *he, *orighe;
    unsigned int h;
    int listlen, listele;
    // 如果正在 rehash ，那么将 1 号哈希表也作为随机查找的目标
    if (dictIsRehashing(d))
    {
        do
        {
            h = random() % (d->ht[0].size + d->ht[1].size);
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] : d->ht[0].table[h];
        } while (he == NULL);
    }
    // 否则，只从 0 号哈希表中查找节点
    else
    {
        do
        {
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while (he == NULL);
    }
    // 目前 he 已经指向一个非空的节点链表
    // 程序将从这个链表随机返回一个节点
    listlen = 0;
    orighe = he;
    // 计算节点数量, T = O(1)
    while (he)
    {
        he = he->next;
        listlen++;
    }
    // 取模，得出随机节点的索引
    listele = random() % listlen;
    he = orighe;
    // 按索引查找节点
    while (listele--)
        he = he->next;
    // 返回随机节点
    return he;
}

//注意des的空间必须足以容纳count个dictEntry指针
//当字典的节点数量少于count时，des内的元素数量会少于count
//能保证不返回重复的节点，速率快于调用count次dictGetRandomKey，但是随机性并不强
//获得的是连续的count个节点
int dictGetRandomKeys(dict *d, dictEntry **des, int count)
{
    int j;
    int stored = 0;

    if (dictSize(d) < count)
        count = dictSize(d);
    while (stored < count)
    {
        for (j = 0; j < 2; j++)
        {
            unsigned int i = random() & d->ht[j].sizemask;
            int size = d->ht[j].size;

            //保证每个桶都被搜查了一遍
            while (size--)
            {
                dictEntry *he = d->ht[j].table[i];
                while (he)
                {
                    //找到连续的count个
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count)
                        return stored;
                }
                i = (i + 1) & d->ht[j].sizemask;
            }
            //如果不在rehash，则在ht[0]中就会返回，不会到达这一步
            assert(dictIsRehashing(d) != 0);
        }
    }
    //永远不会到达这一步
    return stored;
}

//翻转2进制字符串
//http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel
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

unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata)
{
}

static uint32_t dict_hash_function_seed = 5381;

void dictSetHashFunctionSeed(uint32_t seed)
{
    dict_hash_function_seed = seed;
}

uint32_t dictGetHashFunctionSeed(void)
{
    return dict_hash_function_seed;
}

// MurmurHash2哈希函数
unsigned int dictGenHashFunction(const void *key, int len) {
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}


unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}
