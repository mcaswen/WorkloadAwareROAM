#pragma once

#include <cstdlib>
#include <new>

// 单入口测试程序的分配故障支持；只在主动设置的调用线程触发一次
// 各测试目标只由一个入口翻译单元包含，生产源码不依赖此文件
namespace TestAllocation
{
inline thread_local int Countdown=-1;
}

void* operator new(std::size_t size)
{
    if (TestAllocation::Countdown>=0 && TestAllocation::Countdown--==0)
    {
        TestAllocation::Countdown=-1;
        throw std::bad_alloc();
    }
    if (void* memory=std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory,std::size_t) noexcept { std::free(memory); }
