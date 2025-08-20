# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import json
import logging
import os
import sys
import textwrap
from collections import defaultdict
from pathlib import Path

import mozunit
import pytest
from buildconfig import topsrcdir
from mach.util import get_state_dir
from mozpack.files import JarFinder
from mozpack.mozjar import JarReader
from mozprocess import ProcessHandler

logger = logging.getLogger(__name__)


@pytest.fixture(scope="session")
def test_dir():
    return (
        Path(get_state_dir(specific_to_topsrcdir=True, topsrcdir=topsrcdir))
        / "android-gradle-build"
    )


@pytest.fixture(scope="session")
def objdir(test_dir):
    return test_dir / "objdir"


@pytest.fixture(scope="session")
def mozconfig(test_dir, objdir):
    mozconfig_path = test_dir / "mozconfig"
    mozconfig_path.parent.mkdir(parents=True, exist_ok=True)
    mozconfig_path.write_text(
        textwrap.dedent(
            f"""
                ac_add_options --enable-application=mobile/android
                ac_add_options --enable-artifact-builds
                ac_add_options --target=arm
                mk_add_options MOZ_OBJDIR="{objdir}"
                export GRADLE_FLAGS="-PbuildMetrics -PbuildMetricsFileSuffix=test"
            """
        )
    )
    return mozconfig_path


@pytest.fixture(scope="session")
def run_mach(mozconfig):
    def inner(argv, cwd=None):
        env = os.environ.copy()
        env["MOZCONFIG"] = str(mozconfig)
        env["MACH_NO_TERMINAL_FOOTER"] = "1"
        env["MACH_NO_WRITE_TIMES"] = "1"

        def pol(line):
            logger.debug(line)

        proc = ProcessHandler(
            [sys.executable, "mach"] + argv,
            env=env,
            cwd=cwd or topsrcdir,
            processOutputLine=pol,
            universal_newlines=True,
        )
        proc.run()
        proc.wait()

        return proc.poll(), proc.output

    return inner


AARS = {
    "geckoview.aar": "gradle/build/mobile/android/geckoview/outputs/aar/geckoview-debug.aar",
}


APKS = {
    "test_runner.apk": "gradle/build/mobile/android/test_runner/outputs/apk/debug/test_runner-debug.apk",
    "androidTest": "gradle/build/mobile/android/geckoview/outputs/apk/androidTest/debug/geckoview-debug-androidTest.apk",
    "geckoview_example.apk": "gradle/build/mobile/android/geckoview_example/outputs/apk/debug/geckoview_example-debug.apk",
    "messaging_example.apk": "gradle/build/mobile/android/examples/messaging_example/app/outputs/apk/debug/messaging_example-debug.apk",
    "port_messaging_example.apk": "gradle/build/mobile/android/examples/port_messaging_example/app/outputs/apk/debug/port_messaging_example-debug.apk",
}


def hashes(objdir, pattern, targets={**AARS, **APKS}):
    target_to_hash = {}
    hash_to_target = defaultdict(list)
    for shortname, target in targets.items():
        finder = JarFinder(target, JarReader(str(objdir / target)))
        hasher = hashlib.blake2b()

        # We sort paths.  This allows a pattern like `classes*.dex` to capture
        # changes to any of the DEX files, no matter how they are ordered in an
        # AAR or APK.
        for p, f in sorted(finder.find(pattern), key=lambda x: x[0]):
            fp = f.open()
            while True:
                data = fp.read(8192)
                if not len(data):
                    break
                hasher.update(data)

        h = hasher.hexdigest()
        target_to_hash[shortname] = h
        hash_to_target[h].append(shortname)

    return target_to_hash, hash_to_target


def get_test_run_build_metrics(objdir):
    """Find and load the build-metrics JSON file for our test run."""
    log_dir = objdir / "gradle" / "build" / "metrics"
    if not log_dir.exists():
        return None

    suffix = "test"
    build_metrics_file = log_dir / f"build-metrics-{suffix}.json"

    try:
        with build_metrics_file.open(encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError) as e:
        logger.warning(f"Failed to load build metrics from {build_metrics_file}: {e}")
        return None


def assert_task_outcomes(objdir, ordered_expected_task_statuses):
    """Takes a list of (task_name, expected_status) tuples and verifies that they appear
    in the build metrics in the same order with the expected statuses.
    """
    # Get build metrics and fail if not found
    build_metrics = get_test_run_build_metrics(objdir)
    assert build_metrics is not None, "Build metrics JSON not found"
    assert "tasks" in build_metrics, "Build metrics missing 'tasks' section"

    # Extract tasks from metrics in order
    metrics_tasks = build_metrics.get("tasks", [])
    expected_task_names = {task_name for task_name, _ in ordered_expected_task_statuses}
    task_order = [
        task.get("path")
        for task in metrics_tasks
        if task.get("path") in expected_task_names
    ]
    expected_order = [task_name for task_name, _ in ordered_expected_task_statuses]

    # Check that all expected tasks were found
    missing_tasks = expected_task_names - set(task_order)
    assert not missing_tasks, f"Tasks not found in build metrics: {missing_tasks}"

    # Check order matches expectation
    assert (
        task_order == expected_order
    ), f"Task execution order mismatch. Expected: {expected_order}, Got: {task_order}"

    # Check statuses for each task
    task_lookup = {task.get("path"): task for task in metrics_tasks}
    for task_name, expected_status in ordered_expected_task_statuses:
        task_info = task_lookup[task_name]
        actual_status = task_info.get("status")
        assert (
            actual_status == expected_status
        ), f"Task {task_name} had status '{actual_status}', expected '{expected_status}'"


def test_artifact_build(objdir, mozconfig, run_mach):
    (returncode, output) = run_mach(["build"])

    assert returncode == 0

    # Order matters, since `mach build stage-package` depends on the
    # outputs of `mach build faster`.
    assert_task_outcomes(
        objdir, [(":machBuildFaster", "SKIPPED"), (":machStagePackage", "SKIPPED")]
    )

    _, omnijar_hash_to = hashes(objdir, "assets/omni.ja")
    assert len(omnijar_hash_to) == 1
    (omnijar_hash_orig,) = omnijar_hash_to.values()

    (returncode, output) = run_mach(["gradle", "geckoview_example:assembleDebug"])

    assert returncode == 0

    # Order matters, since `mach build stage-package` depends on the
    # outputs of `mach build faster`.
    assert_task_outcomes(
        objdir, [(":machBuildFaster", "EXECUTED"), (":machStagePackage", "EXECUTED")]
    )

    _, omnijar_hash_to = hashes(objdir, "assets/omni.ja")
    assert len(omnijar_hash_to) == 1
    (omnijar_hash_new,) = omnijar_hash_to.values()

    assert omnijar_hash_orig == omnijar_hash_new


def test_minify_fenix_incremental_build(objdir, mozconfig, run_mach):
    """Verify that minifyFenixReleaseWithR8 is UP-TO-DATE on a subsequent
    run when there are no code changes.
    """

    # Ensure a clean state
    (returncode, output) = run_mach(["gradle", ":fenix:cleanMinifyFenixReleaseWithR8"])
    assert returncode == 0

    (returncode, output) = run_mach(["gradle", ":fenix:minifyFenixReleaseWithR8"])
    assert returncode == 0

    assert_task_outcomes(objdir, [(":fenix:minifyFenixReleaseWithR8", "EXECUTED")])

    (returncode, output) = run_mach(["gradle", ":fenix:minifyFenixReleaseWithR8"])
    assert returncode == 0

    assert_task_outcomes(objdir, [(":fenix:minifyFenixReleaseWithR8", "UP-TO-DATE")])


if __name__ == "__main__":
    mozunit.main()
