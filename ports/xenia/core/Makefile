# The Xenia (Waldorf Microwave II/XT) emulator core as one static library, built from the vendored
# gearmulator subset in ../src/gearmulator (see ../src/VENDORED.md). Interpreter only: the DSP56300 JIT
# exists for x86-64 and AArch64, but MPC OS runs a 32-bit ARM process. DSP56K_FORCE_INTERPRETER is set on
# every target so the x86 host test runs the same code path as the device.
#
#   make -C core OUT=../build/core-arm -j4                 (inside arm32v7/gcc:12, see ../build.sh)
#   make -C core OUT=../build/core-host SANITIZE=1 -j4     (host gcc, for tools/test_port.sh; see ../test.sh)
#
# The flags the engine must share with this library (they change class layouts) are in CORE_DEFS, and
# vst.json's cflags repeat them.
HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
G := $(HERE)/../src/gearmulator/source
DSP := $(G)/cpu/dsp56300/source
OUT ?= $(HERE)/../build/core
CC ?= gcc
CXX ?= g++
AR ?= ar

CORE_DEFS := -DDSP56K_FORCE_INTERPRETER=1 -DDSP56300_DEBUGGER=0 -DASMJIT_STATIC -DASMJIT_NO_AARCH64
INCLUDES := -I$(DSP) -I$(DSP)/asmjit/src -I$(G)/cpu -I$(G)/framework -I$(G)/waldi/common -I$(G)/waldi/xt -I$(G)/3rdparty

ifeq ($(SANITIZE),1)
OPT := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1
else
# upstream uses -Ofast; this is the same minus the flags GCC 12 warns about
OPT := -O2 -ffast-math -fno-stack-protector
endif

# -w: upstream's code, upstream's warnings
COMMON := $(OPT) -fPIC -fvisibility=hidden -pthread -w $(CORE_DEFS) $(INCLUDES) -MMD -MP
CFLAGS_ALL := $(COMMON) -std=gnu11
CXXFLAGS_ALL := $(COMMON) -std=gnu++17 -fconstexpr-loop-limit=10000000

# upstream's CMake source lists, minus unit tests, the debugger, profiler SDK glue and the AArch64 JIT backend
CXX_SRC := $(wildcard \
	$(DSP)/dsp56kBase/*.cpp \
	$(DSP)/dsp56kEmu/*.cpp \
	$(G)/cpu/mc68k/*.cpp \
	$(G)/framework/baseLib/*.cpp \
	$(G)/framework/synthLib/*.cpp \
	$(G)/framework/synthLib/midi/*.cpp \
	$(G)/framework/synthLib/analog/*.cpp \
	$(G)/framework/hardwareLib/*.cpp \
	$(G)/waldi/common/wLib/*.cpp \
	$(G)/waldi/xt/xtLib/*.cpp \
	$(DSP)/asmjit/src/asmjit/core/*.cpp \
	$(DSP)/asmjit/src/asmjit/x86/*.cpp)
CXX_SRC := $(filter-out %_test.cpp %unittests.cpp %assemblertest.cpp %jitoptimizertests.cpp %_aarch64.cpp %/win8sync.cpp,$(CXX_SRC))
C_SRC := \
	$(G)/cpu/mc68k/Musashi/m68kcpu.c \
	$(G)/cpu/mc68k/Musashi/m68kdasm.c \
	$(G)/cpu/mc68k/Musashi/m68kfpu.c \
	$(G)/cpu/mc68k/Musashi/m68kops.c \
	$(G)/cpu/mc68k/Musashi/softfloat/softfloat.c \
	$(G)/3rdparty/libresample/src/filterkit.c \
	$(G)/3rdparty/libresample/src/resample.c \
	$(G)/3rdparty/libresample/src/resamplesubs.c

OBJ := $(patsubst $(G)/%,$(OUT)/obj/%.o,$(CXX_SRC) $(C_SRC))

$(OUT)/libxenia_core.a: $(OBJ)
	@rm -f $@
	@echo "  AR  $(notdir $@)"
	@$(AR) rcs $@ $^

$(OUT)/obj/%.cpp.o: $(G)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX $*.cpp"
	@$(CXX) $(CXXFLAGS_ALL) -c $< -o $@

$(OUT)/obj/%.c.o: $(G)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC  $*.c"
	@$(CC) $(CFLAGS_ALL) -c $< -o $@

-include $(OBJ:.o=.d)
