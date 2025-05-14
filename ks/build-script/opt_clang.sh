#!/bin/bash
set -eux
DISABLE_OPENMP=false
CLEAN_BUILD=false
ENABLE_PGO_BOLT=false
BUILD_TYPE=Release
BUILD_LIBCXX=false
SKIP_TRUNK=false


for arg in "$@"; do
  case $arg in
    --build-debug)
      BUILD_TYPE=Debug
      shift
      ;;
    --libcxx-build)
      BUILD_LIBCXX=true
      shift
      ;;
    --disable-openmp)
      DISABLE_OPENMP=true
      shift
      ;;
    --clean-build)
      CLEAN_BUILD=true
      shift 
      ;;
    --enable-pgo-bolt)
      ENABLE_PGO_BOLT=true
      shift 
      ;;
    --skip-trunk)
      SKIP_TRUNK=true
      shift
      ;;
    *)
      ;;
  esac
done




# Set the base directory
CWD="$(pwd)"

BASE_DIR=${CWD}/clang_pgo_bolt_binaries

# If base directory exists, then move it over.
if [[ -d "${BASE_DIR}" ]]; then
    rm -rf ${BASE_DIR}.old
    mv ${BASE_DIR} ${BASE_DIR}.old
fi

mkdir -p "${BASE_DIR}"
# The tmp directory is usually mounted in the / directory,
# and bolt calling `perf script` is prone to insufficient disk space.
TMP_DIR="${BASE_DIR}/tmp"
mkdir -p ${TMP_DIR}
export TMPDIR=${TMP_DIR}

MAKER="Unix Makefiles"
DEFAULT_MAKE_JOB=$[$(lscpu  | grep -E "^CPU\(s\):" | awk '{ print $2 }') / 2 + 1]
# MAKER="Ninja"
MAKE_JOB=${CUSTOM_MAKE_JOB-$DEFAULT_MAKE_JOB}
MAKE="make"
CMAKE="cmake3"

PATH_TO_LLVM_SOURCES=${CWD}
# The build of LLVM used to build other binaries
PATH_TO_TRUNK_LLVM_BUILD=${BASE_DIR}/trunk_llvm_build
PATH_TO_TRUNK_LLVM_INSTALL=${BASE_DIR}/trunk_llvm_install
# This is used to collect profiles and benchmark the different clang binaries.
# Benchmarking recipe:  Use the clang binary to build clang.
BENCHMARKING_CLANG_BUILD=${BASE_DIR}/benchmarking_clang_build
# The pristine baseline clang binary built with PGO and ThinLTO.
PATH_TO_PGO_BOLT_BUILD=${BASE_DIR}/pgo_bolt_build
PATH_TO_OPT_INSTALL=${BASE_DIR}/optimized_intall

PATH_TO_BASE_BUILD=${BASE_DIR}/base_build
PATH_TO_BASE_INSTALL=${BASE_DIR}/base_intall

PATH_TO_LIBCXX_BUILD=${BASE_DIR}/libcxx_build
PATH_TO_LIBCXX_INSTALL=${BASE_DIR}/libcxx_intall

# Symlink all binaries here
PATH_TO_ALL_BINARIES=${BASE_DIR}/PreBuiltBinaries
# Path to all profiles
PATH_TO_PROFILES=${BASE_DIR}/Profiles
# Results Directory
PATH_TO_ALL_RESULTS=${BASE_DIR}/Results
PATH_TO_BOLT_BIN=${PATH_TO_ALL_BINARIES}/llvm/llvm-bolt/bin


mkdir -p ${PATH_TO_ALL_RESULTS}
date > ${PATH_TO_ALL_RESULTS}/script_start_time.txt

mkdir -p ${PATH_TO_ALL_BINARIES}
wget https://artifact.corp.kuaishou.com/api/repo/client/linux/amd64/cvn -O "${PATH_TO_ALL_BINARIES}/cvn"
chmod 755 "${PATH_TO_ALL_BINARIES}/cvn"


if [ ! -d ${PATH_TO_BOLT_BIN} ]; then
  "${PATH_TO_ALL_BINARIES}/cvn" get kbuild_staging/toolchain-Linux-x86_64/llvm/5/llvm_5_.tgz ${PATH_TO_ALL_BINARIES}/
  cd ${PATH_TO_ALL_BINARIES}
  tar xzf llvm_5_.tgz
fi

ALL_PROJECTS="clang;lld"
ALL_RUNTIMES="libcxx;libcxxabi;compiler-rt"
COMPILER_RT_RUNTIMES="compiler-rt"
LIBCXX_RUNTIMES="libcxx;libcxxabi"

if [ ${DISABLE_OPENMP} != "true" ]; then
   ALL_PROJECTS="${ALL_PROJECTS};openmp"
   cd ${PATH_TO_ALL_BINARIES}
   if [ ! -a "gpu11.4" ]; then
      # 当前kbuild的环境里都安装了这个目录，没有的话再从cvn拉
      if [ -a "/data/soft/gpu11.4" ]; then
         ln -sf /data/soft/gpu11.4 .
      else
         "${PATH_TO_ALL_BINARIES}/cvn" get kbuild_staging/toolchain-Linux-x86_64/cuda11.4/1/cuda11.4_1_.tgz ${PATH_TO_ALL_BINARIES}/
         tar xzf cuda11.4_1_.tgz
      fi
   fi
fi

# 所有版本都使用构建选项
COMMON_CMAKE_FLAGS=(
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
  "-DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=ON"
  "-DLLVM_TARGETS_TO_BUILD=X86"
  "-DCUDA_TOOLKIT_ROOT_DIR=${PATH_TO_ALL_BINARIES}/gpu11.4/cuda/"
  "-DLLVM_ENABLE_BINDINGS=OFF" # wuminghui03: llvm-go run test fail, disable it
  "-DLLVM_PARALLEL_LINK_JOBS=1"
  "-DLLVM_STATIC_LINK_CXX_STDLIB=true"
)

# trunk can not use lld
TRUNK_CMAKE_FLAGS=(
  "-DCMAKE_BUILD_TYPE=Release"
  "-DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=ON"
  "-DLLVM_TARGETS_TO_BUILD=X86"
  "-DCUDA_TOOLKIT_ROOT_DIR=${PATH_TO_ALL_BINARIES}/gpu11.4/cuda/"
  "-DLLVM_ENABLE_BINDINGS=OFF" # wuminghui03: llvm-go run test fail, disable it
  "-DLLVM_PARALLEL_LINK_JOBS=1"
  "-DLLVM_STATIC_LINK_CXX_STDLIB=true"
  "-DLLVM_ENABLE_PROJECTS=clang;lld"
  "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi;compiler-rt"
  "-DCMAKE_INSTALL_PREFIX=${PATH_TO_TRUNK_LLVM_INSTALL}"
  "-DCMAKE_C_COMPILER=gcc"
  "-DCMAKE_CXX_COMPILER=g++"
  "-DLLVM_INCLUDE_TESTS=Off"
  "-DLIBOMP_ENABLE_SHARED=OFF"
)

if [ "${SKIP_TRUNK}" == "false" ]; then
# Build Trunk LLVM
  echo "=============== build trunk"
  mkdir -p ${PATH_TO_TRUNK_LLVM_BUILD} && cd ${PATH_TO_TRUNK_LLVM_BUILD}
  ${CMAKE} -G "${MAKER}" ${TRUNK_CMAKE_FLAGS[@]} ${PATH_TO_LLVM_SOURCES}/llvm
  ${MAKE} -j${MAKE_JOB} install
  echo "=============== build trunk end"
else
  cp -r ${BASE_DIR}.old/trunk_llvm_install ${BASE_DIR}/
  cp -r ${BASE_DIR}.old/trunk_llvm_build ${BASE_DIR}/
fi
CLANG_VERSION=$(sed -Ene 's!^CLANG_EXECUTABLE_VERSION:STRING=(.*)$!\1!p' ${PATH_TO_TRUNK_LLVM_BUILD}/CMakeCache.txt)


if [ "${ENABLE_PGO_BOLT}" != "true" ]; then
  # clang和libcxx分开构建管理
  if [ "${BUILD_LIBCXX}" == "false" ]; then 
    echo "=============== build base clang"
    BASE_CMAKE_FLAGS=(
      ${COMMON_CMAKE_FLAGS[@]}
      "-DCMAKE_C_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang"
      "-DCMAKE_CXX_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++"
      "-DLLVM_ENABLE_PROJECTS=${ALL_PROJECTS}"
      "-DLLVM_ENABLE_RUNTIMES=${COMPILER_RT_RUNTIMES}"
      "-DCMAKE_INSTALL_PREFIX=${PATH_TO_BASE_INSTALL}"
      "-DLLVM_USE_LINKER=lld"
      "-DLIBCXXABI_ENABLE_SHARED=ON"
      "-DLIBCXX_ENABLE_SHARED=ON"
      "-DLIBOMP_ENABLE_SHARED=OFF"
    )
    mkdir -p ${PATH_TO_BASE_BUILD} && cd ${PATH_TO_BASE_BUILD}
    ${CMAKE} -G "${MAKER}" "${BASE_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm
    ${MAKE} -j${MAKE_JOB} install
    echo "=============== build base clang end"
  else
    echo "=============== build libcxx"
    LIBCXX_CMAKE_FLAGS=(
      ${COMMON_CMAKE_FLAGS[@]}
      "-DCMAKE_C_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang"
      "-DCMAKE_CXX_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++"
      "-DLLVM_ENABLE_PROJECTS=clang"
      "-DLLVM_ENABLE_RUNTIMES=${LIBCXX_RUNTIMES}"
      "-DCMAKE_INSTALL_PREFIX=${PATH_TO_LIBCXX_INSTALL}"
      "-DLIBCXXABI_ENABLE_SHARED=ON"
      "-DLIBCXX_ENABLE_SHARED=ON"
    )
    mkdir -p ${PATH_TO_LIBCXX_BUILD} && cd ${PATH_TO_LIBCXX_BUILD}
    ${CMAKE} -G "${MAKER}" "${LIBCXX_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm
    ${MAKE} -j${MAKE_JOB} runtimes
    ${MAKE} -j${MAKE_JOB} install-runtimes 
    echo "=============== build libcxx end"
  fi
  exit 0
fi

# Build FDO/PGO Instrumented binary. (stage 2)
echo "=============== build PGO Instrumented binary (stage 2)"

PATH_TO_INSTRUMENTED_BINARY=${BASE_DIR}/clang_instrumented_build
INSTRUMENTED_CMAKE_FLAGS=(
  ${COMMON_CMAKE_FLAGS[@]}
  "-DLLVM_ENABLE_PROJECTS=clang;lld"
  "-DCMAKE_C_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang"
  "-DCMAKE_CXX_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++"
  "-DLLVM_BUILD_INSTRUMENTED=IR"
  "-DLIBOMP_ENABLE_SHARED=OFF"
  "-DLLVM_BUILD_RUNTIME=No"
  "-DLLVM_USE_LINKER=lld"
)

mkdir -p ${PATH_TO_INSTRUMENTED_BINARY} && cd ${PATH_TO_INSTRUMENTED_BINARY}
${CMAKE} -G "${MAKER}" "${INSTRUMENTED_CMAKE_FLAGS[@]}" "${PATH_TO_LLVM_SOURCES}/llvm"
${MAKE} -j${MAKE_JOB}

echo "=============== build PGO Instrumented binary (stage 2) end"


# Set up benchmarking clang BUILD, used to collect profiles.
echo "=============== build pgo benchmark"

mkdir -p ${BENCHMARKING_CLANG_BUILD} && cd ${BENCHMARKING_CLANG_BUILD}
mkdir -p symlink_to_clang_binary && cd symlink_to_clang_binary
ln -sf ${PATH_TO_INSTRUMENTED_BINARY}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_INSTRUMENTED_BINARY}/bin/clang-${CLANG_VERSION} clang++

# Setup ${CMAKE} for instrumented binary build.  The symlink allows us to replace
# with any clang binary of our choice.
BENCHMAKR_CMAKE_FLAGS=(
  ${COMMON_CMAKE_FLAGS[@]}
  "-DCMAKE_C_COMPILER=${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary/clang"
  "-DCMAKE_CXX_COMPILER=${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary/clang++"
  "-DLLVM_ENABLE_PROJECTS=clang"
  "-DLLVM_USE_LINKER=lld"
)

mkdir -p ${BENCHMARKING_CLANG_BUILD}/pgo_bench
cd ${BENCHMARKING_CLANG_BUILD}/pgo_bench
${CMAKE} -G "${MAKER}" "${BENCHMAKR_CMAKE_FLAGS[@]}"  ${PATH_TO_LLVM_SOURCES}/llvm
${MAKE} -j${MAKE_JOB} clang

echo "=============== collect pgo prof"

# Convert PGO instrumented profiles to profdata
cd ${PATH_TO_INSTRUMENTED_BINARY}/profiles
${PATH_TO_TRUNK_LLVM_BUILD}/bin/llvm-profdata merge -output=clang.profdata *
# Copy the instrumented profile for later use to repro the build.
mkdir -p ${PATH_TO_PROFILES}
cp ${PATH_TO_INSTRUMENTED_BINARY}/profiles/clang.profdata ${PATH_TO_PROFILES}

echo "=============== build pgo benchmark end"


# Enable ThinLTO too here.
echo "=============== build pgo and bolt"

PGO_BOLT_CMAKE_FLAGS=(
  ${COMMON_CMAKE_FLAGS[@]}
  "-DCMAKE_C_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang"
  "-DCMAKE_CXX_COMPILER=${PATH_TO_TRUNK_LLVM_INSTALL}/bin/clang++"
  "-DLLVM_ENABLE_PROJECTS=${ALL_PROJECTS}"
  "-DLLVM_ENABLE_RUNTIMES=${ALL_RUNTIMES}"
  "-DLLVM_USE_LINKER=lld"
  "-DLLVM_ENABLE_LTO=Thin"
  "-DLLVM_PROFDATA_FILE=${PATH_TO_INSTRUMENTED_BINARY}/profiles/clang.profdata"
  "-DCMAKE_INSTALL_PREFIX=${PATH_TO_OPT_INSTALL}"
  "-DLIBCXXABI_ENABLE_SHARED=ON"
  "-DLIBCXX_ENABLE_SHARED=ON"
)

PGO_BOLT_CC_LD_CMAKE_FLAGS=(
  "-DCMAKE_C_FLAGS=\"-funique-internal-linkage-names\""
  "-DCMAKE_CXX_FLAGS=\"-funique-internal-linkage-names\""
  "-DCMAKE_EXE_LINKER_FLAGS=\"-Wl,-q\""
)

mkdir -p ${PATH_TO_PGO_BOLT_BUILD} && cd ${PATH_TO_PGO_BOLT_BUILD}
${CMAKE} -G "${MAKER}" "${PGO_BOLT_CMAKE_FLAGS[@]}" "${PGO_BOLT_CC_LD_CMAKE_FLAGS[@]}" ${PATH_TO_LLVM_SOURCES}/llvm
${MAKE} -j${MAKE_JOB} install
cp bin/clang ${PATH_TO_ALL_BINARIES}/clang.pgo

echo "=============== build pgo end"


echo "=============== build bolt benchmark"

# Setup cmake for instrumented binary build.  The symlink allows us to replace
# with any clang binary of our choice.
cd ${BENCHMARKING_CLANG_BUILD}/symlink_to_clang_binary
ln -sf ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION} clang
ln -sf ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION} clang++

mkdir -p ${BENCHMARKING_CLANG_BUILD}/bolt_bench
cd ${BENCHMARKING_CLANG_BUILD}/bolt_bench
${CMAKE} -G "${MAKER}" "${BENCHMAKR_CMAKE_FLAGS[@]}"  ${PATH_TO_LLVM_SOURCES}/llvm
perf record -e cycles:u -j any,u -- ${MAKE} -j${MAKE_JOB} clang
cp perf.data ${PATH_TO_PROFILES}

echo "=============== build bolt"

${PATH_TO_BOLT_BIN}/perf2bolt ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION} \
  -p ${PATH_TO_PROFILES}/perf.data -o ${PATH_TO_PROFILES}/perf.fdata -w ${PATH_TO_PROFILES}/perf.yaml
${PATH_TO_BOLT_BIN}/llvm-bolt ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION} \
	-o ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION}.bolt -b ${PATH_TO_PROFILES}/perf.yaml \
	-reorder-blocks=cache+ -reorder-functions=hfsort+ -split-functions=3 \
	-split-all-cold -dyno-stats -icf=1 -use-gnu-stack 

cp ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION} ${PATH_TO_ALL_BINARIES}/clang.bolt
mv ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION}.bolt ${PATH_TO_OPT_INSTALL}/bin/clang-${CLANG_VERSION}

echo "=============== build pgo and bolt end"


date > ${PATH_TO_ALL_RESULTS}/script_end_time.txt
