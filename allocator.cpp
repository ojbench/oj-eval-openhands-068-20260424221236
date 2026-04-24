

#include "allocator.hpp"
#include <cstring>
#include <algorithm>
#include <cassert>

TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) 
    : memoryPool(nullptr), poolSize(memoryPoolSize) {
    // 初始化索引结构
    for (int i = 0; i < FLI_SIZE; ++i) {
        for (int j = 0; j < SLI_SIZE; ++j) {
            index.freeLists[i][j] = nullptr;
        }
        index.sliBitmaps[i] = 0;
    }
    index.fliBitmap = 0;
    
    // 初始化内存池
    initializeMemoryPool(memoryPoolSize);
}

TLSFAllocator::~TLSFAllocator() {
    if (memoryPool) {
        ::operator delete(memoryPool);
    }
}

void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    // 分配内存池
    memoryPool = ::operator new(size);
    
    // 创建初始空闲块
    FreeBlock* initialBlock = static_cast<FreeBlock*>(memoryPool);
    initialBlock->data = static_cast<char*>(memoryPool) + HEADER_SIZE;
    initialBlock->size = size;
    initialBlock->isFree = true;
    initialBlock->prevPhysBlock = nullptr;
    initialBlock->nextPhysBlock = nullptr;
    initialBlock->prevFree = nullptr;
    initialBlock->nextFree = nullptr;
    
    // 插入到空闲列表
    insertFreeBlock(initialBlock);
}

void* TLSFAllocator::allocate(std::size_t size) {
    if (size == 0) return nullptr;
    
    // 对齐大小并加上头部大小
    std::size_t alignedSize = alignSize(size + HEADER_SIZE);
    
    // 找到合适的块
    FreeBlock* block = findSuitableBlock(alignedSize);
    if (!block) {
        return nullptr; // 内存不足
    }
    
    // 从空闲列表中移除
    removeFreeBlock(block);
    block->isFree = false;
    
    // 如果块太大，进行分割
    if (block->size >= alignedSize + HEADER_SIZE + ALIGNMENT) {
        splitBlock(block, alignedSize);
    }
    
    return getDataPointer(block);
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    // 获取块头
    BlockHeader* block = getBlockHeader(ptr);
    if (!block || block->isFree) {
        return; // 无效指针或已经释放
    }
    
    // 标记为空闲
    block->isFree = true;
    
    // 转换为FreeBlock并插入空闲列表
    FreeBlock* freeBlock = static_cast<FreeBlock*>(block);
    insertFreeBlock(freeBlock);
    
    // 合并相邻的空闲块
    mergeAdjacentFreeBlocks(freeBlock);
}

void TLSFAllocator::splitBlock(TLSFAllocator::FreeBlock* block, std::size_t size) {
    std::size_t remainingSize = block->size - size;
    
    // 创建新的空闲块
    FreeBlock* newBlock = reinterpret_cast<FreeBlock*>(
        reinterpret_cast<char*>(block) + size
    );
    
    newBlock->data = reinterpret_cast<char*>(newBlock) + HEADER_SIZE;
    newBlock->size = remainingSize;
    newBlock->isFree = true;
    newBlock->prevPhysBlock = block;
    newBlock->nextPhysBlock = block->nextPhysBlock;
    newBlock->prevFree = nullptr;
    newBlock->nextFree = nullptr;
    
    // 更新原块大小
    block->size = size;
    
    // 更新物理邻接关系
    if (block->nextPhysBlock) {
        block->nextPhysBlock->prevPhysBlock = newBlock;
    }
    block->nextPhysBlock = newBlock;
    
    // 插入新块到空闲列表
    insertFreeBlock(newBlock);
}

void TLSFAllocator::mergeAdjacentFreeBlocks(TLSFAllocator::FreeBlock* block) {
    // 合并前一个块
    if (block->prevPhysBlock && block->prevPhysBlock->isFree) {
        FreeBlock* prevBlock = static_cast<FreeBlock*>(block->prevPhysBlock);
        
        // 从空闲列表中移除前一个块
        removeFreeBlock(prevBlock);
        
        // 合并大小
        prevBlock->size += block->size;
        prevBlock->nextPhysBlock = block->nextPhysBlock;
        
        // 更新后一个块的前向指针
        if (block->nextPhysBlock) {
            block->nextPhysBlock->prevPhysBlock = prevBlock;
        }
        
        // 从空闲列表中移除当前块
        removeFreeBlock(block);
        
        // 使用合并后的块继续处理
        block = prevBlock;
    }
    
    // 合并后一个块
    if (block->nextPhysBlock && block->nextPhysBlock->isFree) {
        FreeBlock* nextBlock = static_cast<FreeBlock*>(block->nextPhysBlock);
        
        // 从空闲列表中移除后一个块
        removeFreeBlock(nextBlock);
        
        // 合并大小
        block->size += nextBlock->size;
        block->nextPhysBlock = nextBlock->nextPhysBlock;
        
        // 更新后一个块的前向指针
        if (nextBlock->nextPhysBlock) {
            nextBlock->nextPhysBlock->prevPhysBlock = block;
        }
    }
}

TLSFAllocator::FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli;
    mappingFunction(size, fli, sli);
    
    // 首先在当前FLI中查找
    for (int current_fli = fli; current_fli < FLI_SIZE; ++current_fli) {
        int start_sli = (current_fli == fli) ? sli : 0;
        
        // 检查当前FLI是否有空闲块
        if (current_fli == fli) {
            std::uint16_t sliMask = index.sliBitmaps[current_fli] & (~((1 << start_sli) - 1));
            if (sliMask == 0) {
                continue; // 当前FLI中没有合适的块
            }
            sli = findFirstSetBit(sliMask);
        } else {
            if (index.sliBitmaps[current_fli] == 0) {
                continue; // 当前FLI中没有空闲块
            }
            sli = findFirstSetBit(index.sliBitmaps[current_fli]);
        }
        
        // 找到空闲块
        FreeBlock* block = index.freeLists[current_fli][sli];
        if (block) {
            return block;
        }
    }
    
    return nullptr; // 没有找到合适的块
}

void TLSFAllocator::insertFreeBlock(TLSFAllocator::FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    // 插入到双向链表头部
    FreeBlock* head = index.freeLists[fli][sli];
    block->prevFree = nullptr;
    block->nextFree = head;
    
    if (head) {
        head->prevFree = block;
    }
    
    index.freeLists[fli][sli] = block;
    
    // 更新位图
    setSliBit(fli, sli);
    setFliBit(fli);
}

void TLSFAllocator::removeFreeBlock(TLSFAllocator::FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    // 从双向链表中移除
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        // 这是链表头
        index.freeLists[fli][sli] = block->nextFree;
    }
    
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    
    // 更新位图
    if (index.freeLists[fli][sli] == nullptr) {
        clearSliBit(fli, sli);
        if (index.sliBitmaps[fli] == 0) {
            clearFliBit(fli);
        }
    }
    
    block->prevFree = nullptr;
    block->nextFree = nullptr;
}

void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    if (size < 16) {
        fli = 0;
        sli = 0;
        return;
    }
    
    // 计算FLI
    fli = 0;
    std::size_t temp = size;
    while (temp >>= 1) fli++;
    
    // 计算SLI
    std::size_t flBase = 1 << fli;
    std::size_t remainder = size - flBase;
    std::size_t chunkSize = std::max(flBase / SLI_SIZE, std::size_t(1));
    sli = remainder / chunkSize;
    
    // 确保不超出范围
    if (fli >= FLI_SIZE) fli = FLI_SIZE - 1;
    if (sli >= SLI_SIZE) sli = SLI_SIZE - 1;
}

int TLSFAllocator::findFirstSetBit(std::uint32_t bitmap) const {
    if (bitmap == 0) return -1;
    
    int pos = 0;
    while ((bitmap & 1) == 0) {
        bitmap >>= 1;
        pos++;
    }
    return pos;
}

int TLSFAllocator::findFirstSetBit(std::uint16_t bitmap) const {
    if (bitmap == 0) return -1;
    
    int pos = 0;
    while ((bitmap & 1) == 0) {
        bitmap >>= 1;
        pos++;
    }
    return pos;
}

void TLSFAllocator::setFliBit(int fli) {
    index.fliBitmap |= (1 << fli);
}

void TLSFAllocator::clearFliBit(int fli) {
    index.fliBitmap &= ~(1 << fli);
}

void TLSFAllocator::setSliBit(int fli, int sli) {
    index.sliBitmaps[fli] |= (1 << sli);
}

void TLSFAllocator::clearSliBit(int fli, int sli) {
    index.sliBitmaps[fli] &= ~(1 << sli);
}

std::size_t TLSFAllocator::alignSize(std::size_t size) const {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

TLSFAllocator::BlockHeader* TLSFAllocator::getBlockHeader(void* ptr) const {
    return reinterpret_cast<BlockHeader*>(
        reinterpret_cast<char*>(ptr) - HEADER_SIZE
    );
}

void* TLSFAllocator::getDataPointer(TLSFAllocator::BlockHeader* block) const {
    return reinterpret_cast<char*>(block) + HEADER_SIZE;
}

void* TLSFAllocator::getMemoryPoolStart() const {
    return memoryPool;
}

std::size_t TLSFAllocator::getMemoryPoolSize() const {
    return poolSize;
}

std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    std::size_t maxSize = 0;
    
    for (int fli = 0; fli < FLI_SIZE; ++fli) {
        if (index.sliBitmaps[fli] == 0) continue;
        
        for (int sli = 0; sli < SLI_SIZE; ++sli) {
            FreeBlock* block = index.freeLists[fli][sli];
            while (block) {
                if (block->size > maxSize) {
                    maxSize = block->size;
                }
                block = block->nextFree;
            }
        }
    }
    
    return maxSize > HEADER_SIZE ? maxSize - HEADER_SIZE : 0;
}

