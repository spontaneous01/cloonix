CLOONIX_COMMON=../../../../cloonix/common

GLOB_INCLUDE=${CLOONIX_COMMON}/glob_include
BLKD=${CLOONIX_COMMON}/lib_blkd
RPCT=${CLOONIX_COMMON}/lib_rpct

IOC=../lib_ioc
LIB_MUEND=../lib_muend

OBJ_DIR=obj/
EXE = cloonix_mutst
VPATH=src

SRC = main.c

WARNINGS=-Wall -Wstrict-prototypes -Wdeclaration-after-statement

INCLUDES= -I./src \
          -I./include \
          -I${GLOB_INCLUDE} \
          -I${IOC}/include \
          -I${LIB_MUEND}/include \
          -I${RPCT}/include 

LIBS = -L${BLKD} -lblkd \
       -L${IOC} -lioc \
       -L${RPCT} -lrpct \
       -L${LIB_MUEND} -lmuend \
       -lrt -lpthread


CFLAGS+= -g $(WARNINGS) $(INCLUDES)
CC=gcc
all: $(EXE)
OBJ = $(SRC:.c=.o)
OBJS = $(addprefix $(OBJ_DIR), $(OBJ))

$(OBJ_DIR)%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(EXE): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $(EXE) 

clean:
	@-rm -vf $(OBJ_DIR)*.o $(EXE) 


