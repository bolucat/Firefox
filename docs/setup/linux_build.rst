Building Firefox On Linux
=========================

This document will help you get set up to build Firefox on your own
computer. Getting set up can take a while - we need to download a
lot of bytes! Even on a fast connection, this can take ten to fifteen
minutes of work, spread out over an hour or two.

Requirements
------------

-  **Memory:** 4GB RAM minimum, 8GB+ recommended.
-  **Disk Space:** At least 30GB of free disk space.
-  **Operating System:** A 64-bit installation of Linux. It is strongly advised
   that you use a supported distribution; see :ref:`build_hosts`.  We also
   recommend that your system is fully up-to-date.

.. note::

    Some Linux distros are better-supported than others. Mozilla maintains
    bootstrapping code for Ubuntu, but others are managed by the
    community (thanks!). The more esoteric the distro you're using,
    the more likely that you'll need to solve unexpected problems.


1. System preparation
---------------------

To build Firefox, it's necessary to have a Python of version 3.9 or later
installed. Python 2 is no longer required to build Firefox, although it is still
required for running some kinds of tests. Additionally, you will probably need
Python development files as well to install some pip packages.

You should be able to install Python and git using your system package manager:

-  For Debian-based Linux (such as Ubuntu): ``sudo apt update && sudo apt install curl python3 python3-pip git``
-  For Fedora Linux: ``sudo dnf install python3 python3-pip git``

If you need a version of Python that your package manager doesn't have,
then you can use `pyenv <https://github.com/pyenv/pyenv>`_, assuming that your
system is supported.

2. Bootstrap a copy of the Firefox source code
----------------------------------------------

Now that your system is ready, we can download the source code and have Firefox
automatically download the other dependencies it needs. The below command
will download a lot of data (years of Firefox history!) then guide you through
the interactive setup process.

.. code-block:: shell

    curl -LO https://raw.githubusercontent.com/mozilla-firefox/firefox/refs/heads/main/python/mozboot/bin/bootstrap.py

    python3 bootstrap.py

Choosing a build type
~~~~~~~~~~~~~~~~~~~~~

If you aren't modifying the Firefox backend, then select one of the
:ref:`Artifact Mode <Understanding Artifact Builds>` options. If you are
building Firefox for Android, you should also see the :ref:`GeckoView Contributor Guide <geckoview-contributor-guide>`.

3. Build
--------

Now that your system is bootstrapped, you should be able to build!

.. code-block:: shell

    cd firefox
    git pull
    ./mach build

🎉 Congratulations! You've built your own home-grown Firefox!
You should see the following message in your terminal after a successful build:

.. code-block:: console

    Your build was successful!
    To take your build for a test drive, run: |mach run|
    For more information on what to do now, see https://firefox-source-docs.mozilla.org/setup/contributing_code.html

You can now use the ``./mach run`` command to run your locally built Firefox!

If your build fails, please reference the steps in the `Troubleshooting section <#troubleshooting>`_.

Now the fun starts
------------------

Time to start hacking! You should join us on `Matrix <https://chat.mozilla.org/>`_,
say hello in the `Introduction channel
<https://chat.mozilla.org/#/room/#introduction:mozilla.org>`_, and `find a bug to
start working on <https://codetribute.mozilla.org/>`_.
See the :ref:`Firefox Contributors' Quick Reference` to learn how to test your changes,
send patches to Mozilla, update your source code locally, and more.

Troubleshooting
---------------

Build errors
~~~~~~~~~~~~

If you encounter a build error when trying to setup your development environment, please follow these steps:
   1. Copy the entire build error to your clipboard
   2. Paste this error to `gist.github.com <https://gist.github.com/>`_ in the text area
   3. Go to the `introduction channel <https://chat.mozilla.org/#/room/#introduction:mozilla.org>`__ and ask for help with your build error. Make sure to post the link to the gist.github.com snippet you created!

The CLOBBER file has been updated
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is a normal error to encounter and tends to appear when working on a bug for a long period of time.
If you encounter this error, you need to run ``./mach clobber`` before running ``./mach build``.
Running ``./mach clobber`` will remove previous build artifacts to restart a build from scratch.
If you are using an artifact build, this will mean that the next build will take slightly longer than usual.
However, if you are using a non-artifact/full build, the next build will take significantly longer to complete.

Using a non-native file system (NTFS, network drive, etc)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In our experience building Firefox in these hybrid or otherwise complex environments
always ends in unexpected, often silent and always hard-to-diagnose failure.
Building Firefox in that environment is far more likely to reveal the flaws and
shortcomings of those systems than it is to produce a running web browser.
