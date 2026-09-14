#pragma once

#include "tools/CpuThreadPool.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// DOD 对公共线程池的兼容类型，不额外分配或包装任务
/// 管线仍拥有线程生命周期，阶段状态只借用同一实例
/// </summary>
using DataOrientedRoamThreadPool = Tools::CpuThreadPool;
}
