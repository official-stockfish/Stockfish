#!/bin/sh

#
# Returns the best architecture supported by the CPU (as expected by src/Makefile ARCH=).
#
# Output format:
#   "<true_arch>\n"
#

# ---------------------------
# Helpers (POSIX)
# ---------------------------

# Test hooks (optional env overrides)
#   GP_UNAME_S: override `uname -s`
#   GP_UNAME_M: override `uname -m`
#   GP_CPUINFO: path to a cpuinfo-like fixture file (defaults to /proc/cpuinfo)
#   GP_BITS: override getconf LONG_BIT result (32/64)

cpuinfo_path=${GP_CPUINFO:-/proc/cpuinfo}

# Normalize to a single-line, space-separated string.
normalize_ws() {
	printf '%s\n' "$*" | tr '\n\t' '  ' | tr -s ' '
}

die() {
	printf '%s\n' "$*" >&2
	exit 1
}

# Populate $flags from /proc/cpuinfo when available,
# removing underscores and dots to reduce naming variations.
get_flags() {
	if [ -r "$cpuinfo_path" ]; then
		flags=$(
			awk '
				/^flags[ \t]*:|^Features[ \t]*:/ {
					if (!found) {
						gsub(/^flags[ \t]*:[ \t]*|^Features[ \t]*:[ \t]*|[_.]/, "");
						line=$0
						found=1
					}
				}
				END { print line }
			' "$cpuinfo_path" 2>/dev/null
		)
	else
		flags=''
	fi
	flags=$(printf '%s\n' "$flags" | tr '[:upper:]' '[:lower:]')
	flags=$(normalize_ws "$flags")
}

# Best-effort bitness for fallback arch selection.
get_bits() {
	if [ -n "${GP_BITS:-}" ]; then
		bits=$GP_BITS
	else
		bits=$(getconf LONG_BIT 2>/dev/null)
	fi
	case $bits in
		32|64) : ;;
		*) bits=64 ;;
	esac
}

# Extract ARM architecture level (5/6/7/8/...) from /proc/cpuinfo when present.
get_arm_arch_level() {
	[ -r "$cpuinfo_path" ] || return 1
	awk '
		/^CPU architecture[ \t]*:/{
			s=$0
			sub(/^[^:]*:[ \t]*/, "", s)
			if (match(s, /[0-9]+/)) { print substr(s, RSTART, RLENGTH); exit }
		}
		/^Processor[ \t]*:/{
			s=$0
			sub(/^[^:]*:[ \t]*/, "", s)
			if (match(s, /ARMv[0-9]+/)) { print substr(s, RSTART+4, RLENGTH-4); exit }
		}
	' "$cpuinfo_path" 2>/dev/null
}

# Best-effort ARM architecture level (5/6/7/8/...) with a minimal fallback.
# Prefer /proc/cpuinfo when available; fall back to uname -m only when it encodes it.
get_arm_level() {
	arm_level=$(get_arm_arch_level || :)
	if [ -n "$arm_level" ]; then
		printf '%s\n' "$arm_level"
		return 0
	fi
	case ${1:-} in
		armv5*) printf '5\n' ;;
		armv6*) printf '6\n' ;;
		armv7*) printf '7\n' ;;
		armv8l) printf '8\n' ;;
		*) return 1 ;;
	esac
}

# Whole-token membership check.
has_flag() {
	case " $flags " in
		*" $1 "*) return 0 ;;
		*)        return 1 ;;
	esac
}

match_flags() {
	for f; do
		has_flag "$f" || return 1
	done
	return 0
}

match_true() { return 0; }

# Generic selector: reads lines like "arch|predicate|arg1 arg2 ..."
# First match wins; blank lines and lines starting with '#' are ignored.
select_arch_from_table() {
	while IFS='|' read -r arch pred args; do
		[ -z "$arch" ] && continue
		case $arch in \#*) continue ;; esac

		if [ -n "$args" ]; then
			# Intentional splitting of args into words for the predicate.
			# shellcheck disable=SC2086
			$pred $args && { printf '%s\n' "$arch"; return 0; }
		else
			$pred && { printf '%s\n' "$arch"; return 0; }
		fi
	done
	return 1
}

# ---------------------------
# Arch selection (table-driven)
# ---------------------------

set_arch_loongarch64() {
	true_arch=$(
		select_arch_from_table <<'EOF'
loongarch64-lasx|match_flags|lasx
loongarch64-lsx|match_flags|lsx
loongarch64|match_true|
EOF
	)
}

# Use src/universal/<entry> as the source of truth. When compiled with
# DUMP_SELECTED_BUILD it prints the build that the universal binary would dispatch to
#   Args: entry source file name, expected prefix of the selected build
select_arch_via_entry() {
	entry_src="$(cd "$(dirname "$0")" && pwd)/../src/universal/$1"
	[ -r "$entry_src" ] || die "Cannot read $entry_src"
	tmpdir=$(mktemp -d 2>/dev/null) || die "mktemp failed"
	detected=''
	for cxx in ${CXX:-} c++ g++ clang++; do
		command -v "$cxx" >/dev/null 2>&1 || continue
		if "$cxx" -O1 -DDUMP_SELECTED_BUILD "$entry_src" -o "$tmpdir/detect" >/dev/null 2>&1; then
			detected=$("$tmpdir/detect" 2>/dev/null)
			break
		fi
	done
	rm -rf "$tmpdir"
	case $detected in
		"$2"*) true_arch=$(printf '%s\n' "$detected" | tr '_' '-') ;;
		*) die "Failed to compile and run $entry_src" ;;
	esac
}

set_arch_x86_32() {
	true_arch=$(
		select_arch_from_table <<'EOF'
x86-32-sse41-popcnt|match_flags|sse41 popcnt
x86-32-sse2|match_flags|sse2
x86-32|match_true|
EOF
	)
}

# PPC64 needs a little parsing to distinguish vsx vs altivec.
set_arch_ppc_64() {
	if [ -r "$cpuinfo_path" ] && grep -q "altivec" "$cpuinfo_path" 2>/dev/null; then
		# Typical: "cpu : POWER8E" (extract the number after POWER)
		power=$(
			awk -F: '/^cpu[ \t]*:/{print $2; exit}' "$cpuinfo_path" 2>/dev/null \
				| sed -n 's/.*[Pp][Oo][Ww][Ee][Rr][^0-9]*\([0-9][0-9]*\).*/\1/p'
		)
		if [ -z "$power" ]; then
			power=$(
				awk -F: '/^cpu[ \t]*:/{print $2; exit}' "$cpuinfo_path" 2>/dev/null \
					| sed -n 's/.*\([0-9][0-9]*\).*/\1/p'
			)
		fi
		case $power in
			''|*[!0-9]*)
				true_arch='ppc-64-altivec'
				;;
			*)
				if [ "$power" -gt 7 ] 2>/dev/null; then
					true_arch='ppc-64-vsx'
				else
					true_arch='ppc-64-altivec'
				fi
				;;
		esac
	else
		true_arch='ppc-64'
	fi
}

# ---------------------------
# OS / machine dispatch
# ---------------------------

uname_s=$(uname -s 2>/dev/null)
uname_m=$(uname -m 2>/dev/null)
uname_s=${GP_UNAME_S:-$uname_s}
uname_m=${GP_UNAME_M:-$uname_m}

case $uname_s in
	Darwin)
		case $uname_m in
			arm64)
				true_arch='apple-silicon'
				;;
			x86_64)
				select_arch_via_entry entry_x86.cpp x86_64
				;;
			*)
				get_bits
				if [ "$bits" = "32" ]; then
					true_arch='general-32'
				else
					true_arch='general-64'
				fi
				;;
		esac
		;;

	Linux)
		get_flags
		case $uname_m in
			x86_64)
				select_arch_via_entry entry_x86.cpp x86_64
				;;
			i?86)
				set_arch_x86_32
				;;
			ppc64*)
				set_arch_ppc_64
				;;
			aarch64|arm64)
				select_arch_via_entry entry_arm64.cpp armv8
				;;
			armv5*|armv6*|armv7*|armv8l|arm*)
				arm_level=$(get_arm_level "$uname_m" || :)
				case $arm_level in
					5|6)
						true_arch='general-32'
						;;
					7|8)
						true_arch='armv7'
						if match_flags neon; then
							true_arch='armv7-neon'
						fi
						;;
					*)
						true_arch='general-32'
						if match_flags neon; then
							true_arch='armv7-neon'
						fi
						;;
				esac
				;;
			loongarch64*)
				set_arch_loongarch64
				;;
			riscv64)
				select_arch_via_entry entry_riscv64.cpp riscv64
				;;
			e2k*)
				true_arch='e2k'
				;;
			ppc|ppc32|powerpc)
				true_arch='ppc-32'
				;;
			*)
				# Don't hard-fail: fall back to general-* so ARCH=native still builds
				get_bits
				if [ "$bits" = "32" ]; then
					true_arch='general-32'
				else
					true_arch='general-64'
				fi
				;;
		esac
		;;

	MINGW*ARM64*)
		# Windows ARM64 (MSYS2/MinGW)
		select_arch_via_entry entry_arm64.cpp armv8
		;;

	CYGWIN*|MINGW*|MSYS*)
		# Windows x86_64 (MSYS2/Cygwin/MinGW)
		select_arch_via_entry entry_x86.cpp x86_64
		;;

	*)
		die "Unsupported system type: $uname_s"
		;;
esac

printf '%s\n' "$true_arch"
