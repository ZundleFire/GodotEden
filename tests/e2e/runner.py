#!/usr/bin/env python3
"""
GodotEden E2E Test Suite Runner.

Executable Python script for discovering and running E2E tests, filtering by tier
or feature, reporting pass/fail statistics, and generating summary reports.
"""

import argparse
import importlib
import json
import os
import sys
import time
from pathlib import Path
from typing import List, Optional

# Ensure project root is in sys.path
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tests.e2e.framework import (
    E2ETestCase,
    E2ETestContext,
    TestResult,
    TestStatus,
    e2e_test,
    get_global_registry,
)


def discover_and_import_tests():
    """Dynamically import all python files in tests/e2e/ to trigger test registration."""
    e2e_dir = PROJECT_ROOT / "tests" / "e2e"
    if not e2e_dir.exists():
        return

    for file_path in e2e_dir.glob("**/*.py"):
        if file_path.name in ("__init__.py", "framework.py", "runner.py"):
            continue
        rel_path = file_path.relative_to(PROJECT_ROOT)
        module_parts = list(rel_path.with_suffix("").parts)
        module_name = ".".join(module_parts)
        try:
            importlib.import_module(module_name)
        except Exception as e:
            print(f"[Warning] Failed to import test module {module_name}: {e}")


# --- Built-in Infrastructure Sanity / Verification Tests ---

@e2e_test(
    test_id="INFRA_T1_001",
    feature_id="F15",
    tier=1,
    description="Verify test framework context assertions and execution harness",
)
def _test_infra_framework_assertions(ctx: E2ETestContext):
    ctx.assert_equal(1 + 1, 2)
    ctx.assert_true(True)
    ctx.assert_false(False)
    ctx.assert_not_none(ctx.test_id)
    ctx.assert_almost_equal(3.14159, 3.14159, delta=1e-4)


@e2e_test(
    test_id="INFRA_T2_001",
    feature_id="F15",
    tier=2,
    description="Verify test framework context resource cleanup lifecycle",
)
def _test_infra_cleanup_lifecycle(ctx: E2ETestContext):
    cleaned = []
    ctx.add_cleanup(lambda: cleaned.append("resource_1"))
    ctx.assert_equal(len(cleaned), 0)


def parse_args():
    parser = argparse.ArgumentParser(
        description="GodotEden E2E Test Suite Runner",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "--tier",
        "-t",
        type=int,
        choices=[1, 2, 3, 4],
        help="Filter tests by execution tier (1, 2, 3, or 4)",
    )
    parser.add_argument(
        "--feature",
        "-f",
        type=str,
        help="Filter tests by Feature ID (e.g. F1, F4) or feature name keyword",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Enable verbose output showing individual test status and log output",
    )
    parser.add_argument(
        "--json-report",
        "-j",
        type=str,
        help="Path to write JSON test execution summary report",
    )
    parser.add_argument(
        "--list",
        "-l",
        action="store_true",
        help="List all registered test cases matching filters without executing them",
    )
    parser.add_argument(
        "--status-check",
        action="store_true",
        help="Print runner configuration status and exit cleanly",
    )
    return parser.parse_args()


def print_runner_header():
    print("=" * 70)
    print("           GodotEden End-to-End (E2E) Test Runner           ")
    print("=" * 70)


def print_summary_table(results: List[TestResult], elapsed_total: float):
    print("\n" + "=" * 70)
    print("                         EXECUTION SUMMARY                          ")
    print("=" * 70)

    tier_stats = {1: {"total": 0, "pass": 0, "fail": 0, "error": 0},
                  2: {"total": 0, "pass": 0, "fail": 0, "error": 0},
                  3: {"total": 0, "pass": 0, "fail": 0, "error": 0},
                  4: {"total": 0, "pass": 0, "fail": 0, "error": 0}}

    total_pass = 0
    total_fail = 0
    total_error = 0

    for r in results:
        t = r.tier if r.tier in tier_stats else 1
        tier_stats[t]["total"] += 1
        if r.status == TestStatus.PASS:
            tier_stats[t]["pass"] += 1
            total_pass += 1
        elif r.status == TestStatus.FAIL:
            tier_stats[t]["fail"] += 1
            total_fail += 1
        else:
            tier_stats[t]["error"] += 1
            total_error += 1

    print(f"{'Tier':<10} | {'Total':<8} | {'Pass':<8} | {'Fail':<8} | {'Error':<8}")
    print("-" * 52)
    for tier in sorted(tier_stats.keys()):
        st = tier_stats[tier]
        print(f"{'Tier ' + str(tier):<10} | {st['total']:<8} | {st['pass']:<8} | {st['fail']:<8} | {st['error']:<8}")

    print("-" * 52)
    total_tests = len(results)
    print(f"{'TOTAL':<10} | {total_tests:<8} | {total_pass:<8} | {total_fail:<8} | {total_error:<8}")
    print("=" * 70)

    pass_rate = (total_pass / total_tests * 100.0) if total_tests > 0 else 0.0
    print(f"Total Time: {elapsed_total:.4f}s | Pass Rate: {pass_rate:.1f}%")
    
    if total_fail == 0 and total_error == 0:
        print("OVERALL STATUS: SUCCESS")
    else:
        print("OVERALL STATUS: FAILURE")
    print("=" * 70)


def main():
    args = parse_args()

    print_runner_header()
    discover_and_import_tests()

    registry = get_global_registry()
    tests = registry.filter_tests(tier=args.tier, feature=args.feature)

    print(f"[Runner Status] Registered tests found: {len(registry.get_all_tests())}")
    print(f"[Runner Status] Active test filter -> Tier: {args.tier or 'All'}, Feature: {args.feature or 'All'}")
    print(f"[Runner Status] Executing test count: {len(tests)}")

    if args.status_check:
        print("[Runner Status] Status check complete. Runner ready.")
        sys.exit(0)

    if args.list:
        print("\nRegistered Matching Tests:")
        print("-" * 70)
        for t in tests:
            print(f"  [{t.test_id}] (Tier {t.tier}, Feature {t.feature_id}) {t.description}")
        print("-" * 70)
        sys.exit(0)

    if not tests:
        print("\n[Notice] No tests matched the specified filter criteria.")
        sys.exit(0)

    print("\nRunning E2E Test Suite...")
    print("-" * 70)

    start_time = time.perf_counter()
    results: List[TestResult] = []

    for idx, test_case in enumerate(tests, 1):
        if args.verbose:
            print(f"[{idx}/{len(tests)}] Running {test_case.test_id} ({test_case.description})...", end="", flush=True)

        res = test_case.run()
        results.append(res)

        if args.verbose:
            status_str = f"[{res.status.value}]"
            print(f" {status_str} ({res.duration_seconds * 1000:.2f}ms)")
            if res.error_message:
                print(f"    Error: {res.error_message}")
        else:
            symbol = "." if res.status == TestStatus.PASS else ("F" if res.status == TestStatus.FAIL else "E")
            print(symbol, end="", flush=True)
            if idx % 50 == 0 or idx == len(tests):
                print(f"  [{idx}/{len(tests)}]")

    if not args.verbose:
        print()

    elapsed = time.perf_counter() - start_time
    print_summary_table(results, elapsed)

    if args.json_report:
        report_data = {
            "total_tests": len(results),
            "elapsed_seconds": round(elapsed, 4),
            "filters": {"tier": args.tier, "feature": args.feature},
            "results": [r.to_dict() for r in results],
        }
        report_path = Path(args.json_report).resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with open(report_path, "w", encoding="utf-8") as f:
            json.dump(report_data, f, indent=2)
        print(f"[Report] JSON execution report saved to: {report_path}")

    has_failures = any(r.status in (TestStatus.FAIL, TestStatus.ERROR) for r in results)
    sys.exit(1 if has_failures else 0)


if __name__ == "__main__":
    main()
