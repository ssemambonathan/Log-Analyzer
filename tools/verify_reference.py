#!/usr/bin/env python3
"""Independently verify hpla's statistics and filters using Python.

Usage:
    python tools/verify_reference.py LOG HPLA_EXE

The log must be CLF/Combined format without backslash-escaped quotes in quoted
fields (hpla_genlog output is suitable). The script recomputes counts from the
log with the standard library, runs HPLA with several filter combinations, and
exits non-zero if any summary field or filter count differs.
"""

import collections
import datetime as dt
import json
import os
import re
import subprocess
import sys
import tempfile

LINE_RE = re.compile(
    r'^(\S+) (\S+) (\S+) \[([^\]]+)\] "([^"]*)" (\d{3}) (\d+|-)(?: "([^"]*)" "([^"]*)")?$'
)
METHOD_NAMES = ["GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS", "OTHER"]


def load_log(path):
    records = []
    lines = malformed = 0
    with open(path, encoding="utf-8", newline="") as handle:
        for raw in handle:
            line = raw.rstrip("\r\n")
            lines += 1
            match = LINE_RE.match(line)
            if not match:
                malformed += 1
                continue
            ip, ident, user, stamp, request, status, size, referrer, ua = match.groups()
            if not 100 <= int(status) <= 599:
                malformed += 1
                continue
            parts = request.split()
            if len(parts) != 3:
                malformed += 1
                continue
            method, target, protocol = parts
            try:
                epoch = int(dt.datetime.strptime(stamp, "%d/%b/%Y:%H:%M:%S %z").timestamp())
            except ValueError:
                malformed += 1
                continue
            records.append(
                dict(ip=ip, method=method, target=target, status=int(status),
                     size=None if size == "-" else int(size), epoch=epoch, ua=ua or "")
            )
    return lines, malformed, records


def parse_timestamp(text):
    return int(dt.datetime.strptime(text, "%d/%b/%Y:%H:%M:%S %z").timestamp())


def iso(epoch):
    return dt.datetime.fromtimestamp(epoch, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def top(counter, limit=10):
    return sorted(counter.items(), key=lambda item: (-item[1], item[0]))[:limit]


class Verifier:
    def __init__(self, log_path, binary):
        self.log_path = log_path
        self.binary = binary
        self.tempdir = tempfile.TemporaryDirectory(prefix="hpla_verify_")

    def run(self, args):
        output = os.path.join(self.tempdir.name, "report.json")
        subprocess.run(
            [self.binary, self.log_path, *args, "--format", "json", "-o", output],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        with open(output, encoding="utf-8") as handle:
            return json.load(handle)

    def check(self, label, actual, expected):
        if actual != expected:
            print(f"MISMATCH {label}: hpla={actual!r} python={expected!r}")
            sys.exit(1)

    def verify_summary(self, summary, lines, malformed, records):
        status_counts = collections.Counter(r["status"] for r in records)
        method_counts = collections.Counter(
            r["method"] if r["method"] in METHOD_NAMES else "OTHER" for r in records
        )
        sizes = [r["size"] for r in records if r["size"] is not None]
        ip_counts = collections.Counter(r["ip"] for r in records)
        path_counts = collections.Counter(r["target"] for r in records if r["target"])
        minute_counts = collections.Counter(r["epoch"] // 60 for r in records)

        self.check("lines", summary["processing"]["lines_examined"], lines)
        self.check("parsed", summary["processing"]["parsed_records"], len(records))
        self.check("malformed", summary["processing"]["malformed_records"], malformed)
        self.check("matched", summary["processing"]["matched_records"], len(records))
        self.check("total bytes", summary["response_sizes"]["total_bytes"], sum(sizes))
        self.check("sized", summary["response_sizes"]["sized_records"], len(sizes))
        self.check("min size", summary["response_sizes"]["minimum"], min(sizes))
        self.check("max size", summary["response_sizes"]["maximum"], max(sizes))
        self.check("unique ips", summary["unique_client_ips"]["count"], len(ip_counts))
        self.check(
            "status distribution",
            [(entry["status"], entry["count"]) for entry in summary["status_distribution"]],
            sorted(status_counts.items()),
        )
        self.check(
            "method distribution",
            [(entry["method"], entry["count"]) for entry in summary["method_distribution"]],
            [(name, method_counts[name]) for name in METHOD_NAMES if method_counts[name]],
        )
        self.check(
            "status classes",
            [summary["status_classes"][f"{n}xx"] for n in range(1, 6)],
            [sum(1 for r in records if r["status"] // 100 == n) for n in range(1, 6)],
        )
        self.check(
            "top ips",
            [(entry["ip"], entry["count"]) for entry in summary["top_client_ips"]],
            top(ip_counts),
        )
        self.check(
            "top resources",
            [(entry["resource"], entry["count"]) for entry in summary["top_resources"]],
            top(path_counts),
        )
        self.check(
            "busiest minutes",
            [(entry["minute"], entry["requests"])
             for entry in summary["traffic"]["busiest_minutes"]],
            [(iso(minute * 60), count) for minute, count in top(minute_counts, 5)],
        )
        self.check("first timestamp", summary["traffic"]["first_timestamp"],
                   iso(min(r["epoch"] for r in records)))
        self.check("last timestamp", summary["traffic"]["last_timestamp"],
                   iso(max(r["epoch"] for r in records)))
        first, last = min(r["epoch"] for r in records), max(r["epoch"] for r in records)
        expected_rpm = len(records) * 60.0 / (last - first) if last > first else float(len(records))
        if abs(summary["traffic"]["requests_per_minute"] - expected_rpm) > 5e-5:
            print("MISMATCH requests/minute",
                  summary["traffic"]["requests_per_minute"], expected_rpm)
            sys.exit(1)

    def verify_filters(self, records):
        start = parse_timestamp("30/Oct/2026:10:00:00 +0000")
        end = parse_timestamp("30/Oct/2026:12:00:00 +0000")
        cases = {
            ("--status", "404,500"): lambda r: r["status"] in (404, 500),
            ("--status-range", "400", "499"): lambda r: 400 <= r["status"] <= 499,
            ("--method", "get,post"): lambda r: r["method"] in ("GET", "POST"),
            ("--ip", "10.0.0.1,10.0.7.7"): lambda r: r["ip"] in ("10.0.0.1", "10.0.7.7"),
            ("--path-contains", "api"): lambda r: "api" in r["target"],
            ("--path", "/login"): lambda r: r["target"] == "/login",
            ("--user-agent", "Firefox"): lambda r: "Firefox" in r["ua"],
            ("--from", "30/Oct/2026:10:00:00 +0000",
             "--to", "30/Oct/2026:12:00:00 +0000"): lambda r: start <= r["epoch"] <= end,
            ("--min-size", "50000", "--max-size", "100000"): (
                lambda r: r["size"] is not None and 50000 <= r["size"] <= 100000
            ),
            ("--status", "200", "--method", "GET", "--path-contains", "api",
             "--min-size", "1000", "--user-agent", "Mozilla"): lambda r: (
                r["status"] == 200 and r["method"] == "GET" and "api" in r["target"]
                and r["size"] is not None and r["size"] >= 1000 and "Mozilla" in r["ua"]
            ),
        }
        for args, predicate in cases.items():
            report = self.run(list(args))
            self.check("filter " + " ".join(args),
                       report["processing"]["matched_records"], sum(map(predicate, records)))
        return len(cases)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    log_path, binary = argv[1], argv[2]
    lines, malformed, records = load_log(log_path)
    if not records:
        print("no valid records found; is the log in CLF/Combined format?")
        return 2
    verifier = Verifier(log_path, binary)
    verifier.verify_summary(verifier.run([]), lines, malformed, records)
    filter_count = verifier.verify_filters(records)
    print(f"OK: {lines} lines, {len(records)} valid, "
          f"{len({r['ip'] for r in records})} unique IPs, {filter_count} filters verified")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
