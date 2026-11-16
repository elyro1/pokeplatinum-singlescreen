#!/bin/bash
BUILDDIR=$1
OUTPUTDIR=$BUILDDIR/$3
SKIP_ANIM_SCRIPTS=false
for option in "${@:4}"; do
  case "$option" in
    --skip-anim-scripts) SKIP_ANIM_SCRIPTS=true ;;
    *) printf 'Unknown staging option: %s\n' "$option" >&2; exit 1 ;;
  esac
done
mkdir -p "$OUTPUTDIR" || exit 1

# A running game caches archive indexes and keeps the corresponding files open.
# Never overwrite their inodes: replacing a completed copy lets existing readers
# keep the old bytes until they close the file, while the next run gets the update.
copy_newer_atomic() {
  local source=$1 destination=$2 temporary
  if [[ ! -f "$source" ]]; then
    printf 'Missing build resource: %s\n' "$source" >&2
    return 1
  fi
  if [[ -e "$destination" && ! "$source" -nt "$destination" ]]; then
    return 0
  fi
  mkdir -p "$(dirname "$destination")" || return 1
  temporary=$(mktemp "$destination.tmp.XXXXXX") || return 1
  if cp -p "$source" "$temporary" && mv -f "$temporary" "$destination"; then
    return 0
  fi
  rm -f "$temporary"
  return 1
}

while IFS=, read -r SourceFile DestFile; do
  if [[ "$SourceFile" != "Source File" ]]; then
    if [[ "$SKIP_ANIM_SCRIPTS" == true ]]; then
      case "$SourceFile" in
        res/moves/anim_scripts.narc|res/battle/scripts/waza_seq.narc) continue ;;
      esac
    fi
    copy_newer_atomic "$BUILDDIR/$SourceFile" "$OUTPUTDIR$DestFile" || exit 1
  fi
done < "$2"
copy_newer_atomic "$BUILDDIR/../res/pc_port/header.bin" "$OUTPUTDIR/header.bin" || exit 1
copy_newer_atomic "$BUILDDIR/../res/pc_port/dwc_nv.bin" "$OUTPUTDIR/firmware.bin" || exit 1
# The field light table; edits in the game's folder survive until the source changes.
copy_newer_atomic "$BUILDDIR/../res/pc_port/field_lights.txt" "$OUTPUTDIR/field_lights.txt" || exit 1
EXECUTABLE=main
if [[ -f "$BUILDDIR/main.exe" ]]; then
  EXECUTABLE=main.exe
fi
cp "$BUILDDIR/$EXECUTABLE" "$OUTPUTDIR/$EXECUTABLE.tmp" || exit 1
mv -f "$OUTPUTDIR/$EXECUTABLE.tmp" "$OUTPUTDIR/$EXECUTABLE" || exit 1

if [[ "$(uname -s)" =~ ^MSYS_NT.* ]]; then
  cp -u /ucrt64/bin/SDL2.dll $OUTPUTDIR/SDL2.dll
  cp -u /ucrt64/bin/libwinpthread-1.dll $OUTPUTDIR/libwinpthread-1.dll
  cp -u /ucrt64/bin/libenet-7.dll $OUTPUTDIR/libenet-7.dll
  cp -u /ucrt64/bin/libgcc_s_seh-1.dll $OUTPUTDIR/libgcc_s_seh-1.dll
  cp -u /ucrt64/bin/libstdc++-6.dll $OUTPUTDIR/libstdc++-6.dll
  for soundtouch_dll in /ucrt64/bin/*SoundTouch*.dll; do
    if [[ -f "$soundtouch_dll" ]]; then
      cp -u "$soundtouch_dll" "$OUTPUTDIR/"
    fi
  done
fi
