
BINARIES=finfo.exe

!INCLUDE "..\config\common.mk"

!IF $(PDB)==1
LINKPDB=/Pdb:finfo.pdb
!ENDIF

BIN_OBJS=\
	 finfo.obj         \

MOD_OBJS=\
	 mod_finfo.obj     \

compile: $(BIN_OBJS) builtins.lib

finfo.exe: $(BIN_OBJS) $(YORILIB) $(YORICRT) $(YORIVER)
	@echo $@
	@$(LINK) $(LDFLAGS) -entry:$(YENTRY) $(BIN_OBJS) $(LIBS) $(CRTLIB) $(YORILIB) $(YORIVER) -version:$(YORI_VER_MAJOR).$(YORI_VER_MINOR) $(LINKPDB) -out:$@

mod_finfo.obj: finfo.c
	@echo $@
	@$(CC) -c -DYORI_BUILTIN=1 $(CFLAGS) -Fo$@ finfo.c

builtins.lib: $(MOD_OBJS)
	@echo $@
	@$(LIB32) $(LIBFLAGS) $(MOD_OBJS) -out:$@
