# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import copy

import requests

from marionette_harness import MarionetteTestCase


class TestCommandLineArguments(MarionetteTestCase):
    def setUp(self):
        super(TestCommandLineArguments, self).setUp()

        self.orig_arguments = copy.copy(self.marionette.instance.app_args)

    def tearDown(self):
        self.marionette.instance.app_args = self.orig_arguments
        self.marionette.quit(in_app=False, clean=True)

        super(TestCommandLineArguments, self).tearDown()

    def test_websocket_url(self):
        # By default Remote Agent is not enabled
        self.assertNotIn("webSocketUrl", self.marionette.session_capabilities)

        # With BiDi enabled the capability is returned
        self.marionette.quit()
        self.marionette.instance.app_args.append("-remote-debugging-port")
        self.marionette.start_session({"webSocketUrl": True})

        session_id = self.marionette.session_id
        websocket_url = self.marionette.session_capabilities.get("webSocketUrl")

        self.assertEqual(
            websocket_url, "ws://127.0.0.1:9222/session/{}".format(session_id)
        )

        # Clean the profile so that the preference is definitely reset.
        self.marionette.quit(in_app=False, clean=True)

    # An issue in the command line argument handling lead to open Firefox on
    # random URLs when remote-debugging-port is set to an explicit value, on macos.
    # See Bug 1724251.
    def test_start_page_about_blank(self):
        self.marionette.quit()
        self.marionette.instance.app_args.append("-remote-debugging-port=0")
        self.marionette.start_session({"webSocketUrl": True})
        self.assertEqual(self.marionette.get_url(), "about:blank")

    def test_startup_timeout(self):
        try:
            self.marionette.quit()
            with self.assertRaisesRegex(IOError, "Process killed after 0s"):
                # Use a small enough timeout which should always cause an IOError
                self.marionette.start_session(timeout=0)
        finally:
            self.marionette.start_session()
