# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from mach.config import ConfigType
from mach.decorators import SettingsProvider


class NullableBooleanType(ConfigType):
    """ConfigType for nullable boolean: True, False, or None."""

    @staticmethod
    def validate(value):
        if value is not None and not isinstance(value, bool):
            raise TypeError()

    @staticmethod
    def from_config(config, section, option):
        value = config.get(section, option).lower()
        if value == "true":
            return True
        elif value == "false":
            return False
        return None

    @staticmethod
    def to_config(value):
        if value is None:
            return "unknown"
        return "true" if value else "false"


@SettingsProvider
class MachSettings:
    @classmethod
    def config_settings(cls):
        def telemetry_config_settings():
            return [
                (
                    "build.telemetry",
                    "boolean",
                    "Enable submission of build system telemetry "
                    '(Deprecated, replaced by "telemetry.is_enabled")',
                ),
                (
                    "mach_telemetry.is_enabled",
                    "boolean",
                    "Build system telemetry is allowed",
                    False,
                ),
                (
                    "mach_telemetry.is_set_up",
                    "boolean",
                    "The telemetry setup workflow has been completed "
                    "(e.g.: user has been prompted to opt-in)",
                    False,
                ),
                (
                    "mach_telemetry.is_employee",
                    NullableBooleanType,
                    "Cached value for whether the user is a Mozilla employee "
                    "(None=unknown, True=employee, False=not an employee)",
                    None,
                ),
            ]

        def dispatch_config_settings():
            return [
                (
                    "alias.*",
                    "string",
                    """
        Create a command alias of the form `<alias>=<command> <args>`.
        Aliases can also be used to set default arguments:
        <command>=<command> <args>
        """.strip(),
                ),
            ]

        def run_config_settings():
            return [
                (
                    "runprefs.*",
                    "string",
                    """
        Pass a pref into Firefox when using `mach run`, of the form `foo.bar=value`.
        Prefs will automatically be cast into the appropriate type. Integers can be
        single quoted to force them to be strings.
        """.strip(),
                ),
            ]

        def try_config_settings():
            desc = "The default selector to use when running `mach try` without a subcommand."
            choices = [
                "fuzzy",
                "chooser",
                "auto",
                "again",
                "empty",
                "syntax",
                "coverage",
                "release",
                "scriptworker",
                "compare",
                "perf",
            ]

            return [
                ("try.default", "string", desc, "auto", {"choices": choices}),
                (
                    "try.maxhistory",
                    "int",
                    "Maximum number of pushes to save in history.",
                    10,
                ),
                (
                    "try.nobrowser",
                    "boolean",
                    "Do not automatically open a browser during authentication.",
                    False,
                ),
            ]

        def taskgraph_config_settings():
            return [
                (
                    "taskgraph.diffcmd",
                    "string",
                    "The command to run with `./mach taskgraph --diff`",
                    "diff --report-identical-files "
                    "--label={attr}@{base} --label={attr}@{cur} -U20",
                    {},
                )
            ]

        def test_config_settings():
            from mozlog.commandline import log_formatters
            from mozlog.structuredlog import log_levels

            format_desc = (
                "The default format to use when running tests with `mach test`."
            )
            format_choices = list(log_formatters)
            level_desc = (
                "The default log level to use when running tests with `mach test`."
            )
            level_choices = [l.lower() for l in log_levels]
            return [
                (
                    "test.format",
                    "string",
                    format_desc,
                    "mach",
                    {"choices": format_choices},
                ),
                (
                    "test.level",
                    "string",
                    level_desc,
                    "info",
                    {"choices": level_choices},
                ),
            ]

        settings = []
        settings.extend(telemetry_config_settings())
        settings.extend(dispatch_config_settings())
        settings.extend(run_config_settings())
        settings.extend(try_config_settings())
        settings.extend(taskgraph_config_settings())
        settings.extend(test_config_settings())

        return settings
