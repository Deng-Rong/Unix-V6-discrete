#ifndef MAP_NODE_H
#define MAP_NODE_H

struct MapNode
{
	unsigned long m_Size;
	unsigned long m_AddressIdx; // 分配空间的起始地址
};

class BitMap
{
public:
	unsigned long pageCount;
	unsigned long rows;
	unsigned long long *map;
};

#endif
