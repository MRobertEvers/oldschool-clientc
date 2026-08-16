#!/usr/bin/env python3
"""Regenerate cache dbtable inverted indexes from their dbrow records.

The existing .dbi entry order is retained as an encoding-order hint; all row
membership is re-derived. New values are appended in a deterministic order.
This keeps a pristine tree byte-identical while making omitted or stale rows a
loud check failure.
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path


HEADER = """// A dbtable's inverted indexes — what DB_FIND scans.
//
// [master] is file 0, every row id in the table, which DB_FINDALL returns.
// [column_N] is file N+1, the index for that column. A column with several
// typed fields gets one tuple position per field.
//
//   base=<pos>:<int|long|string>   declared for every position, empty or not
//   index=<pos>:<value>:<row>,...  entries in binary order; DB_FIND is linear
//
// Derived from the dbrows rather than authored — a readable view, not a
// source of truth. {stem}.compack ties each block to its file id.

"""


@dataclass(frozen=True)
class Schema:
    table_id: int
    columns: dict[int, tuple[str, ...]]
    defaults: dict[int, tuple[int | str, ...]]


def parse_blocks(path: Path) -> dict[str, list[tuple[str, str]]]:
    blocks: dict[str, list[tuple[str, str]]] = {}
    current: list[tuple[str, str]] | None = None
    for lineno, raw in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            name = line[1:-1]
            if name in blocks:
                raise ValueError(f"{path}:{lineno}: duplicate block [{name}]")
            current = blocks[name] = []
            continue
        if current is None or "=" not in line:
            raise ValueError(f"{path}:{lineno}: expected key=value inside a block")
        key, value = line.split("=", 1)
        current.append((key.strip(), value.strip()))
    return blocks


def parse_id_map(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for lineno, raw in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{lineno}: expected id=name")
        raw_id, name = line.split("=", 1)
        result[name.strip()] = int(raw_id.strip(), 0)
    return result


def merge_blocks(paths: list[Path]) -> dict[str, list[tuple[str, str]]]:
    result: dict[str, list[tuple[str, str]]] = {}
    for path in paths:
        for name, lines in parse_blocks(path).items():
            if name in result:
                raise ValueError(f"{path}: duplicate dbrow block [{name}] across sources")
            result[name] = lines
    return result


def one_value(lines: list[tuple[str, str]], key: str) -> str | None:
    values = [value for got, value in lines if got == key]
    if len(values) > 1:
        raise ValueError(f"duplicate {key}= in block")
    return values[0] if values else None


def base_type(type_name: str) -> str:
    if type_name == "string":
        return "string"
    if type_name == "long":
        return "long"
    return "int"


def load_schemas(content: Path) -> tuple[dict[int, Schema], dict[str, int]]:
    table_ids = parse_id_map(content / "configs/all.dbtable.compack")
    blocks = parse_blocks(content / "configs/all.dbtable")
    schemas: dict[int, Schema] = {}
    for name, table_id in table_ids.items():
        lines = blocks.get(name)
        if lines is None:
            raise ValueError(f"dbtable id {table_id} names missing block [{name}]")
        columns: dict[int, tuple[str, ...]] = {}
        for key, value in lines:
            if key != "columndef":
                continue
            raw_column, definition = value.split(":", 1)
            fields = next(csv.reader([definition], skipinitialspace=False))
            if len(fields) < 2:
                raise ValueError(f"[{name}] malformed columndef={value}")
            columns[int(raw_column)] = tuple(base_type(t) for t in fields[1:])
        defaults: dict[int, tuple[int | str, ...]] = {}
        for key, value in lines:
            if key != "defaults":
                continue
            raw_column, _repeat, raw_fields = value.split(":", 2)
            column = int(raw_column)
            kinds = columns.get(column)
            if kinds is None:
                raise ValueError(f"[{name}] default for undeclared column {column}")
            fields = next(csv.reader([raw_fields], skipinitialspace=False))
            if len(fields) != len(kinds) and len(kinds) == 1 and kinds[0] == "string":
                fields = [raw_fields]
            if len(fields) != len(kinds):
                raise ValueError(
                    f"[{name}] column {column}: {len(fields)} defaults for {len(kinds)} fields"
                )
            defaults[column] = tuple(parse_scalar(v, k) for v, k in zip(fields, kinds))
        schemas[table_id] = Schema(table_id, columns, defaults)
    return schemas, table_ids


def parse_scalar(raw: str, kind: str) -> int | str:
    if kind == "string":
        return raw
    return int(raw, 0)


def load_rows(
    content: Path,
    table_ids: dict[str, int],
) -> dict[int, list[tuple[int, dict[int, list[tuple[int | str, ...]]]]]]:
    base_path = content / "configs/all.dbrow"
    extra_paths = sorted(path for path in (content / "configs").rglob("*.dbrow") if path != base_path)
    blocks = merge_blocks([base_path, *extra_paths])
    row_ids = parse_id_map(content / "configs/all.dbrow.compack")
    if extra_paths:
        alloc_path = content / "pack/dbrow.alloc"
        if not alloc_path.is_file():
            raise ValueError("authored client dbrows require pack/dbrow.alloc")
        allocated = parse_id_map(alloc_path)
        for path in extra_paths:
            for name in parse_blocks(path):
                if name not in allocated:
                    raise ValueError(f"{path}: dbrow [{name}] has no id in pack/dbrow.alloc")
                if name in row_ids and row_ids[name] != allocated[name]:
                    raise ValueError(f"dbrow [{name}] has conflicting cache/allocation ids")
                row_ids[name] = allocated[name]
    schemas, _ = load_schemas(content)
    indexed_columns: dict[int, set[int]] = defaultdict(set)
    for compack in (content / "dbindex").glob("dbindex_*.compack"):
        table_id = int(compack.stem.split("_", 1)[1])
        for file_id, _name in parse_compack(compack):
            if file_id > 0:
                indexed_columns[table_id].add(file_id - 1)
    rows: dict[int, list[tuple[int, dict[int, list[tuple[int | str, ...]]]]]] = defaultdict(list)

    for name, row_id in row_ids.items():
        lines = blocks.get(name)
        if lines is None:
            raise ValueError(f"dbrow id {row_id} names missing block [{name}]")
        table_name = one_value(lines, "table")
        if table_name is None or table_name not in table_ids:
            raise ValueError(f"dbrow [{name}] has unknown table={table_name}")
        table_id = table_ids[table_name]
        schema = schemas[table_id]
        values: dict[int, list[tuple[int | str, ...]]] = defaultdict(list)
        for key, value in lines:
            if key != "values":
                continue
            raw_column, _repeat, raw_fields = value.split(":", 2)
            column = int(raw_column)
            if column not in indexed_columns[table_id]:
                continue
            kinds = schema.columns.get(column)
            if kinds is None:
                raise ValueError(f"dbrow [{name}] values for undeclared column {column}")
            fields = next(csv.reader([raw_fields], skipinitialspace=False))
            if len(fields) != len(kinds):
                raise ValueError(
                    f"dbrow [{name}] column {column}: {len(fields)} values for {len(kinds)} fields"
                )
            values[column].append(tuple(parse_scalar(v, k) for v, k in zip(fields, kinds)))
        for column in indexed_columns[table_id]:
            if column not in values and column in schema.defaults:
                values[column].append(schema.defaults[column])
        rows[table_id].append((row_id, values))

    for table_rows in rows.values():
        table_rows.sort(key=lambda row: row[0])
    return rows


def parse_compack(path: Path) -> list[tuple[int, str]]:
    members: list[tuple[int, str]] = []
    for lineno, raw in enumerate(path.read_text(encoding="latin-1").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{lineno}: expected id=name")
        raw_id, name = line.split("=", 1)
        members.append((int(raw_id), name.strip()))
    return members


def parse_template_order(path: Path) -> dict[str, dict[int, list[int | str]]]:
    order: dict[str, dict[int, list[int | str]]] = defaultdict(lambda: defaultdict(list))
    current = ""
    bases: dict[tuple[str, int], str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1]
        elif line.startswith("base="):
            raw_pos, kind = line[5:].split(":", 1)
            bases[(current, int(raw_pos))] = kind
        elif line.startswith("index="):
            raw_pos, remainder = line[6:].split(":", 1)
            pos = int(raw_pos)
            kind = bases.get((current, pos), "int")
            if kind == "string":
                if not remainder.startswith('"'):
                    raise ValueError(f"{path}: unquoted string index")
                escaped = False
                chars: list[str] = []
                end = -1
                for i, char in enumerate(remainder[1:], 1):
                    if escaped:
                        chars.append(char)
                        escaped = False
                    elif char == "\\":
                        escaped = True
                    elif char == '"':
                        end = i
                        break
                    else:
                        chars.append(char)
                if end < 0 or remainder[end + 1 : end + 2] != ":":
                    raise ValueError(f"{path}: malformed quoted string index")
                value: int | str = "".join(chars)
            else:
                raw_value, _rows = remainder.split(":", 1)
                value = int(raw_value, 0)
            order[current][pos].append(value)
    return order


def quote_value(value: int | str, kind: str) -> str:
    if kind != "string":
        return str(value)
    escaped = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def derive_file(
    table_rows: list[tuple[int, dict[int, list[tuple[int | str, ...]]]]],
    schema: Schema,
    file_id: int,
) -> tuple[tuple[str, ...], list[dict[int | str, list[int]]]]:
    if file_id == 0:
        row_ids = [row_id for row_id, _values in table_rows]
        return ("int",), [{0: row_ids} if row_ids else {}]
    column = file_id - 1
    kinds = schema.columns.get(column)
    if kinds is None:
        raise ValueError(f"table {schema.table_id} indexes undeclared column {column}")
    indexes: list[dict[int | str, list[int]]] = [defaultdict(list) for _ in kinds]
    for row_id, values in table_rows:
        for occurrence in values.get(column, []):
            for pos, value in enumerate(occurrence):
                indexes[pos][value].append(row_id)
    return kinds, [dict(index) for index in indexes]


def ordered_values(
    derived: dict[int | str, list[int]],
    hint: list[int | str],
) -> list[int | str]:
    kept = [value for value in hint if value in derived]
    seen = set(kept)
    added = sorted(value for value in derived if value not in seen)
    return kept + added


def render_table(
    stem: str,
    members: list[tuple[int, str]],
    schema: Schema,
    table_rows: list[tuple[int, dict[int, list[tuple[int | str, ...]]]]],
    hint: dict[str, dict[int, list[int | str]]],
) -> str:
    output = [HEADER.format(stem=stem)]
    for file_id, block_name in members:
        kinds, indexes = derive_file(table_rows, schema, file_id)
        output.append(f"[{block_name}]\n")
        for pos, kind in enumerate(kinds):
            output.append(f"base={pos}:{kind}\n")
        for pos, (kind, entries) in enumerate(zip(kinds, indexes)):
            for value in ordered_values(entries, hint.get(block_name, {}).get(pos, [])):
                rows = ",".join(str(row) for row in sorted(entries[value]))
                output.append(f"index={pos}:{quote_value(value, kind)}:{rows}\n")
        output.append("\n")
    return "".join(output)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--content", type=Path, default=repo / "OSRS-Content/osrs239-content"
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="fail if any committed index is stale")
    mode.add_argument("--write", action="store_true", help="rewrite stale index files")
    args = parser.parse_args()

    content = args.content.resolve()
    schemas, table_ids = load_schemas(content)
    rows = load_rows(content, table_ids)
    checked = 0
    stale: list[Path] = []

    for path in sorted((content / "dbindex").glob("dbindex_*.dbi")):
        table_id = int(path.stem.split("_", 1)[1])
        if table_id not in schemas:
            raise ValueError(f"{path}: no dbtable schema for table {table_id}")
        compack = path.with_suffix(".compack")
        members = parse_compack(compack)
        generated = render_table(
            path.stem, members, schemas[table_id], rows.get(table_id, []), parse_template_order(path)
        )
        checked += 1
        if generated.encode("utf-8") != path.read_bytes():
            stale.append(path)
            if args.write:
                path.write_text(generated, encoding="utf-8")

    if checked == 0:
        print("gen_dbindex: ERROR: checked zero indexes", file=sys.stderr)
        return 1
    if stale and not args.write:
        print(f"gen_dbindex: {len(stale)} stale of {checked} index files", file=sys.stderr)
        for path in stale[:20]:
            print(f"  {path}", file=sys.stderr)
        return 1
    action = "rewrote" if args.write else "verified"
    print(f"gen_dbindex: {action} {checked} index files; stale={len(stale)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"gen_dbindex: ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
