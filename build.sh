#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ARCH="${ARCH:-arm64}"
DEVICE="${DEVICE:-}"
TARGET_CONFIG=
PREBUILT=0
PREBUILT_ZIP=0
PREBUILT_TGZ=0
ANYKERNEL_ZIP=0
MANUAL=0
AUTO=0
CHECK=0
FETCH_CLANG=0
USE_CCACHE=1
AK3_INCLUDE_DTBO="${AK3_INCLUDE_DTBO:-1}"
ACTION=
ZIP_LEVEL=9
ZIP_LEVEL_SET=0
PINNED_CLANG_REVISION="${PINNED_CLANG_REVISION:-clang-r383902b}"
CLANG_REVISION="${CLANG_REVISION:-}"
TOOLCHAIN_BASE="${TOOLCHAIN_BASE:-${XDG_CACHE_HOME:-$HOME/.cache}/android-kernel-toolchains}"
LANG_CODE="${BUILD_LANG:-${LC_ALL:-${LC_MESSAGES:-${LANG:-en}}}}"
case "$LANG_CODE" in
	ru*) LANG_CODE=ru ;;
	*) LANG_CODE=en ;;
esac

for argument in "$@"; do
	case "$argument" in
		--lang=ru|--lang=en)
			LANG_CODE="${argument#*=}"
			;;
	esac
done

die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

usage() {
	if [[ "$LANG_CODE" == ru ]]; then
		cat <<EOF
Использование: $0 [конфиг] [параметры]

Результаты сборки:
  -p, --prebuilt         Экспортировать kernel, mtk_dtb и dtbo.img для прошивки
  -z, --zip              Упаковать prebuilts в ZIP (требует --prebuilt)
  -t, --tgz              Упаковать prebuilts в tar.gz (требует --prebuilt)
  --level N              Уровень сжатия ZIP: 0-9 (требует --zip)
  -a, --azip             Создать прошиваемый AnyKernel3 ZIP (несовместим с --prebuilt)
  --no-ak3-dtbo          Не включать dtbo.img в AnyKernel3 ZIP

Toolchain и зависимости:
  -A, --auto             Установить зависимости и загрузить выбранный clang
  -c, --check            Проверить зависимости и выйти
  --fetch-clang          Загрузить выбранный clang, если он отсутствует
  --cver REVISION        Выбрать ревизию Android clang
  --list-cver            Показать доступные в AOSP ревизии Android clang
  --list-installed-cver  Показать установленные локально ревизии Android clang
  --detect-cver          Предложить подходящую ревизию clang для текущего дерева
  -m, --manual           Отключить автоматические проверки, загрузки и AnyKernel ZIP
  --no-ccache            Собирать без ccache
  -j, --jobs N           Задать число параллельных задач

Обслуживание:
  --clean                Выполнить clean и удалить каталог результата плюс dist/
  --mrproper             Выполнить mrproper для каталога результата
  --fclean               Очистить ccache, clean, mrproper и все каталоги результата

Интеграция с shell:
  --list-configs         Показать обнаруженные defconfig текущего дерева
  --autocomplete=SHELL   Напечатать completion для bash, zsh или fish
  --lang=LANG            Выбрать язык ru или en
  -h, --help             Показать эту справку

Короткие флаги можно группировать, например: -pzt

Командные псевдонимы:
  clean, mrproper, fclean, check, configs
  clang-list, clang-installed, clang-detect
EOF
	else
		cat <<EOF
Usage: $0 [config] [options]

Build outputs:
  -p, --prebuilt         Export firmware prebuilts: kernel, mtk_dtb and dtbo.img
  -z, --zip              Pack firmware prebuilts into a ZIP (requires --prebuilt)
  -t, --tgz              Pack firmware prebuilts into a tar.gz (requires --prebuilt)
  --level N              ZIP compression level: 0-9 (requires --zip)
  -a, --azip             Create an AnyKernel3 flashable ZIP (conflicts with --prebuilt)
  --no-ak3-dtbo          Do not include dtbo.img in the AnyKernel3 ZIP

Toolchain and dependencies:
  -A, --auto             Install build dependencies and download the selected clang
  -c, --check            Check dependencies and exit
  --fetch-clang          Download the selected clang if it is missing
  --cver REVISION        Select an Android clang revision
  --list-cver            List Android clang revisions available from AOSP
  --list-installed-cver  List locally installed Android clang revisions
  --detect-cver          Print the likely clang revision for this kernel tree
  -m, --manual           Disable implicit checks, clang download and AnyKernel ZIP
  --no-ccache            Build without ccache
  -j, --jobs N           Set parallel build jobs

Maintenance:
  --clean                Run clean and remove this output plus dist/
  --mrproper             Run mrproper for this output
  --fclean               Clear ccache, run clean/mrproper and remove all build outputs

Shell integration:
  --list-configs         List detected defconfigs from the current kernel tree
  --autocomplete=SHELL   Print completion for bash, zsh or fish
  --lang=LANG            Select ru or en language
  -h, --help             Show this help

Short flags may be grouped, for example: -pzt

Command aliases:
  clean, mrproper, fclean, check, configs
  clang-list, clang-installed, clang-detect
EOF
	fi
	printf '\n'
	if [[ "$LANG_CODE" == ru ]]; then
		printf 'Обнаруженные конфиги:\n'
	else
		printf 'Detected configs:\n'
	fi
	list_configs
}

list_configs() {
	find "$ROOT_DIR/arch/$ARCH/configs" -maxdepth 1 -type f -name '*defconfig' -printf '%f\n' |
		sort
}

generate_completion() {
	local shell="$1"

	case "$shell" in
		bash)
			cat <<'EOF'
_moonlightkernel_build() {
	local configs cur prev opts
	COMPREPLY=()
	cur="${COMP_WORDS[COMP_CWORD]}"
	prev="${COMP_WORDS[COMP_CWORD-1]}"
	configs="$(./build.sh --list-configs 2>/dev/null)"
	opts="$configs clean mrproper fclean check configs clang-list clang-installed clang-detect -p -z -t -a -A -m -c -h --prebuilt --zip --tgz --level --azip --no-ak3-dtbo --auto --check --fetch-clang --cver --list-cver --list-installed-cver --detect-cver --manual --no-ccache --jobs --clean --mrproper --fclean --list-configs --lang=ru --lang=en --autocomplete=bash --autocomplete=zsh --autocomplete=fish --help"
	case "$prev" in
		--level) COMPREPLY=( $(compgen -W "0 1 2 3 4 5 6 7 8 9" -- "$cur") ); return ;;
		--autocomplete) COMPREPLY=( $(compgen -W "bash zsh fish" -- "$cur") ); return ;;
	esac
	COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
}
complete -F _moonlightkernel_build ./build.sh build.sh
EOF
			;;
			zsh)
				if [[ "$LANG_CODE" == ru ]]; then
					cat <<'EOF'
#compdef build.sh
local -a configs
configs=("${(@f)$(./build.sh --list-configs 2>/dev/null)}" clean mrproper fclean check configs clang-list clang-installed clang-detect)
_arguments \
  '1:конфиг:($configs)' \
  '(-p --prebuilt)'{-p,--prebuilt}'[экспортировать prebuilts прошивки]' \
  '(-z --zip)'{-z,--zip}'[упаковать prebuilts в ZIP]' \
  '(-t --tgz)'{-t,--tgz}'[упаковать prebuilts в tar.gz]' \
  '--level[уровень сжатия ZIP]:уровень:(0 1 2 3 4 5 6 7 8 9)' \
  '(-a --azip)'{-a,--azip}'[создать AnyKernel3 ZIP]' \
  '--no-ak3-dtbo[не включать dtbo.img в AnyKernel3 ZIP]' \
  '(-A --auto)'{-A,--auto}'[установить зависимости и clang]' \
  '(-c --check)'{-c,--check}'[проверить зависимости]' \
  '--fetch-clang[загрузить clang]' \
  '--cver[выбрать ревизию clang]:ревизия:' \
  '--list-cver[показать доступные ревизии clang]' \
  '--list-installed-cver[показать установленные ревизии clang]' \
  '--detect-cver[предложить ревизию clang]' \
  '(-m --manual)'{-m,--manual}'[отключить автоматические действия]' \
  '--no-ccache[собирать без ccache]' \
  '--jobs[параллельные задачи]:число:' \
  '--clean[очистить результат]' \
  '--mrproper[выполнить mrproper]' \
  '--fclean[полная очистка]' \
  '--list-configs[показать defconfig]' \
  '--lang=[выбрать язык]:язык:(ru en)' \
  '--autocomplete=[напечатать completion]:shell:(bash zsh fish)' \
  '(-h --help)'{-h,--help}'[показать справку]'
EOF
				else
					cat <<'EOF'
#compdef build.sh
local -a configs
configs=("${(@f)$(./build.sh --list-configs 2>/dev/null)}" clean mrproper fclean check configs clang-list clang-installed clang-detect)
_arguments \
  '1:config:($configs)' \
  '(-p --prebuilt)'{-p,--prebuilt}'[export firmware prebuilts]' \
  '(-z --zip)'{-z,--zip}'[pack firmware prebuilts into ZIP]' \
  '(-t --tgz)'{-t,--tgz}'[pack firmware prebuilts into tar.gz]' \
  '--level[ZIP compression level]:level:(0 1 2 3 4 5 6 7 8 9)' \
  '(-a --azip)'{-a,--azip}'[create AnyKernel3 ZIP]' \
  '--no-ak3-dtbo[do not include dtbo.img in the AnyKernel3 ZIP]' \
  '(-A --auto)'{-A,--auto}'[install dependencies and clang]' \
  '(-c --check)'{-c,--check}'[check dependencies]' \
  '--fetch-clang[download clang]' \
  '--cver[select clang revision]:revision:' \
  '--list-cver[list available clang revisions]' \
  '--list-installed-cver[list installed clang revisions]' \
  '--detect-cver[suggest a clang revision]' \
  '(-m --manual)'{-m,--manual}'[disable implicit automation]' \
  '--no-ccache[build without ccache]' \
  '--jobs[parallel jobs]:jobs:' \
  '--clean[clean output]' \
  '--mrproper[run mrproper]' \
  '--fclean[clear all outputs]' \
  '--list-configs[list defconfigs]' \
  '--lang=[select language]:language:(ru en)' \
  '--autocomplete=[print completion]:shell:(bash zsh fish)' \
  '(-h --help)'{-h,--help}'[show help]'
EOF
				fi
				;;
			fish)
				if [[ "$LANG_CODE" == ru ]]; then
					cat <<'EOF'
complete -c build.sh -f
complete -c build.sh -a '(./build.sh --list-configs 2>/dev/null) clean mrproper fclean check configs clang-list clang-installed clang-detect'
complete -c build.sh -s p -l prebuilt -d 'Экспортировать prebuilts прошивки'
complete -c build.sh -s z -l zip -d 'Упаковать prebuilts в ZIP'
complete -c build.sh -s t -l tgz -d 'Упаковать prebuilts в tar.gz'
complete -c build.sh -l level -xa '0 1 2 3 4 5 6 7 8 9' -d 'Уровень сжатия ZIP'
complete -c build.sh -s a -l azip -d 'Создать AnyKernel3 ZIP'
complete -c build.sh -l no-ak3-dtbo -d 'Не включать dtbo.img в AnyKernel3 ZIP'
complete -c build.sh -s A -l auto -d 'Установить зависимости и clang'
complete -c build.sh -s c -l check -d 'Проверить зависимости'
complete -c build.sh -l fetch-clang -d 'Загрузить clang'
complete -c build.sh -l cver -x -d 'Выбрать ревизию clang'
complete -c build.sh -l list-cver -d 'Показать доступные ревизии clang'
complete -c build.sh -l list-installed-cver -d 'Показать установленные ревизии clang'
complete -c build.sh -l detect-cver -d 'Предложить ревизию clang'
complete -c build.sh -s m -l manual -d 'Отключить автоматические действия'
complete -c build.sh -l no-ccache -d 'Собирать без ccache'
complete -c build.sh -s j -l jobs -x -d 'Параллельные задачи'
complete -c build.sh -l clean -d 'Очистить результат'
complete -c build.sh -l mrproper -d 'Выполнить mrproper'
complete -c build.sh -l fclean -d 'Полная очистка'
complete -c build.sh -l list-configs -d 'Показать defconfig'
complete -c build.sh -l lang -xa 'ru en' -d 'Выбрать язык'
complete -c build.sh -l autocomplete -xa 'bash zsh fish' -d 'Напечатать completion'
complete -c build.sh -s h -l help -d 'Показать справку'
EOF
				else
					cat <<'EOF'
complete -c build.sh -f
complete -c build.sh -a '(./build.sh --list-configs 2>/dev/null) clean mrproper fclean check configs clang-list clang-installed clang-detect'
complete -c build.sh -s p -l prebuilt -d 'Export firmware prebuilts'
complete -c build.sh -s z -l zip -d 'Pack firmware prebuilts into ZIP'
complete -c build.sh -s t -l tgz -d 'Pack firmware prebuilts into tar.gz'
complete -c build.sh -l level -xa '0 1 2 3 4 5 6 7 8 9' -d 'ZIP compression level'
complete -c build.sh -s a -l azip -d 'Create AnyKernel3 ZIP'
complete -c build.sh -l no-ak3-dtbo -d 'Do not include dtbo.img in the AnyKernel3 ZIP'
complete -c build.sh -s A -l auto -d 'Install dependencies and clang'
complete -c build.sh -s c -l check -d 'Check dependencies'
complete -c build.sh -l fetch-clang -d 'Download clang'
complete -c build.sh -l cver -x -d 'Select clang revision'
complete -c build.sh -l list-cver -d 'List available clang revisions'
complete -c build.sh -l list-installed-cver -d 'List installed clang revisions'
complete -c build.sh -l detect-cver -d 'Suggest a clang revision'
complete -c build.sh -s m -l manual -d 'Disable implicit automation'
complete -c build.sh -l no-ccache -d 'Build without ccache'
complete -c build.sh -s j -l jobs -x -d 'Parallel jobs'
complete -c build.sh -l clean -d 'Clean output'
complete -c build.sh -l mrproper -d 'Run mrproper'
complete -c build.sh -l fclean -d 'Clear all outputs'
complete -c build.sh -l list-configs -d 'List defconfigs'
complete -c build.sh -l lang -xa 'ru en' -d 'Select language'
complete -c build.sh -l autocomplete -xa 'bash zsh fish' -d 'Print completion'
complete -c build.sh -s h -l help -d 'Show help'
EOF
				fi
			;;
		*)
			die "unsupported completion shell: $shell"
			;;
	esac
}

need_value() {
	(($# >= 2)) || die "$1 requires a value"
}

while (($#)); do
	case "$1" in
		-p|--prebuilt)
			PREBUILT=1
			;;
		-z|--zip)
			PREBUILT_ZIP=1
			;;
		-t|--tgz)
			PREBUILT_TGZ=1
			;;
		-a|--azip)
			ANYKERNEL_ZIP=1
			;;
		--no-ak3-dtbo)
			AK3_INCLUDE_DTBO=0
			;;
		--level)
			need_value "$@"
			ZIP_LEVEL="$2"
			ZIP_LEVEL_SET=1
			shift
			;;
		--level=*)
			ZIP_LEVEL="${1#*=}"
			ZIP_LEVEL_SET=1
			;;
		-A|--auto)
			AUTO=1
			;;
		-c|--check|check)
			CHECK=1
			;;
		--fetch-clang)
			FETCH_CLANG=1
			;;
		--cver)
			need_value "$@"
			CLANG_REVISION="$2"
			shift
			;;
		--cver=*)
			CLANG_REVISION="${1#*=}"
			;;
		--list-cver|clang-list)
			ACTION=list-cver
			;;
		--list-installed-cver|clang-installed)
			ACTION=list-installed-cver
			;;
		--detect-cver|clang-detect)
			ACTION=detect-cver
			;;
		--list-configs|configs)
			ACTION=list-configs
			;;
		--lang)
			need_value "$@"
			LANG_CODE="$2"
			shift
			;;
		--lang=*)
			LANG_CODE="${1#*=}"
			;;
		-m|--manual)
			MANUAL=1
			;;
		--no-ccache)
			USE_CCACHE=0
			;;
		-j|--jobs)
			need_value "$@"
			JOBS="$2"
			shift
			;;
		-j*)
			JOBS="${1#-j}"
			;;
		--jobs=*)
			JOBS="${1#*=}"
			;;
		-[pztamcAh]*)
			short_flags="${1#-}"
			while [[ -n "$short_flags" ]]; do
				short_flag="${short_flags:0:1}"
				short_flags="${short_flags:1}"
				case "$short_flag" in
					p) PREBUILT=1 ;;
					z) PREBUILT_ZIP=1 ;;
					t) PREBUILT_TGZ=1 ;;
					a) ANYKERNEL_ZIP=1 ;;
					A) AUTO=1 ;;
					m) MANUAL=1 ;;
					c) CHECK=1 ;;
					h) usage; exit 0 ;;
				esac
			done
			;;
		--clean|clean)
			ACTION=clean
			;;
		--mrproper|mrproper)
			ACTION=mrproper
			;;
		--fclean|fclean)
			ACTION=fclean
			;;
		--autocomplete=*)
			generate_completion "${1#*=}"
			exit 0
			;;
		--autocomplete)
			need_value "$@"
			generate_completion "$2"
			exit 0
			;;
		-h|--help)
			usage
			exit 0
			;;
		-*)
			usage >&2
			die "unknown argument: $1"
			;;
		*)
			[[ -z "$TARGET_CONFIG" ]] || die "only one kernel config may be selected"
			TARGET_CONFIG="$1"
			;;
	esac
	shift
done

[[ "$LANG_CODE" == ru || "$LANG_CODE" == en ]] || die "--lang must be ru or en"
[[ "$AK3_INCLUDE_DTBO" == 0 || "$AK3_INCLUDE_DTBO" == 1 ]] || die "AK3_INCLUDE_DTBO must be 0 or 1"
JOBS="${JOBS:-$(nproc --all)}"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die "jobs must be a positive integer"
[[ "$ZIP_LEVEL" =~ ^[0-9]$ ]] || die "ZIP compression level must be between 0 and 9"
((PREBUILT_ZIP == 0 || PREBUILT == 1)) || die "--zip requires --prebuilt"
((PREBUILT_TGZ == 0 || PREBUILT == 1)) || die "--tgz requires --prebuilt"
((ZIP_LEVEL_SET == 0 || PREBUILT_ZIP == 1)) || die "--level requires --zip"
((PREBUILT == 0 || ANYKERNEL_ZIP == 0)) || die "--azip conflicts with --prebuilt"
((MANUAL == 0 || AUTO == 0)) || die "--manual conflicts with --auto"

resolve_config() {
	local candidate="${DEFCONFIG:-$TARGET_CONFIG}"

	candidate="${candidate:-defconfig}"
	if [[ -f "$ROOT_DIR/arch/$ARCH/configs/$candidate" ]]; then
		DEFCONFIG="$candidate"
	elif [[ -f "$ROOT_DIR/arch/$ARCH/configs/${candidate}_defconfig" ]]; then
		DEFCONFIG="${candidate}_defconfig"
	else
		die "kernel config not found: $candidate; use --list-configs"
	fi
	DEVICE="${DEVICE:-${DEFCONFIG%_defconfig}}"
}

if [[ -z "$ACTION" || "$ACTION" == clean || "$ACTION" == mrproper || "$ACTION" == fclean ]]; then
	resolve_config
fi
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out/$DEVICE}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
PREBUILT_KERNEL_OUT="${PREBUILT_KERNEL_OUT:-$DIST_DIR/kernel}"
PREBUILT_DTB_OUT="${PREBUILT_DTB_OUT:-$DIST_DIR/mtk_dtb}"
PREBUILT_DTBO_OUT="${PREBUILT_DTBO_OUT:-$DIST_DIR/dtbo.img}"
BUILD_NAME="${BUILD_NAME:-MoonLightKernel}"
CCACHE_DIR="${CCACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/ccache/inferno-kernel}"
ANYKERNEL_REPO="${ANYKERNEL_REPO:-https://github.com/osm0sis/AnyKernel3.git}"
ANYKERNEL_COMMIT="${ANYKERNEL_COMMIT:-dca9dc370838d919d56c1f59ec78b27a14a72c68}"
ANYKERNEL_CACHE="${ANYKERNEL_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/AnyKernel3}"

require_command() {
	command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

list_installed_clang() {
	local directory found=0

	[[ -d "$TOOLCHAIN_BASE" ]] || return
	while IFS= read -r directory; do
		[[ -x "$directory/bin/clang" ]] || continue
		basename "$directory"
		found=1
	done < <(find "$TOOLCHAIN_BASE" -mindepth 1 -maxdepth 1 -type d -name 'clang-*' | sort -V)
	((found)) || true
}

select_clang() {
	local installed

	if [[ -z "$CLANG_REVISION" ]]; then
		installed="$(list_installed_clang | tail -n 1)"
		CLANG_REVISION="${installed:-$PINNED_CLANG_REVISION}"
	fi
	[[ "$CLANG_REVISION" == clang-* ]] || CLANG_REVISION="clang-$CLANG_REVISION"
	TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-$TOOLCHAIN_BASE/$CLANG_REVISION}"
	CLANG_URL="${CLANG_URL:-https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/android11-release/${CLANG_REVISION}.tar.gz}"
}

list_available_clang() {
	local url

	require_command curl
	url="https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+/refs/heads/android11-release/?format=JSON"
	curl -fsSL "$url" |
		sed -n 's/.*"\(clang-r[0-9][0-9]*[a-z]*\)".*/\1/p' |
		sort -Vu
}

detect_clang() {
	local major minor recommendation

	major="$(sed -n 's/^VERSION = //p' "$ROOT_DIR/Makefile")"
	minor="$(sed -n 's/^PATCHLEVEL = //p' "$ROOT_DIR/Makefile")"
	case "$major.$minor" in
		4.14|4.19|5.4)
			recommendation=clang-r383902b
			;;
		5.10)
			recommendation=clang-r416183b
			;;
		*)
			recommendation="$PINNED_CLANG_REVISION"
			;;
	esac
	printf '%s\n' "$recommendation"
	printf 'Detected Linux %s.%s; recommendation is heuristic, verify with a full build.\n' \
		"$major" "$minor" >&2
}

check_dependencies() {
	local command missing=0
	local -a commands=(make curl tar git zip gzip mkdtboimg)

	((USE_CCACHE)) && commands+=(ccache)
	for command in "${commands[@]}"; do
		if ! command -v "$command" >/dev/null 2>&1; then
			printf 'Missing dependency: %s\n' "$command" >&2
			missing=1
		fi
	done
	((missing == 0)) || die "install missing dependencies or run with --auto"
	printf 'Dependency check passed.\n'
}

as_root() {
	if ((EUID == 0)); then
		"$@"
	else
		require_command sudo
		sudo "$@"
	fi
}

install_dependencies() {
	if command -v pacman >/dev/null 2>&1; then
		as_root pacman -S --needed --noconfirm base-devel bc bison ccache curl dtc flex git libelf openssl pahole perl python tar unzip zip android-tools
	elif command -v apt-get >/dev/null 2>&1; then
		as_root apt-get update
		as_root apt-get install -y build-essential bc bison ccache curl device-tree-compiler flex git libelf-dev libssl-dev dwarves python3 tar unzip zip
	elif command -v dnf >/dev/null 2>&1; then
		as_root dnf install -y bc bison ccache curl dtc elfutils-libelf-devel flex gcc git make openssl-devel pahole perl python3 tar unzip zip
	elif command -v zypper >/dev/null 2>&1; then
		as_root zypper --non-interactive install bc bison ccache curl dtc flex gcc git libelf-devel libopenssl-devel make pahole perl python3 tar unzip zip
	elif command -v apk >/dev/null 2>&1; then
		as_root apk add bash bc bison build-base ccache curl dtc flex git libelf openssl-dev pahole perl python3 tar unzip zip
	else
		die "supported package manager not found; install dependencies manually"
	fi
}

fetch_clang() {
	local archive tmp_dir

	[[ -x "$TOOLCHAIN_DIR/bin/clang" ]] && return
	require_command curl
	require_command tar
	printf 'Downloading Android Clang %s...\n' "$CLANG_REVISION"
	archive="$(mktemp)"
	tmp_dir="$(mktemp -d)"
	trap 'rm -f "$archive"; rm -rf "$tmp_dir"' RETURN
	curl -fL --retry 2 "$CLANG_URL" -o "$archive"
	tar -xzf "$archive" -C "$tmp_dir"
	mkdir -p "$(dirname "$TOOLCHAIN_DIR")"
	rm -rf "$TOOLCHAIN_DIR"
	mv "$tmp_dir" "$TOOLCHAIN_DIR"
	rm -f "$archive"
	trap - RETURN
}

run_make_maintenance() {
	local target="$1"

	[[ -d "$OUT_DIR" ]] || return
	require_command make
	make O="$OUT_DIR" ARCH="$ARCH" "$target"
}

run_action() {
	case "$ACTION" in
		list-cver)
			list_available_clang
			;;
		list-installed-cver)
			list_installed_clang
			;;
		detect-cver)
			detect_clang
			;;
		list-configs)
			list_configs
			;;
		clean)
			run_make_maintenance clean
			rm -rf "$OUT_DIR" "$DIST_DIR"
			;;
		mrproper)
			run_make_maintenance mrproper
			;;
		fclean)
			run_make_maintenance clean
			run_make_maintenance mrproper
			if command -v ccache >/dev/null 2>&1; then
				CCACHE_DIR="$CCACHE_DIR" ccache --clear
			else
				rm -rf "$CCACHE_DIR"
			fi
			rm -rf "$ROOT_DIR/out" "$OUT_DIR" "$DIST_DIR"
			;;
	esac
}

build_dtbo_image() {
	local boot_dir dtbo_image
	local -a overlays

	boot_dir="$OUT_DIR/arch/$ARCH/boot"
	dtbo_image="$boot_dir/dtbo.img"
	rm -f "$dtbo_image"
	mapfile -t overlays < <(find "$boot_dir/dts" -type f -name '*.dtbo' -print 2>/dev/null | sort)
	((${#overlays[@]})) || return

	require_command mkdtboimg
	mkdtboimg create "$dtbo_image" --page_size=2048 "${overlays[@]}"
	printf 'DTBO image: %s\n' "$dtbo_image"
}

prepare_prebuilt() {
	local dtb_dir dtbo_image image
	local -a dtbs

	image="$OUT_DIR/arch/$ARCH/boot/Image.gz"
	[[ -f "$image" ]] || die "kernel image was not produced"
	dtb_dir="$OUT_DIR/arch/$ARCH/boot/dts"
	dtbo_image="$OUT_DIR/arch/$ARCH/boot/dtbo.img"

	mkdir -p "$(dirname "$PREBUILT_KERNEL_OUT")"
	cp "$image" "$PREBUILT_KERNEL_OUT"
	printf 'Prebuilt kernel: %s\n' "$PREBUILT_KERNEL_OUT"

	rm -f "$PREBUILT_DTB_OUT"
	rm -rf "$DIST_DIR/dtbs"
	mapfile -t dtbs < <(find "$dtb_dir" -type f -name '*.dtb' -print 2>/dev/null | sort)
	if ((${#dtbs[@]} == 1)); then
		mkdir -p "$(dirname "$PREBUILT_DTB_OUT")"
		cp "${dtbs[0]}" "$PREBUILT_DTB_OUT"
		printf 'Prebuilt DTB image: %s\n' "$PREBUILT_DTB_OUT"
	elif ((${#dtbs[@]} > 1)); then
		mkdir -p "$DIST_DIR/dtbs"
		cp "${dtbs[@]}" "$DIST_DIR/dtbs/"
		printf 'Prebuilt DTB directory: %s\n' "$DIST_DIR/dtbs"
	fi

	rm -f "$PREBUILT_DTBO_OUT"
	if [[ -f "$dtbo_image" ]]; then
		mkdir -p "$(dirname "$PREBUILT_DTBO_OUT")"
		cp "$dtbo_image" "$PREBUILT_DTBO_OUT"
		printf 'Prebuilt DTBO image: %s\n' "$PREBUILT_DTBO_OUT"
	fi
}

kernel_version() {
	git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || printf local
}

package_prebuilt() {
	local archive stage version

	version="$(kernel_version)"
	stage="$(mktemp -d)"
	trap 'rm -rf "$stage"' RETURN
	cp "$PREBUILT_KERNEL_OUT" "$stage/kernel"
	[[ -f "$PREBUILT_DTB_OUT" ]] && cp "$PREBUILT_DTB_OUT" "$stage/mtk_dtb"
	[[ -d "$DIST_DIR/dtbs" ]] && cp -R "$DIST_DIR/dtbs" "$stage/dtbs"
	[[ -f "$PREBUILT_DTBO_OUT" ]] && cp "$PREBUILT_DTBO_OUT" "$stage/dtbo.img"
	mkdir -p "$DIST_DIR"

	if ((PREBUILT_ZIP)); then
		require_command zip
		archive="$DIST_DIR/${BUILD_NAME}-${DEVICE}-${version}-prebuilt.zip"
		rm -f "$archive"
		(cd "$stage" && zip -qr"$ZIP_LEVEL" "$archive" .)
		printf 'Prebuilt ZIP: %s\n' "$archive"
	fi
	if ((PREBUILT_TGZ)); then
		require_command tar
		archive="$DIST_DIR/${BUILD_NAME}-${DEVICE}-${version}-prebuilt.tar.gz"
		rm -f "$archive"
		tar -czf "$archive" -C "$stage" .
		printf 'Prebuilt tar.gz: %s\n' "$archive"
	fi
	rm -rf "$stage"
	trap - RETURN
}

prepare_anykernel() {
	local package_dir image update_binary zip_name version

	image="$OUT_DIR/arch/$ARCH/boot/Image.gz-dtb"
	[[ -f "$image" ]] || image="$OUT_DIR/arch/$ARCH/boot/Image.gz"
	[[ -f "$image" ]] || image="$OUT_DIR/arch/$ARCH/boot/Image"
	[[ -f "$image" ]] || die "kernel image was not produced"
	require_command git
	require_command tar
	require_command zip

	if [[ ! -d "$ANYKERNEL_CACHE/.git" ]]; then
		mkdir -p "$(dirname "$ANYKERNEL_CACHE")"
		git clone --no-checkout "$ANYKERNEL_REPO" "$ANYKERNEL_CACHE"
	fi
	git -C "$ANYKERNEL_CACHE" fetch --depth=1 origin "$ANYKERNEL_COMMIT"

	package_dir="$OUT_DIR/AnyKernel3"
	rm -rf "$package_dir"
	mkdir -p "$package_dir"
	git -C "$ANYKERNEL_CACHE" archive "$ANYKERNEL_COMMIT" | tar -x -C "$package_dir"
	update_binary="$package_dir/META-INF/com/google/android/update-binary"
	if [[ -f "$update_binary" ]]; then
		perl -0pi -e 's/(restore_env\(\) \{\n(?:.*\n)*?  sleep 1;\n)  umount_all;/$1  [ "\$(file_getprop anykernel.sh do.unmount 2>\/dev\/null)" == 1 ] \&\& umount_all;/s' "$update_binary"
		perl -0pi -e 's/\nsetup_env;\n/\nif [ "\$(file_getprop anykernel.sh do.systemmount 2>\/dev\/null)" != 0 ]; then\n  setup_env;\n  AK3_SYSTEMMOUNT=1;\nfi;\n/s; s/\nrestore_env;\n/\n[ "\$AK3_SYSTEMMOUNT" == 1 ] \&\& restore_env;\n/g' "$update_binary"
	fi
	rm -rf "$package_dir/.github" "$package_dir/README.md"
	cp "$image" "$package_dir/$(basename "$image")"
	[[ -f "$ROOT_DIR/AUTHORS" ]] && cp "$ROOT_DIR/AUTHORS" "$package_dir/AUTHORS"
	[[ "$AK3_INCLUDE_DTBO" == 1 && -f "$OUT_DIR/arch/$ARCH/boot/dtbo.img" ]] &&
		cp "$OUT_DIR/arch/$ARCH/boot/dtbo.img" "$package_dir/dtbo.img"

	cat > "$package_dir/anykernel.sh" <<EOF
### AnyKernel3 Ramdisk Mod Script
## Kernel package generated by build.sh

properties() { '
kernel.string=Kernel ${DEVICE}
do.devicecheck=1
do.modules=0
do.systemless=1
do.systemmount=0
do.cleanup=0
do.cleanuponabort=0
do.unmount=0
device.name1=${DEVICE}
supported.versions=
supported.patchlevels=
'; }

boot_attributes() {
set_perm_recursive 0 0 755 644 \$RAMDISK/*;
set_perm_recursive 0 0 750 750 \$RAMDISK/init* \$RAMDISK/sbin;
}

BLOCK=boot;
IS_SLOT_DEVICE=1;
RAMDISK_COMPRESSION=auto;
PATCH_VBMETA_FLAG=auto;

. tools/ak3-core.sh;
dump_boot;
write_boot;
EOF

	version="$(kernel_version)"
	mkdir -p "$DIST_DIR"
	zip_name="$DIST_DIR/${BUILD_NAME}-${DEVICE}-${version}-AnyKernel3.zip"
	rm -f "$zip_name"
	(cd "$package_dir" && zip -qr9 "$zip_name" . -x '.git*')
	printf 'AnyKernel3 package: %s\n' "$zip_name"
}

if [[ -n "$ACTION" ]]; then
	run_action
	exit 0
fi

select_clang
if ((AUTO)); then
	install_dependencies
fi
if ((CHECK)); then
	check_dependencies
	exit 0
fi
if ((!MANUAL)); then
	check_dependencies
fi
if ((AUTO || FETCH_CLANG || !MANUAL)); then
	fetch_clang
fi
[[ -x "$TOOLCHAIN_DIR/bin/clang" ]] ||
	die "clang is missing from $TOOLCHAIN_DIR; use --fetch-clang, --auto or TOOLCHAIN_DIR"

export PATH="$TOOLCHAIN_DIR/bin:$PATH"
export LD_LIBRARY_PATH="$TOOLCHAIN_DIR/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$OUT_DIR"

compiler=clang
if ((USE_CCACHE)); then
	require_command ccache
	export CCACHE_DIR
	export CCACHE_CPP2=yes
	mkdir -p "$CCACHE_DIR"
	ccache --max-size="${CCACHE_MAXSIZE:-10G}" >/dev/null
	compiler="ccache clang"
fi

MAKE_ARGS=(
	O="$OUT_DIR"
	ARCH="$ARCH"
	LLVM=1
	LLVM_IAS=1
	CC="$compiler"
	HOSTCC="$compiler"
	LD=ld.lld
	AS=llvm-as
	AR=llvm-ar
	NM=llvm-nm
	OBJCOPY=llvm-objcopy
	OBJDUMP=llvm-objdump
	READELF=llvm-readelf
	STRIP=llvm-strip
	CROSS_COMPILE=aarch64-linux-gnu-
	LOCALVERSION="${LOCALVERSION:-}"
)
MAKE_TARGETS=()
if [[ -n "${BUILD_TARGETS:-}" ]]; then
	read -r -a MAKE_TARGETS <<<"$BUILD_TARGETS"
fi

printf 'Building %s with %s (%s jobs, ccache %s)...\n' \
	"$DEFCONFIG" "$("$TOOLCHAIN_DIR/bin/clang" --version | head -1)" "$JOBS" \
	"$([[ "$USE_CCACHE" == 1 ]] && printf enabled || printf disabled)"
make "${MAKE_ARGS[@]}" "$DEFCONFIG"
make "${MAKE_ARGS[@]}" olddefconfig
if ((${#MAKE_TARGETS[@]})); then
	printf 'Build targets: %s\n' "${MAKE_TARGETS[*]}"
	make -j"$JOBS" "${MAKE_ARGS[@]}" "${MAKE_TARGETS[@]}"
else
	make -j"$JOBS" "${MAKE_ARGS[@]}"
fi
build_dtbo_image

if ((PREBUILT)); then
	prepare_prebuilt
	((PREBUILT_ZIP || PREBUILT_TGZ)) && package_prebuilt
elif ((ANYKERNEL_ZIP || !MANUAL)); then
	prepare_anykernel
fi

if ((USE_CCACHE)); then
	ccache --show-stats
fi
