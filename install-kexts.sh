#!/bin/bash
# Installs kexts into /Library/Extensions and rebuilds the Auxiliary Kernel Collection.
#   sudo ./install-kexts.sh path/to/A.kext [path/to/B.kext ...]
# Keeps a backup of the old collection, refuses to continue if any kext that is in the
# current collection would be missing from the new one, and prints the way back.
# Afterwards: approve in System Settings > Privacy & Security if asked, then restart.
set -euo pipefail
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
[[ $(id -u) == 0 ]] || { echo "run with sudo"; exit 1; }
[[ $# -ge 1 ]] || { echo "usage: sudo $0 X.kext [Y.kext ...]"; exit 1; }
ext=/Library/Extensions; base=/System/Library/KernelCollections; kc_dir=/Library/KernelCollections
old="$kc_dir/AuxiliaryKernelExtensions.kc"
backup=$(mktemp -d "$kc_dir/scar-backup.XXXXXXXX"); candidate="$backup/New.kc"
echo "Backup: $backup"
[[ -f "$old" ]] && cp -p "$old" "$backup/AuxiliaryKernelExtensions.kc"
installed=()
restore() {
    for k in "${installed[@]}"; do
        rm -rf "$ext/$k"; [[ -d "$backup/previous-$k" ]] && mv "$backup/previous-$k" "$ext/$k"
    done
    echo "ERROR: nothing changed."; exit 1
}
for src in "$@"; do
    k=$(basename "$src"); [[ -d "$src/Contents" ]] || { echo "not a kext: $src"; restore; }
    [[ -d "$ext/$k" ]] && mv "$ext/$k" "$backup/previous-$k"
    ditto "$src" "$ext/$k"; chown -R root:wheel "$ext/$k"; chmod -R go-w "$ext/$k"; installed+=("$k")
done
kmutil create -n aux --volume-root / --allow-missing-kdk -B "$base/BootKernelExtensions.kc" \
    -S "$base/SystemKernelExtensions.kc" --repository "$ext" --repository /Library/Apple/System/Library/Extensions -A "$candidate" -z >"$backup/kmutil.log" 2>&1 \
    || { tail -30 "$backup/kmutil.log"; restore; }
if [[ -f "$old" ]]; then
    # Kexts that were only loaded from a temporary folder may drop out; everything else must stay.
    kmutil inspect -a x86_64 -A "$old" | awk -F '\t' 'NF>2 && $3 !~ /^\/private\/var\/tmp\//{print $1}' | sort -u >"$backup/before"
    kmutil inspect -a x86_64 -A "$candidate" | awk -F '\t' 'NF>2{print $1}' | sort -u >"$backup/after"
    if [[ -n $(comm -23 "$backup/before" "$backup/after") ]]; then
        echo "would be missing:"; comm -23 "$backup/before" "$backup/after"; restore
    fi
fi
kmutil inspect -a x86_64 -A "$candidate" | awk -F '\t' 'NF>2{print $1"\t"$2}' | sort -u
mv -f "$candidate" "$old"
echo; echo "Installed. Approve in System Settings if asked, then restart."
echo "Way back: sudo cp $backup/AuxiliaryKernelExtensions.kc $old  (and restore $backup/previous-*)"
