
BINARIES=sdir.exe

!INCLUDE "..\config\common.mk"

!IF $(PDB)==1
LINKPDB=/Pdb:sdir.pdb
!ENDIF

BIN_OBJS=\
		 callbacks.obj \
		 color.obj     \
		 display.obj   \
		 init.obj      \
		 sdir.obj      \
		 usage.obj     \
		 utils.obj

MOD_OBJS=\
		 callbacks.obj \
		 color.obj     \
		 display.obj   \
		 init.obj      \
		 mod_sdir.obj  \
		 usage.obj     \
		 utils.obj


compile: $(BIN_OBJS) builtins.lib

sdir.exe: $(BIN_OBJS) $(YORILIB) $(YORICRT) $(YORIVER)
	@echo $@
	@$(LINK) $(LDFLAGS) -entry:$(YENTRY) $(BIN_OBJS) $(LIBS) $(CRTLIB) $(YORILIB) $(YORIVER) -version:$(YORI_VER_MAJOR).$(YORI_VER_MINOR) $(LINKPDB) -out:$@

mod_sdir.obj: sdir.c
	@echo $@
	@$(CC) -c -DYORI_BUILTIN=1 $(CFLAGS) -Fo$@ sdir.c

builtins.lib: $(MOD_OBJS)
	@echo $@
	@$(LIB32) $(LIBFLAGS) $(MOD_OBJS) -out:$@


