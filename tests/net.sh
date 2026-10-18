#!/bin/bash
# Test network downloads without accessing the network or using real NNUE files.
set -eu

NET_SCRIPT=$(cd "$(dirname "$0")/../scripts" && pwd)/net.sh
TEST_DIR=$(mktemp -d)
trap 'rm -rf "$TEST_DIR"' EXIT

mkdir -p "$TEST_DIR/bin" "$TEST_DIR/src/universal"
export PATH="$TEST_DIR/bin:$PATH"
export DOWNLOAD_LOG="$TEST_DIR/downloads"

# net.sh prefers wget, but mock both tools to keep these tests offline.
cat > "$TEST_DIR/bin/wget" << 'EOF'
#!/bin/sh
for url; do :; done
echo "$url" >> "$DOWNLOAD_LOG"
case "$DOWNLOAD_MODE:$url" in
  success:*|fallback:https://github.com/*) echo 'valid network' ;;
  invalid:*) echo 'invalid network' ;;
  *) echo 'partial download'; exit 1 ;;
esac
EOF
cp "$TEST_DIR/bin/wget" "$TEST_DIR/bin/curl"

# Use a fixed checksum so the test does not depend on a system hash utility.
cat > "$TEST_DIR/bin/shasum" << 'EOF'
#!/bin/sh
for file; do :; done
if [ "$(cat "$file")" = 'valid network' ]; then
  echo '123456789abc'
else
  echo '000000000000'
fi
EOF
chmod +x "$TEST_DIR/bin/"*

cd "$TEST_DIR/src"
NETWORK=nn-123456789abc.nnue
echo "#define EvalFileDefaultName \"$NETWORK\"" > evaluate.h

fail() {
  echo "Network download test failed: $*" >&2
  exit 1
}

# Cover make net, universal builds with #embed, and string-literal embedding.
for mode in fail invalid; do
  export DOWNLOAD_MODE=$mode
  for embed in '' 1 0; do
    : > "$DOWNLOAD_LOG"
    if sh "$NET_SCRIPT" $embed > "$TEST_DIR/output" 2>&1; then
      cat "$TEST_DIR/output"
      fail "$mode downloads returned success (embed='$embed')"
    fi
    [ "$(wc -l < "$DOWNLOAD_LOG")" -eq 2 ] || fail 'both mirrors were not tried'
    [ ! -e "$NETWORK" ] || fail 'failed download was retained'
    [ ! -e universal/network_dump.inc ] || fail 'embedding ran after download failure'
    grep -q "Failed to download $NETWORK" "$TEST_DIR/output" || fail 'missing failure diagnostic'
  done
done

export DOWNLOAD_MODE=fallback
: > "$DOWNLOAD_LOG"
sh "$NET_SCRIPT" > "$TEST_DIR/output" 2>&1
[ "$(wc -l < "$DOWNLOAD_LOG")" -eq 2 ] || fail 'fallback mirror was not tried'
[ "$(cat "$NETWORK")" = 'valid network' ] || fail 'fallback download was not retained'

# A valid cached network should not trigger a download.
: > "$DOWNLOAD_LOG"
sh "$NET_SCRIPT" > "$TEST_DIR/output" 2>&1
[ ! -s "$DOWNLOAD_LOG" ] || fail 'cached network was downloaded again'

rm "$NETWORK"
export DOWNLOAD_MODE=success
sh "$NET_SCRIPT" 1 > "$TEST_DIR/output" 2>&1
[ "$(cat "$NETWORK")" = 'valid network' ] || fail 'download was not retained'
[ "$(wc -l < "$DOWNLOAD_LOG")" -eq 1 ] || fail 'second mirror was tried after success'

echo 'Network download tests passed'
