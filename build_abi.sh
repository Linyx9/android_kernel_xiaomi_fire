# SPDX-License-Identifier: GPL-2.0
#!/bin/bash

set -e

DEVICE_MODULES_DIR=$(basename $(dirname $0))
source "${DEVICE_MODULES_DIR}/kernel/kleaf/_setup_env.sh"

CCACHE_EXEC=${CCACHE_EXEC:-$(command -v ccache || true)}
CCACHE_DIR=${CCACHE_DIR:-${HOME}/.cache/ccache/inferno-kernel}
CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-35G}
CCACHE_CPP2=${CCACHE_CPP2:-yes}
CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR:-true}
KLEAF_CCACHE_WRAPPER_DIR=${KLEAF_CCACHE_WRAPPER_DIR:-${CCACHE_DIR}/wrappers}

KLEAF_CCACHE_ARGS=()
if [[ -n "${CCACHE_EXEC}" ]]
then
  mkdir -p "${CCACHE_DIR}"
  mkdir -p "${KLEAF_CCACHE_WRAPPER_DIR}"
  ln -sf "${CCACHE_EXEC}" "${KLEAF_CCACHE_WRAPPER_DIR}/clang"
  ln -sf "${CCACHE_EXEC}" "${KLEAF_CCACHE_WRAPPER_DIR}/clang++"
  "${CCACHE_EXEC}" -M "${CCACHE_MAXSIZE}" >/dev/null
  export CCACHE_EXEC CCACHE_DIR CCACHE_MAXSIZE CCACHE_CPP2 CCACHE_NOHASHDIR KLEAF_CCACHE_WRAPPER_DIR
  KLEAF_CCACHE_ARGS=(
	"--action_env=CCACHE_EXEC=${CCACHE_EXEC}"
	"--action_env=CCACHE_DIR=${CCACHE_DIR}"
	"--action_env=CCACHE_MAXSIZE=${CCACHE_MAXSIZE}"
	"--action_env=CCACHE_CPP2=${CCACHE_CPP2}"
	"--action_env=CCACHE_NOHASHDIR=${CCACHE_NOHASHDIR}"
	"--action_env=KLEAF_CCACHE_WRAPPER_DIR=${KLEAF_CCACHE_WRAPPER_DIR}"
	"--sandbox_writable_path=${CCACHE_DIR}"
  )
fi

# run kleaf commands or legacy build.sh
result=$(echo ${KLEAF_SUPPORTED_PROJECTS} | grep -wo ${PROJECT}) || result=""
if [[ ${result} != "" ]]
then # run kleaf commands

build_scope=internal
if [ ! -d "vendor/mediatek/tests/kernel" ]
then
  build_scope=customer
fi

KLEAF_OUT=("--output_user_root=${OUT_DIR} --output_base=${OUT_DIR}/bazel/output_user_root/output_base")
KLEAF_ARGS=("${DEBUG_ARGS} ${SANDBOX_ARGS} ${KLEAF_CCACHE_ARGS[*]} --experimental_writable_outputs --noenable_bzlmod")

set -x
(
  tools/bazel ${KLEAF_OUT} run ${KLEAF_ARGS} \
	--//build/bazel_mgk_rules:kernel_version=${KERNEL_VERSION_NUM} \
	//${DEVICE_MODULES_DIR}:${PROJECT}.user_${build_scope}_abi_update_symbol_list
  tools/bazel ${KLEAF_OUT} run ${KLEAF_ARGS} //${KERNEL_VERSION}:kernel_aarch64_abi_update
)

if [[ ${MODE} == "user" && ${KLEAF_GKI_CHECKER} != "no" ]]
then
  KLEAF_GKI_CHECKER_COMMANDS=("${KLEAF_GKI_CHECKER_COMMANDS} \
	  -m ${OUT_DIR}/bazel/output_user_root/output_base/execroot/__main__/bazel-out/k8-fastbuild*/bin/${DEVICE_MODULES_DIR}/${PROJECT}_kernel_aarch64.${MODE}/vmlinux")
  set -x
  (
    ${KLEAF_GKI_CHECKER_COMMANDS} -o file
    ${KLEAF_GKI_CHECKER_COMMANDS} -o config
    ${KLEAF_GKI_CHECKER_COMMANDS} -o symbol
  )
  set +x
fi

else
  echo "Cannnot support ABI check for ${PROJECT}!"
  exit 1
fi
