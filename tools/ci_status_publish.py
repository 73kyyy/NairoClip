#!/usr/bin/env python3
"""把 GitHub Actions 的运行结果写回仓库（分支 ci-status）。

为什么需要这个脚本
------------------
仓库是私有的。开发容器（aarch64 手机环境）按 docs/DEV_ENVIRONMENT.md 的
规定**不在本地执行任何脚本**，同时容器里没有 PAT、没有 ``gh``，
REST API 一律 401 —— 也就是说「本地读不到 Actions 的运行结果」。

唯一稳定的读取通道是让 CI 自己把结论写进 git：
本脚本在 runner 上用 ``GITHUB_TOKEN`` 调 ``gh``，把结论落到
``docs/ci-status/``，再由 workflow 提交到 ``ci-status`` 分支。
开发容器只需要 ``git fetch origin ci-status`` 就能看到真实结论。

输出
----
``RUNS.md``      人读：最近各工作流结论 + 失败的工作/步骤 + 失败日志尾部
``latest.json``  机读：每个工作流最近一次运行的结论、运行号、提交、链接

设计约束
--------
* **不伪造**：拿不到数据就退出码非 0，让 workflow 变红。
  一个「静默通过」的汇报通道比没有通道更糟。
* 日志必须截断：一次失败日志可能有几十 MB，全塞进 git 会让仓库膨胀。
* 只读 API，不修改任何 run 状态。
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

# 失败日志最多保留这么多行 / 字节，避免把 git 仓库撑大
MAX_LOG_LINES = 300
MAX_LOG_BYTES = 30000


def _gh(*args: str, timeout: int = 120) -> subprocess.CompletedProcess:
    """调用 gh CLI。找不到 gh 时给出明确错误。"""
    try:
        return subprocess.run(
            ["gh", *args],
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError:
        raise SystemExit(
            "找不到 gh CLI。本脚本必须在 GitHub Actions runner 上运行。"
        )
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(args, 124, "", "gh 调用超时")


def gh_api(path: str) -> dict:
    p = _gh("api", path)
    if p.returncode != 0:
        raise SystemExit(f"gh api {path} 失败（{p.returncode}）：{p.stderr.strip()[:800]}")
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError as e:
        raise SystemExit(f"gh api {path} 返回的不是 JSON：{e}")


def failed_log(run_id: int, repo: str) -> str:
    """失败日志尾部。截断到 MAX_LOG_LINES / MAX_LOG_BYTES。"""
    p = _gh("run", "view", str(run_id), "--repo", repo, "--log-failed", timeout=180)
    if p.returncode != 0 or not p.stdout.strip():
        return ""
    lines = p.stdout.splitlines()
    if len(lines) > MAX_LOG_LINES:
        dropped = len(lines) - MAX_LOG_LINES
        lines = [f"… 省略前 {dropped} 行 …"] + lines[-MAX_LOG_LINES:]
    text = "\n".join(lines)
    if len(text.encode("utf-8")) > MAX_LOG_BYTES:
        text = text.encode("utf-8")[-MAX_LOG_BYTES:].decode("utf-8", "replace")
        text = "… 省略前半段（超字节上限）…\n" + text
    return text


def step_summary(run_id: int, repo: str) -> tuple[list[dict], list[dict]]:
    """返回 (jobs, failed_steps)。failed_steps 里每项含 job 名与 step 名。"""
    data = gh_api(f"/repos/{repo}/actions/runs/{run_id}/jobs?per_page=100")
    jobs = data.get("jobs") or []
    failed: list[dict] = []
    for j in jobs:
        for s in j.get("steps") or []:
            if s.get("conclusion") in ("failure", "timed_out", "cancelled"):
                failed.append(
                    {
                        "job": j.get("name"),
                        "step": s.get("name"),
                        "conclusion": s.get("conclusion"),
                    }
                )
    return jobs, failed


ICON = {
    "success": "✅",
    "failure": "❌",
    "cancelled": "⚪",
    "skipped": "⚪",
    "timed_out": "⏱️",
    "action_required": "🔒",
    "neutral": "➖",
    None: "⏳",
}


def icon(conclusion: str | None) -> str:
    return ICON.get(conclusion, conclusion or "⏳")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--out",
        default="docs/ci-status",
        help="输出目录（默认 docs/ci-status）",
    )
    ap.add_argument(
        "--limit",
        type=int,
        default=40,
        help="拉取最近多少次运行（默认 40）",
    )
    ap.add_argument(
        "--max-failed",
        type=int,
        default=3,
        help="最多为多少次失败运行抓取日志（默认 3）",
    )
    args = ap.parse_args()

    repo = os.environ.get("REPO") or os.environ.get("GITHUB_REPOSITORY") or ""
    if not repo:
        raise SystemExit("需要环境变量 REPO 或 GITHUB_REPOSITORY（形如 owner/name）")

    os.makedirs(args.out, exist_ok=True)

    runs = gh_api(f"/repos/{repo}/actions/runs?per_page={args.limit}")
    items = runs.get("workflow_runs") or []
    if not items:
        # 没有任何运行记录通常意味着 token 权限不足 —— 这必须暴露，不能静默通过
        raise SystemExit("gh api 返回了空运行列表：检查 permissions: actions: read")

    # 每个工作流最近一次（列表已按创建时间倒序）
    latest: dict[str, dict] = {}
    for r in items:
        name = r.get("name") or (r.get("workflow") or {}).get("name") or "?"
        latest.setdefault(name, r)

    md: list[str] = []
    md.append("# CI 运行状态")
    md.append("")
    md.append(
        "> 本文件由 `.github/workflows/ci-status.yml` 在 runner 上自动生成，"
        "提交到 `ci-status` 分支。"
    )
    md.append("> 开发容器不跑脚本、也读不到 Actions API，只能靠这条通道看结论。")
    md.append("> **不要手工编辑**——下一次运行会覆盖。")
    md.append("")
    md.append(
        f"生成时间（UTC）：`{os.environ.get('STATUS_TS') or _now()}`　"
        f"触发：`{os.environ.get('GITHUB_EVENT_NAME') or '?'}`"
    )
    md.append("")

    md.append("## 各工作流最近一次")
    md.append("")
    md.append("| | 工作流 | 结论 | 提交 | 事件 | 运行 |")
    md.append("|---|---|---|---|---|---|")
    for name in sorted(latest):
        r = latest[name]
        sha = (r.get("head_sha") or "")[:7]
        url = r.get("html_url") or ""
        md.append(
            f"| {icon(r.get('conclusion'))} | {name} | `{r.get('conclusion') or 'running'}` "
            f"| `{sha}` | `{r.get('event')}` | [#{r.get('run_number')}]({url}) |"
        )
    md.append("")

    # 详细：只看失败的最新运行
    to_inspect = [
        r
        for r in latest.values()
        if r.get("conclusion") in ("failure", "timed_out", "cancelled")
    ][: args.max_failed]

    if to_inspect:
        md.append("## 失败细节")
        md.append("")
    for r in to_inspect:
        rid = int(r["id"])
        name = r.get("name") or "?"
        md.append(f"### {name} · run #{r.get('run_number')} · `{(r.get('head_sha') or '')[:7]}`")
        md.append("")
        md.append(f"链接：{r.get('html_url')}")
        md.append("")
        try:
            _jobs, failed = step_summary(rid, repo)
        except SystemExit as e:
            md.append(f"抓取 job 列表失败：`{e}`")
            failed = []
        if failed:
            md.append("**失败点：**")
            md.append("")
            for f in failed:
                md.append(f"- `{f['job']}` → `{f['step']}`（{f['conclusion']}）")
            md.append("")
        try:
            log = failed_log(rid, repo)
        except SystemExit as e:
            md.append(f"抓取日志失败：`{e}`")
            log = ""
        if log:
            md.append("<details><summary>失败日志尾部（截断）</summary>")
            md.append("")
            md.append("```")
            md.append(log)
            md.append("```")
            md.append("")
            md.append("</details>")
            md.append("")
        elif not failed:
            md.append("（没有失败步骤也没有失败日志 —— 多半是被 cancel 的）")
            md.append("")

    md.append("## 最近运行")
    md.append("")
    md.append("| | 工作流 | 结论 | 提交 | 创建时间 |")
    md.append("|---|---|---|---|---|")
    for r in items[:20]:
        md.append(
            f"| {icon(r.get('conclusion'))} | {r.get('name')} "
            f"| `{r.get('conclusion') or 'running'}` "
            f"| `{(r.get('head_sha') or '')[:7]}` | {r.get('created_at')} |"
        )
    md.append("")

    runs_path = os.path.join(args.out, "RUNS.md")
    with open(runs_path, "w", encoding="utf-8") as f:
        f.write("\n".join(md) + "\n")

    payload = {
        "generated_at": os.environ.get("STATUS_TS") or _now(),
        "repo": repo,
        "latest": {
            name: {
                "conclusion": r.get("conclusion"),
                "run_number": r.get("run_number"),
                "run_id": r.get("id"),
                "head_sha": r.get("head_sha"),
                "event": r.get("event"),
                "url": r.get("html_url"),
                "created_at": r.get("created_at"),
            }
            for name, r in sorted(latest.items())
        },
        "recent": [
            {
                "name": r.get("name"),
                "conclusion": r.get("conclusion"),
                "head_sha": r.get("head_sha"),
                "run_number": r.get("run_number"),
                "created_at": r.get("created_at"),
            }
            for r in items[:20]
        ],
    }
    with open(os.path.join(args.out, "latest.json"), "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
        f.write("\n")

    # 控制台摘要，方便直接在 job log 里一眼看到
    print("== 各工作流最近一次 ==")
    for name in sorted(latest):
        r = latest[name]
        print(
            f"  {icon(r.get('conclusion'))} {name:<22} "
            f"{str(r.get('conclusion')):<12} {(r.get('head_sha') or '')[:7]}"
        )
    print(f"已写出 {runs_path} 与 latest.json")
    return 0


def _now() -> str:
    import datetime

    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")


if __name__ == "__main__":
    sys.exit(main())
