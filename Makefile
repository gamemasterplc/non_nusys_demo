include /usr/include/n64/make/PRdefs

#Project name
PROJECT = non_nusys_demo
#Libultra directories
LIB = $(ROOT)/usr/lib
LPR = $(LIB)/PR
INC = $(ROOT)/usr/include

#Audio Library
AUDIOLIB = -lmus

#Compiler/Linker settings
OPTIMIZER = -O1
LCDEFS = -DNDEBUG -D_FINALROM -DF3DEX_GBI_2
N64LIB = -lultra_rom
CFLAGS := $(CFLAGS) -DNDEBUG -D_FINALROM -DF3DEX_GBI_2 -G 0 -MMD -MP -Iinclude -I. -I$(NUSTDINCDIR) -I$(ROOT)/usr/include/PR -Wa,-Iinclude
CXXFLAGS := $(CXXFLAGS) -DNDEBUG -D_FINALROM -DF3DEX_GBI_2 -MMD -MP -G 0 -std=c++17 -fno-builtin -fno-exceptions -fno-rtti -fno-use-cxa-atexit -Iinclude -I$(NUSYSINCDIR) -I$(NUSTDINCDIR) -I$(ROOT)/usr/include/PR

#Linking outputs
ELF		= ./build/$(PROJECT).elf
TARGETS	= $(PROJECT).z64
MAP		= ./build/$(PROJECT).map
#Linker requirements
LD_SCRIPT	= $(PROJECT).ld
CP_LD_SCRIPT	= ./build/$(PROJECT)_cp.ld
LD_DEPS  = $(CP_LD_SCRIPT).d

#Header file list
HFILES  := $(wildcard src/*.h) $(wildcard include/*.h)

#Code file list
ASMFILES   := $(wildcard asm/*.s)
CODEFILES   := $(wildcard src/*.c)
CXXFILES    := $(wildcard src/*.cpp)
#Data file list
DATAFILES   := $(wildcard data/*.c)

#Path for build artifacts
OBJPATH		= 	./build/obj
DEPPATH		=	./build/dep

#Object definitions
CODEOBJECTS =	$(CODEFILES:.c=.o) $(CXXFILES:.cpp=.o)
CODEOBJNAME =   $(notdir $(CODEOBJECTS))
CODEOBJPATH =   $(addprefix $(OBJPATH)/,$(CODEOBJNAME))

ASMOBJECTS =	$(ASMFILES:.s=.o)
ASMOBJNAME =   $(notdir $(ASMOBJECTS))
ASMOBJPATH =   $(addprefix $(OBJPATH)/,$(ASMOBJNAME))

DATAOBJECTS =	$(DATAFILES:.c=.o)
DATAOBJNAME =   $(notdir $(DATAOBJECTS))
DATAOBJPATH =   $(addprefix $(OBJPATH)/,$(DATAOBJNAME))

#Bootcode definitions
BOOT		= /usr/lib/n64/PR/bootcode/boot.6102
BOOT_OBJ	= ./build/obj/boot.6102.o

#Dependency list
DEPFILES	= $(LD_DEPS) $(addprefix $(DEPPATH)/,$(CODEOBJNAME:%.o=%.d))

#Object for codesegment
CODESEGMENT =	./build/codesegment.o

#Required objects to link
OBJECTS =	$(ASMOBJPATH) $(BOOT_OBJ) $(CODESEGMENT) $(DATAOBJPATH)

#Linker options
LCINCS =	-I. -I$(ROOT)/usr/include/PR
LCOPTS =	-G 0
LDIRT  =	$(APP) $(TARGETS)

LDFLAGS = -L$(LIB) $(AUDIOLIB) $(N64LIB) -L$(N64_LIBGCCDIR) -lgcc

#Default target
default: $(TARGETS)

#Clean target
clean:
	@echo "\e[31mCleaning output...\e[0m"
	@rm -rf ./build

#Include dependencies
-include $(DEPFILES)

#Compile rules
build/obj/%.o: */%.s | makeDirs
	@echo "\e[35mCompiling $<...\e[0m"
	@$(CC) -MF $(DEPPATH)/$*.d -o $@ $(CFLAGS) $<

build/obj/%.o: */%.c | makeDirs
	@echo "\e[35mCompiling $<...\e[0m"
	@$(CC) -MF $(DEPPATH)/$*.d -o $@ $(CFLAGS) $<
	
build/obj/%.o: */%.cpp | makeDirs
	@echo "\e[35mCompiling $<...\e[0m"
	@$(CC) -MF $(DEPPATH)/$*.d -o $@ $(CXXFLAGS) $<

#Code segment linking rule
$(CODESEGMENT):	$(CODEOBJPATH)
	@echo "\e[31mLinking codesegment...\e[0m"
	$(LD) -o $(CODESEGMENT) -r $(CODEOBJPATH) $(LDFLAGS)

#Generate bootcode object
$(BOOT_OBJ): $(BOOT) | makeDirs
	@echo "\e[35mLinking $<...\e[0m"
	@$(OBJCOPY) -I binary -B mips -O elf32-bigmips $< $@

#Preprocess linker script
$(CP_LD_SCRIPT): $(LD_SCRIPT) | makeDirs
	@echo "\e[35mPreprocessing linkerscript $<...\e[0m"
	@cpp -MMD -MP -MF $(LD_DEPS) -MMD -MP -MT $@ -MF $@.d -P -Wno-trigraphs -Iinclude -I. -o $@ $<

#Generate output ROM
$(TARGETS) $(APP): $(CP_LD_SCRIPT) $(OBJECTS)
	@echo "\e[35mLinking ROM $<...\e[0m"
	@$(LD) -L. -T $(CP_LD_SCRIPT) -Map $(MAP) -o $(ELF) 
	@echo "\e[35mConverting ROM $<...\e[0m"
	@$(OBJCOPY) --gap-fill=0xFF $(ELF) $(TARGETS) -O binary
	@makemask $(TARGETS)
	@echo "\e[32mDone! ./build/$(PROJECT).z64\e[0m"

#Generate build directories
makeDirs:
	@echo "\e[33mCreating build directories... $<\e[0m"
	@mkdir -p "build/obj"
	@mkdir -p "build/dep"

# Remove built-in rules, to improve performance
MAKEFLAGS += --no-builtin-rules