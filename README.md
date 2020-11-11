# NUMASplitOrderedHashTable
A NUMA-aware concurrent hash table. It is based on [Split-ordered lists: Lock-free extensible hash tables](https://dl.acm.org/doi/abs/10.1145/1147954.1147958) and inspired from [NUMASK: High Performance Scalable Skip List for NUMA](https://drops.dagstuhl.de/opus/volltexte/2018/9807/).

It has a separeted bucket array for each NUMA node, and threads running on a NUMA node use the node's bucket array to access a bucket data. Doing that, remote memory accesses which have long latency are reduced, so this hash table shows better performance than several hash tables on NUMA environments.

[**Paper**](https://www.dbpia.co.kr/journal/articleDetail?nodeId=NODE10477320)
