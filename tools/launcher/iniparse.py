"""Reader for the boot-manifest / profile INI dialect.

This is the dialect `src/bootmanifest/bootmanifest.c` accepts, and the reasons
configparser cannot stand in for it are specific:

  * A key may REPEAT inside a section (`lane=`, `arg=`, `c=`). configparser
    keeps the last one, which would silently drop every content lane but the
    final one.
  * `[revconfig:layout:root]` separates records with a bare `=` line.
  * Section names carry colons (`[revconfig:component:world]`).

Trailing `;`/`#` comments are NOT stripped from values. bootmanifest.c strips
them only inside its numeric field parsers, and stripping globally here would
corrupt the two places it matters: `[net:boot] cheat=` uses `;` to separate the
`::` commands sent at login, and `rsa_mod=` is a 256-char hex blob.
"""


class Ini:
    """Parsed INI: ordered (section, key, value) triples, duplicates intact."""

    def __init__(self, path, entries):
        self.path = path
        self.entries = entries

    @classmethod
    def load(cls, path):
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return cls.loads(handle.read(), path)

    @classmethod
    def loads(cls, text, path="<string>"):
        entries = []
        section = ""
        for raw in text.splitlines():
            line = raw.strip()
            if not line or line[0] in ";#":
                continue
            if line.startswith("["):
                end = line.find("]")
                if end > 0:
                    section = line[1:end].strip()
                continue
            if "=" not in line:
                continue
            key, _, value = line.partition("=")
            entries.append((section, key.strip(), value.strip()))
        return cls(path, entries)

    def get(self, section, key, default=None):
        """First value of `key` in `section`, else `default`."""
        for entry_section, entry_key, value in self.entries:
            if entry_section == section and entry_key == key:
                return value
        return default

    def get_any(self, key, default=None):
        """First value of `key` in ANY section.

        run-live.sh reads most manifest fields this way because each appears
        exactly once. Prefer get() where the section is known; this exists for
        the handful of fields whose section has drifted between manifests.
        """
        for _, entry_key, value in self.entries:
            if entry_key == key:
                return value
        return default

    def get_all(self, section, key):
        """Every value of a repeated key, in file order."""
        return [
            value
            for entry_section, entry_key, value in self.entries
            if entry_section == section and entry_key == key
        ]

    def items(self, section):
        """Every (key, value) in a section, in file order, duplicates intact."""
        return [
            (entry_key, value)
            for entry_section, entry_key, value in self.entries
            if entry_section == section
        ]

    def sections(self):
        """Section names in first-appearance order."""
        seen = []
        for entry_section, _, _ in self.entries:
            if entry_section not in seen:
                seen.append(entry_section)
        return seen

    def has_section(self, section):
        return any(entry_section == section for entry_section, _, _ in self.entries)

    def sections_with_prefix(self, prefix):
        """Section names starting with `prefix` — how `[derived:*]` and
        `[override:*]` blocks are discovered without naming each one."""
        return [name for name in self.sections() if name.startswith(prefix)]
