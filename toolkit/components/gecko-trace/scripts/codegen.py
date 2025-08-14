# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

from os.path import dirname
from pathlib import Path

import jinja2
from buildconfig import config, topsrcdir  # type: ignore
from mozbuild.util import memoize  # type: ignore
from schema_parser import parse_and_validate

THIS_DIR = Path(dirname(__file__))
TEMPLATES = THIS_DIR / "templates"


@memoize
def get_deps():
    # Any imported python module is added as a dep automatically,
    # so we only need the templates.
    return {
        *[str(p) for p in TEMPLATES.iterdir()],
        str(THIS_DIR / "gecko-trace.schema.yaml"),
    }


def generate_cpp_events(output_fd, *inputs):
    """Generate C++ events header"""

    events = parse_and_validate(inputs if inputs else load_schema_index())

    template = _jinja2_env().get_template("GeckoTraceEvents.h.jinja2")
    output_fd.write(
        template.render(
            events=events, enabled=config.substs.get("GECKO_TRACE_ENABLE", False)
        )
    )
    output_fd.write("\n")

    return get_deps().union(load_schema_index() if not inputs else {})


@memoize
def load_schema_index():
    index = THIS_DIR.parent / "index.py"

    with open(index) as f:
        index_src = f.read()

    global gecko_trace_files
    exec(index_src, globals())

    return [str(Path(topsrcdir) / x) for x in gecko_trace_files]  # type: ignore


@memoize
def _jinja2_env():
    from jinja2.exceptions import TemplateRuntimeError
    from jinja2.ext import Extension

    class RaiseExtension(Extension):
        tags = set(["raise"])

        def parse(self, parser):
            lineno = next(parser.stream).lineno

            message_node = parser.parse_expression()

            return jinja2.nodes.CallBlock(
                self.call_method("_raise", [message_node], lineno=lineno),
                [],
                [],
                [],
                lineno=lineno,
            )

        def _raise(self, msg, caller):
            raise TemplateRuntimeError(msg)

    def camelize(name):
        """Convert snake_case to PascalCase"""
        return "".join(
            word.capitalize()
            for word in name.replace("-", "_").replace(".", "_").split("_")
        )

    def snake_case(name):
        """Convert name to snake_case"""
        return name.replace(".", "_").replace("-", "_").lower()

    def debug(value):
        print(value)
        return value

    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(TEMPLATES),
        trim_blocks=True,
        lstrip_blocks=True,
        extensions=[RaiseExtension],
    )

    env.filters["camelize"] = camelize
    env.filters["snake_case"] = snake_case
    env.filters["debug"] = debug

    return env


def main(*args):  # mach requires this
    pass
