# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


import json
import os
import sys

from mach.util import get_state_dir
from mozbuild.base import MozbuildObject
from mozversioncontrol import MissingVCSExtension, get_repository_object

from .lando import push_to_lando_try

GIT_CINNABAR_NOT_FOUND = """
Could not detect `git-cinnabar`.

The `mach try` command requires git-cinnabar to be installed when
pushing from git. Please install it by running:

    $ ./mach vcs-setup
""".lstrip()

HG_PUSH_TO_TRY_NOT_FOUND = """
Could not detect `push-to-try`.

The `mach try` command requires the push-to-try extension enabled
when pushing from hg. Please install it by running:

    $ ./mach vcs-setup
""".lstrip()

VCS_NOT_FOUND = """
Could not detect version control. Only `hg` or `git` are supported.
""".strip()

UNCOMMITTED_CHANGES = """
ERROR please commit changes before continuing
""".strip()

LARGE_PUSH_THRESHOLD = 1000
LARGE_PUSH_WARNING = f"""
Your push would schedule at least {{}} tasks. To avoid backlogs that cause delays for
others, your tasks will be scheduled at a lower priority and may not run before
their deadline. Consider selecting fewer than {LARGE_PUSH_THRESHOLD} tasks to save resources and
get results faster.
"""

MAX_HISTORY = 10

MACH_TRY_PUSH_TO_VCS = os.getenv("MACH_TRY_PUSH_TO_VCS") == "1"

TREEHERDER_LANDO_TRY_RUN_URL = (
    "https://treeherder.mozilla.org/jobs?repo=try&landoCommitID={job_id}"
)

here = os.path.abspath(os.path.dirname(__file__))
build = MozbuildObject.from_environment(cwd=here)
vcs = get_repository_object(build.topsrcdir)

history_path = os.path.join(
    get_state_dir(specific_to_topsrcdir=True), "history", "try_task_configs.json"
)


def write_task_config_history(msg, try_task_config):
    if not os.path.isfile(history_path):
        if not os.path.isdir(os.path.dirname(history_path)):
            os.makedirs(os.path.dirname(history_path))
        history = []
    else:
        with open(history_path) as fh:
            history = fh.read().strip().splitlines()

    history.insert(0, json.dumps([msg, try_task_config]))
    history = history[:MAX_HISTORY]
    with open(history_path, "w") as fh:
        fh.write("\n".join(history))


def check_working_directory(push=True):
    if not push:
        return

    if not vcs.working_directory_clean():
        print(UNCOMMITTED_CHANGES)
        sys.exit(1)


def generate_try_task_config(method, labels, params=None, routes=None):
    params = params or {}

    # The user has explicitly requested a set of jobs, so run them all
    # regardless of optimization (unless the selector explicitly sets this to
    # True). Their dependencies can be optimized though.
    params.setdefault("optimize_target_tasks", False)

    # Remove selected labels from 'existing_tasks' parameter if present
    if "existing_tasks" in params:
        params["existing_tasks"] = {
            label: tid
            for label, tid in params["existing_tasks"].items()
            if label not in labels
        }

    try_config = params.setdefault("try_task_config", {})
    try_config.setdefault("env", {})["TRY_SELECTOR"] = method

    # In reality, the number of tasks will likely be much larger thanks to test
    # chunks being collapsed behind a wildcard. However because we use
    # `taskgraph.fast` when generating tasks, we don't process test manifests
    # and have no way of knowing how many chunks will be scheduled for a given
    # task. For the purposes of this check, we'll ignore test chunks as it's
    # causing us to underestimate anyway.
    num_tasks = len(labels) * try_config.get("rebuild", 1)
    if "priority" not in try_config and num_tasks > LARGE_PUSH_THRESHOLD:
        print(LARGE_PUSH_WARNING.format(num_tasks))
        while True:
            answer = input("Would you like to proceed anyway? [Y/n]: ").lower()
            if answer in ("n", "no"):
                sys.exit(1)

            if answer in ("y", "yes"):
                break

            print(f"Invalid answer: '{answer}'")

        try_config["priority"] = "lowest"

    try_config["tasks"] = sorted(labels)

    if routes:
        try_config["routes"] = routes

    try_task_config = {"version": 2, "parameters": params}
    return try_task_config


def task_labels_from_try_config(try_task_config):
    if try_task_config["version"] == 2:
        parameters = try_task_config.get("parameters", {})
        if "try_task_config" in parameters:
            return parameters["try_task_config"].get("tasks")
        else:
            return None
    elif try_task_config["version"] == 1:
        return try_task_config.get("tasks", list())
    else:
        return None


# improves on `" ".join(sys.argv[:])` by requoting argv items containing spaces or single quotes
def get_sys_argv(injected_argv=None):
    argv_to_use = injected_argv or sys.argv[:]

    formatted_argv = []
    for item in argv_to_use:
        if " " in item or "'" in item:
            formatted_item = f'"{item}"'
        else:
            formatted_item = item
        formatted_argv.append(formatted_item)

    return " ".join(formatted_argv)


def push_to_try(
    method,
    msg,
    try_task_config=None,
    stage_changes=False,
    dry_run=False,
    closed_tree=False,
    files_to_change=None,
    allow_log_capture=False,
    push_to_vcs=False,
):
    push = not stage_changes and not dry_run
    push_to_vcs |= MACH_TRY_PUSH_TO_VCS
    check_working_directory(push)

    # Format the commit message
    closed_tree_string = " ON A CLOSED TREE" if closed_tree else ""
    the_cmdline = get_sys_argv()
    full_commandline_entry = f"mach try command: `{the_cmdline}`"
    commit_message = f"{msg}{closed_tree_string}\n\n{full_commandline_entry}\n\nPushed via `mach try {method}`"

    changed_files = {}

    if try_task_config:
        changed_files["try_task_config.json"] = (
            json.dumps(
                try_task_config, indent=4, separators=(",", ": "), sort_keys=True
            )
            + "\n"
        )
        if push and method not in ("again", "auto", "empty"):
            write_task_config_history(msg, try_task_config)

    if (push or stage_changes) and files_to_change:
        changed_files.update(files_to_change.items())

    if not push:
        print("Commit message:")
        print(commit_message)
        config = changed_files.pop("try_task_config.json", None)
        if config:
            print("Calculated try_task_config.json:")
            print(config)
        if stage_changes:
            vcs.stage_changes(changed_files)

        return

    try:
        if push_to_vcs:
            vcs.push_to_try(
                commit_message,
                changed_files=changed_files,
                allow_log_capture=allow_log_capture,
            )
        else:
            job_id = push_to_lando_try(vcs, commit_message, changed_files)
            if job_id:
                print(
                    f"Follow the progress of your build on Treeherder: "
                    f"{TREEHERDER_LANDO_TRY_RUN_URL.format(job_id=job_id)}"
                )

            return job_id
    except MissingVCSExtension as e:
        if e.ext == "push-to-try":
            print(HG_PUSH_TO_TRY_NOT_FOUND)
        elif e.ext == "cinnabar":
            print(GIT_CINNABAR_NOT_FOUND)
        else:
            raise
        sys.exit(1)
    finally:
        if "try_task_config.json" in changed_files and os.path.isfile(
            "try_task_config.json"
        ):
            os.remove("try_task_config.json")
