"""Shared code of the NextToonz command-line tools (tools/nt, tools/ab, tools/ci).

The contract these modules implement (sub-commands, JSON documents, exit
codes) is doc/nexttoonz_cli.md; the JSON Schemas are in tools/nt/schemas/.
Everything here runs with the Python of the golden tests (3.10 or later,
numpy and Pillow); nothing else is required.
"""

VERSION = "0.1"
