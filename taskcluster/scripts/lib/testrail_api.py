#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
# TestRail API Wrapper Module

# This module provides methods to interact with the TestRail API.
# API Version: v2
# TestRail Version: 8.0.5 https://mozilla.testrail.io

# For detailed information on the TestRail API, please refer to the official documentation:
# https://support.testrail.com/hc/en-us/sections/7077185274644-API-reference
"""
This module provides a TestRail class for interfacing with the TestRail API, enabling the creation and management of test milestones, test runs, and updating test cases. It facilitates automation and integration of TestRail functionalities into testing workflows, particularly for projects requiring automated test management and reporting.

The TestRail class encapsulates methods to interact with TestRail's API, including creating milestones and test runs, updating test cases, and checking the existence of milestones. It also features a method to retry API calls, enhancing the robustness of network interactions.

Key Components:
- TestRail Class: A class providing methods for interacting with TestRail's API.
  - create_milestone: Create a new milestone in a TestRail project.
  - create_milestone_and_test_runs: Create a milestone and associated test runs for multiple devices in a project.
  - create_test_run: Create a test run within a TestRail project.
  - does_milestone_exist: Check if a milestone already exists in a TestRail project.
  - update_test_cases_to_passed: Update the status of test cases to 'passed' in a test run.
- Private Methods: Utility methods for internal use to fetch test cases, update test run results, and retrieve milestones.
- Retry Mechanism: A method to retry API calls with a specified number of attempts and delay, improving reliability in case of intermittent network issues.

Usage:
This module is intended to be used as part of a larger automated testing system, where integration with TestRail is required for test management and reporting.

"""

import os
import sys
import time

# Ensure the directory containing this script is in Python's search path
script_directory = os.path.dirname(os.path.abspath(__file__))
if script_directory not in sys.path:
    sys.path.append(script_directory)

from testrail_conn import APIClient


class TestRail:
    def __init__(self, host, username, password):
        if not all([host, username, password]):
            raise ValueError("TestRail host, username, and password must be provided.")
        self.client = APIClient(host)
        self.client.user = username
        self.client.password = password

    # Public Methods

    def create_milestone(self, project_id, title, description):
        if not all([project_id, title, description]):
            raise ValueError("Project ID, title, and description must be provided.")
        data = {"name": title, "description": description}
        return self.client.send_post(f"add_milestone/{project_id}", data)

    def create_test_run(
        self,
        project_id,
        milestone_id,
        test_run_name,
        suite_id,
    ):
        if not all([project_id, milestone_id, test_run_name, suite_id]):
            raise ValueError(
                "Project ID, milestone ID, test run name, and suite ID must be provided."
            )
        data = {
            "name": test_run_name,
            "milestone_id": milestone_id,
            "suite_id": suite_id,
        }
        return self.client.send_post(f"add_run/{project_id}", data)

    def does_milestone_exist(self, project_id, milestone_name):
        """
        Check if a milestone with a specific name exists among open milestones.

        Args:
            project_id (int): ID of the project.
            milestone_name (str): Name of the milestone to search for.

        Returns:
            bool: True if the milestone exists, False otherwise.
        """
        if not all([project_id, milestone_name]):
            raise ValueError("Project ID and milestone name must be provided.")

        # Fetch open milestones (is_completed=False)
        response = self._get_milestones(project_id, is_completed=False)
        milestones = response.get("milestones", [])
        # Check for the milestone name
        return any(milestone["name"] == milestone_name for milestone in milestones)

    def update_test_run_tests(self, test_run_id, test_status):
        if not all([test_run_id, test_status]):
            raise ValueError("Test run ID and test status must be provided.")
        tests = self._get_tests(test_run_id)
        data = {
            "results": [
                {"test_id": test["id"], "status_id": test_status} for test in tests
            ]
        }
        return self.client.send_post(f"add_results/{test_run_id}", data)

    # Private Methods

    def _get_test_cases(self, project_id, suite_id):
        if not all([project_id, suite_id]):
            raise ValueError("Project ID and suite ID must be provided.")
        return self.client.send_get(f"get_cases/{project_id}&suite_id={suite_id}")[
            "cases"
        ]

    def _get_milestone(self, milestone_id):
        if not milestone_id:
            raise ValueError("Milestone ID must be provided.")
        return self.client.send_get(f"get_milestone/{milestone_id}")

    def _get_milestones(self, project_id, **filters):
        """
        Retrieves milestones for a given project with optional filters.

        Args:
            project_id (int): The ID of the project.
            **filters: Arbitrary keyword arguments representing API filters.

        Available Filters:
            is_completed (bool or int):
                - Set to True or 1 to return completed milestones only.
                - Set to False or 0 to return open (active/upcoming) milestones only.

            is_started (bool or int):
                - Set to True or 1 to return started milestones only.
                - Set to False or 0 to return upcoming milestones only.

            limit (int):
                - The number of milestones the response should return.
                - The default response size is 250 milestones.

            offset (int):
                - Where to start counting the milestones from (the offset).
                - Used for pagination.

        Returns:
            dict: The API response containing milestones.
        """

        if not project_id:
            raise ValueError("Project ID must be provided.")

        # Base endpoint
        endpoint = f"get_milestones/{project_id}"

        # Process filters
        if filters:
            # Convert boolean values to integers (API expects 1 or 0)
            for key in ["is_completed", "is_started"]:
                if key in filters and isinstance(filters[key], bool):
                    filters[key] = int(filters[key])

            # Build query parameters
            query_params = "&".join(f"{key}={value}" for key, value in filters.items())
            endpoint = f"{endpoint}&{query_params}"

        # Make API call
        return self.client.send_get(endpoint)

    def _get_tests(self, test_run_id):
        if not test_run_id:
            raise ValueError("Test run ID must be provided.")
        return self.client.send_get(f"get_tests/{test_run_id}")["tests"]

    def _get_test_run(self, test_run_id):
        if not test_run_id:
            raise ValueError("Test run ID must be provided.")
        return self.client.send_get(f"get_run/{test_run_id}")

    def _get_test_runs(self, project_id):
        if not project_id:
            raise ValueError("Project ID must be provided.")
        return self.client.send_get(f"get_runs/{project_id}")["runs"]

    def _get_test_run_results(self, test_run_id):
        if not test_run_id:
            raise ValueError("Test run ID must be provided.")
        return self.client.send_get(f"get_results_for_run/{test_run_id}")["results"]

    def _retry_api_call(self, api_call, *args, max_retries=3, delay=5):
        if not all([api_call, args]):
            raise ValueError("API call and arguments must be provided.")
        """
        Retries the given API call up to max_retries times with a delay between attempts.

        :param api_call: The API call method to retry.
        :param args: Arguments to pass to the API call.
        :param max_retries: Maximum number of retries.
        :param delay: Delay between retries in seconds.
        """
        for attempt in range(max_retries):
            try:
                return api_call(*args)
            except Exception:
                if attempt == max_retries - 1:
                    raise  # Reraise the last exception
                time.sleep(delay)
