#!/bin/sh

#
# Returns properties of the native system.
# best architecture as supported by the CPU
# filename of the best binary uploaded as an artifact during CI
#

# Check if all the given flags are present in the CPU flags list
check_flags() {
  for flag; do
    printf '%s\n' "$flags" | grep -q -w "$flag" || return 1
  done
}

# Set the CPU flags list
# remove underscores and points from flags, e.g. gcc uses avx512vnni, while some cpuinfo can have avx512_vnni, some systems use sse4_1 others sse4.1
get_flags() {
  flags=$(awk '/^flags[ \t]*:|^Features[ \t]*:/{gsub(/^flags[ \t]*:[ \t]*|^Features[ \t]*:[ \t]*|[_.]/, ""); line=$0} END{print line}' /proc/cpuinfo)
}

# Check for gcc march "znver1" or "znver2" https://en.wikichip.org/wiki/amd/cpuid
check_znver_1_2() {
  vendor_id=$(awk '/^vendor_id/{print $3; exit}' /proc/cpuinfo)
  cpu_family=$(awk '/^cpu family/{print $4; exit}' /proc/cpuinfo)
  [ "$vendor_id" = "AuthenticAMD" ] && [ "$cpu_family" = "23" ] && znver_1_2=true
}

# Set the file CPU loongarch64 architecture
set_arch_loongarch64() {
  if check_flags 'lasx'; then
    true_arch='loongarch64-lasx'
  elif check_flags 'lsx'; then
    true_arch='lonngarch64-lsx'
  else
    true_arch='loongarch64'
  fi
}

# Set the file CPU x86_64 architecture
set_arch_x86_64() {
  if check_flags 'avx512vnni' 'avx512dq' 'avx512f' 'avx512bw' 'avx512vl'; then
    true_arch='x86-64-vnni256'
  elif check_flags 'avx512f' 'avx512bw'; then
    true_arch='x86-64-avx512'
  elif [ -z "${znver_1_2+1}" ] && check_flags 'bmi2'; then
    true_arch='x86-64-bmi2'
  elif check_flags 'avx2'; then
    true_arch='x86-64-avx2'
  elif check_flags 'sse41' && check_flags 'popcnt'; then
    true_arch='x86-64-sse41-popcnt'
  else
    true_arch='x86-64'
  fi
}

set_arch_ppc_64() {
  if $(grep -q -w "altivec" /proc/cpuinfo); then
    power=$(grep -oP -m 1 'cpu\t+: POWER\K\d+' /proc/cpuinfo)
    if [ "0$power" -gt 7 ]; then
      # VSX started with POWER8
      true_arch='ppc-64-vsx'
    else
      true_arch='ppc-64-altivec'
    fi
  else
    true_arch='ppc-64'
  fi
}

# Check the system type
uname_s=$(uname -s)
uname_m=$(uname -m)
case $uname_s in
  'Darwin') # Mac OSX system
    case $uname_m in
      'arm64')
        true_arch='apple-silicon'
        file_arch='m1-apple-silicon'
        ;;
      'x86_64')
        flags=$(sysctl -n machdep.cpu.features machdep.cpu.leaf7_features | tr '\n' ' ' | tr '[:upper:]' '[:lower:]' | tr -d '_.')
        set_arch_x86_64
        if [ "$true_arch" = 'x86-64-vnni256' ] || [ "$true_arch" = 'x86-64-avx512' ]; then
           file_arch='x86-64-bmi2'
        fi
        ;;
    esac
    file_os='macos'
    file_ext='tar'
    ;;
  'Linux') # Linux system
    get_flags
    case $uname_m in
      'x86_64')
        file_os='ubuntu'
        check_znver_1_2
        set_arch_x86_64
        ;;
      'i686')
        file_os='ubuntu'
        true_arch='x86-32'
        ;;
      'ppc64'*)
        file_os='ubuntu'
        set_arch_ppc_64
        ;;
      'aarch64')
        file_os='android'
        true_arch='armv8'
        if check_flags 'asimddp'; then
          true_arch="$true_arch-dotprod"
        fi
        ;;
      'armv7'*)
        file_os='android'
        true_arch='armv7'
        if check_flags 'neon'; then
          true_arch="$true_arch-neon"
        fi
        ;;
      'loongarch64'*)
        file_os='linux'
        set_arch_loongarch64
        ;;
      *) # Unsupported machine type, exit with error
        printf 'Unsupported machine type: %s\n' "$uname_m"
        exit 1
        ;;
    esac
    file_ext='tar'
    ;;
  'CYGWIN'*|'MINGW'*|'MSYS'*) # Windows system with POSIX compatibility layer
    get_flags
    check_znver_1_2
    set_arch_x86_64
    file_os='windows'
    file_ext='zip'
    ;;
  *)
    # Unknown system type, exit with error
    printf 'Unsupported system type: %s\n' "$uname_s"
    exit 1
    ;;
esac

if [ -z "$file_arch" ]; then
  file_arch=$true_arch
fi

file_name="stockfish-$file_os-$file_arch.$file_ext"

printf '%s %s\n' "$true_arch" "$file_name"
