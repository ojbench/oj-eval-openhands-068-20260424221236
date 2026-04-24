


#include "allocator.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    // 读取内存池大小
    std::size_t poolSize;
    if (!(std::cin >> poolSize)) {
        return 0;
    }
    
    // 创建分配器
    TLSFAllocator allocator(poolSize);
    
    // 读取操作数量
    int opCount;
    std::cin >> opCount;
    
    // 存储分配的指针用于后续释放
    std::vector<void*> allocatedPtrs;
    
    for (int i = 0; i < opCount; ++i) {
        std::string op;
        std::cin >> op;
        
        if (op == "allocate") {
            std::size_t size;
            std::cin >> size;
            
            void* ptr = allocator.allocate(size);
            if (ptr) {
                allocatedPtrs.push_back(ptr);
                std::cout << "allocated " << size << " bytes at " << ptr << "\n";
            } else {
                std::cout << "allocation failed for " << size << " bytes\n";
            }
        } else if (op == "deallocate") {
            std::size_t index;
            std::cin >> index;
            
            if (index < allocatedPtrs.size()) {
                allocator.deallocate(allocatedPtrs[index]);
                std::cout << "deallocated pointer at " << allocatedPtrs[index] << "\n";
                allocatedPtrs[index] = nullptr; // 标记为已释放
            } else {
                std::cout << "invalid deallocation index " << index << "\n";
            }
        } else if (op == "status") {
            std::cout << "max available block size: " << allocator.getMaxAvailableBlockSize() << " bytes\n";
        }
    }
    
    return 0;
}

