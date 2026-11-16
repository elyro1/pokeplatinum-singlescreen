# Keep this list in alphabetical order for ease of reference.
.PHONY:           \
	all           \
	clean         \
	distclean     \
	format        \
	linux         \
	linux-asan    \
	meson         \
	mingw         \
	purge         \
	target        \
	update        \
	win64

SUBPROJ_DIR := subprojects

MESON_VER := 1.12.0
MESON_DIR := $(SUBPROJ_DIR)/meson-$(MESON_VER)

MESON ?= $(MESON_DIR)/meson.py
NINJA ?= ninja
GIT ?= git

BUILD ?= build
BUILD_LINUX ?= $(BUILD)_linux
BUILD_LINUX_ASAN ?= $(BUILD)_linux_asan
BUILD_WIN64 ?= $(BUILD)_win64
BUILD_DIRS := $(BUILD_LINUX) $(BUILD_LINUX_ASAN) $(BUILD_WIN64)
ROOT_INI_LINUX := $(BUILD_LINUX)/root.ini
ROOT_INI_LINUX_ASAN := $(BUILD_LINUX_ASAN)/root.ini
ROOT_INI_WIN64 := $(BUILD_WIN64)/root.ini

UNAME_S := $(shell uname -s)

GITHUB_CI = ${GITHUB_ACTIONS}

ifneq (,$(findstring Darwin,$(UNAME_S)))
  NATIVE := native_macos.ini
else
  NATIVE := native.ini
endif

export NINJA_STATUS := [%p %f/%t] 

all: linux

format: $(BUILD_LINUX)/build.ninja
	$(NINJA) -C $(BUILD_LINUX) clang-format

# make target MESON_TARGET=<name> builds one Meson target of the Linux build
target: $(BUILD_LINUX)/build.ninja
	$(MESON) compile -C $(BUILD_LINUX) $(MESON_TARGET)

linux: $(BUILD_LINUX)/build.ninja
	$(MESON) compile -C $(BUILD_LINUX)

# ASan + UBSan build in its own directory (docs/sanitizers.md)
linux-asan: $(BUILD_LINUX_ASAN)/build.ninja
	ASAN_OPTIONS=detect_leaks=0 $(MESON) compile -C $(BUILD_LINUX_ASAN)

win64: $(BUILD_WIN64)/build.ninja
	$(MESON) compile -C $(BUILD_WIN64)

# Cross-build on Linux; the MSYS2 win64 target above remains the CI entry point.
mingw:
	python3 tools/scripts/build_mingw.py --native-build $(BUILD_LINUX) --build $(BUILD_WIN64)

clean:
	for dir in $(BUILD_DIRS); do \
		if [ -f "$$dir/build.ninja" ]; then $(MESON) compile -C "$$dir" --clean && rm -rf "$$dir/res"; fi; \
	done

distclean:
	rm -rf $(BUILD_DIRS)

purge: distclean
	! test -f $(MESON) || $(MESON) subprojects purge --confirm
	rm -rf $(MESON_DIR)

update: meson
	$(MESON) subprojects update || true

$(BUILD_LINUX)/build.ninja: $(ROOT_INI_LINUX) | $(BUILD_LINUX) meson
	$(MESON) setup \
		-Dbuild_target=linux \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_LINUX) \
		--cross-file=meson/cross_pcport_linux.ini \
		--cross-file=$(ROOT_INI_LINUX) \
		-- $(BUILD_LINUX)

$(BUILD_LINUX_ASAN)/build.ninja: $(ROOT_INI_LINUX_ASAN) | $(BUILD_LINUX_ASAN) meson
	$(MESON) setup \
		-Dbuild_target=linux \
		-Dsanitize=true \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_LINUX_ASAN) \
		--cross-file=meson/cross_pcport_linux.ini \
		--cross-file=$(ROOT_INI_LINUX_ASAN) \
		-- $(BUILD_LINUX_ASAN)

ifeq ($(GITHUB_CI),)
$(BUILD_WIN64)/build.ninja: $(ROOT_INI_WIN64) | $(BUILD_WIN64) meson
	$(MESON) setup \
		-Dbuild_target=win64 \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_WIN64) \
		--cross-file=meson/cross_pcport_win64.ini \
		--cross-file=$(ROOT_INI_WIN64) \
		-- $(BUILD_WIN64)
else
$(BUILD_WIN64)/build.ninja: $(ROOT_INI_WIN64) | $(BUILD_WIN64) meson
	$(MESON) setup \
		-Dbuild_target=win64 \
		-Dskip_anim_scripts=true \
		--native-file=meson/$(NATIVE) \
		--native-file=$(ROOT_INI_WIN64) \
		--cross-file=meson/cross_pcport_win64.ini \
		--cross-file=$(ROOT_INI_WIN64) \
		-- $(BUILD_WIN64)
endif

$(ROOT_INI_LINUX): | $(BUILD_LINUX)
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@
	
$(ROOT_INI_LINUX_ASAN): | $(BUILD_LINUX_ASAN)
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@

$(ROOT_INI_WIN64): | $(BUILD_WIN64)
	echo "[constants]" > $@
	echo "root = '$$PWD'" >> $@

$(BUILD_LINUX):
	mkdir -p -- $(BUILD_LINUX)

$(BUILD_LINUX_ASAN):
	mkdir -p -- $(BUILD_LINUX_ASAN)

$(BUILD_WIN64):
	mkdir -p -- $(BUILD_WIN64)

meson: $(MESON)

$(MESON):
	$(GIT) clone --depth=1 -b $(MESON_VER) https://github.com/mesonbuild/meson $(@D)
