#include "PageManager.h"
#include "Allocator.h"

Allocator Allocator::m_Instance;

Allocator &Allocator::GetInstance()
{
	return Allocator::m_Instance;
}

unsigned long Allocator::Alloc(MapNode map[], unsigned long size)
{
	MapNode *pNode;
	unsigned long retIdx = 0;

	/* 若pNode->m_Size == 0则表示已经遍历到结尾 */
	for (pNode = map; pNode->m_Size; pNode++)
	{
		if (pNode->m_Size >= size)
		{
			retIdx = pNode->m_AddressIdx;
			pNode->m_AddressIdx += size;
			pNode->m_Size -= size;
			/* 当前内存正好分配完成，将该MapNode所在位置后面的MapNode都向前移动一个位置 */
			if (pNode->m_Size == 0)
			{
				MapNode *pNextNode = (pNode + 1);
				for (; pNextNode->m_Size; ++pNode, ++pNextNode)
				{
					pNode->m_AddressIdx = pNextNode->m_AddressIdx;
					pNode->m_Size = pNextNode->m_Size;
				}
				pNode->m_AddressIdx = pNode->m_Size = 0;
			}
			return retIdx;
		}
	}
	/* no match found */
	return 0;
}

unsigned long Allocator::Free(MapNode map[], unsigned long size, unsigned long addrIdx)
{
	MapNode *pNode;
	/* 首先，pNode指向正好是addrIdx下一块空闲区 */
	for (pNode = map; pNode->m_AddressIdx <= addrIdx && pNode->m_Size != 0; ++pNode)
		;
	/*
	 * 1) pNode不是第一块，即不是在头部插入
	 * 2) 需要free的数据块正好与pLastNode相邻
	 * 3) 因为如果pNode是第一个话条件2)一定不会成立，
	 * 因此可以保证pLastNode在条件内一定不会小于map的地址
	 */
	MapNode *pLastNode = pNode - 1;
	if (pNode > map && addrIdx == pLastNode->m_AddressIdx + pLastNode->m_Size)
	{
		pLastNode->m_Size += size;
		/* 这里处理与后面空闲块相邻的情况，需要合并后依次向前移动空闲块 */
		if (addrIdx + size == pNode->m_AddressIdx)
		{
			pLastNode->m_Size += pNode->m_Size;
			for (++pLastNode, ++pNode; pNode->m_Size; ++pLastNode, ++pNode)
			{
				pLastNode->m_AddressIdx = pNode->m_AddressIdx;
				pLastNode->m_Size = pNode->m_Size;
			}
			pLastNode->m_AddressIdx = pLastNode->m_Size = 0;
		}
	}
	/* 这里处理两种情况
	 * 1) 正好与pNode相邻且pNode不是无效的，只要修改pNode的AddressIdx属性即可
	 * 2) 前后都不相邻，则需要将pNode及以后的节点依次向后移动
	 */
	else
	{
		if (addrIdx + size == pNode->m_AddressIdx && pNode->m_Size)
		{
			pNode->m_AddressIdx = addrIdx;
			pNode->m_Size += size;
		}
		else if (size) // 合法性判断
		{
			// 处理条件2)的情况
			MapNode tmpNode1, tmpNode2;
			tmpNode1.m_AddressIdx = addrIdx;
			tmpNode1.m_Size = size;

			for (; pNode->m_Size; ++pNode)
			{
				tmpNode2.m_AddressIdx = pNode->m_AddressIdx;
				tmpNode2.m_Size = pNode->m_Size;

				pNode->m_AddressIdx = tmpNode1.m_AddressIdx;
				pNode->m_Size = tmpNode1.m_Size;

				tmpNode1.m_AddressIdx = tmpNode2.m_AddressIdx;
				tmpNode1.m_Size = tmpNode2.m_Size;
			}
			/* 将最后一个填入pNode中 */
			pNode->m_AddressIdx = tmpNode1.m_AddressIdx;
			pNode->m_Size = tmpNode1.m_Size;
		}
	}
	return 0;
}

int BitMapAllocator::is_free(BitMap &bitmap, unsigned long index)
{
	return index < bitmap.pageCount &&
		!(bitmap.map[index / 64] & (1ULL << (index % 64)));
}

void BitMapAllocator::set_bit(BitMap &bitmap, unsigned long index, int value)
{
	if (value)
		bitmap.map[index / 64] |= (1ULL << (index % 64));
	else
		bitmap.map[index / 64] &= ~(1ULL << (index % 64));
}

BitMapAllocator BitMapAllocator::m_Instance_bitmap;

BitMapAllocator &BitMapAllocator::GetInstance()
{
	return BitMapAllocator::m_Instance_bitmap;
}

unsigned long BitMapAllocator::Alloc(BitMap &bitmap, unsigned long size,
	unsigned long firstPage, unsigned long endPage)
{
	unsigned long pagesNeeded = (size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
	if (pagesNeeded == 0)
		return 0;
	if (endPage > bitmap.pageCount)
		endPage = bitmap.pageCount;

	unsigned long runStart = firstPage;
	unsigned long runLength = 0;
	for (unsigned long page = firstPage; page < endPage; page++)
	{
		if (is_free(bitmap, page))
		{
			if (runLength == 0)
				runStart = page;
			runLength++;
			if (runLength == pagesNeeded)
			{
				for (unsigned long allocated = runStart; allocated < runStart + pagesNeeded; allocated++)
					set_bit(bitmap, allocated, 1);
				return runStart * PageManager::PAGE_SIZE;
			}
		}
		else
		{
			runLength = 0;
		}
	}
	return 0;
}

unsigned long BitMapAllocator::Alloc(BitMap &bitmap,
	unsigned long firstPage, unsigned long endPage)
{
	if (endPage > bitmap.pageCount)
		endPage = bitmap.pageCount;
	for (unsigned long page = firstPage; page < endPage; page++)
	{
		if (is_free(bitmap, page))
		{
			set_bit(bitmap, page, 1);
			return page;
		}
	}
	return 0;
}

unsigned long BitMapAllocator::Free(BitMap &bitmap, unsigned long size, unsigned long page)
{
	unsigned long pages = (size + PageManager::PAGE_SIZE - 1) / PageManager::PAGE_SIZE;
	for (unsigned long index = page; index < page + pages; index++)
		set_bit(bitmap, index, 0);
	return 0;
}

unsigned long BitMapAllocator::Free(BitMap &bitmap, unsigned long page)
{
	set_bit(bitmap, page, 0);
	return 0;
}
