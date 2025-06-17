#include "llvm/CodeGen/HotMachineBasicBlockInfoGenerator.h"
#include "llvm/InitializePasses.h"
#include "llvm/CodeGen/MachineBlockHashInfo.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Transforms/Utils/CodeLayout.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/WithColor.h"
#include <unordered_set>
#include <llvm/Support/CommandLine.h>

using namespace llvm;

static cl::opt<bool> PropellerMatchInfer("propeller-match-infer", 
    cl::desc("Use match&infer to evaluate stale profile"), cl::init(false), cl::Optional);
static cl::opt<bool> PathClone("propeller-path-clone",
    cl::desc("Enable path clone for propeller"), cl::init(false), cl::Optional);
static cl::opt<float> PropellerInferThreshold("propeller-infer-threshold", 
    cl::desc("Threshold for infer stale profile"), cl::init(0.6), cl::Optional);

/// The object is used to identify and match basic blocks given their hashes.
class StaleMatcher {
public:
  /// Initialize stale matcher.
  void init(const std::vector<MachineBasicBlock *> &Blocks,
            const std::vector<BlendedBlockHash> &Hashes) {
    assert(Blocks.size() == Hashes.size() &&
           "incorrect matcher initialization");
    for (size_t I = 0; I < Blocks.size(); I++) {
      MachineBasicBlock *Block = Blocks[I];
      uint16_t OpHash = Hashes[I].OpcodeHash;
      OpHashToBlocks[OpHash].push_back(std::make_pair(Hashes[I], Block));
    }
  }

  /// Find the most similar block for a given hash.
  MachineBasicBlock *matchBlock(BlendedBlockHash BlendedHash) const {
    auto BlockIt = OpHashToBlocks.find(BlendedHash.OpcodeHash);
    if (BlockIt == OpHashToBlocks.end()) {
      return nullptr;
    }
    MachineBasicBlock *BestBlock = nullptr;
    uint64_t BestDist = std::numeric_limits<uint64_t>::max();
    for (auto It : BlockIt->second) {
      MachineBasicBlock *Block = It.second;
      BlendedBlockHash Hash = It.first;
      uint64_t Dist = Hash.distance(BlendedHash);
      if (BestBlock == nullptr || Dist < BestDist) {
        BestDist = Dist;
        BestBlock = Block;
      }
    }
    return BestBlock;
  }

private:
  using HashBlockPairType = std::pair<BlendedBlockHash, MachineBasicBlock *>;
  std::unordered_map<uint16_t, std::vector<HashBlockPairType>> OpHashToBlocks;
};

INITIALIZE_PASS_BEGIN(HotMachineBasicBlockInfoGenerator, "machine-block-match-infer",
                      "Machine Block Matching and Inference Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineBlockHashInfo)
INITIALIZE_PASS_DEPENDENCY(FuncHotBBHashesProfileReader)               
INITIALIZE_PASS_END(HotMachineBasicBlockInfoGenerator, "machine-block-match-infer",
                    "Machine Block Matching and Inference Analysis", true, true)

char HotMachineBasicBlockInfoGenerator::ID = 0;

HotMachineBasicBlockInfoGenerator::HotMachineBasicBlockInfoGenerator() : MachineFunctionPass(ID) {
    initializeHotMachineBasicBlockInfoGeneratorPass(*PassRegistry::getPassRegistry());
}

void HotMachineBasicBlockInfoGenerator::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineBlockHashInfo>();
  AU.addRequired<FuncHotBBHashesProfileReader>();
  AU.addRequired<MachineBlockFrequencyInfo>();
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

std::optional<SmallVector<MachineBasicBlock *, 4>> 
HotMachineBasicBlockInfoGenerator::getHotMBBs(StringRef FuncName) const {
  auto It = FuncToHotMBBs.find(FuncName);
  if (It == FuncToHotMBBs.end()) {
    return std::nullopt;
  }
  return It->second;
}

std::pair<bool, SmallVector<SmallVector<MachineBasicBlock *, 4>>>
HotMachineBasicBlockInfoGenerator::getMBBPathsCloningInfo(StringRef FuncName) const {
  auto& ProfileReader = getAnalysis<FuncHotBBHashesProfileReader>();
  auto R = FuncToMBBClonePaths.find(ProfileReader.getAliasName(FuncName));
  return R != FuncToMBBClonePaths.end()
              ? std::pair(true, R->second)
              : std::pair(false, SmallVector<SmallVector<MachineBasicBlock *, 4>>{});
}

std::pair<bool, SmallVector<SmallVector<unsigned>>>
HotMachineBasicBlockInfoGenerator::getBBIDPathsCloningInfo(StringRef FuncName) const {
  auto& ProfileReader = getAnalysis<FuncHotBBHashesProfileReader>();
  auto R = FuncToBBIDClonePaths.find(ProfileReader.getAliasName(FuncName));
  return R != FuncToBBIDClonePaths.end()
              ? std::pair(true, R->second)
              : std::pair(false, SmallVector<SmallVector<unsigned>>{});
}
    
SmallVector<SmallVector<std::pair<MachineBasicBlock *, MachineBasicBlock *>>>&
HotMachineBasicBlockInfoGenerator::getSuccNamesCloningInfo(StringRef FuncName) {
  auto &ProfileReader = getAnalysis<FuncHotBBHashesProfileReader>();
  return FuncToSuccBBIDClonePaths[ProfileReader.getAliasName(FuncName)];
}

void HotMachineBasicBlockInfoGenerator::layoutClonedMBBForFunction(MachineFunction &MF) {
  auto &ClonePaths = getSuccNamesCloningInfo(MF.getName());
  size_t cnt = 0;
  for (auto &ClonePath : ClonePaths) {
    MachineBasicBlock *Prev = nullptr;
    for (auto &[BaseMBB, ClonedMBB] : ClonePath) {
      // Handle the frequency of MBBs
      if (Prev == nullptr) {
        // The first block of path cloning info is not be cloned, it only indicates
        // which path is being processed. So we don't need to modify the frequency of 
        // the first block.
        Prev = BaseMBB;
        continue;
      }
      MBBToFreq[ClonedMBB] = MBBToFreq[Prev];
      MBBToFreq[BaseMBB] = MBBToFreq[BaseMBB] > MBBToFreq[ClonedMBB] ? 
                           MBBToFreq[BaseMBB] - MBBToFreq[ClonedMBB] : 1;

      // Handle the successors of MBBs
      for (auto &Succ : Successors[BaseMBB])
        Successors[ClonedMBB].push_back(Succ);

      auto &SuccessorList = Successors[Prev];
      auto it = std::find(SuccessorList.begin(), SuccessorList.end(), BaseMBB);
      assert(it != SuccessorList.end() && "BaseMBB not found in SuccessorList");
      SuccessorList.erase(it);
      SuccessorList.push_back(ClonedMBB);

      HotBBs.push_back(ClonedMBB);
      Prev = ClonedMBB;
      cnt++;
    }
  }
  if (cnt != 0)
    WithColor::note() << "Cloned " << cnt << " MBB for function" << MF.getName() << "\n";

  SampleProfileInference<MachineFunction> SPI(MF, Successors, MBBToFreq);
  BlockWeightMap BlockWeights;
  EdgeWeightMap EdgeWeights;
  SPI.apply(BlockWeights, EdgeWeights);
  generateHotBBsforFunction(MF, MBBToFreq, BlockWeights, EdgeWeights, HotBBs);
  return;
}

void HotMachineBasicBlockInfoGenerator::matchHotBBsByHashes(
    MachineFunction &MF, 
    SmallVector<HotBBInfo, 4> &HotMBBInfos,
    BlockWeightMap &MBBToFreq, 
    BlockEdgeMap &Successors,
    SmallVector<MachineBasicBlock *, 4> &HotBBs) {
  std::vector<MachineBasicBlock *> Blocks;
  std::vector<BlendedBlockHash> Hashes;
  for (auto &Block : MF) {
    Blocks.push_back(&Block);
    Hashes.push_back(BlendedBlockHash(Block.getHash()));
    for (auto *Succ : Block.successors()) {
      Successors[&Block].push_back(Succ);
    }
  }
  StaleMatcher Matcher;
  Matcher.init(Blocks, Hashes);
  for (auto &item : HotMBBInfos) {
    MachineBasicBlock *Block 
        = Matcher.matchBlock(BlendedBlockHash(item.BBHash));
    if (Block != nullptr) {
      HotBBs.push_back(Block);
      MBBToFreq[Block] = item.Freq;
    }
  }
}

void HotMachineBasicBlockInfoGenerator::generateHotBBsforFunction(
    MachineFunction &MF,
    BlockWeightMap &OriBlockWeights,
    BlockWeightMap &BlockWeights, 
    EdgeWeightMap &EdgeWeights,
    SmallVector<MachineBasicBlock *, 4> &HotBBs) {
  if (!PropellerMatchInfer && !PathClone) {
    for (auto MBB : HotBBs) {
      if (MBB->isEntryBlock() || OriBlockWeights[MBB] > 0) {
        FuncToHotMBBs[MF.getName()].push_back(MBB);
      }
    }
    return;
  }
  std::vector<uint64_t> BlockSizes;
  std::vector<uint64_t> BlockCounts;
  std::vector<MachineBasicBlock *> OrigOrder;
  using JumpT = std::pair<uint64_t, uint64_t>;
  std::vector<std::pair<JumpT, uint64_t>> JumpCounts;

  if (MF.size() <= 2) {
    for (auto &MBB : MF) {
      if (MBB.isEntryBlock() || BlockWeights[&MBB] > 0) {
        FuncToHotMBBs[MF.getName()].push_back(&MBB);
      }
    }
    return;
  }

  MF.RenumberBlocks();

  // Init the MBB size and count.
  for (auto &MBB : MF) {
    auto NonDbgInsts =
        instructionsWithoutDebug(MBB.instr_begin(), MBB.instr_end());
    int NumInsts = std::distance(NonDbgInsts.begin(), NonDbgInsts.end());
    BlockSizes.push_back(4 * NumInsts);
    BlockCounts.push_back(BlockWeights[&MBB]);
    OrigOrder.push_back(&MBB);
  }
  
  // Init the edge count.
  for (auto &MBB : MF) {
    for (auto *Succ : MBB.successors()) {
      auto Jump = std::make_pair(MBB.getNumber(), Succ->getNumber());
      auto EdgeWeight = EdgeWeights[std::make_pair(&MBB, Succ)];
      JumpCounts.push_back(std::make_pair(Jump, EdgeWeight));
    }
  }
  
  // Run the layout algorithm
  auto Result = applyExtTspLayout(BlockSizes, BlockCounts, JumpCounts);
  for (uint64_t R : Result) {
    auto Block = OrigOrder[R];
    if (Block->isEntryBlock() || BlockWeights[Block] > 0)
      FuncToHotMBBs[MF.getName()].push_back(Block);
  }
}

void HotMachineBasicBlockInfoGenerator::matchMBBClonePathsByHashes(
    MachineFunction &MF,
    SmallVector<SmallVector<uint64_t, 4>> &HashPathsCloningInfo) {
  std::vector<MachineBasicBlock *> Blocks;
  std::vector<BlendedBlockHash> Hashes;
  auto& ProfileReader = getAnalysis<FuncHotBBHashesProfileReader>();
  for (auto &Block : MF) {
    Blocks.push_back(&Block);
    Hashes.push_back(BlendedBlockHash(Block.getHash()));
  }
  StaleMatcher Matcher;
  Matcher.init(Blocks, Hashes);
  for (auto &Path : HashPathsCloningInfo) {
    SmallVector<MachineBasicBlock *, 4> MBBClonePath;
    for (auto &item : Path) {
      MachineBasicBlock *Block 
          = Matcher.matchBlock(BlendedBlockHash(item));
      if (Block != nullptr) {
        MBBClonePath.push_back(Block);
      }
    }
    FuncToMBBClonePaths[ProfileReader.getAliasName(MF.getName())].push_back(MBBClonePath);
  }
}

void HotMachineBasicBlockInfoGenerator::matchBBIDClonePathsByHashes(
    MachineFunction &MF,
    SmallVector<SmallVector<uint64_t, 4>> &HashPathsCloningInfo) {
  matchMBBClonePathsByHashes(MF, HashPathsCloningInfo);
  auto& ProfileReader = getAnalysis<FuncHotBBHashesProfileReader>();
  for (auto &item : FuncToMBBClonePaths[MF.getName()]) {
    SmallVector<unsigned> BBIDClonePath;
    for (auto &MBB : item) {
      BBIDClonePath.push_back(MBB->getNumber());
    }
    FuncToBBIDClonePaths[ProfileReader.getAliasName(MF.getName())].push_back(BBIDClonePath);
  }
}

bool HotMachineBasicBlockInfoGenerator::runOnMachineFunction(MachineFunction &MF) {
  MBBToFreq.clear();
  Successors.clear();
  HotBBs.clear();
  auto [FindFlag, HotMBBInfos]
    = getAnalysis<FuncHotBBHashesProfileReader>()
    .getHotBBInfosForFunction(MF.getName());
  if (!FindFlag || MF.size() == 0) {
    return false;
  }
  matchHotBBsByHashes(MF, HotMBBInfos, MBBToFreq, Successors, HotBBs);

  // If the ratio of the number of MBBs in matching to the total number of MBBs in the 
  // function is less than the threshold value, the processing should be abandoned.
  if (static_cast<float>(HotBBs.size()) / MF.size() < PropellerInferThreshold) {
    return false;
  }

  auto [Flag, HashPathsCloningInfo]
    = getAnalysis<FuncHotBBHashesProfileReader>()
    .getHashPathsCloningInfo(MF.getName());
  if (!Flag || MF.size() == 0) {
    return false;
  }
  matchBBIDClonePathsByHashes(MF, HashPathsCloningInfo);
  return false;
}

MachineFunctionPass *llvm::createHotMachineBasicBlockInfoGeneratorPass() {
  return new HotMachineBasicBlockInfoGenerator();
}