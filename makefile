PS5_HOST ?= ps5ip
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK ?= /Users/bizkut/Downloads/PS5/sdk/host

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

TARGET := unrar_ps5
ELF := $(TARGET).elf
SRC_DIR := src

CXXFLAGS := -O2 -std=c++11 -Wall -Wno-logical-op-parentheses -Wno-switch -Wno-dangling-else
DEFINES := -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_UNIX -DRAR_SMP -DPS5_PAYLOAD -DSILENT
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
PAYLOAD_OBJ := $(SRC_DIR)/ps5_payload.o
OBJECTS := $(COMMON_OBJ) $(UNRAR_OBJ) $(PAYLOAD_OBJ)

all: $(ELF)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) -DUNRAR -c $< -o $@

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
