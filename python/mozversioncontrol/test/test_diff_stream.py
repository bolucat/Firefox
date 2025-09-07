# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import re

import mozunit

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        echo constant > constant.txt
        hg add constant.txt
        hg commit -m "BASE PATCH"
        """,
        """
        echo foo > file1.txt
        echo autre > anotherfile.txt
        hg add file1.txt anotherfile.txt
        hg commit -m "FIRST PATCH"
        echo bar > file1.txt
        """,
    ],
    "git": [
        """
        git branch -m main
        echo constant > constant.txt
        git add constant.txt
        git commit -m "BASE PATCH"
        """,
        """
        git switch -c dev
        echo foo > file1.txt
        echo autre > anotherfile.txt
        git add file1.txt anotherfile.txt
        git commit -m "FIRST PATCH"
        echo bar > file1.txt
        git add file1.txt
        """,
    ],
    "jj": [
        # Make a base commit.
        """
        echo constant > constant.txt
        jj commit -m "BASE PATCH"
        """,
        # Create a conflict and then resolve the conflict.
        """
        echo foo > file1.txt
        jj desc -m "FIRST PATCH"
        jj new 'description("BASE PATCH")'
        echo notfoo > file1.txt
        echo bar > anotherfile.txt
        jj desc -m "OTHER PATCH"
        jj new 'description("FIRST PATCH")' @ -m "SECOND PATCH"
        jj new -m "resolve conflict"
        echo merged > file1.txt
       """,
    ],
}


def test_diff_stream(repo):
    vcs = get_repository_object(repo.dir)

    # Create a base commit.
    repo.execute_next_step()
    base_rev = vcs.head_ref

    # Add some commits on top.
    repo.execute_next_step()

    def changed_files(stream):
        files = set()
        for line in stream:
            print(line)
            if m := re.match(r"diff --git \w/(\S+)", line):
                files.add(m[1])
        return files

    # Default: "uncommitted" changes (meaning @ in jj)
    files = changed_files(vcs.diff_stream())
    assert "file1.txt" in files
    assert "anotherfile.txt" not in files
    assert "constant.txt" not in files

    # Changes in selected revision ("BASE PATCH")
    files = changed_files(vcs.diff_stream(base_rev))
    assert "file1.txt" not in files
    assert "anotherfile.txt" not in files
    assert "constant.txt" in files

    # All changes since the main branch.
    #
    # For jj, verify that diff_stream only includes files modified in mutable
    # changes (git diff HEAD would include all files in the repository because
    # of the dummy conflict commit).
    range = {
        "hg": f"{base_rev}::",
        "git": "main..",
        "jj": f"{base_rev}..@",
    }[vcs.name]
    files = changed_files(vcs.diff_stream(range))
    assert "file1.txt" in files
    assert "anotherfile.txt" in files
    assert "constant.txt" not in files


if __name__ == "__main__":
    mozunit.main()
