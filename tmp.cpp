/******************************************************************************
 * MyNewPMHBM.cpp
 *
 * 演示一个更完整的 LLVM 新 PassManager 插件示例，整合了：
 *   - unmatched free 处理
 *   - OpenMP 并行加分
 *   - AliasAnalysis 去重
 *   - Metadata 注解, Profile Gating
 *   - 最终替换 malloc->hbm_malloc, free->hbm_free
 *
 * 需要根据你的实际情况在 CMake/Build 上配置搜索 LLVM 路径，并编译成 .so 插件。
 ******************************************************************************/

 #include "llvm/Passes/PassBuilder.h"
 #include "llvm/Passes/PassPlugin.h"
 
 #include "llvm/IR/PassManager.h"
 #include "llvm/IR/Module.h"
 #include "llvm/IR/Function.h"
 #include "llvm/IR/Instructions.h"
 #include "llvm/IR/Metadata.h"
 #include "llvm/IR/DerivedTypes.h"
 #include "llvm/IR/IRBuilder.h"
 #include "llvm/IR/DebugInfoMetadata.h"
 
 #include "llvm/Analysis/LoopAnalysis.h"
 #include "llvm/Analysis/ScalarEvolution.h"
 #include "llvm/Analysis/AliasAnalysis.h"
 #include "llvm/Analysis/TargetLibraryInfo.h"
 #include "llvm/Analysis/OptimizationRemarkEmitter.h"
 
 #include "llvm/Support/CommandLine.h"
 #include "llvm/Support/raw_ostream.h"
 
 #include <vector>
 #include <string>
 #include <algorithm>
 #include <unordered_set>
 #include <unordered_map>
 #include <memory>
 #include <cmath>
 
 using namespace llvm;
 
 /******************************************************************************
  * 0. 数据结构
  ******************************************************************************/
 
 /// 记录单个 malloc 调用点的分析结果
 struct MallocRecord {
   CallInst *MallocCall = nullptr;          // malloc指令
   double Score = 0.0;                      // 静态分析评分
   uint64_t AllocSize = 0;                  // 分配大小(若能解析)
   bool UserForcedHot = false;              // 是否用户/metadata强制hot
   bool UnmatchedFree = false;              // 若无法找到对应的free
   std::vector<CallInst*> FreeCalls;        // 匹配到的 free 指令
 
   // 也可在这里添加“Escaped”字段，用于跨函数或多次传递的场景
 };
 
 /// 函数级的分析结果
 struct FunctionMallocInfo {
   std::vector<MallocRecord> MallocRecords;
 };
 
 /******************************************************************************
  * 1. 函数级分析Pass (新PM) - MyFunctionAnalysisPass
  *
  *   - 扫描 malloc, free
  *   - 对 malloc 进行静态打分：循环深度、写/读次数、OpenMP 并行、profile gating、metadata等
  *   - 若 free 未匹配到 => 标记 unmatched => 可在此处扣分(示例)
  *   - 结合 AliasAnalysis，避免多个别名指针的重复计分
  ******************************************************************************/
 namespace {
 class MyFunctionAnalysisPass : public AnalysisInfoMixin<MyFunctionAnalysisPass> {
 public:
   using Result = FunctionMallocInfo;  // 分析结果类型
 
   Result run(Function &F, FunctionAnalysisManager &FAM);
 
 private:
   friend AnalysisInfoMixin<MyFunctionAnalysisPass>;
   static AnalysisKey Key;
 
   // 辅助函数
   double analyzeMalloc(CallInst *CI, Function &F,
                        LoopAnalysis::Result &LA,
                        ScalarEvolution &SE,
                        AAResults &AA);
 
   void matchFreeCalls(FunctionMallocInfo &FMI,
                       std::vector<CallInst*> &freeCalls);
 
   // Access/loop/alias
   void explorePointerUsers(Value *RootPtr, Value *V,
                            LoopAnalysis::Result &LA,
                            ScalarEvolution &SE,
                            AAResults &AA,
                            double &Score,
                            std::unordered_set<Value*> &Visited);
 
   double computeAccessScore(Instruction *I,
                             LoopAnalysis::Result &LA,
                             ScalarEvolution &SE,
                             AAResults &AA,
                             bool isWrite);
 
   uint64_t getLoopTripCount(Loop *L, ScalarEvolution &SE);
 };
 
 AnalysisKey MyFunctionAnalysisPass::Key;
 
 /******************************************************************************
  * run()：分析一个函数
  ******************************************************************************/
 MyFunctionAnalysisPass::Result
 MyFunctionAnalysisPass::run(Function &F, FunctionAnalysisManager &FAM) {
   FunctionMallocInfo FMI;
 
   // 获取必要的分析
   auto &LA = FAM.getResult<LoopAnalysis>(F);
   auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
   auto &AA = FAM.getResult<AAManager>(F);
   // auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F); //可选
 
   // 收集本函数中的 free
   std::vector<CallInst*> freeCalls;
 
   // 遍历指令, 识别 malloc/free
   for (auto &BB : F) {
     for (auto &I : BB) {
       if (auto *CI = dyn_cast<CallInst>(&I)) {
         if (Function *Callee = CI->getCalledFunction()) {
           auto Name = Callee->getName();
           if (Name == "malloc") {
             // 记录
             MallocRecord MR;
             MR.MallocCall = CI;
 
             // 分配大小
             if (CI->arg_size() >= 1) {
               if (auto *Cst = dyn_cast<ConstantInt>(CI->getArgOperand(0))) {
                 MR.AllocSize = Cst->getZExtValue();
               }
             }
             // 检查metadata -> "hot_mem"
             if (CI->hasMetadata("hot_mem")) {
               MR.UserForcedHot = true;
             }
             // 如果函数层级有属性 "hot_mem" (e.g. F.hasFnAttribute("hot_mem"))
             if (F.hasFnAttribute("hot_mem")) {
               MR.UserForcedHot = true;
             }
             // 计算打分
             MR.Score = analyzeMalloc(CI, F, LA, SE, AA);
 
             FMI.MallocRecords.push_back(MR);
 
           } else if (Name == "free") {
             freeCalls.push_back(CI);
           }
         }
       }
     }
   }
 
   // 匹配free
   matchFreeCalls(FMI, freeCalls);
 
   return FMI;
 }
 
 /******************************************************************************
  * 分析单个 malloc 调用点: 处理Profile Gating、OpenMP等
  ******************************************************************************/
 double MyFunctionAnalysisPass::analyzeMalloc(CallInst *CI, Function &F,
                                              LoopAnalysis::Result &LA,
                                              ScalarEvolution &SE,
                                              AAResults &AA) {
   double Score = 0.0;
 
   // (1) 基础：分配大小
   if (CI->arg_size() >= 1) {
     if (auto *Cst = dyn_cast<ConstantInt>(CI->getArgOperand(0))) {
       double kb = (double)Cst->getZExtValue() / 1024.0;
       Score += kb * 0.1; // 每KB加0.1分
     }
   }
 
   // (2) Metadata: Profile Gating
   //    比如在 IR 中给malloc CallInst 赋值 !prof.memusage = <some number>
   if (MDNode *ProfMD = CI->getMetadata("prof.memusage")) {
     // 简单解析：第一个操作数为ConstantAsMetadata => int
     if (auto *Op = dyn_cast<ConstantAsMetadata>(ProfMD->getOperand(0))) {
       if (auto *CInt = dyn_cast<ConstantInt>(Op->getValue())) {
         uint64_t usage = CInt->getZExtValue();
         // 表示某种插桩时记录的实际访问次数/大小
         // 这里额外+ sqrt(usage)/10 之类的分数
         Score += (std::sqrt((double)usage) / 10.0);
       }
     }
   }
 
   // (3) 如果检测到OpenMP(示例)
   //    这里仅检查函数是否带 "openmp" 属性
   if (F.hasFnAttribute("openmp")) {
     Score += 20.0;  // 并行函数加20分
   }
   // 或者检测循环metadata：llvm.loop.parallel_accesses
   // => 在 computeAccessScore() 里加分
 
   // (4) 遍历指针use，统计Load/Store, 并用AliasAnalysis避免重复计分
   std::unordered_set<Value*> visited;
   explorePointerUsers(CI, CI, LA, SE, AA, Score, visited);
 
   return Score;
 }
 
 /******************************************************************************
  * 为本函数找到对应的 free；若没找到 => unmatched => 可能扣分
  ******************************************************************************/
 void MyFunctionAnalysisPass::matchFreeCalls(FunctionMallocInfo &FMI,
                                             std::vector<CallInst*> &freeCalls) {
   // 简单场景：如果 free(Ptr) 的实参 == MallocCall，就认为匹配
   for (auto &MR : FMI.MallocRecords) {
     Value *mallocPtr = MR.MallocCall;
     bool matched = false;
     for (auto *fc : freeCalls) {
       if (fc->arg_size() == 1) {
         Value *freeArg = fc->getArgOperand(0);
         if (freeArg == mallocPtr) {
           MR.FreeCalls.push_back(fc);
           matched = true;
         }
       }
     }
     // 若依然没匹配到 => unmatched
     if (!matched) {
       MR.UnmatchedFree = true;
       // 视为逃逸 => 这里做一个小扣分(示例：-10 分)
       // 亦可在后面再处理
       for (auto &m : FMI.MallocRecords) {
         if (m.MallocCall == MR.MallocCall) {
           m.Score -= 10.0;
         }
       }
     }
   }
 }
 
 /******************************************************************************
  * explorePointerUsers：递归遍历所有Use
  *   - 识别Load/Store
  *   - 识别其他调用 => 可能是逃逸
  *   - 使用AliasAnalysis避免重复计分
  ******************************************************************************/
 void MyFunctionAnalysisPass::explorePointerUsers(Value *RootPtr,
                                                  Value *V,
                                                  LoopAnalysis::Result &LA,
                                                  ScalarEvolution &SE,
                                                  AAResults &AA,
                                                  double &Score,
                                                  std::unordered_set<Value*> &Visited) {
   if (Visited.count(V))
     return;
   Visited.insert(V);
 
   for (auto *U : V->users()) {
     auto *I = dyn_cast<Instruction>(U);
     if (!I) continue;
 
     // 先检查别名(示例性处理):
     //   如果 alias(RootPtr, V) == NoAlias，就可能是不同内存 => 不计
     //   (当然 RootPtr 和 V 本身肯定 must alias, 这里更适合
     //    当我们检测到别的指针 P 时, with AA.alias(RootPtr, P)).
     //   但为了演示, 这里只是说明思路:
     // if (AA.alias(RootPtr, MemoryLocation::UnknownSize,
     //              V, MemoryLocation::UnknownSize) == NoAlias) {
     //   continue; // 不计分
     // }
 
     // 如果是 Load
     if (auto *LD = dyn_cast<LoadInst>(I)) {
       Score += computeAccessScore(LD, LA, SE, AA, false /*isWrite*/);
     }
     // 如果是 Store
     else if (auto *ST = dyn_cast<StoreInst>(I)) {
       // Check pointer operand
       if (ST->getPointerOperand() == V) {
         Score += computeAccessScore(ST, LA, SE, AA, true /*isWrite*/);
       }
     }
     // 如果是其他 call => 可能逃逸, 这里加分/扣分看需求
     else if (auto *CallI = dyn_cast<CallInst>(I)) {
       Score += 5.0; // 简单加5分
     }
     // GEP, BitCast, PHI => 继续递归
     else if (auto *GEP = dyn_cast<GetElementPtrInst>(I)) {
       explorePointerUsers(RootPtr, GEP, LA, SE, AA, Score, Visited);
     }
     else if (auto *BC = dyn_cast<BitCastInst>(I)) {
       explorePointerUsers(RootPtr, BC, LA, SE, AA, Score, Visited);
     }
     else if (auto *PN = dyn_cast<PHINode>(I)) {
       explorePointerUsers(RootPtr, PN, LA, SE, AA, Score, Visited);
     }
     // ...
   }
 }
 
 /******************************************************************************
  * computeAccessScore：根据循环深度、迭代数、是否写等计算一次访问得分
  *   - 如果检测到 loop metadata "llvm.loop.parallel_accesses"，再额外加分
  ******************************************************************************/
 double MyFunctionAnalysisPass::computeAccessScore(Instruction *I,
                                                   LoopAnalysis::Result &LA,
                                                   ScalarEvolution &SE,
                                                   AAResults &AA,
                                                   bool isWrite) {
   double base = (isWrite ? 8.0 : 5.0);
 
   BasicBlock *BB = I->getParent();
   Loop *L = LA.getLoopFor(BB);
 
   int depth = 0;
   uint64_t tripCount = 1;
   double parallelBonus = 0.0;
 
   if (L) {
     depth = LA.getLoopDepth(BB);
     tripCount = getLoopTripCount(L, SE);
 
     // 检测loop元数据
     if (MDNode *LoopMD = L->getLoopID()) {
       // 查找 "llvm.loop.parallel_accesses"
       for (unsigned i = 0, e = LoopMD->getNumOperands(); i < e; ++i) {
         if (auto *Sub = dyn_cast<MDNode>(LoopMD->getOperand(i))) {
           if (auto *S = dyn_cast<MDString>(Sub->getOperand(0))) {
             if (S->getString().equals("llvm.loop.parallel_accesses")) {
               // 并行循环 => 加额外权重
               parallelBonus = 10.0;
             }
           }
         }
       }
     }
   }
 
   // 简易公式: base * (depth+1) * sqrt(tripCount) + parallelBonus
   // （随意举例，视需求调参）
   double result = base * (depth + 1) * std::sqrt((double)tripCount) + parallelBonus;
   return result;
 }
 
 /// 获取循环迭代次数(若能解析为常量)
 uint64_t MyFunctionAnalysisPass::getLoopTripCount(Loop *L, ScalarEvolution &SE) {
   if (!L) return 1;
   const SCEV *BEC = SE.getBackedgeTakenCount(L);
   if (auto *SC = dyn_cast<SCEVConstant>(BEC)) {
     const APInt &val = SC->getAPInt();
     if (!val.isMaxValue()) {
       return val.getZExtValue() + 1;
     }
   }
   return 1;
 }
 
 /******************************************************************************
  * 2. 模块级Pass - MyModuleTransformPass
  *
  *   - 汇总所有函数的 analysis 结果
  *   - 统一排序(用户强制Hot优先, Score降序)
  *   - 考虑HBM容量
  *   - 替换 malloc -> hbm_malloc, free -> hbm_free
  ******************************************************************************/
 namespace {
 class MyModuleTransformPass : public PassInfoMixin<MyModuleTransformPass> {
 public:
   PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
 
 private:
   static constexpr uint64_t DefaultHBMCapacity = 1ULL << 30; // 1GB
 
   void processMallocRecords(Module &M,
                             SmallVectorImpl<MallocRecord> &AllMallocs);
 };
 } // end anonymous namespace
 
 PreservedAnalyses
 MyModuleTransformPass::run(Module &M, ModuleAnalysisManager &MAM) {
   // 1) 汇总所有函数的 FunctionMallocInfo
   SmallVector<MallocRecord, 16> AllMallocs;
 
   for (Function &F : M) {
     if (F.isDeclaration()) continue;
 
     // 从 MAM 获取“函数级分析”结果
     auto &FMI = MAM.getResult<MyFunctionAnalysisPass>(F);
     for (auto &MR : FMI.MallocRecords) {
       AllMallocs.push_back(MR);
     }
   }
 
   // 2) 全局处理
   processMallocRecords(M, AllMallocs);
 
   // 假设做了修改
   return PreservedAnalyses::none();
 }
 
 /******************************************************************************
  * 对AllMallocs做排序、容量限制、替换
  ******************************************************************************/
 void MyModuleTransformPass::processMallocRecords(Module &M,
        SmallVectorImpl<MallocRecord> &AllMallocs) {
   // (A) 排序: 先看UserForcedHot，再看Score
   std::sort(AllMallocs.begin(), AllMallocs.end(),
     [](const MallocRecord &A, const MallocRecord &B) {
       if (A.UserForcedHot != B.UserForcedHot)
         return A.UserForcedHot > B.UserForcedHot;
       return A.Score > B.Score;
     }
   );
 
   // (B) HBM容量
   uint64_t used = 0ULL;
   uint64_t capacity = DefaultHBMCapacity;
 
   LLVMContext &Ctx = M.getContext();
   auto *Int64Ty   = Type::getInt64Ty(Ctx);
   auto *Int8PtrTy = Type::getInt8PtrTy(Ctx);
   auto *VoidTy    = Type::getVoidTy(Ctx);
 
   // 声明/获取 hbm_malloc, hbm_free
   FunctionCallee HBMAlloc =
       M.getOrInsertFunction("hbm_malloc",
         FunctionType::get(Int8PtrTy, {Int64Ty}, false));
 
   FunctionCallee HBMFree =
       M.getOrInsertFunction("hbm_free",
         FunctionType::get(VoidTy, {Int8PtrTy}, false));
 
   // (C) 逐个替换
   for (auto &MR : AllMallocs) {
     if (!MR.MallocCall) continue; // 防御
 
     // 如果 score 太低，且又不是强制hot，就跳过
     if (!MR.UserForcedHot && MR.Score < 80.0) {
       continue;
     }
     // 看 HBM 容量(非强制hot)
     if (!MR.UserForcedHot && (used + MR.AllocSize > capacity)) {
       continue;
     }
 
     // 替换
     MR.MallocCall->setCalledFunction(HBMAlloc);
     used += MR.AllocSize;
 
     // free -> hbm_free
     for (auto *fc : MR.FreeCalls) {
       fc->setCalledFunction(HBMFree);
     }
   }
 
   // 可以在这里打印日志:
   // errs() << "[MyModuleTransformPass] Used " << used << "/" << capacity << " bytes in HBM.\n";
 }
 
 /******************************************************************************
  * 3. PassPlugin: 注册给新PM
  ******************************************************************************/
 extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
 llvmGetPassPluginInfo() {
   return {
     LLVM_PLUGIN_API_VERSION,
     "MyAdvancedHBMPlugin",
     LLVM_VERSION_STRING,
     [](PassBuilder &PB) {
       // 注册函数级分析
       PB.registerAnalysisRegistrationCallback(
         [&](FunctionAnalysisManager &FAM) {
           FAM.registerPass([&](){ return MyFunctionAnalysisPass(); });
         }
       );
 
       // 注册模块级转换：用户可通过 -passes="my-module-transform" 启用
       PB.registerPipelineParsingCallback(
         [&](StringRef Name, ModulePassManager &MPM,
             ArrayRef<PassBuilder::PipelineElement>) {
           if (Name == "my-module-transform") {
             MPM.addPass(MyModuleTransformPass());
             return true;
           }
           return false;
         }
       );
     }
   };
 }
 