#include "llvm/CodeGen/FuncHotBBHashesProfileReader.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/LineIterator.h"
#include <fstream>

using namespace llvm;

char FuncHotBBHashesProfileReader::ID = 0;
INITIALIZE_PASS(FuncHotBBHashesProfileReader, "func-hotbb-hashes-reader",
                "Read and parse the hashes of hot basic blocks for function.", false,
                false)

FuncHotBBHashesProfileReader::FuncHotBBHashesProfileReader(const std::string PropellerProfile) 
    : ImmutablePass(ID), PropellerFilePath(PropellerProfile) {
  initializeFuncHotBBHashesProfileReaderPass(
    *PassRegistry::getPassRegistry());
}

FuncHotBBHashesProfileReader::FuncHotBBHashesProfileReader() : ImmutablePass(ID) {
  initializeFuncHotBBHashesProfileReaderPass(
    *PassRegistry::getPassRegistry());
}

std::pair<bool, SmallVector<HotBBInfo, 4>>
FuncHotBBHashesProfileReader::getHotBBInfosForFunction(StringRef FuncName) const {
    auto R = FuncToHotBBHashes.find(getAliasName(FuncName));
    return R != FuncToHotBBHashes.end()
                ? std::pair(true, R->second)
                : std::pair(false, SmallVector<HotBBInfo, 4>{});
}

std::pair<bool, SmallVector<SmallVector<uint64_t, 4>>>
FuncHotBBHashesProfileReader::getHashPathsCloningInfo(StringRef FuncName) const {
    auto R = FuncToHashPathsCloningInfo.find(getAliasName(FuncName));
    return R != FuncToHashPathsCloningInfo.end()
                ? std::pair(true, R->second)
                : std::pair(false, SmallVector<SmallVector<uint64_t, 4>>{});
}

// Reads the basic block frequency with hash profile for functions in this module.
// The profile record the map from basic block hash to basic block frequency and 
// path cloning info of each function. The profile format looks like this:
// ---------------------------------
// !foo
// !!0x123 156 0
// !!0x456 300 2
// !!!0x111 0x222
// !!!0x333 0x444 0x555
Error FuncHotBBHashesProfileReader::ReadProfile() {
  ErrorOr<std::unique_ptr<MemoryBuffer>> buffer = MemoryBuffer::getFile(PropellerFilePath);
  if (!buffer) {
    return make_error<StringError>(Twine("Invalid propeller profile."), inconvertibleErrorCode());
  }
  MBuf = std::move(*buffer);
  line_iterator LineIt(*MBuf, /*SkipBlanks=*/true, /*CommentMarker=*/'#');

  auto invalidProfileError = [&](auto Message) {
    return make_error<StringError>(
        Twine("Invalid profile " + MBuf->getBufferIdentifier() + " at line " +
              Twine(LineIt.line_number()) + ": " + Message),
        inconvertibleErrorCode());
  };

  auto FI = FuncToHotBBHashes.end();
  auto PI = FuncToHashPathsCloningInfo.end();

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef S(*LineIt);
    // Check for the leading "!"
    if (!S.consume_front("!") || S.empty())
      break;
    // Check for the situation of "!!!", handle path cloning info
    if (S.consume_front("!!")) {
      if (PI == FuncToHashPathsCloningInfo.end())
        continue;
      
      SmallVector<uint64_t, 4> PathCloningInfo;
      SmallVector<StringRef, 4> StrPathCloningInfo;
      S.split(StrPathCloningInfo, ' ');
      for (auto &Info : StrPathCloningInfo) {
        unsigned long long Hash;
        if (getAsUnsignedInteger(Info, 16, Hash)) {
          return invalidProfileError(Twine("Unsigned integer expected: '") +
                                        Info + "'.");
        }
        PathCloningInfo.push_back(Hash);
      }
      PI->second.push_back(PathCloningInfo);
    }
    // Check for second "!" which indicates a basic block hash.
    else if (S.consume_front("!")) {
      // Skip the profile when we the profile iterator (FI) refers to the
      // past-the-end element.
      if (FI == FuncToHotBBHashes.end())
        continue;
      SmallVector<StringRef, 3> BBHashes;
      S.split(BBHashes, ' ');
      if (BBHashes.size() != 3) {
        return invalidProfileError("Unexpected elem number.");
      }
      unsigned long long Hash, Freq;
      BBHashes[0].consume_front("0x");
      if (getAsUnsignedInteger(BBHashes[0], 16, Hash)) {
        return invalidProfileError(Twine("Unsigned integer expected: '") +
                                      BBHashes[0] + "'.");
      }
      if (getAsUnsignedInteger(BBHashes[1], 10, Freq)) {
        return invalidProfileError(Twine("Unsigned integer expected: '") +
                                      BBHashes[1] + "'.");
      }
      auto It = std::find_if(FI->second.begin(), FI->second.end(), 
          [Hash](HotBBInfo &BBInfo) { return BBInfo.BBHash == Hash; });
      if (It == FI->second.end())
        FI->second.push_back({Hash, Freq});
    } else {
      // This is a function name specifier. 
      auto [AliasesStr, TotalBBSize] = S.split(' ');
      // Function aliases are separated using '/'. We use the first function
      // name for the cluster info mapping and delegate all other aliases to
      // this one.
      SmallVector<StringRef, 4> Aliases;
      AliasesStr.split(Aliases, '/');
      for (size_t i = 1; i < Aliases.size(); ++i)
        FuncAliasMap.try_emplace(Aliases[i], Aliases.front());

      // Prepare for parsing clusters of this function name.
      // Start a new cluster map for this function name.
      auto R = FuncToHotBBHashes.try_emplace(Aliases.front());
      auto H = FuncToHashPathsCloningInfo.try_emplace(Aliases.front());

      // Report error when multiple profiles have been specified for the same
      // function.
      if (!R.second)
        return invalidProfileError("Duplicate profile for function '" +
                                   Aliases.front() + "'.");
      FI = R.first;
      PI = H.first;
    }
  }
  return Error::success();
}

bool FuncHotBBHashesProfileReader::doInitialization(Module &M) {
  if (PropellerFilePath.empty())
    return false;
  if (auto Err = ReadProfile())
    report_fatal_error(std::move(Err));
  return false;
}

ImmutablePass *
llvm::createFuncHotBBHashesProfileReaderPass(const std::string PropellerProfile) {
  return new FuncHotBBHashesProfileReader(PropellerProfile);
}

