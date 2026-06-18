PS5_HOST ?= ps5ip
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK ?= /Users/bizkut/Downloads/PS5/sdk/host

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

TARGET := unrar_ps5
ELF := $(TARGET).elf
SRC_DIR := src
LZMA_DIR := lzma2601
LZMA_C_DIR := $(LZMA_DIR)/C
LZMA_CPP_DIR := $(LZMA_DIR)/CPP
SEVENZ_DIR := $(LZMA_CPP_DIR)/7zip

CXXFLAGS := -O2 -std=c++11 -Wall -Wno-logical-op-parentheses -Wno-switch -Wno-dangling-else -Wno-unused-parameter -Wno-reorder
CFLAGS := -O2 -Wall -Wno-unused-parameter
DEFINES := -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_UNIX -DRAR_SMP -DPS5_PAYLOAD -DSILENT
SEVENZ_DEFINES := -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_UNIX -DZ7_EXTRACT_ONLY -DZ7_PROG_VARIANT_R -DZ7_AFFINITY_DISABLE
SEVENZ_INCLUDES := -I$(LZMA_C_DIR) -I$(LZMA_CPP_DIR) -I$(SEVENZ_DIR)
LDFLAGS := -pthread -lSceNotification

COMMON_OBJ := \
	$(SRC_DIR)/strlist.o $(SRC_DIR)/strfn.o $(SRC_DIR)/pathfn.o $(SRC_DIR)/smallfn.o \
	$(SRC_DIR)/global.o $(SRC_DIR)/file.o $(SRC_DIR)/filefn.o $(SRC_DIR)/filcreat.o \
	$(SRC_DIR)/archive.o $(SRC_DIR)/arcread.o $(SRC_DIR)/unicode.o $(SRC_DIR)/system.o \
	$(SRC_DIR)/crypt.o $(SRC_DIR)/crc.o $(SRC_DIR)/rawread.o $(SRC_DIR)/encname.o \
	$(SRC_DIR)/resource.o $(SRC_DIR)/match.o $(SRC_DIR)/timefn.o $(SRC_DIR)/rdwrfn.o \
	$(SRC_DIR)/consio.o $(SRC_DIR)/options.o $(SRC_DIR)/errhnd.o $(SRC_DIR)/rarvm.o \
	$(SRC_DIR)/secpassword.o $(SRC_DIR)/rijndael.o $(SRC_DIR)/getbits.o $(SRC_DIR)/sha1.o \
	$(SRC_DIR)/sha256.o $(SRC_DIR)/blake2s.o $(SRC_DIR)/hash.o $(SRC_DIR)/extinfo.o \
	$(SRC_DIR)/extract.o $(SRC_DIR)/volume.o $(SRC_DIR)/list.o $(SRC_DIR)/find.o \
	$(SRC_DIR)/unpack.o $(SRC_DIR)/headers.o $(SRC_DIR)/threadpool.o $(SRC_DIR)/rs16.o \
	$(SRC_DIR)/cmddata.o $(SRC_DIR)/ui.o $(SRC_DIR)/largepage.o

UNRAR_OBJ := $(SRC_DIR)/filestr.o $(SRC_DIR)/recvol.o $(SRC_DIR)/rs.o $(SRC_DIR)/scantree.o $(SRC_DIR)/qopen.o
PAYLOAD_OBJ := $(SRC_DIR)/ps5_payload.o $(SRC_DIR)/ps5_7z.o $(SRC_DIR)/ps5_7z_stubs.o

SEVENZ_C_OBJ := \
	$(LZMA_C_DIR)/7zCrc.o $(LZMA_C_DIR)/7zCrcOpt.o $(LZMA_C_DIR)/7zStream.o $(LZMA_C_DIR)/Aes.o \
	$(LZMA_C_DIR)/Alloc.o $(LZMA_C_DIR)/Bcj2.o \
	$(LZMA_C_DIR)/Bra.o $(LZMA_C_DIR)/Bra86.o $(LZMA_C_DIR)/BraIA64.o \
	$(LZMA_C_DIR)/CpuArch.o $(LZMA_C_DIR)/Delta.o $(LZMA_C_DIR)/Lzma2Dec.o \
	$(LZMA_C_DIR)/Lzma2DecMt.o $(LZMA_C_DIR)/LzmaDec.o $(LZMA_C_DIR)/MtCoder.o \
	$(LZMA_C_DIR)/MtDec.o $(LZMA_C_DIR)/Sha256.o \
	$(LZMA_C_DIR)/SwapBytes.o $(LZMA_C_DIR)/Threads.o

SEVENZ_COMMON_OBJ := \
	$(LZMA_CPP_DIR)/Common/CRC.o $(LZMA_CPP_DIR)/Common/IntToString.o \
	$(LZMA_CPP_DIR)/Common/MyWindows.o \
	$(LZMA_CPP_DIR)/Common/MyString.o $(LZMA_CPP_DIR)/Common/MyVector.o \
	$(LZMA_CPP_DIR)/Common/StringConvert.o $(LZMA_CPP_DIR)/Common/StringToInt.o \
	$(LZMA_CPP_DIR)/Common/UTFConvert.o \
	$(LZMA_CPP_DIR)/Windows/PropVariant.o $(LZMA_CPP_DIR)/Windows/System.o \
	$(SEVENZ_DIR)/Common/CreateCoder.o $(SEVENZ_DIR)/Common/CWrappers.o \
	$(SEVENZ_DIR)/Common/FilterCoder.o $(SEVENZ_DIR)/Common/LimitedStreams.o \
	$(SEVENZ_DIR)/Common/MethodId.o $(SEVENZ_DIR)/Common/MethodProps.o \
	$(SEVENZ_DIR)/Common/ProgressUtils.o $(SEVENZ_DIR)/Common/PropId.o \
	$(SEVENZ_DIR)/Common/StreamBinder.o \
	$(SEVENZ_DIR)/Common/StreamObjects.o $(SEVENZ_DIR)/Common/StreamUtils.o \
	$(SEVENZ_DIR)/Common/VirtThread.o \
	$(SEVENZ_DIR)/Archive/Common/CoderMixer2.o $(SEVENZ_DIR)/Archive/Common/DummyOutStream.o \
	$(SEVENZ_DIR)/Archive/Common/HandlerOut.o $(SEVENZ_DIR)/Archive/Common/InStreamWithCRC.o \
	$(SEVENZ_DIR)/Archive/Common/MultiStream.o $(SEVENZ_DIR)/Archive/Common/ParseProperties.o

SEVENZ_CODEC_OBJ := \
	$(SEVENZ_DIR)/Compress/Bcj2Coder.o $(SEVENZ_DIR)/Compress/Bcj2Register.o \
	$(SEVENZ_DIR)/Compress/BcjCoder.o $(SEVENZ_DIR)/Compress/BcjRegister.o \
	$(SEVENZ_DIR)/Compress/BranchMisc.o $(SEVENZ_DIR)/Compress/BranchRegister.o \
	$(SEVENZ_DIR)/Compress/ByteSwap.o $(SEVENZ_DIR)/Compress/CopyCoder.o \
	$(SEVENZ_DIR)/Compress/CopyRegister.o $(SEVENZ_DIR)/Compress/DeltaFilter.o \
	$(SEVENZ_DIR)/Compress/Lzma2Decoder.o $(SEVENZ_DIR)/Compress/Lzma2Register.o \
	$(SEVENZ_DIR)/Compress/LzmaDecoder.o $(SEVENZ_DIR)/Compress/LzmaRegister.o \
	$(SEVENZ_DIR)/Crypto/7zAes.o $(SEVENZ_DIR)/Crypto/7zAesRegister.o \
	$(SEVENZ_DIR)/Crypto/MyAes.o $(SEVENZ_DIR)/Crypto/MyAesReg.o

SEVENZ_ARCHIVE_OBJ := \
	$(SEVENZ_DIR)/Archive/7z/7zCompressionMode.o $(SEVENZ_DIR)/Archive/7z/7zDecode.o \
	$(SEVENZ_DIR)/Archive/7z/7zExtract.o $(SEVENZ_DIR)/Archive/7z/7zHandler.o \
	$(SEVENZ_DIR)/Archive/7z/7zHeader.o $(SEVENZ_DIR)/Archive/7z/7zIn.o \
	$(SEVENZ_DIR)/Archive/7z/7zProperties.o \
	$(SEVENZ_DIR)/Archive/7z/7zSpecStream.o

SEVENZ_OBJ := $(SEVENZ_C_OBJ) $(SEVENZ_COMMON_OBJ) $(SEVENZ_CODEC_OBJ) $(SEVENZ_ARCHIVE_OBJ)
OBJECTS := $(COMMON_OBJ) $(UNRAR_OBJ) $(PAYLOAD_OBJ) $(SEVENZ_OBJ)

all: $(ELF)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) -DUNRAR -c $< -o $@

$(SRC_DIR)/ps5_7z.o: $(SRC_DIR)/ps5_7z.cpp $(SRC_DIR)/ps5_7z.hpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(SEVENZ_DEFINES) $(SEVENZ_INCLUDES) -DUNRAR -c $< -o $@

$(SRC_DIR)/ps5_7z_stubs.o: $(SRC_DIR)/ps5_7z_stubs.c
	$(CC) $(CFLAGS) $(SEVENZ_DEFINES) $(SEVENZ_INCLUDES) -c $< -o $@

$(LZMA_C_DIR)/%.o: $(LZMA_C_DIR)/%.c
	$(CC) $(CFLAGS) $(SEVENZ_DEFINES) $(SEVENZ_INCLUDES) -c $< -o $@

$(LZMA_CPP_DIR)/%.o: $(LZMA_CPP_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(SEVENZ_DEFINES) $(SEVENZ_INCLUDES) -c $< -o $@

$(ELF): $(OBJECTS)
	$(CXX) -o $@ $(OBJECTS) $(LDFLAGS)

clean:
	rm -f $(OBJECTS) $(ELF)

send: $(ELF)
	nc -w 10 $(PS5_HOST) $(PS5_PORT) < $<

test: send

docker-build:
	docker run --rm -v "$(CURDIR):/work" -w /work -e PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk ps5-payload-sdk:libcxx make

.PHONY: all clean send test docker-build
