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
#   GP_SYSCTL_FEATURES: override sysctl feature strings on Darwin x86_64

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

# Populate $flags from sysctl on Darwin x86_64.
get_sysctl_flags() {
	if [ -n "${GP_SYSCTL_FEATURES:-}" ]; then
		flags=$(printf '%s\n' "$GP_SYSCTL_FEATURES")
	else
		flags=$(sysctl -n machdep.cpu.features machdep.cpu.leaf7_features 2>/dev/null)
	fi
	flags=$(printf '%s\n' "$flags" | tr '\n' ' ' | tr '[:upper:]' '[:lower:]' | tr -d '._')
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

match_any_flags() {
	for f; do
		has_flag "$f" && return 0
	done
	return 1
}

# SSE3 is often exposed as "pni" in /proc/cpuinfo.
match_sse3() {
	match_any_flags sse3 pni
}

# AMD Zen1/2 exclusion logic (used for bmi2 tier).
# https://web.archive.org/web/20250821132355/https://en.wikichip.org/wiki/amd/cpuid
is_znver_1_2() (
	[ -r "$cpuinfo_path" ] || exit 1
	vendor_id=$(awk '/^vendor_id/{print $3; exit}' "$cpuinfo_path" 2>/dev/null)
	cpu_family=$(awk '/^cpu family/{print $4; exit}' "$cpuinfo_path" 2>/dev/null)
	[ "$vendor_id" = "AuthenticAMD" ] && [ "$cpu_family" = "23" ]
)

match_not_znver12_and_flags() {
	is_znver_1_2 && return 1
	match_flags "$@"
}

match_sse3_popcnt() {
	has_flag popcnt || return 1
	match_sse3
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

set_arch_x86_64() {
	true_arch=$(
		select_arch_from_table <<'EOF'
# Strongest -> weakest (first match wins)
x86-64-avx512icl|match_flags|avx512f avx512cd avx512vl avx512dq avx512bw avx512ifma avx512vbmi avx512vbmi2 avx512vpopcntdq avx512bitalg avx512vnni vpclmulqdq gfni vaes
x86-64-vnni512|match_flags|avx512vnni avx512dq avx512f avx512bw avx512vl
x86-64-avx512|match_flags|avx512f avx512bw
x86-64-avxvnni|match_flags|avxvnni
x86-64-bmi2|match_not_znver12_and_flags|bmi2
x86-64-avx2|match_flags|avx2
x86-64-sse41-popcnt|match_flags|sse41 popcnt
x86-64-ssse3|match_flags|ssse3
x86-64-sse3-popcnt|match_sse3_popcnt|
x86-64|match_true|
EOF
	)
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
				get_sysctl_flags
				set_arch_x86_64
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
				set_arch_x86_64
				;;
			i?86)
				set_arch_x86_32
				;;
			ppc64*)
				set_arch_ppc_64
				;;
			aarch64|arm64)
				true_arch='armv8'
				if match_flags asimddp; then
					true_arch='armv8-dotprod'
				fi
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
				true_arch='riscv64'
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
		# Can't reliably detect ARM CPU features here
		true_arch='armv8-dotprod'
		;;

	CYGWIN*|MINGW*|MSYS*)
		# Windows x86_64 (MSYS2/Cygwin/MinGW)
		get_flags
		set_arch_x86_64
		;;

	*)
		die "Unsupported system type: $uname_s"
		;;
esac

printf '%s\n' "$true_arch"
