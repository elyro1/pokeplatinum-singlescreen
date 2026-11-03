## Building from source

Clone **this fork**:

```bash
git clone https://github.com/elyro1/pokeplatinum.git
cd pokeplatinum
```

The Makefile fetches Meson and the compiler tools as needed, and Meson fetches the SDK subprojects. The initial build requires network access. You still need your own ROM to run the PC port.

Git-based subprojects use a `lib_<subproject>` branch in the corresponding
`elyro1` fork, for example `lib_libntr` in `elyro1/libntr`. The SDK patches
previously applied by Meson are committed on those branches. Make SDK dependency
changes in the corresponding fork branch. RapidJSON and yyjson retain their
WrapDB releases and local wrap patches.

### Linux with Docker

Install Docker and ensure your user can run it, then run:

```bash
./drun.sh make linux
```

The script builds the container image on first use. Output is written to `build_linux/pokeplatinum`, including `main` and `pokeplatinum.AppImage`.

### Linux without Docker

Use a 64-bit Linux environment. The build requires GCC/G++, Make, Python 3, Git, Ninja, NASM, Flex, Bison, pkg-config, the ARM GNU toolchain, libpng, SDL2, ENet, and SoundTouch development packages. Some build tools also require 32-bit compatibility libraries; AppImage packaging requires FUSE.

For Debian / Ubuntu, the core dependencies are:

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install build-essential python3 git ninja-build nasm flex bison \
    pkg-config gcc-arm-none-eabi libpng-dev libsdl2-dev libenet-dev \
    libsoundtouch-dev libc6:i386 wget ca-certificates fuse file patchelf
make linux
```

Refer to [the build container](tools/docker/Dockerfile) for the full container environment and [INSTALL.md](INSTALL.md) for additional setup information inherited from pret. Its ROM-build instructions are separate from the PC targets here.

### Windows

From a fresh clone, run the included setup script in PowerShell:

```powershell
.\Install_MSys2.ps1
```

This creates a portable MSYS2 environment and installs dependencies. Then launch its shell:

```powershell
.\Launch_MSys2.ps1
```

In that shell, build with:

```bash
make win64
```

The game and its required DLLs are placed in `build_win64/pokeplatinum`.

### Other build targets

The inherited Nintendo Switch target requires DevKitA64 (included in the Docker environment):

```bash
./drun.sh make nx
```

This target is experimental and has poor performance; the changes in this fork primarily target PC play.

The source tree also retains the Nintendo DS ROM target:

```bash
make
```

That output is `build/pokeplatinum.us.nds`. PC-specific features do not necessarily apply to the DS build. See [INSTALL.md](INSTALL.md) for ROM revisions and debugger setup.

### Development notes

- To update SDK subprojects, run `make update`.
- To enable Tracy profiling for a Linux build, run:

  ```bash
  ./drun.sh ./subprojects/meson-1.12.0/meson.py configure -Dtracy_enable=true build_linux
  ```

  Profiling is also available for the Win64 and NX build targets.
- `firmware.bin` is a placeholder for Wi-Fi configuration storage, not a dump of real DS firmware.
