# peking547-a-b20000：完整函数和调用路径明细

2026-09-14；从既有 FPR-02 官方 `perf-script.txt` 重新聚合，未重录。返回[总报告](cpu_function_profiling_findings.md)。

范围：128 个更新窗口，2508 个样本，period 分母 **5026052064**。本表覆盖全部 121 个有自身或祖先权重的符号、198 条采样栈相邻边、187 条完整栈，不限前 50 名。

自身百分比只归入栈顶；含调用百分比按每个样本中的唯一符号累计。主/池线程两列均使用同一全 ROI 分母，二者之和等于自身占比。含调用、相邻边不可相加。采样命中数不是调用次数；少于 30 个命中的条目只表示出现，不支持细微排名。完整符号保留编译器克隆和模板类型，简写不参与合并。函数没有自身样本不代表零执行成本。

算法列指向[函数算法与复杂度分析](fpr_function_cost_analysis.md)中的 C00～C13；库实例共享同一机制分析，不虚构每个模板的不同算法。外层未知符号不能恢复丢失祖先。

## 全部函数占比

| 身份与简写 | 自身 % | 含调用 % | 自身命中 | 含调用命中 | 主线程自身 % | 池线程自身 % | 算法 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [P001](#p001) `TransactionalSamples::Project` | 22.0893% | 22.0893% | 554 | 554 | 0.0000% | 22.0893% | [C01](fpr_function_cost_analysis.md#c01) |
| [P002](#p002) `std::function/TransactionalSamples::PrepareView/lambda#2` | 16.9059% | 19.9761% | 424 | 501 | 0.0000% | 16.9059% | [C10](fpr_function_cost_analysis.md#c10) |
| [P003](#p003) `TransactionalState::IsBoundary` | 13.4370% | 13.4370% | 337 | 337 | 13.1978% | 0.2392% | [C02](fpr_function_cost_analysis.md#c02) |
| [P004](#p004) `TransactionalSamples::PublishView` | 8.8118% | 13.0781% | 221 | 328 | 8.8118% | 0.0000% | [C01](fpr_function_cost_analysis.md#c01) |
| [P005](#p005) `std::function/TransactionalSamples::PrepareView/lambda#1` | 7.8549% | 31.5789% | 197 | 792 | 0.0000% | 7.8549% | [C10](fpr_function_cost_analysis.md#c10) |
| [P006](#p006) `TransactionalState::Vertex` | 2.6715% | 2.6715% | 67 | 67 | 0.0000% | 2.6715% | [C02](fpr_function_cost_analysis.md#c02) |
| [P007](#p007) `TransactionalSamples::PrepareView` | 2.2727% | 21.9298% | 57 | 550 | 2.2727% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P008](#p008) `std::_Rb_tree::_M_get_insert_unique_pos` | 2.0734% | 2.0734% | 52 | 52 | 2.0734% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P009](#p009) `TransactionalSamples::Decode` | 2.0335% | 2.0335% | 51 | 51 | 0.0399% | 1.9936% | [C01](fpr_function_cost_analysis.md#c01) |
| [P010](#p010) `std::_Rb_tree::_M_get_insert_unique_pos` | 1.3955% | 1.3955% | 35 | 35 | 1.3955% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P011](#p011) `TransactionalSamples::BuildOrders` | 1.1164% | 15.5502% | 28 | 390 | 1.1164% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P012](#p012) `_int_free_merge_chunk` | 1.1164% | 1.2360% | 28 | 31 | 1.1164% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | 1.0766% | 4.9043% | 27 | 123 | 0.0000% | 1.0766% | [C12](fpr_function_cost_analysis.md#c12) |
| [P014](#p014) `std::_Rb_tree::_M_erase` | 1.0367% | 1.4753% | 26 | 37 | 1.0367% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P015](#p015) `__nextafter` | 1.0367% | 1.0367% | 26 | 26 | 0.0000% | 1.0367% | [C12](fpr_function_cost_analysis.md#c12) |
| [P016](#p016) `Boost::eval_gcd` | 1.0367% | 1.0367% | 26 | 26 | 0.0000% | 1.0367% | [C12](fpr_function_cost_analysis.md#c12) |
| [P017](#p017) `std::_Rb_tree::_M_erase` | 0.9569% | 2.0734% | 24 | 52 | 0.9569% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P018](#p018) `__wrap_scalbnl` | 0.7974% | 0.7974% | 20 | 20 | 0.0000% | 0.7974% | [C12](fpr_function_cost_analysis.md#c12) |
| [P019](#p019) `Boost::do_assign_float` | 0.6778% | 1.7145% | 17 | 43 | 0.0000% | 0.6778% | [C12](fpr_function_cost_analysis.md#c12) |
| [P020](#p020) `Boost::divide_unsigned_helper` | 0.6778% | 0.7974% | 17 | 20 | 0.0000% | 0.6778% | [C12](fpr_function_cost_analysis.md#c12) |
| [P021](#p021) `std::_Rb_tree::_Rb_tree_insert_and_rebalance` | 0.6778% | 0.6778% | 17 | 17 | 0.5981% | 0.0797% | [C11](fpr_function_cost_analysis.md#c11) |
| [P022](#p022) `TransactionalSamples::Priority` | 0.6380% | 0.6380% | 16 | 16 | 0.0000% | 0.6380% | [C01](fpr_function_cost_analysis.md#c01) |
| [P023](#p023) `TransactionalSamples::VisibleSupport` | 0.5582% | 0.6778% | 14 | 17 | 0.0000% | 0.5582% | [C11](fpr_function_cost_analysis.md#c11) |
| [P024](#p024) `__frexpl` | 0.5582% | 0.5582% | 14 | 14 | 0.0000% | 0.5582% | [C12](fpr_function_cost_analysis.md#c12) |
| [P025](#p025) `_int_free_chunk` | 0.5183% | 1.9139% | 13 | 48 | 0.4785% | 0.0399% | [C11](fpr_function_cost_analysis.md#c11) |
| [P026](#p026) `__libc_malloc2` | 0.5183% | 1.1563% | 13 | 29 | 0.4386% | 0.0797% | [C11](fpr_function_cost_analysis.md#c11) |
| [P027](#p027) `_int_malloc` | 0.4785% | 0.6380% | 12 | 16 | 0.3987% | 0.0797% | [C11](fpr_function_cost_analysis.md#c11) |
| [P028](#p028) `Boost::eval_gcd` | 0.4386% | 0.4785% | 11 | 12 | 0.0000% | 0.4386% | [C12](fpr_function_cost_analysis.md#c12) |
| [P029](#p029) `__memmove_avx512_unaligned_erms` | 0.4386% | 0.4386% | 11 | 11 | 0.0797% | 0.3589% | [C11](fpr_function_cost_analysis.md#c11) |
| [P030](#p030) `cfree@GLIBC_2.2.5` | 0.4386% | 0.4386% | 11 | 11 | 0.3190% | 0.1196% | [C11](fpr_function_cost_analysis.md#c11) |
| [P031](#p031) `__scalbnl` | 0.3987% | 0.3987% | 10 | 10 | 0.0000% | 0.3987% | [C12](fpr_function_cost_analysis.md#c12) |
| [P032](#p032) `Boost::eval_multiply` | 0.3589% | 0.5183% | 9 | 13 | 0.0000% | 0.3589% | [C12](fpr_function_cost_analysis.md#c12) |
| [P033](#p033) `unlink_chunk.isra.0` | 0.3589% | 0.3589% | 9 | 9 | 0.2392% | 0.1196% | [C13](fpr_function_cost_analysis.md#c13) |
| [P034](#p034) `Boost::eval_add` | 0.2791% | 1.9139% | 7 | 48 | 0.0000% | 0.2791% | [C12](fpr_function_cost_analysis.md#c12) |
| [P035](#p035) `TransactionalState::Face` | 0.2791% | 0.2791% | 7 | 7 | 0.1595% | 0.1196% | [C02](fpr_function_cost_analysis.md#c02) |
| [P036](#p036) `malloc` | 0.2791% | 0.2791% | 7 | 7 | 0.1994% | 0.0797% | [C11](fpr_function_cost_analysis.md#c11) |
| [P037](#p037) `TransactionalPredicates::Shape` | 0.2392% | 7.2967% | 6 | 183 | 0.0000% | 0.2392% | [C05](fpr_function_cost_analysis.md#c05) |
| [P038](#p038) `Boost::divide_unsigned_helper` | 0.2392% | 0.2791% | 6 | 7 | 0.0000% | 0.2392% | [C12](fpr_function_cost_analysis.md#c12) |
| [P039](#p039) `Boost::multiprecision::backends::is_trivial_cpp_int` | 0.2392% | 0.2392% | 6 | 6 | 0.0000% | 0.2392% | [C12](fpr_function_cost_analysis.md#c12) |
| [P040](#p040) `local::Prepare` | 0.1994% | 2.1531% | 5 | 54 | 0.0000% | 0.1994% | [C11](fpr_function_cost_analysis.md#c11) |
| [P041](#p041) `Boost::multiprecision::backends::rational_adaptor` | 0.1994% | 0.1994% | 5 | 5 | 0.0000% | 0.1994% | [C12](fpr_function_cost_analysis.md#c12) |
| [P042](#p042) `local::operator*` | 0.1595% | 0.1595% | 4 | 4 | 0.0000% | 0.1595% | [C05](fpr_function_cost_analysis.md#c05) |
| [P043](#p043) `__vdso_clock_gettime` | 0.1595% | 0.1595% | 4 | 4 | 0.0000% | 0.1595% | [C13](fpr_function_cost_analysis.md#c13) |
| [P044](#p044) `Boost::eval_multiply` | 0.1196% | 0.4785% | 3 | 12 | 0.0000% | 0.1196% | [C12](fpr_function_cost_analysis.md#c12) |
| [P045](#p045) `_int_free_maybe_trim` | 0.1196% | 0.1196% | 3 | 3 | 0.1196% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P046](#p046) `nextafter@plt` | 0.1196% | 0.1196% | 3 | 3 | 0.0000% | 0.1196% | [C12](fpr_function_cost_analysis.md#c12) |
| [P047](#p047) `Boost::multiprecision::backends::subtract_unsigned` | 0.1196% | 0.1196% | 3 | 3 | 0.0000% | 0.1196% | [C12](fpr_function_cost_analysis.md#c12) |
| [P048](#p048) `void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsign…` | 0.1196% | 0.1196% | 3 | 3 | 0.0000% | 0.1196% | [C11](fpr_function_cost_analysis.md#c11) |
| [P049](#p049) `TransactionalProposals::Receivers` | 0.0797% | 2.6715% | 2 | 67 | 0.0000% | 0.0797% | [C04](fpr_function_cost_analysis.md#c04) |
| [P050](#p050) `TransactionalPredicates::Orientation` | 0.0797% | 0.9569% | 2 | 24 | 0.0000% | 0.0797% | [C05](fpr_function_cost_analysis.md#c05) |
| [P051](#p051) `TransactionalMesh::Prepare` | 0.0797% | 0.0797% | 2 | 2 | 0.0797% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P052](#p052) `TransactionalSamples::Weights` | 0.0797% | 0.0797% | 2 | 2 | 0.0399% | 0.0399% | [C05](fpr_function_cost_analysis.md#c05) |
| [P053](#p053) `Boost::resize` | 0.0797% | 0.0797% | 2 | 2 | 0.0000% | 0.0797% | [C12](fpr_function_cost_analysis.md#c12) |
| [P054](#p054) `std::_Rb_tree::template` | 0.0797% | 0.0797% | 2 | 2 | 0.0797% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P055](#p055) `std::_Rb_tree::_Rb_tree_decrement` | 0.0797% | 0.0797% | 2 | 2 | 0.0797% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P056](#p056) `Boost::eval_msb` | 0.0797% | 0.0797% | 2 | 2 | 0.0000% | 0.0797% | [C12](fpr_function_cost_analysis.md#c12) |
| [P057](#p057) `Boost::eval_multiply` | 0.0797% | 0.0797% | 2 | 2 | 0.0000% | 0.0797% | [C12](fpr_function_cost_analysis.md#c12) |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | 0.0399% | 10.3270% | 1 | 259 | 0.0000% | 0.0399% | [C10](fpr_function_cost_analysis.md#c10) |
| [P059](#p059) `TransactionalProposals::Donor` | 0.0399% | 1.9936% | 1 | 50 | 0.0000% | 0.0399% | [C06](fpr_function_cost_analysis.md#c06) |
| [P060](#p060) `std::_Rb_tree::_M_erase` | 0.0399% | 0.3987% | 1 | 10 | 0.0399% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P061](#p061) `cert::Reference<Interval>` | 0.0399% | 0.2791% | 1 | 7 | 0.0000% | 0.0399% | [C05](fpr_function_cost_analysis.md#c05) |
| [P062](#p062) `std::_Rb_tree::template` | 0.0399% | 0.1994% | 1 | 5 | 0.0000% | 0.0399% | [C11](fpr_function_cost_analysis.md#c11) |
| [P063](#p063) `TransactionalSamples::Prepare` | 0.0399% | 0.1595% | 1 | 4 | 0.0399% | 0.0000% | [C08](fpr_function_cost_analysis.md#c08) |
| [P064](#p064) `TransactionalCommit::Prepare` | 0.0399% | 0.1196% | 1 | 3 | 0.0399% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P065](#p065) `WorkLedger::Touch` | 0.0399% | 0.0797% | 1 | 2 | 0.0000% | 0.0399% | [C10](fpr_function_cost_analysis.md#c10) |
| [P066](#p066) `Boost::eval_multiply` | 0.0399% | 0.0797% | 1 | 2 | 0.0000% | 0.0399% | [C12](fpr_function_cost_analysis.md#c12) |
| [P067](#p067) `ParallelRoam::Tools::Profiling::ProfileSession::End()` | 0.0399% | 0.0399% | 1 | 1 | 0.0399% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P068](#p068) `__udivti3` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C13](fpr_function_cost_analysis.md#c13) |
| [P069](#p069) `__umodti3` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C13](fpr_function_cost_analysis.md#c13) |
| [P070](#p070) `auto std::__tuple_cmp<std::partial_ordering, std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned…` | 0.0399% | 0.0399% | 1 | 1 | 0.0399% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P071](#p071) `Boost::multiprecision::backends::cpp_int_backend` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C12](fpr_function_cost_analysis.md#c12) |
| [P072](#p072) `Boost::assign` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C12](fpr_function_cost_analysis.md#c12) |
| [P073](#p073) `memcpy@plt` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C11](fpr_function_cost_analysis.md#c11) |
| [P074](#p074) `operator delete(void*, unsigned long)` | 0.0399% | 0.0399% | 1 | 1 | 0.0399% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P075](#p075) `operator delete(void*, unsigned long)@plt` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C11](fpr_function_cost_analysis.md#c11) |
| [P076](#p076) `pthread_mutex_unlock@plt` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C13](fpr_function_cost_analysis.md#c13) |
| [P077](#p077) `std::_Rb_tree::_Rb_tree_rebalance_for_erase` | 0.0399% | 0.0399% | 1 | 1 | 0.0399% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P078](#p078) `std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string<std::allocator<char> >(c…` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C11](fpr_function_cost_analysis.md#c11) |
| [P079](#p079) `Boost::multiprecision::detail::karatsuba_sqrt` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C12](fpr_function_cost_analysis.md#c12) |
| [P080](#p080) `Boost::multiprecision::backends::add_unsigned` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C12](fpr_function_cost_analysis.md#c12) |
| [P081](#p081) `Boost::eval_multiply` | 0.0399% | 0.0399% | 1 | 1 | 0.0000% | 0.0399% | [C12](fpr_function_cost_analysis.md#c12) |
| [P082](#p082) `[unknown]` | 0.0000% | 64.3939% | 0 | 1615 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P083](#p083) `__GI___clone3` | 0.0000% | 64.3939% | 0 | 1615 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P084](#p084) `start_thread` | 0.0000% | 64.3939% | 0 | 1615 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P085](#p085) `ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool::WorkerLoop()` | 0.0000% | 64.2743% | 0 | 1612 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P086](#p086) `std::function/callback/lambda#1` | 0.0000% | 64.2743% | 0 | 1612 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | 0.0000% | 64.2743% | 0 | 1612 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P088](#p088) `std::function/callback/lambda#1` | 0.0000% | 64.2743% | 0 | 1612 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P089](#p089) `__libc_start_call_main` | 0.0000% | 35.6061% | 0 | 893 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P090](#p090) `__libc_start_main@@GLIBC_2.34` | 0.0000% | 35.6061% | 0 | 893 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P091](#p091) `_start` | 0.0000% | 35.6061% | 0 | 893 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P092](#p092) `main` | 0.0000% | 35.5662% | 0 | 892 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P093](#p093) `TransactionalPipeline::SetView` | 0.0000% | 35.0080% | 0 | 878 | 0.0000% | 0.0000% | [C00](fpr_function_cost_analysis.md#c00) |
| [P094](#p094) `TransactionalCertification::Fit` | 0.0000% | 7.4163% | 0 | 186 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P095](#p095) `std::function/TransactionalReservation::Plan/lambda#2` | 0.0000% | 1.9936% | 0 | 50 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P096](#p096) `TransactionalCertification::Measure` | 0.0000% | 1.4753% | 0 | 37 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P097](#p097) `local::ErrorBounds` | 0.0000% | 1.3955% | 0 | 35 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P098](#p098) `operator new(unsigned long)` | 0.0000% | 1.1164% | 0 | 28 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P099](#p099) `TransactionalPipeline::Update` | 0.0000% | 0.5582% | 0 | 14 | 0.0000% | 0.0000% | [C00](fpr_function_cost_analysis.md#c00) |
| [P100](#p100) `TransactionalPipeline::Apply` | 0.0000% | 0.5183% | 0 | 13 | 0.0000% | 0.0000% | [C00](fpr_function_cost_analysis.md#c00) |
| [P101](#p101) `Boost::assign` | 0.0000% | 0.2392% | 0 | 6 | 0.0000% | 0.0000% | [C12](fpr_function_cost_analysis.md#c12) |
| [P102](#p102) `cert::Clip<Interval>` | 0.0000% | 0.1595% | 0 | 4 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P103](#p103) `std::chrono::_V2::steady_clock::now()` | 0.0000% | 0.1595% | 0 | 4 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P104](#p104) `clock_gettime@@GLIBC_2.17` | 0.0000% | 0.1196% | 0 | 3 | 0.0000% | 0.0000% | [C13](fpr_function_cost_analysis.md#c13) |
| [P105](#p105) `cert::Height<Interval>` | 0.0000% | 0.0797% | 0 | 2 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P106](#p106) `TransactionalCommit::Prepare/lambda` | 0.0000% | 0.0797% | 0 | 2 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P107](#p107) `TransactionalSamples::Enumerate` | 0.0000% | 0.0797% | 0 | 2 | 0.0000% | 0.0000% | [C08](fpr_function_cost_analysis.md#c08) |
| [P108](#p108) `WorkLedger::CheckLimit` | 0.0000% | 0.0797% | 0 | 2 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P109](#p109) `Boost::multiprecision::backends::rational_adaptor` | 0.0000% | 0.0797% | 0 | 2 | 0.0000% | 0.0000% | [C12](fpr_function_cost_analysis.md#c12) |
| [P110](#p110) `local::CoveringFace` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P111](#p111) `local::ExactError` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P112](#p112) `local::Prepare` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P113](#p113) `TransactionalExecution::Run` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P114](#p114) `TransactionalReservation::Plan` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C10](fpr_function_cost_analysis.md#c10) |
| [P115](#p115) `std::_Rb_tree::_M_erase` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P116](#p116) `std::_Rb_tree::_M_erase` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P117](#p117) `std::_Rb_tree::template` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P118](#p118) `std::_Rb_tree::template` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P119](#p119) `std::_Rb_tree::template` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C11](fpr_function_cost_analysis.md#c11) |
| [P120](#p120) `cert::Clip<Rational>` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C05](fpr_function_cost_analysis.md#c05) |
| [P121](#p121) `Boost::multiprecision::default_ops::eval_karatsuba_sqrt` | 0.0000% | 0.0399% | 0 | 1 | 0.0000% | 0.0000% | [C12](fpr_function_cost_analysis.md#c12) |

## 每线程与每轮覆盖

| OS TID | 样本 | period 占比 |
| --- | --- | --- |
| 4663 | 893 | 35.6061% |
| 4664 | 411 | 16.3876% |
| 4665 | 398 | 15.8692% |
| 4666 | 398 | 15.8692% |
| 4667 | 408 | 16.2679% |

| round（合并各重放） | 样本 | period 占比 |
| --- | --- | --- |
| 0 | 91 | 3.6284% |
| 1 | 40 | 1.5949% |
| 2 | 33 | 1.3158% |
| 3 | 476 | 18.9793% |
| 4 | 473 | 18.8596% |
| 5 | 441 | 17.5837% |
| 6 | 480 | 19.1388% |
| 7 | 474 | 18.8995% |

## 全部采样调用边

方向为调用者 → 被调用者。它是记录栈中的相邻关系；禁用内联展开和优化可能省去中间源码函数，因此不宣称完整源码直接调用图。每个样本对同一有向边只计一次。

| 调用者 | 被调用者 | 命中 | ROI period 占比 |
| --- | --- | --- | --- |
| [P084](#p084) `start_thread` | [P082](#p082) `[unknown]` | 1615 | 64.3939% |
| [P083](#p083) `__GI___clone3` | [P084](#p084) `start_thread` | 1615 | 64.3939% |
| [P085](#p085) `ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool::WorkerLoop()` | [P086](#p086) `std::function/callback/lambda#1` | 1612 | 64.2743% |
| [P082](#p082) `[unknown]` | [P085](#p085) `ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool::WorkerLoop()` | 1612 | 64.2743% |
| [P088](#p088) `std::function/callback/lambda#1` | [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | 1612 | 64.2743% |
| [P086](#p086) `std::function/callback/lambda#1` | [P088](#p088) `std::function/callback/lambda#1` | 1612 | 64.2743% |
| [P091](#p091) `_start` | [P090](#p090) `__libc_start_main@@GLIBC_2.34` | 893 | 35.6061% |
| [P090](#p090) `__libc_start_main@@GLIBC_2.34` | [P089](#p089) `__libc_start_call_main` | 893 | 35.6061% |
| [P089](#p089) `__libc_start_call_main` | [P092](#p092) `main` | 892 | 35.5662% |
| [P092](#p092) `main` | [P093](#p093) `TransactionalPipeline::SetView` | 878 | 35.0080% |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | [P005](#p005) `std::function/TransactionalSamples::PrepareView/lambda#1` | 792 | 31.5789% |
| [P093](#p093) `TransactionalPipeline::SetView` | [P007](#p007) `TransactionalSamples::PrepareView` | 550 | 21.9298% |
| [P005](#p005) `std::function/TransactionalSamples::PrepareView/lambda#1` | [P001](#p001) `TransactionalSamples::Project` | 545 | 21.7305% |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | [P002](#p002) `std::function/TransactionalSamples::PrepareView/lambda#2` | 501 | 19.9761% |
| [P007](#p007) `TransactionalSamples::PrepareView` | [P011](#p011) `TransactionalSamples::BuildOrders` | 390 | 15.5502% |
| [P011](#p011) `TransactionalSamples::BuildOrders` | [P003](#p003) `TransactionalState::IsBoundary` | 331 | 13.1978% |
| [P093](#p093) `TransactionalPipeline::SetView` | [P004](#p004) `TransactionalSamples::PublishView` | 328 | 13.0781% |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | 259 | 10.3270% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P094](#p094) `TransactionalCertification::Fit` | 186 | 7.4163% |
| [P094](#p094) `TransactionalCertification::Fit` | [P037](#p037) `TransactionalPredicates::Shape` | 180 | 7.1770% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | 105 | 4.1866% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P049](#p049) `TransactionalProposals::Receivers` | 67 | 2.6715% |
| [P002](#p002) `std::function/TransactionalSamples::PrepareView/lambda#2` | [P006](#p006) `TransactionalState::Vertex` | 60 | 2.3923% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P040](#p040) `local::Prepare` | 53 | 2.1132% |
| [P004](#p004) `TransactionalSamples::PublishView` | [P017](#p017) `std::_Rb_tree::_M_erase` | 52 | 2.0734% |
| [P007](#p007) `TransactionalSamples::PrepareView` | [P008](#p008) `std::_Rb_tree::_M_get_insert_unique_pos` | 52 | 2.0734% |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | [P095](#p095) `std::function/TransactionalReservation::Plan/lambda#2` | 50 | 1.9936% |
| [P095](#p095) `std::function/TransactionalReservation::Plan/lambda#2` | [P059](#p059) `TransactionalProposals::Donor` | 50 | 1.9936% |
| [P005](#p005) `std::function/TransactionalSamples::PrepareView/lambda#1` | [P009](#p009) `TransactionalSamples::Decode` | 50 | 1.9936% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P019](#p019) `Boost::do_assign_float` | 43 | 1.7145% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P034](#p034) `Boost::eval_add` | 39 | 1.5550% |
| [P059](#p059) `TransactionalProposals::Donor` | [P096](#p096) `TransactionalCertification::Measure` | 37 | 1.4753% |
| [P004](#p004) `TransactionalSamples::PublishView` | [P014](#p014) `std::_Rb_tree::_M_erase` | 37 | 1.4753% |
| [P096](#p096) `TransactionalCertification::Measure` | [P097](#p097) `local::ErrorBounds` | 35 | 1.3955% |
| [P007](#p007) `TransactionalSamples::PrepareView` | [P010](#p010) `std::_Rb_tree::_M_get_insert_unique_pos` | 35 | 1.3955% |
| [P025](#p025) `_int_free_chunk` | [P012](#p012) `_int_free_merge_chunk` | 31 | 1.2360% |
| [P098](#p098) `operator new(unsigned long)` | [P026](#p026) `__libc_malloc2` | 28 | 1.1164% |
| [P017](#p017) `std::_Rb_tree::_M_erase` | [P025](#p025) `_int_free_chunk` | 27 | 1.0766% |
| [P040](#p040) `local::Prepare` | [P050](#p050) `TransactionalPredicates::Orientation` | 23 | 0.9171% |
| [P011](#p011) `TransactionalSamples::BuildOrders` | [P098](#p098) `operator new(unsigned long)` | 21 | 0.8373% |
| [P097](#p097) `local::ErrorBounds` | [P015](#p015) `__nextafter` | 17 | 0.6778% |
| [P026](#p026) `__libc_malloc2` | [P027](#p027) `_int_malloc` | 16 | 0.6380% |
| [P002](#p002) `std::function/TransactionalSamples::PrepareView/lambda#2` | [P022](#p022) `TransactionalSamples::Priority` | 16 | 0.6380% |
| [P007](#p007) `TransactionalSamples::PrepareView` | [P021](#p021) `std::_Rb_tree::_Rb_tree_insert_and_rebalance` | 15 | 0.5981% |
| [P050](#p050) `TransactionalPredicates::Orientation` | [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | 15 | 0.5981% |
| [P019](#p019) `Boost::do_assign_float` | [P018](#p018) `__wrap_scalbnl` | 15 | 0.5981% |
| [P092](#p092) `main` | [P099](#p099) `TransactionalPipeline::Update` | 14 | 0.5582% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P016](#p016) `Boost::eval_gcd` | 13 | 0.5183% |
| [P099](#p099) `TransactionalPipeline::Update` | [P100](#p100) `TransactionalPipeline::Apply` | 13 | 0.5183% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P020](#p020) `Boost::divide_unsigned_helper` | 10 | 0.3987% |
| [P014](#p014) `std::_Rb_tree::_M_erase` | [P025](#p025) `_int_free_chunk` | 10 | 0.3987% |
| [P004](#p004) `TransactionalSamples::PublishView` | [P060](#p060) `std::_Rb_tree::_M_erase` | 10 | 0.3987% |
| [P034](#p034) `Boost::eval_add` | [P016](#p016) `Boost::eval_gcd` | 10 | 0.3987% |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | [P001](#p001) `TransactionalSamples::Project` | 9 | 0.3589% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P044](#p044) `Boost::eval_multiply` | 9 | 0.3589% |
| [P040](#p040) `local::Prepare` | [P023](#p023) `TransactionalSamples::VisibleSupport` | 9 | 0.3589% |
| [P060](#p060) `std::_Rb_tree::_M_erase` | [P025](#p025) `_int_free_chunk` | 9 | 0.3589% |
| [P059](#p059) `TransactionalProposals::Donor` | [P023](#p023) `TransactionalSamples::VisibleSupport` | 8 | 0.3190% |
| [P034](#p034) `Boost::eval_add` | [P032](#p032) `Boost::eval_multiply` | 8 | 0.3190% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P024](#p024) `__frexpl` | 8 | 0.3190% |
| [P034](#p034) `Boost::eval_add` | [P028](#p028) `Boost::eval_gcd` | 8 | 0.3190% |
| [P097](#p097) `local::ErrorBounds` | [P061](#p061) `cert::Reference<Interval>` | 7 | 0.2791% |
| [P004](#p004) `TransactionalSamples::PublishView` | [P030](#p030) `cfree@GLIBC_2.2.5` | 7 | 0.2791% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P039](#p039) `Boost::multiprecision::backends::is_trivial_cpp_int` | 6 | 0.2392% |
| [P019](#p019) `Boost::do_assign_float` | [P024](#p024) `__frexpl` | 6 | 0.2392% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P101](#p101) `Boost::assign` | 6 | 0.2392% |
| [P034](#p034) `Boost::eval_add` | [P020](#p020) `Boost::divide_unsigned_helper` | 6 | 0.2392% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P003](#p003) `TransactionalState::IsBoundary` | 6 | 0.2392% |
| [P040](#p040) `local::Prepare` | [P006](#p006) `TransactionalState::Vertex` | 6 | 0.2392% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P031](#p031) `__scalbnl` | 5 | 0.1994% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P018](#p018) `__wrap_scalbnl` | 5 | 0.1994% |
| [P101](#p101) `Boost::assign` | [P034](#p034) `Boost::eval_add` | 5 | 0.1994% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P041](#p041) `Boost::multiprecision::backends::rational_adaptor` | 5 | 0.1994% |
| [P061](#p061) `cert::Reference<Interval>` | [P015](#p015) `__nextafter` | 5 | 0.1994% |
| [P019](#p019) `Boost::do_assign_float` | [P031](#p031) `__scalbnl` | 5 | 0.1994% |
| [P040](#p040) `local::Prepare` | [P062](#p062) `std::_Rb_tree::template` | 5 | 0.1994% |
| [P011](#p011) `TransactionalSamples::BuildOrders` | [P036](#p036) `malloc` | 5 | 0.1994% |
| [P097](#p097) `local::ErrorBounds` | [P102](#p102) `cert::Clip<Interval>` | 4 | 0.1595% |
| [P027](#p027) `_int_malloc` | [P033](#p033) `unlink_chunk.isra.0` | 4 | 0.1595% |
| [P050](#p050) `TransactionalPredicates::Orientation` | [P034](#p034) `Boost::eval_add` | 4 | 0.1595% |
| [P100](#p100) `TransactionalPipeline::Apply` | [P063](#p063) `TransactionalSamples::Prepare` | 4 | 0.1595% |
| [P023](#p023) `TransactionalSamples::VisibleSupport` | [P048](#p048) `void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsign…` | 3 | 0.1196% |
| [P103](#p103) `std::chrono::_V2::steady_clock::now()` | [P104](#p104) `clock_gettime@@GLIBC_2.17` | 3 | 0.1196% |
| [P104](#p104) `clock_gettime@@GLIBC_2.17` | [P043](#p043) `__vdso_clock_gettime` | 3 | 0.1196% |
| [P050](#p050) `TransactionalPredicates::Orientation` | [P044](#p044) `Boost::eval_multiply` | 3 | 0.1196% |
| [P044](#p044) `Boost::eval_multiply` | [P032](#p032) `Boost::eval_multiply` | 3 | 0.1196% |
| [P014](#p014) `std::_Rb_tree::_M_erase` | [P014](#p014) `std::_Rb_tree::_M_erase` | 3 | 0.1196% |
| [P034](#p034) `Boost::eval_add` | [P038](#p038) `Boost::divide_unsigned_helper` | 3 | 0.1196% |
| [P034](#p034) `Boost::eval_add` | [P047](#p047) `Boost::multiprecision::backends::subtract_unsigned` | 3 | 0.1196% |
| [P044](#p044) `Boost::eval_multiply` | [P016](#p016) `Boost::eval_gcd` | 3 | 0.1196% |
| [P100](#p100) `TransactionalPipeline::Apply` | [P064](#p064) `TransactionalCommit::Prepare` | 3 | 0.1196% |
| [P011](#p011) `TransactionalSamples::BuildOrders` | [P035](#p035) `TransactionalState::Face` | 3 | 0.1196% |
| [P012](#p012) `_int_free_merge_chunk` | [P033](#p033) `unlink_chunk.isra.0` | 3 | 0.1196% |
| [P017](#p017) `std::_Rb_tree::_M_erase` | [P017](#p017) `std::_Rb_tree::_M_erase` | 3 | 0.1196% |
| [P062](#p062) `std::_Rb_tree::template` | [P098](#p098) `operator new(unsigned long)` | 3 | 0.1196% |
| [P059](#p059) `TransactionalProposals::Donor` | [P037](#p037) `TransactionalPredicates::Shape` | 3 | 0.1196% |
| [P097](#p097) `local::ErrorBounds` | [P042](#p042) `local::operator*` | 3 | 0.1196% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P038](#p038) `Boost::divide_unsigned_helper` | 3 | 0.1196% |
| [P048](#p048) `void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsign…` | [P048](#p048) `void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsign…` | 2 | 0.0797% |
| [P020](#p020) `Boost::divide_unsigned_helper` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 2 | 0.0797% |
| [P011](#p011) `TransactionalSamples::BuildOrders` | [P055](#p055) `std::_Rb_tree::_Rb_tree_decrement` | 2 | 0.0797% |
| [P096](#p096) `TransactionalCertification::Measure` | [P065](#p065) `WorkLedger::Touch` | 2 | 0.0797% |
| [P100](#p100) `TransactionalPipeline::Apply` | [P054](#p054) `std::_Rb_tree::template` | 2 | 0.0797% |
| [P032](#p032) `Boost::eval_multiply` | [P053](#p053) `Boost::resize` | 2 | 0.0797% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P066](#p066) `Boost::eval_multiply` | 2 | 0.0797% |
| [P097](#p097) `local::ErrorBounds` | [P105](#p105) `cert::Height<Interval>` | 2 | 0.0797% |
| [P105](#p105) `cert::Height<Interval>` | [P015](#p015) `__nextafter` | 2 | 0.0797% |
| [P063](#p063) `TransactionalSamples::Prepare` | [P107](#p107) `TransactionalSamples::Enumerate` | 2 | 0.0797% |
| [P032](#p032) `Boost::eval_multiply` | [P057](#p057) `Boost::eval_multiply` | 2 | 0.0797% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P109](#p109) `Boost::multiprecision::backends::rational_adaptor` | 2 | 0.0797% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 2 | 0.0797% |
| [P102](#p102) `cert::Clip<Interval>` | [P015](#p015) `__nextafter` | 2 | 0.0797% |
| [P060](#p060) `std::_Rb_tree::_M_erase` | [P060](#p060) `std::_Rb_tree::_M_erase` | 2 | 0.0797% |
| [P025](#p025) `_int_free_chunk` | [P045](#p045) `_int_free_maybe_trim` | 2 | 0.0797% |
| [P025](#p025) `_int_free_chunk` | [P033](#p033) `unlink_chunk.isra.0` | 2 | 0.0797% |
| [P100](#p100) `TransactionalPipeline::Apply` | [P051](#p051) `TransactionalMesh::Prepare` | 2 | 0.0797% |
| [P108](#p108) `WorkLedger::CheckLimit` | [P103](#p103) `std::chrono::_V2::steady_clock::now()` | 2 | 0.0797% |
| [P044](#p044) `Boost::eval_multiply` | [P020](#p020) `Boost::divide_unsigned_helper` | 2 | 0.0797% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P030](#p030) `cfree@GLIBC_2.2.5` | 2 | 0.0797% |
| [P094](#p094) `TransactionalCertification::Fit` | [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | 2 | 0.0797% |
| [P040](#p040) `local::Prepare` | [P035](#p035) `TransactionalState::Face` | 2 | 0.0797% |
| [P064](#p064) `TransactionalCommit::Prepare` | [P106](#p106) `TransactionalCommit::Prepare/lambda` | 2 | 0.0797% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P028](#p028) `Boost::eval_gcd` | 2 | 0.0797% |
| [P040](#p040) `local::Prepare` | [P098](#p098) `operator new(unsigned long)` | 2 | 0.0797% |
| [P102](#p102) `cert::Clip<Interval>` | [P046](#p046) `nextafter@plt` | 1 | 0.0399% |
| [P082](#p082) `[unknown]` | [P075](#p075) `operator delete(void*, unsigned long)@plt` | 1 | 0.0399% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P112](#p112) `local::Prepare` | 1 | 0.0399% |
| [P117](#p117) `std::_Rb_tree::template` | [P036](#p036) `malloc` | 1 | 0.0399% |
| [P112](#p112) `local::Prepare` | [P117](#p117) `std::_Rb_tree::template` | 1 | 0.0399% |
| [P111](#p111) `local::ExactError` | [P120](#p120) `cert::Clip<Rational>` | 1 | 0.0399% |
| [P120](#p120) `cert::Clip<Rational>` | [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | 1 | 0.0399% |
| [P094](#p094) `TransactionalCertification::Fit` | [P111](#p111) `local::ExactError` | 1 | 0.0399% |
| [P065](#p065) `WorkLedger::Touch` | [P103](#p103) `std::chrono::_V2::steady_clock::now()` | 1 | 0.0399% |
| [P017](#p017) `std::_Rb_tree::_M_erase` | [P045](#p045) `_int_free_maybe_trim` | 1 | 0.0399% |
| [P004](#p004) `TransactionalSamples::PublishView` | [P074](#p074) `operator delete(void*, unsigned long)` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P050](#p050) `TransactionalPredicates::Orientation` | 1 | 0.0399% |
| [P066](#p066) `Boost::eval_multiply` | [P020](#p020) `Boost::divide_unsigned_helper` | 1 | 0.0399% |
| [P038](#p038) `Boost::divide_unsigned_helper` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P107](#p107) `TransactionalSamples::Enumerate` | [P052](#p052) `TransactionalSamples::Weights` | 1 | 0.0399% |
| [P082](#p082) `[unknown]` | [P030](#p030) `cfree@GLIBC_2.2.5` | 1 | 0.0399% |
| [P013](#p013) `Boost::multiprecision::backends::rational_adaptor` | [P068](#p068) `__udivti3` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P020](#p020) `Boost::divide_unsigned_helper` | 1 | 0.0399% |
| [P109](#p109) `Boost::multiprecision::backends::rational_adaptor` | [P056](#p056) `Boost::eval_msb` | 1 | 0.0399% |
| [P028](#p028) `Boost::eval_gcd` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P109](#p109) `Boost::multiprecision::backends::rational_adaptor` | [P032](#p032) `Boost::eval_multiply` | 1 | 0.0399% |
| [P116](#p116) `std::_Rb_tree::_M_erase` | [P025](#p025) `_int_free_chunk` | 1 | 0.0399% |
| [P099](#p099) `TransactionalPipeline::Update` | [P114](#p114) `TransactionalReservation::Plan` | 1 | 0.0399% |
| [P113](#p113) `TransactionalExecution::Run` | [P116](#p116) `std::_Rb_tree::_M_erase` | 1 | 0.0399% |
| [P114](#p114) `TransactionalReservation::Plan` | [P113](#p113) `TransactionalExecution::Run` | 1 | 0.0399% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P101](#p101) `Boost::assign` | [P032](#p032) `Boost::eval_multiply` | 1 | 0.0399% |
| [P061](#p061) `cert::Reference<Interval>` | [P046](#p046) `nextafter@plt` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P073](#p073) `memcpy@plt` | 1 | 0.0399% |
| [P014](#p014) `std::_Rb_tree::_M_erase` | [P030](#p030) `cfree@GLIBC_2.2.5` | 1 | 0.0399% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P036](#p036) `malloc` | 1 | 0.0399% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P078](#p078) `std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string<std::allocator<char> >(c…` | 1 | 0.0399% |
| [P087](#p087) `std::function/TransactionalExecution::Run/lambda#1` | [P108](#p108) `WorkLedger::CheckLimit` | 1 | 0.0399% |
| [P103](#p103) `std::chrono::_V2::steady_clock::now()` | [P043](#p043) `__vdso_clock_gettime` | 1 | 0.0399% |
| [P107](#p107) `TransactionalSamples::Enumerate` | [P009](#p009) `TransactionalSamples::Decode` | 1 | 0.0399% |
| [P094](#p094) `TransactionalCertification::Fit` | [P071](#p071) `Boost::multiprecision::backends::cpp_int_backend` | 1 | 0.0399% |
| [P063](#p063) `TransactionalSamples::Prepare` | [P035](#p035) `TransactionalState::Face` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P056](#p056) `Boost::eval_msb` | 1 | 0.0399% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P115](#p115) `std::_Rb_tree::_M_erase` | 1 | 0.0399% |
| [P115](#p115) `std::_Rb_tree::_M_erase` | [P025](#p025) `_int_free_chunk` | 1 | 0.0399% |
| [P094](#p094) `TransactionalCertification::Fit` | [P121](#p121) `Boost::multiprecision::default_ops::eval_karatsuba_sqrt` | 1 | 0.0399% |
| [P121](#p121) `Boost::multiprecision::default_ops::eval_karatsuba_sqrt` | [P079](#p079) `Boost::multiprecision::detail::karatsuba_sqrt` | 1 | 0.0399% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P103](#p103) `std::chrono::_V2::steady_clock::now()` | 1 | 0.0399% |
| [P007](#p007) `TransactionalSamples::PrepareView` | [P070](#p070) `auto std::__tuple_cmp<std::partial_ordering, std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned…` | 1 | 0.0399% |
| [P034](#p034) `Boost::eval_add` | [P080](#p080) `Boost::multiprecision::backends::add_unsigned` | 1 | 0.0399% |
| [P102](#p102) `cert::Clip<Interval>` | [P042](#p042) `local::operator*` | 1 | 0.0399% |
| [P094](#p094) `TransactionalCertification::Fit` | [P108](#p108) `WorkLedger::CheckLimit` | 1 | 0.0399% |
| [P002](#p002) `std::function/TransactionalSamples::PrepareView/lambda#2` | [P035](#p035) `TransactionalState::Face` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P028](#p028) `Boost::eval_gcd` | 1 | 0.0399% |
| [P119](#p119) `std::_Rb_tree::template` | [P098](#p098) `operator new(unsigned long)` | 1 | 0.0399% |
| [P106](#p106) `TransactionalCommit::Prepare/lambda` | [P119](#p119) `std::_Rb_tree::template` | 1 | 0.0399% |
| [P082](#p082) `[unknown]` | [P076](#p076) `pthread_mutex_unlock@plt` | 1 | 0.0399% |
| [P040](#p040) `local::Prepare` | [P021](#p021) `std::_Rb_tree::_Rb_tree_insert_and_rebalance` | 1 | 0.0399% |
| [P049](#p049) `TransactionalProposals::Receivers` | [P006](#p006) `TransactionalState::Vertex` | 1 | 0.0399% |
| [P059](#p059) `TransactionalProposals::Donor` | [P021](#p021) `std::_Rb_tree::_Rb_tree_insert_and_rebalance` | 1 | 0.0399% |
| [P089](#p089) `__libc_start_call_main` | [P067](#p067) `ParallelRoam::Tools::Profiling::ProfileSession::End()` | 1 | 0.0399% |
| [P118](#p118) `std::_Rb_tree::template` | [P098](#p098) `operator new(unsigned long)` | 1 | 0.0399% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P118](#p118) `std::_Rb_tree::template` | 1 | 0.0399% |
| [P034](#p034) `Boost::eval_add` | [P069](#p069) `__umodti3` | 1 | 0.0399% |
| [P044](#p044) `Boost::eval_multiply` | [P028](#p028) `Boost::eval_gcd` | 1 | 0.0399% |
| [P062](#p062) `std::_Rb_tree::template` | [P026](#p026) `__libc_malloc2` | 1 | 0.0399% |
| [P032](#p032) `Boost::eval_multiply` | [P032](#p032) `Boost::eval_multiply` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P038](#p038) `Boost::divide_unsigned_helper` | 1 | 0.0399% |
| [P106](#p106) `TransactionalCommit::Prepare/lambda` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P040](#p040) `local::Prepare` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P034](#p034) `Boost::eval_add` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P097](#p097) `local::ErrorBounds` | [P110](#p110) `local::CoveringFace` | 1 | 0.0399% |
| [P110](#p110) `local::CoveringFace` | [P052](#p052) `TransactionalSamples::Weights` | 1 | 0.0399% |
| [P100](#p100) `TransactionalPipeline::Apply` | [P029](#p029) `__memmove_avx512_unaligned_erms` | 1 | 0.0399% |
| [P058](#p058) `std::function/TransactionalReservation::Plan/lambda#1` | [P040](#p040) `local::Prepare` | 1 | 0.0399% |
| [P020](#p020) `Boost::divide_unsigned_helper` | [P072](#p072) `Boost::assign` | 1 | 0.0399% |
| [P100](#p100) `TransactionalPipeline::Apply` | [P077](#p077) `std::_Rb_tree::_Rb_tree_rebalance_for_erase` | 1 | 0.0399% |
| [P037](#p037) `TransactionalPredicates::Shape` | [P081](#p081) `Boost::eval_multiply` | 1 | 0.0399% |
| [P097](#p097) `local::ErrorBounds` | [P046](#p046) `nextafter@plt` | 1 | 0.0399% |

## 全部记录调用路径

路径由外到内，最右为自身采样点；每条完整记录路径互斥，权重之和为 100%。未知外层不删除。序列链接到下面的完整符号，可与上一节的可读边对照。

| 序号 | 命中 | ROI period 占比 | 外层 → 自身 |
| --- | --- | --- | --- |
| 1 | 545 | 21.7305% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P005](#p005) → [P001](#p001) |
| 2 | 424 | 16.9059% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P002](#p002) |
| 3 | 331 | 13.1978% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P003](#p003) |
| 4 | 221 | 8.8118% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) |
| 5 | 197 | 7.8549% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P005](#p005) |
| 6 | 60 | 2.3923% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P002](#p002) → [P006](#p006) |
| 7 | 57 | 2.2727% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) |
| 8 | 52 | 2.0734% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P008](#p008) |
| 9 | 50 | 1.9936% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P005](#p005) → [P009](#p009) |
| 10 | 35 | 1.3955% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P010](#p010) |
| 11 | 28 | 1.1164% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) |
| 12 | 24 | 0.9569% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) |
| 13 | 24 | 0.9569% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) |
| 14 | 22 | 0.8772% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) |
| 15 | 18 | 0.7177% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P025](#p025) → [P012](#p012) |
| 16 | 17 | 0.6778% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P015](#p015) |
| 17 | 16 | 0.6380% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P002](#p002) → [P022](#p022) |
| 18 | 15 | 0.5981% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P021](#p021) |
| 19 | 15 | 0.5981% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P019](#p019) |
| 20 | 14 | 0.5582% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P019](#p019) → [P018](#p018) |
| 21 | 12 | 0.4785% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P016](#p016) |
| 22 | 11 | 0.4386% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P098](#p098) → [P026](#p026) |
| 23 | 9 | 0.3589% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P098](#p098) → [P026](#p026) → [P027](#p027) |
| 24 | 9 | 0.3589% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P001](#p001) |
| 25 | 9 | 0.3589% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P023](#p023) |
| 26 | 9 | 0.3589% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P016](#p016) |
| 27 | 8 | 0.3190% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P024](#p024) |
| 28 | 7 | 0.2791% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P030](#p030) |
| 29 | 6 | 0.2392% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) |
| 30 | 6 | 0.2392% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) |
| 31 | 6 | 0.2392% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) → [P025](#p025) → [P012](#p012) |
| 32 | 6 | 0.2392% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P003](#p003) |
| 33 | 6 | 0.2392% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P006](#p006) |
| 34 | 5 | 0.1994% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P023](#p023) |
| 35 | 5 | 0.1994% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P039](#p039) |
| 36 | 5 | 0.1994% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P019](#p019) → [P024](#p024) |
| 37 | 5 | 0.1994% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P025](#p025) |
| 38 | 5 | 0.1994% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P041](#p041) |
| 39 | 5 | 0.1994% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P061](#p061) → [P015](#p015) |
| 40 | 5 | 0.1994% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P019](#p019) → [P031](#p031) |
| 41 | 5 | 0.1994% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P036](#p036) |
| 42 | 4 | 0.1595% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P020](#p020) |
| 43 | 4 | 0.1595% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) |
| 44 | 4 | 0.1595% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P032](#p032) |
| 45 | 4 | 0.1595% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P020](#p020) |
| 46 | 4 | 0.1595% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P028](#p028) |
| 47 | 3 | 0.1196% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P031](#p031) |
| 48 | 3 | 0.1196% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P018](#p018) |
| 49 | 3 | 0.1196% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P060](#p060) → [P025](#p025) |
| 50 | 3 | 0.1196% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P047](#p047) |
| 51 | 3 | 0.1196% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P035](#p035) |
| 52 | 3 | 0.1196% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P042](#p042) |
| 53 | 3 | 0.1196% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P020](#p020) |
| 54 | 3 | 0.1196% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P101](#p101) → [P034](#p034) → [P028](#p028) |
| 55 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) → [P025](#p025) |
| 56 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P055](#p055) |
| 57 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P054](#p054) |
| 58 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) |
| 59 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P044](#p044) |
| 60 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) → [P014](#p014) |
| 61 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P105](#p105) → [P015](#p015) |
| 62 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P029](#p029) |
| 63 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P102](#p102) → [P015](#p015) |
| 64 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P060](#p060) → [P060](#p060) → [P025](#p025) |
| 65 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P017](#p017) |
| 66 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P051](#p051) |
| 67 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P044](#p044) → [P020](#p020) |
| 68 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P038](#p038) |
| 69 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P030](#p030) |
| 70 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P044](#p044) → [P016](#p016) |
| 71 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P013](#p013) |
| 72 | 2 | 0.0797% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P060](#p060) → [P025](#p025) → [P012](#p012) |
| 73 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P035](#p035) |
| 74 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P018](#p018) |
| 75 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P028](#p028) |
| 76 | 2 | 0.0797% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P031](#p031) |
| 77 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P102](#p102) → [P046](#p046) |
| 78 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P023](#p023) → [P048](#p048) → [P048](#p048) → [P048](#p048) → [P048](#p048) |
| 79 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P023](#p023) → [P048](#p048) → [P048](#p048) → [P048](#p048) |
| 80 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P075](#p075) |
| 81 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P112](#p112) → [P117](#p117) → [P036](#p036) |
| 82 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P020](#p020) → [P029](#p029) |
| 83 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P111](#p111) → [P120](#p120) → [P013](#p013) → [P016](#p016) |
| 84 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P011](#p011) → [P098](#p098) → [P026](#p026) → [P027](#p027) → [P033](#p033) |
| 85 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P065](#p065) |
| 86 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P065](#p065) → [P103](#p103) → [P104](#p104) → [P043](#p043) |
| 87 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P019](#p019) → [P018](#p018) |
| 88 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P034](#p034) → [P032](#p032) |
| 89 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P045](#p045) |
| 90 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P074](#p074) |
| 91 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) |
| 92 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P050](#p050) |
| 93 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P044](#p044) → [P032](#p032) → [P053](#p053) |
| 94 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P060](#p060) |
| 95 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P066](#p066) → [P020](#p020) |
| 96 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P038](#p038) → [P029](#p029) |
| 97 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P063](#p063) → [P107](#p107) → [P052](#p052) |
| 98 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P030](#p030) |
| 99 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P101](#p101) → [P034](#p034) → [P032](#p032) → [P057](#p057) |
| 100 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P068](#p068) |
| 101 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P020](#p020) |
| 102 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P109](#p109) → [P056](#p056) |
| 103 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P032](#p032) → [P057](#p057) |
| 104 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P044](#p044) → [P016](#p016) |
| 105 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P039](#p039) |
| 106 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P064](#p064) |
| 107 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P028](#p028) → [P029](#p029) |
| 108 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P109](#p109) → [P032](#p032) |
| 109 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P020](#p020) → [P029](#p029) |
| 110 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) → [P014](#p014) → [P025](#p025) → [P012](#p012) |
| 111 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P044](#p044) → [P032](#p032) |
| 112 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P019](#p019) |
| 113 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P114](#p114) → [P113](#p113) → [P116](#p116) → [P025](#p025) → [P012](#p012) → [P033](#p033) |
| 114 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P062](#p062) |
| 115 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P029](#p029) |
| 116 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P101](#p101) → [P032](#p032) |
| 117 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P038](#p038) |
| 118 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P060](#p060) → [P025](#p025) → [P012](#p012) → [P033](#p033) |
| 119 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P025](#p025) → [P045](#p045) |
| 120 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P060](#p060) → [P025](#p025) → [P033](#p033) |
| 121 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P062](#p062) → [P098](#p098) → [P026](#p026) → [P027](#p027) |
| 122 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P019](#p019) → [P024](#p024) |
| 123 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P037](#p037) → [P013](#p013) → [P020](#p020) |
| 124 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P061](#p061) → [P046](#p046) |
| 125 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P073](#p073) |
| 126 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) → [P030](#p030) |
| 127 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P036](#p036) |
| 128 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P078](#p078) |
| 129 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P023](#p023) → [P048](#p048) |
| 130 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P108](#p108) → [P103](#p103) → [P043](#p043) |
| 131 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P063](#p063) → [P107](#p107) → [P009](#p009) |
| 132 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P071](#p071) |
| 133 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P063](#p063) → [P035](#p035) |
| 134 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P037](#p037) → [P056](#p056) |
| 135 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P034](#p034) |
| 136 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P115](#p115) → [P025](#p025) |
| 137 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P017](#p017) → [P025](#p025) → [P012](#p012) |
| 138 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P044](#p044) |
| 139 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P101](#p101) → [P034](#p034) → [P038](#p038) |
| 140 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P121](#p121) → [P079](#p079) |
| 141 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P103](#p103) → [P104](#p104) → [P043](#p043) |
| 142 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P007](#p007) → [P070](#p070) |
| 143 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P080](#p080) |
| 144 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P034](#p034) → [P016](#p016) |
| 145 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P102](#p102) → [P042](#p042) |
| 146 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P108](#p108) → [P103](#p103) → [P104](#p104) → [P043](#p043) |
| 147 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P002](#p002) → [P035](#p035) |
| 148 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P066](#p066) |
| 149 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P062](#p062) → [P098](#p098) → [P026](#p026) |
| 150 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P034](#p034) → [P020](#p020) |
| 151 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) |
| 152 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P028](#p028) |
| 153 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P064](#p064) → [P106](#p106) → [P119](#p119) → [P098](#p098) → [P026](#p026) → [P027](#p027) |
| 154 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P098](#p098) → [P026](#p026) → [P027](#p027) → [P033](#p033) |
| 155 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P076](#p076) |
| 156 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P021](#p021) |
| 157 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P006](#p006) |
| 158 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P021](#p021) |
| 159 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P067](#p067) |
| 160 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P118](#p118) → [P098](#p098) → [P026](#p026) → [P027](#p027) → [P033](#p033) |
| 161 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P069](#p069) |
| 162 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P025](#p025) → [P033](#p033) |
| 163 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P062](#p062) → [P098](#p098) → [P026](#p026) → [P027](#p027) → [P033](#p033) |
| 164 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P044](#p044) → [P028](#p028) |
| 165 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P062](#p062) → [P026](#p026) |
| 166 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P037](#p037) → [P013](#p013) → [P019](#p019) |
| 167 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P044](#p044) → [P032](#p032) → [P032](#p032) |
| 168 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P038](#p038) |
| 169 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P032](#p032) → [P053](#p053) |
| 170 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P064](#p064) → [P106](#p106) → [P029](#p029) |
| 171 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P017](#p017) → [P025](#p025) → [P012](#p012) → [P033](#p033) |
| 172 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P029](#p029) |
| 173 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P093](#p093) → [P004](#p004) → [P014](#p014) → [P025](#p025) → [P045](#p045) |
| 174 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P034](#p034) → [P029](#p029) |
| 175 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P110](#p110) → [P052](#p052) |
| 176 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P029](#p029) |
| 177 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P040](#p040) |
| 178 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) → [P038](#p038) |
| 179 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P098](#p098) → [P026](#p026) → [P027](#p027) |
| 180 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P013](#p013) → [P020](#p020) → [P072](#p072) |
| 181 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P077](#p077) |
| 182 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P094](#p094) → [P037](#p037) → [P081](#p081) |
| 183 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P058](#p058) → [P049](#p049) → [P040](#p040) → [P050](#p050) → [P013](#p013) |
| 184 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P061](#p061) |
| 185 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) → [P096](#p096) → [P097](#p097) → [P046](#p046) |
| 186 | 1 | 0.0399% | [P083](#p083) → [P084](#p084) → [P082](#p082) → [P085](#p085) → [P086](#p086) → [P088](#p088) → [P087](#p087) → [P095](#p095) → [P059](#p059) |
| 187 | 1 | 0.0399% | [P091](#p091) → [P090](#p090) → [P089](#p089) → [P092](#p092) → [P099](#p099) → [P100](#p100) → [P063](#p063) |

## 完整符号字典

<a id="p001"></a>

### P001 — TransactionalSamples::Project

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::Project(ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, unsigned int, ParallelRoam::Experiment::GreedyTransactionalLod::SampleValue, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&) const
```

<a id="p002"></a>

### P002 — std::function/TransactionalSamples::PrepareView/lambda#2

```text
std::_Function_handler<void (unsigned long, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&), ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::PrepareView(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&) const::{lambda(auto:1, auto:2, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)#2}>::_M_invoke(std::_Any_data const&, unsigned long&&, unsigned long&&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p003"></a>

### P003 — TransactionalState::IsBoundary

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState::IsBoundary(long) const
```

<a id="p004"></a>

### P004 — TransactionalSamples::PublishView

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::PublishView(ParallelRoam::Experiment::GreedyTransactionalLod::PreparedView&&)
```

<a id="p005"></a>

### P005 — std::function/TransactionalSamples::PrepareView/lambda#1

```text
std::_Function_handler<void (unsigned long, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&), ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::PrepareView(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&) const::{lambda(auto:1, auto:2, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)#1}>::_M_invoke(std::_Any_data const&, unsigned long&&, unsigned long&&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p006"></a>

### P006 — TransactionalState::Vertex

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState::Vertex(long) const
```

<a id="p007"></a>

### P007 — TransactionalSamples::PrepareView

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::PrepareView(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&) const
```

<a id="p008"></a>

### P008 — std::_Rb_tree::_M_get_insert_unique_pos

```text
std::_Rb_tree<std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned int>, std::_Identity<std::tuple<double, long, unsigned int> >, std::less<std::tuple<double, long, unsigned int> >, std::allocator<std::tuple<double, long, unsigned int> > >::_M_get_insert_unique_pos(std::tuple<double, long, unsigned int> const&)
```

<a id="p009"></a>

### P009 — TransactionalSamples::Decode

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::Decode(unsigned int) const
```

<a id="p010"></a>

### P010 — std::_Rb_tree::_M_get_insert_unique_pos

```text
std::_Rb_tree<std::pair<double, long>, std::pair<double, long>, std::_Identity<std::pair<double, long> >, std::less<std::pair<double, long> >, std::allocator<std::pair<double, long> > >::_M_get_insert_unique_pos(std::pair<double, long> const&)
```

<a id="p011"></a>

### P011 — TransactionalSamples::BuildOrders

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::BuildOrders(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, std::vector<double, std::allocator<double> > const&, std::set<std::tuple<double, long, unsigned int>, std::less<std::tuple<double, long, unsigned int> >, std::allocator<std::tuple<double, long, unsigned int> > >&, std::set<std::pair<double, long>, std::less<std::pair<double, long> >, std::allocator<std::pair<double, long> > >&, std::map<long, double, std::less<long>, std::allocator<std::pair<long const, double> > >&)
```

<a id="p012"></a>

### P012 — _int_free_merge_chunk

```text
_int_free_merge_chunk
```

<a id="p013"></a>

### P013 — Boost::multiprecision::backends::rational_adaptor

```text
std::enable_if<std::is_floating_point<long double>::value, boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >&>::type boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::operator=<long double>(long double) [clone .isra.0]
```

<a id="p014"></a>

### P014 — std::_Rb_tree::_M_erase

```text
std::_Rb_tree<std::pair<double, long>, std::pair<double, long>, std::_Identity<std::pair<double, long> >, std::less<std::pair<double, long> >, std::allocator<std::pair<double, long> > >::_M_erase(std::_Rb_tree_node<std::pair<double, long> >*) [clone .isra.0]
```

<a id="p015"></a>

### P015 — __nextafter

```text
__nextafter
```

<a id="p016"></a>

### P016 — Boost::eval_gcd

```text
std::enable_if<!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value, void>::type boost::multiprecision::backends::eval_gcd<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, unsigned long long)
```

<a id="p017"></a>

### P017 — std::_Rb_tree::_M_erase

```text
std::_Rb_tree<std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned int>, std::_Identity<std::tuple<double, long, unsigned int> >, std::less<std::tuple<double, long, unsigned int> >, std::allocator<std::tuple<double, long, unsigned int> > >::_M_erase(std::_Rb_tree_node<std::tuple<double, long, unsigned int> >*) [clone .isra.0]
```

<a id="p018"></a>

### P018 — __wrap_scalbnl

```text
__wrap_scalbnl
```

<a id="p019"></a>

### P019 — Boost::do_assign_float

```text
void boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >::do_assign_float<long double>(long double)
```

<a id="p020"></a>

### P020 — Boost::divide_unsigned_helper

```text
void boost::multiprecision::backends::divide_unsigned_helper<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >*, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, unsigned long long, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&)
```

<a id="p021"></a>

### P021 — std::_Rb_tree::_Rb_tree_insert_and_rebalance

```text
std::_Rb_tree_insert_and_rebalance(bool, std::_Rb_tree_node_base*, std::_Rb_tree_node_base*, std::_Rb_tree_node_base&)
```

<a id="p022"></a>

### P022 — TransactionalSamples::Priority

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::Priority(ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, std::array<ParallelRoam::Experiment::GreedyTransactionalLod::Point, 3ul> const&, double)
```

<a id="p023"></a>

### P023 — TransactionalSamples::VisibleSupport

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::VisibleSupport(std::vector<unsigned int, std::allocator<unsigned int> > const&) const
```

<a id="p024"></a>

### P024 — __frexpl

```text
__frexpl
```

<a id="p025"></a>

### P025 — _int_free_chunk

```text
_int_free_chunk
```

<a id="p026"></a>

### P026 — __libc_malloc2

```text
__libc_malloc2
```

<a id="p027"></a>

### P027 — _int_malloc

```text
_int_malloc
```

<a id="p028"></a>

### P028 — Boost::eval_gcd

```text
std::enable_if<!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value, void>::type boost::multiprecision::backends::eval_gcd<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&)
```

<a id="p029"></a>

### P029 — __memmove_avx512_unaligned_erms

```text
__memmove_avx512_unaligned_erms
```

<a id="p030"></a>

### P030 — cfree@GLIBC_2.2.5

```text
cfree@GLIBC_2.2.5
```

<a id="p031"></a>

### P031 — __scalbnl

```text
__scalbnl
```

<a id="p032"></a>

### P032 — Boost::eval_multiply

```text
std::enable_if<((!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value)&&(!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value))&&(!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value), void>::type boost::multiprecision::backends::eval_multiply<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long>, 0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long>, 0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&)
```

<a id="p033"></a>

### P033 — unlink_chunk.isra.0

```text
unlink_chunk.isra.0
```

<a id="p034"></a>

### P034 — Boost::eval_add

```text
void boost::multiprecision::backends::eval_add_subtract_imp<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >&, boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > > const&, boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > > const&, bool)
```

<a id="p035"></a>

### P035 — TransactionalState::Face

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState::Face(unsigned int) const
```

<a id="p036"></a>

### P036 — malloc

```text
malloc
```

<a id="p037"></a>

### P037 — TransactionalPredicates::Shape

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalPredicates::Shape(ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&)
```

<a id="p038"></a>

### P038 — Boost::divide_unsigned_helper

```text
void boost::multiprecision::backends::divide_unsigned_helper<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >*, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&)
```

<a id="p039"></a>

### P039 — Boost::multiprecision::backends::is_trivial_cpp_int

```text
std::enable_if<!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value, void>::type boost::multiprecision::backends::eval_left_shift<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, unsigned __int128) [clone .part.0]
```

<a id="p040"></a>

### P040 — local::Prepare

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Prepare(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int, char, std::vector<unsigned int, std::allocator<unsigned int> >, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, long, unsigned long)
```

<a id="p041"></a>

### P041 — Boost::multiprecision::backends::rational_adaptor

```text
boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::rational_adaptor()
```

<a id="p042"></a>

### P042 — local::operator*

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::operator*(ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval, ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval)
```

<a id="p043"></a>

### P043 — __vdso_clock_gettime

```text
__vdso_clock_gettime
```

<a id="p044"></a>

### P044 — Boost::eval_multiply

```text
void boost::multiprecision::backends::eval_multiply_imp<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >&, boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&)
```

<a id="p045"></a>

### P045 — _int_free_maybe_trim

```text
_int_free_maybe_trim
```

<a id="p046"></a>

### P046 — nextafter@plt

```text
nextafter@plt
```

<a id="p047"></a>

### P047 — Boost::multiprecision::backends::subtract_unsigned

```text
void boost::multiprecision::backends::subtract_unsigned<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&)
```

<a id="p048"></a>

### P048 — void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsign…

```text
void std::__introsort_loop<__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsigned int> > >, long, __gnu_cxx::__ops::_Iter_less_iter>(__gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsigned int> > >, __gnu_cxx::__normal_iterator<unsigned int*, std::vector<unsigned int, std::allocator<unsigned int> > >, long, __gnu_cxx::__ops::_Iter_less_iter) [clone .isra.0]
```

<a id="p049"></a>

### P049 — TransactionalProposals::Receivers

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalProposals::Receivers(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int)
```

<a id="p050"></a>

### P050 — TransactionalPredicates::Orientation

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalPredicates::Orientation(ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&)
```

<a id="p051"></a>

### P051 — TransactionalMesh::Prepare

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalMesh::Prepare(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::PreparedTopology const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&)
```

<a id="p052"></a>

### P052 — TransactionalSamples::Weights

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::Weights(unsigned int, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, std::array<double, 3ul>&) const
```

<a id="p053"></a>

### P053 — Boost::resize

```text
boost::multiprecision::backends::cpp_int_base<0ul, 18446744073709551615ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long>, false>::resize(unsigned long, unsigned long) [clone .isra.0]
```

<a id="p054"></a>

### P054 — std::_Rb_tree::template

```text
std::_Rb_tree<std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned int>, std::_Identity<std::tuple<double, long, unsigned int> >, std::less<std::tuple<double, long, unsigned int> >, std::allocator<std::tuple<double, long, unsigned int> > >::equal_range(std::tuple<double, long, unsigned int> const&)
```

<a id="p055"></a>

### P055 — std::_Rb_tree::_Rb_tree_decrement

```text
std::_Rb_tree_decrement(std::_Rb_tree_node_base*)
```

<a id="p056"></a>

### P056 — Boost::eval_msb

```text
std::enable_if<!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value, unsigned long>::type boost::multiprecision::backends::eval_msb<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&)
```

<a id="p057"></a>

### P057 — Boost::eval_multiply

```text
std::enable_if<(!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value)&&(!boost::multiprecision::backends::is_trivial_cpp_int<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value), void>::type boost::multiprecision::backends::eval_multiply<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long>, 0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, unsigned long long const&)
```

<a id="p058"></a>

### P058 — std::function/TransactionalReservation::Plan/lambda#1

```text
std::_Function_handler<void (unsigned long, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&), ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalReservation::Plan(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&)::{lambda(auto:1, auto:2, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)#1}>::_M_invoke(std::_Any_data const&, unsigned long&&, unsigned long&&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p059"></a>

### P059 — TransactionalProposals::Donor

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalProposals::Donor(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p060"></a>

### P060 — std::_Rb_tree::_M_erase

```text
std::_Rb_tree<long, std::pair<long const, double>, std::_Select1st<std::pair<long const, double> >, std::less<long>, std::allocator<std::pair<long const, double> > >::_M_erase(std::_Rb_tree_node<std::pair<long const, double> >*) [clone .isra.0]
```

<a id="p061"></a>

### P061 — cert::Reference<Interval>

```text
std::array<ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval, 3ul> ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Reference<ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval>(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int)
```

<a id="p062"></a>

### P062 — std::_Rb_tree::template

```text
std::_Rb_tree_iterator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> > std::_Rb_tree<long, std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point>, std::_Select1st<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> >, std::less<long>, std::allocator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> > >::_M_emplace_hint_unique<long&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&>(std::_Rb_tree_const_iterator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> >, long&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&) [clone .isra.0]
```

<a id="p063"></a>

### P063 — TransactionalSamples::Prepare

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::Prepare(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::PreparedTopology const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p064"></a>

### P064 — TransactionalCommit::Prepare

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalCommit::Prepare(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState&, ParallelRoam::Experiment::GreedyTransactionalLod::CertifiedBatch const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&)
```

<a id="p065"></a>

### P065 — WorkLedger::Touch

```text
ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger::Touch()
```

<a id="p066"></a>

### P066 — Boost::eval_multiply

```text
std::enable_if<std::is_convertible<long long, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::value&&std::is_integral<long long>::value, void>::type boost::multiprecision::backends::eval_multiply_imp<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, long long>(boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, long long)
```

<a id="p067"></a>

### P067 — ParallelRoam::Tools::Profiling::ProfileSession::End()

```text
ParallelRoam::Tools::Profiling::ProfileSession::End()
```

<a id="p068"></a>

### P068 — __udivti3

```text
__udivti3
```

<a id="p069"></a>

### P069 — __umodti3

```text
__umodti3
```

<a id="p070"></a>

### P070 — auto std::__tuple_cmp<std::partial_ordering, std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned…

```text
auto std::__tuple_cmp<std::partial_ordering, std::tuple<double, long, unsigned int>, std::tuple<double, long, unsigned int>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul> >(std::tuple<double, long, unsigned int> const&, std::tuple<double, long, unsigned int> const&, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul>)::{lambda<unsigned long... $N0>(std::integer_sequence<unsigned long, ($N0)...>)#1}::operator()<0ul, 1ul, 2ul>(std::integer_sequence<unsigned long, 0ul, 1ul, 2ul>) const [clone .constprop.0] [clone .isra.0]
```

<a id="p071"></a>

### P071 — Boost::multiprecision::backends::cpp_int_backend

```text
boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >::~cpp_int_backend()
```

<a id="p072"></a>

### P072 — Boost::assign

```text
boost::multiprecision::backends::cpp_int_base<0ul, 18446744073709551615ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long>, false>::assign(boost::multiprecision::backends::cpp_int_base<0ul, 18446744073709551615ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long>, false> const&) [clone .part.0]
```

<a id="p073"></a>

### P073 — memcpy@plt

```text
memcpy@plt
```

<a id="p074"></a>

### P074 — operator delete(void*, unsigned long)

```text
operator delete(void*, unsigned long)
```

<a id="p075"></a>

### P075 — operator delete(void*, unsigned long)@plt

```text
operator delete(void*, unsigned long)@plt
```

<a id="p076"></a>

### P076 — pthread_mutex_unlock@plt

```text
pthread_mutex_unlock@plt
```

<a id="p077"></a>

### P077 — std::_Rb_tree::_Rb_tree_rebalance_for_erase

```text
std::_Rb_tree_rebalance_for_erase(std::_Rb_tree_node_base*, std::_Rb_tree_node_base&)
```

<a id="p078"></a>

### P078 — std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string<std::allocator<char> >(c…

```text
std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >::basic_string<std::allocator<char> >(char const*, std::allocator<char> const&)
```

<a id="p079"></a>

### P079 — Boost::multiprecision::detail::karatsuba_sqrt

```text
unsigned __int128 boost::multiprecision::detail::karatsuba_sqrt<unsigned __int128>(unsigned __int128 const&, unsigned __int128&, unsigned long)
```

<a id="p080"></a>

### P080 — Boost::multiprecision::backends::add_unsigned

```text
void boost::multiprecision::backends::add_unsigned<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&)
```

<a id="p081"></a>

### P081 — Boost::eval_multiply

```text
void boost::multiprecision::backends::eval_multiply_imp<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >, long long>(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, long long)
```

<a id="p082"></a>

### P082 — [unknown]

```text
[unknown]
```

<a id="p083"></a>

### P083 — __GI___clone3

```text
__GI___clone3
```

<a id="p084"></a>

### P084 — start_thread

```text
start_thread
```

<a id="p085"></a>

### P085 — ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool::WorkerLoop()

```text
ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool::WorkerLoop()
```

<a id="p086"></a>

### P086 — std::function/callback/lambda#1

```text
std::_Function_handler<void (), ParallelRoam::Algorithms::DataOrientedRoam::DataOrientedRoamThreadPool::ParallelFor(unsigned long, std::function<void (unsigned long)> const&)::{lambda()#1}>::_M_invoke(std::_Any_data const&)
```

<a id="p087"></a>

### P087 — std::function/TransactionalExecution::Run/lambda#1

```text
std::_Function_handler<void (unsigned long), ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution::Run(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, std::function<void (unsigned long, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)> const&) const::{lambda(unsigned long)#1}>::_M_invoke(std::_Any_data const&, unsigned long&&)
```

<a id="p088"></a>

### P088 — std::function/callback/lambda#1

```text
std::_Function_handler<void (unsigned long), ParallelRoam::Experiment::RoamMaterialization::MaterializationExecutor::Dispatch(unsigned long, std::function<void (unsigned long)> const&)::{lambda(unsigned long)#1}>::_M_invoke(std::_Any_data const&, unsigned long&&)
```

<a id="p089"></a>

### P089 — __libc_start_call_main

```text
__libc_start_call_main
```

<a id="p090"></a>

### P090 — __libc_start_main@@GLIBC_2.34

```text
__libc_start_main@@GLIBC_2.34
```

<a id="p091"></a>

### P091 — _start

```text
_start
```

<a id="p092"></a>

### P092 — main

```text
main
```

<a id="p093"></a>

### P093 — TransactionalPipeline::SetView

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalPipeline::SetView(ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p094"></a>

### P094 — TransactionalCertification::Fit

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalCertification::Fit[abi:cxx11](ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, ParallelRoam::Experiment::GreedyTransactionalLod::Proposal&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p095"></a>

### P095 — std::function/TransactionalReservation::Plan/lambda#2

```text
std::_Function_handler<void (unsigned long, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&), ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalReservation::Plan(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&)::{lambda(auto:1, auto:2, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)#2}>::_M_invoke(std::_Any_data const&, unsigned long&&, unsigned long&&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p096"></a>

### P096 — TransactionalCertification::Measure

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalCertification::Measure(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, ParallelRoam::Experiment::GreedyTransactionalLod::Proposal&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p097"></a>

### P097 — local::ErrorBounds

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::ErrorBounds(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int, ParallelRoam::Experiment::GreedyTransactionalLod::Proposal const*, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p098"></a>

### P098 — operator new(unsigned long)

```text
operator new(unsigned long)
```

<a id="p099"></a>

### P099 — TransactionalPipeline::Update

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalPipeline::Update(ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p100"></a>

### P100 — TransactionalPipeline::Apply

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalPipeline::Apply(ParallelRoam::Experiment::GreedyTransactionalLod::CertifiedBatch const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)
```

<a id="p101"></a>

### P101 — Boost::assign

```text
void boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>::do_assign<boost::multiprecision::detail::expression<boost::multiprecision::detail::plus, boost::multiprecision::detail::expression<boost::multiprecision::detail::multiply_immediates, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, void, void>, boost::multiprecision::detail::expression<boost::multiprecision::detail::multiply_immediates, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, void, void>, void, void> >(boost::multiprecision::detail::expression<boost::multiprecision::detail::plus, boost::multiprecision::detail::expression<boost::multiprecision::detail::multiply_immediates, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, void, void>, boost::multiprecision::detail::expression<boost::multiprecision::detail::multiply_immediates, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, void, void>, void, void> const&, boost::multiprecision::detail::plus const&) [clone .isra.0]
```

<a id="p102"></a>

### P102 — cert::Clip<Interval>

```text
std::array<ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval, 4ul> ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Clip<ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval>(ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval const&, ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval const&, ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval const&)
```

<a id="p103"></a>

### P103 — std::chrono::_V2::steady_clock::now()

```text
std::chrono::_V2::steady_clock::now()
```

<a id="p104"></a>

### P104 — clock_gettime@@GLIBC_2.17

```text
clock_gettime@@GLIBC_2.17
```

<a id="p105"></a>

### P105 — cert::Height<Interval>

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Height<ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval>(ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval const&, ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Interval const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&) [clone .isra.0]
```

<a id="p106"></a>

### P106 — TransactionalCommit::Prepare/lambda

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalCommit::Prepare(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState&, ParallelRoam::Experiment::GreedyTransactionalLod::CertifiedBatch const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&)::{lambda(ParallelRoam::Experiment::GreedyTransactionalLod::Proposal const&, bool)#1}::operator()(ParallelRoam::Experiment::GreedyTransactionalLod::Proposal const&, bool) const
```

<a id="p107"></a>

### P107 — TransactionalSamples::Enumerate

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples::Enumerate(std::array<ParallelRoam::Experiment::GreedyTransactionalLod::Point, 3ul> const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&) const
```

<a id="p108"></a>

### P108 — WorkLedger::CheckLimit

```text
ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger::CheckLimit() const
```

<a id="p109"></a>

### P109 — Boost::multiprecision::backends::rational_adaptor

```text
boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >::compare(boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > > const&) const
```

<a id="p110"></a>

### P110 — local::CoveringFace

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::CoveringFace(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int, ParallelRoam::Experiment::GreedyTransactionalLod::Proposal const*)
```

<a id="p111"></a>

### P111 — local::ExactError

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::ExactError(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int, ParallelRoam::Experiment::GreedyTransactionalLod::Proposal const*)
```

<a id="p112"></a>

### P112 — local::Prepare

```text
ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Prepare(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, unsigned int, char, std::vector<unsigned int, std::allocator<unsigned int> >, ParallelRoam::Experiment::GreedyTransactionalLod::Point const&, long, unsigned long) [clone .constprop.0]
```

<a id="p113"></a>

### P113 — TransactionalExecution::Run

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution::Run(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, std::function<void (unsigned long, unsigned long, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&)> const&) const
```

<a id="p114"></a>

### P114 — TransactionalReservation::Plan

```text
ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalReservation::Plan(ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalState const&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalSamples const&, ParallelRoam::Experiment::GreedyTransactionalLod::WorkLedger&, ParallelRoam::Experiment::GreedyTransactionalLod::TransactionalExecution const&)
```

<a id="p115"></a>

### P115 — std::_Rb_tree::_M_erase

```text
std::_Rb_tree<long, std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point>, std::_Select1st<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> >, std::less<long>, std::allocator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> > >::_M_erase(std::_Rb_tree_node<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> >*) [clone .isra.0]
```

<a id="p116"></a>

### P116 — std::_Rb_tree::_M_erase

```text
std::_Rb_tree<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long>, std::_Select1st<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> >, std::less<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > >, std::allocator<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> > >::_M_erase(std::_Rb_tree_node<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> >*) [clone .isra.0]
```

<a id="p117"></a>

### P117 — std::_Rb_tree::template

```text
std::_Rb_tree_iterator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> > std::_Rb_tree<long, std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point>, std::_Select1st<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> >, std::less<long>, std::allocator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> > >::_M_emplace_hint_unique<long&, ParallelRoam::Experiment::GreedyTransactionalLod::Point&>(std::_Rb_tree_const_iterator<std::pair<long const, ParallelRoam::Experiment::GreedyTransactionalLod::Point> >, long&, ParallelRoam::Experiment::GreedyTransactionalLod::Point&) [clone .isra.0]
```

<a id="p118"></a>

### P118 — std::_Rb_tree::template

```text
std::_Rb_tree_iterator<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> > std::_Rb_tree<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long>, std::_Select1st<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> >, std::less<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > >, std::allocator<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> > >::_M_emplace_hint_unique<std::piecewise_construct_t const&, std::tuple<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&>, std::tuple<> >(std::_Rb_tree_const_iterator<std::pair<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const, unsigned long> >, std::piecewise_construct_t const&, std::tuple<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&>&&, std::tuple<>&&) [clone .isra.0]
```

<a id="p119"></a>

### P119 — std::_Rb_tree::template

```text
std::_Rb_tree_node_base* std::_Rb_tree<std::tuple<char, long, long>, std::tuple<char, long, long>, std::_Identity<std::tuple<char, long, long> >, std::less<std::tuple<char, long, long> >, std::allocator<std::tuple<char, long, long> > >::_M_copy<false, std::_Rb_tree<std::tuple<char, long, long>, std::tuple<char, long, long>, std::_Identity<std::tuple<char, long, long> >, std::less<std::tuple<char, long, long> >, std::allocator<std::tuple<char, long, long> > >::_Alloc_node>(std::_Rb_tree_node<std::tuple<char, long, long> >*, std::_Rb_tree_node_base*, std::_Rb_tree<std::tuple<char, long, long>, std::tuple<char, long, long>, std::_Identity<std::tuple<char, long, long> >, std::less<std::tuple<char, long, long> >, std::allocator<std::tuple<char, long, long> > >::_Alloc_node&) [clone .isra.0]
```

<a id="p120"></a>

### P120 — cert::Clip<Rational>

```text
std::array<boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1>, 4ul> ParallelRoam::Experiment::GreedyTransactionalLod::(anonymous namespace)::Clip<boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1> >(ParallelRoam::Experiment::GreedyTransactionalLod::Configuration const&, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1> const&, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1> const&, boost::multiprecision::number<boost::multiprecision::backends::rational_adaptor<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >, (boost::multiprecision::expression_template_option)1> const&)
```

<a id="p121"></a>

### P121 — Boost::multiprecision::default_ops::eval_karatsuba_sqrt

```text
void boost::multiprecision::default_ops::eval_karatsuba_sqrt<boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > >(boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> > const&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, boost::multiprecision::backends::cpp_int_backend<0ul, 0ul, (boost::multiprecision::cpp_integer_type)1, (boost::multiprecision::cpp_int_check_type)0, std::allocator<unsigned long long> >&, unsigned long)
```
