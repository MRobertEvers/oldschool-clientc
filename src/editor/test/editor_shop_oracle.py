#!/usr/bin/env python3
"""What tools/gen_shop_scripts.py writes for each shop of a samples file.

    editor_shop_oracle.py <tools-dir> <shop_samples.tsv> <out-dir>

Imports gen_shop_scripts itself and calls its own renderers, render_rs2 and
render_inv, with the rows the samples file lists (the same rows
editor_shop_test.c feeds editor_shop.c), so the bytes compared are the
generator's and not a restatement of them. Writes <out-dir>/<key>.rs2 and
<out-dir>/<key>.inv. No content tree is read: the samples stand in for the
wiki catalogue and shop_inv_map.tsv.

render_inv's text is what the generator hands config_text.write_config; a
cache inv (every verified binding is one) is an overlay block that
complete_new_blocks leaves as it is, so that text is the file.
"""

import os
import sys


def main():
    tools, samples, out_dir = sys.argv[1:4]
    sys.path.insert(0, tools)
    import gen_shop_scripts as gen  # noqa: E402 -- from the path given

    os.makedirs(out_dir, exist_ok=True)
    shops = []
    with open(samples, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if cells[0] == "shop":
                (key, name, page, revid, date, special, members, buy, sell, delta, inv) = cells[1:12]
                shops.append({
                    "key": key,
                    "inv": inv,
                    "shop": {
                        "shop_name": name, "page_title": page, "revid": revid, "fetch_date": date,
                        "special": special, "members": members, "buymultiplier": buy,
                        "sellmultiplier": sell, "delta": delta,
                    },
                    "owners": [],
                    "fresh": [],
                    "stock": [],
                })
            elif cells[0] == "owner":
                shops[-1]["owners"].append(cells[1])
                if cells[2] == "1":
                    shops[-1]["fresh"].append(cells[1])
            elif cells[0] == "stock":
                shops[-1]["stock"].append({"obj_gameval": cells[1], "stock": cells[2], "restock": cells[3]})
            else:
                raise SystemExit("%s: unknown row %r" % (samples, cells[0]))
    for entry in shops:
        rs2 = gen.render_rs2(entry["key"], entry["shop"], entry["owners"], entry["fresh"], entry["inv"])
        inv = gen.render_inv(entry["key"], entry["shop"], entry["stock"], entry["inv"])
        with open(os.path.join(out_dir, entry["key"] + ".rs2"), "w", encoding="utf-8") as handle:
            handle.write(rs2)
        with open(os.path.join(out_dir, entry["key"] + ".inv"), "w", encoding="utf-8") as handle:
            handle.write(inv)
    print("editor_shop_oracle: %d shop(s) rendered by gen_shop_scripts.py" % len(shops))


if __name__ == "__main__":
    main()
