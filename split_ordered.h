#include "lf_set.h"

class SO_Key
{
public:
    SO_Key(unsigned long key);
    operator unsigned long() { return reversed_key; }

private:
    unsigned long reversed_key;
};

class SO_Hashtable
{
public:
    void init_bucket(uintptr_t bucket);
    bool remove(SO_Key key);
    unsigned long find(SO_Key key);
    bool insert(SO_Key key, unsigned long value);

private:
    LFSET item_set;
};
