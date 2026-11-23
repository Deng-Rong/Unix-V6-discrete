#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "MapNode.h"

class Allocator
{
public:
	unsigned long Alloc(MapNode map[], unsigned long size);
	unsigned long Free(MapNode map[], unsigned long size, unsigned long addrIdx);

public:
	static Allocator &GetInstance();

private:
	static Allocator m_Instance;
};

class BitMapAllocator
{
public:
	unsigned long Alloc(BitMap &bitmap, unsigned long size, unsigned long firstPage, unsigned long endPage);
	unsigned long Alloc(BitMap &bitmap, unsigned long firstPage, unsigned long endPage);
	unsigned long Free(BitMap &bitmap, unsigned long size, unsigned long page);
	unsigned long Free(BitMap &bitmap, unsigned long page);
	int is_free(BitMap &bitmap, unsigned long index);
	void set_bit(BitMap &bitmap, unsigned long index, int value);

public:
	static BitMapAllocator &GetInstance();

private:
	static BitMapAllocator m_Instance_bitmap;
};

#endif
