"""A small JSON Schema checker for the schemas in tools/nt/schemas/.

The machines this runs on have numpy and Pillow, not the jsonschema
package, so this implements the part of JSON Schema (2020-12) those files
use: type, const, enum, required, properties, additionalProperties, items,
minItems, maxItems, minimum, maximum, pattern, minLength, $ref to "#/$defs/..",
anyOf, oneOf. Unknown keywords are ignored, as JSON Schema says.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from . import paths

_TYPES = {
    "object": dict, "array": list, "string": str, "boolean": bool, "null": type(None),
}


def _is_type(value, t: str) -> bool:
    if t == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if t == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    return isinstance(value, _TYPES[t])


def _check(value, schema: dict, root: dict, where: str, errors: list[str]) -> None:
    if "$ref" in schema:
        ref = schema["$ref"]
        if not ref.startswith("#/"):
            errors.append(f"{where}: unsupported $ref {ref}")
            return
        target = root
        for part in ref[2:].split("/"):
            target = target[part]
        _check(value, target, root, where, errors)
        return
    t = schema.get("type")
    if t is not None:
        types = t if isinstance(t, list) else [t]
        if not any(_is_type(value, x) for x in types):
            errors.append(f"{where}: expected {t}, got {type(value).__name__}")
            return
    if "const" in schema and value != schema["const"]:
        errors.append(f"{where}: expected {schema['const']!r}, got {value!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{where}: {value!r} not one of {schema['enum']}")
    for key in ("anyOf", "oneOf"):
        if key in schema:
            matches = 0
            for sub in schema[key]:
                sub_errors: list[str] = []
                _check(value, sub, root, where, sub_errors)
                matches += not sub_errors
            if matches == 0 or (key == "oneOf" and matches > 1):
                errors.append(f"{where}: matches {matches} of the {key} alternatives")
    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                errors.append(f"{where}: missing {name!r}")
        props = schema.get("properties", {})
        extra = schema.get("additionalProperties", True)
        for name, v in value.items():
            if name in props:
                _check(v, props[name], root, f"{where}.{name}", errors)
            elif extra is False:
                errors.append(f"{where}: unexpected property {name!r}")
            elif isinstance(extra, dict):
                _check(v, extra, root, f"{where}.{name}", errors)
    if isinstance(value, list):
        if "minItems" in schema and len(value) < schema["minItems"]:
            errors.append(f"{where}: fewer than {schema['minItems']} items")
        if "maxItems" in schema and len(value) > schema["maxItems"]:
            errors.append(f"{where}: more than {schema['maxItems']} items")
        if "items" in schema:
            for i, v in enumerate(value):
                _check(v, schema["items"], root, f"{where}[{i}]", errors)
    if isinstance(value, str):
        if "pattern" in schema and not re.search(schema["pattern"], value):
            errors.append(f"{where}: {value!r} does not match {schema['pattern']}")
        if "minLength" in schema and len(value) < schema["minLength"]:
            errors.append(f"{where}: shorter than {schema['minLength']}")
    if _is_type(value, "number"):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{where}: {value} < {schema['minimum']}")
        if "maximum" in schema and value > schema["maximum"]:
            errors.append(f"{where}: {value} > {schema['maximum']}")


def validate(doc, schema: dict) -> list[str]:
    errors: list[str] = []
    _check(doc, schema, schema, "$", errors)
    return errors


def schema_for(name: str) -> dict:
    """The schema file of a document's "schema" value, e.g. nexttoonz.render/1."""
    m = re.fullmatch(r"nexttoonz\.([a-z0-9-]+)/(\d+)", name)
    if not m:
        raise KeyError(f"not a NextToonz schema name: {name}")
    path = paths.schemas_dir() / f"{m.group(1)}.schema.json"
    schema = json.loads(path.read_text())
    if schema.get("$id") != name:
        raise KeyError(f"{path} is {schema.get('$id')}, not {name}")
    return schema


def validate_file(path: Path) -> list[str]:
    doc = json.loads(Path(path).read_text())
    if not isinstance(doc, dict) or "schema" not in doc:
        return ["$: no \"schema\" field"]
    try:
        return validate(doc, schema_for(doc["schema"]))
    except (KeyError, OSError) as e:
        return [f"$: {e}"]
