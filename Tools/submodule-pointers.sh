#!/usr/bin/env bash
# 子模块指针守卫
#
# 目的：gitlink 必须指向该子模块远端真实存在的提交，否则干净克隆执行
# `git submodule update --init` 会失败（典型事故：把 A 仓库的提交写进 B 仓库的指针）。
#
# 用法：
#   Tools/submodule-pointers.sh check             校验已暂存的 gitlink（pre-commit 调用）
#   Tools/submodule-pointers.sh stage <路径>...   用子模块当前 HEAD 更新 gitlink，写入前先校验全部可达
#
# 失败时以非零退出；stage 模式先校验全部目标，任一不可达则不写入任何指针。

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "${REPO_ROOT}"

MODE="${1:-check}"
shift || true

usage() {
    echo "用法：" >&2
    echo "  $0 check             校验已暂存的 gitlink 在对应子模块远端可达" >&2
    echo "  $0 stage <路径>...   用子模块当前 HEAD 更新 gitlink（先校验后写入）" >&2
}

# 在子模块内部执行 git。
# pre-commit 运行期间 GIT_DIR/GIT_WORK_TREE/GIT_INDEX_FILE 指向父仓库，
# 不清除会让 git -C 子模块误用父仓库的 git 目录。
submodule_git() {
    local path="$1"
    shift
    env -u GIT_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE git -C "${path}" "$@"
}

# 由子模块路径反查 .gitmodules 中的远端 URL（路径与 submodule 名可能不同）
submodule_url() {
    local path="$1" key value name
    while read -r key value; do
        if [[ "${key}" == submodule.*.path && "${value}" == "${path}" ]]; then
            name="${key#submodule.}"
            name="${name%.path}"
            git config -f .gitmodules --get "submodule.${name}.url"
            return 0
        fi
    done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$')
    return 1
}

# 断言 sha 在对应子模块远端可达：fetch 该 SHA 成功且 cat-file -e 通过
verify_reachable() {
    local path="$1" url="$2" sha="$3" tmp
    if [[ ! "${sha}" =~ ^[0-9a-f]{40}$ ]]; then
        echo "  ${path}: 指针不是完整的 40 位 SHA（${sha}）" >&2
        return 1
    fi
    if [[ -e "${path}/.git" ]]; then
        submodule_git "${path}" fetch --quiet "${url}" "${sha}" >/dev/null 2>&1 || return 1
        submodule_git "${path}" cat-file -e "${sha}^{commit}" >/dev/null 2>&1
        return
    fi
    # 子模块未初始化：用临时裸库探测，不依赖工作区
    tmp="$(mktemp -d)"
    if env -u GIT_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE git init --quiet --bare "${tmp}" >/dev/null 2>&1 \
        && env -u GIT_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE git -C "${tmp}" fetch --quiet "${url}" "${sha}" >/dev/null 2>&1 \
        && env -u GIT_DIR -u GIT_WORK_TREE -u GIT_INDEX_FILE git -C "${tmp}" cat-file -e "${sha}^{commit}" >/dev/null 2>&1; then
        rm -rf "${tmp}"
        return 0
    fi
    rm -rf "${tmp}"
    return 1
}

# 校验已暂存的 gitlink
check_staged() {
    local base meta path old_mode new_mode old_sha new_sha status url failed=0
    base="$(git rev-parse --verify -q HEAD || git hash-object -t tree /dev/null)"
    while IFS=$'\t' read -r meta path; do
        [[ -z "${meta}" || -z "${path}" ]] && continue
        meta="${meta#:}"
        read -r old_mode new_mode old_sha new_sha status <<< "${meta}"
        [[ "${new_mode}" == "160000" ]] || continue
        if ! url="$(submodule_url "${path}")"; then
            echo "  ${path}: .gitmodules 中找不到 URL" >&2
            failed=1
            continue
        fi
        if ! verify_reachable "${path}" "${url}" "${new_sha}"; then
            echo "  ${path}: 暂存指针 ${new_sha} 在 ${url} 不可达" >&2
            failed=1
        fi
    done < <(git diff --cached --raw --no-renames --no-abbrev "${base}")
    if [[ ${failed} -ne 0 ]]; then
        echo "" >&2
        echo "提交被拒绝：子模块指针必须指向对应子模块远端存在的提交。" >&2
        echo "请先确认该提交已推到子模块远端，再更新父仓指针。" >&2
        return 1
    fi
    return 0
}

# 用子模块当前 HEAD 更新 gitlink（显式按路径取 HEAD，先校验后写入）
stage_paths() {
    local -a ok_paths=() ok_shas=() ok_urls=()
    local path sha url i failed=0
    if [[ $# -eq 0 ]]; then
        usage
        return 2
    fi
    for path in "$@"; do
        if [[ ! -d "${path}" ]]; then
            echo "  ${path}: 目录不存在" >&2
            failed=1
            continue
        fi
        if ! sha="$(submodule_git "${path}" rev-parse HEAD 2>/dev/null)"; then
            echo "  ${path}: 读取 HEAD 失败（子模块未初始化？）" >&2
            failed=1
            continue
        fi
        if ! url="$(submodule_url "${path}")"; then
            echo "  ${path}: .gitmodules 中找不到 URL" >&2
            failed=1
            continue
        fi
        ok_paths+=("${path}")
        ok_shas+=("${sha}")
        ok_urls+=("${url}")
    done
    if [[ ${failed} -ne 0 ]]; then
        echo "未写入任何指针。" >&2
        return 1
    fi
    for i in "${!ok_paths[@]}"; do
        if ! verify_reachable "${ok_paths[$i]}" "${ok_urls[$i]}" "${ok_shas[$i]}"; then
            echo "  ${ok_paths[$i]}: HEAD ${ok_shas[$i]} 在 ${ok_urls[$i]} 不可达" >&2
            failed=1
        fi
    done
    if [[ ${failed} -ne 0 ]]; then
        echo "未写入任何指针：目标提交不在子模块远端，干净克隆会失败。" >&2
        return 1
    fi
    for i in "${!ok_paths[@]}"; do
        git update-index --cacheinfo "160000,${ok_shas[$i]},${ok_paths[$i]}"
        echo "已暂存 ${ok_paths[$i]} -> ${ok_shas[$i]}"
    done
    return 0
}

case "${MODE}" in
    check)
        check_staged
        ;;
    stage)
        stage_paths "$@"
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        usage
        exit 2
        ;;
esac
