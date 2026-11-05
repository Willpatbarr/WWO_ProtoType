"""Reads the Godot project's data/*.tres files, applies the tuned values from
saved_settings.json on top (the user's "Save as defaults"), and writes one JSON file
that the Unreal setup script turns into data assets.

usage: python3 tres_to_json.py <godot project> <out.json>
"""
import json
import os
import re
import sys

src, out = sys.argv[1], sys.argv[2]


def parse_value(text):
    text = text.strip()
    if text in ("true", "false"):
        return text == "true"
    m = re.match(r'^(Vector3|Vector2|Color)\((.*)\)$', text)
    if m:
        return [float(v) for v in m.group(2).split(",")]
    if text.startswith('"'):
        return json.loads(text)
    m = re.match(r'^SubResource\("(.*)"\)$', text)
    if m:
        return {"sub": m.group(1)}
    m = re.match(r'^Array\[.*?\]\(\[(.*)\]\)$', text)
    if m:
        return [parse_value(p) for p in re.findall(r'SubResource\("[^"]*"\)', m.group(1))]
    try:
        return float(text)
    except ValueError:
        return text


def parse_tres(path):
    blocks = {}
    current = None
    key = None
    with open(path) as f:
        lines = f.read().split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r'^\[sub_resource .*id="([^"]+)"\]', line)
        if m:
            current = blocks.setdefault(m.group(1), {})
        elif line.startswith("[resource]"):
            current = blocks.setdefault("__main__", {})
        elif line.startswith("["):
            current = None
        elif current is not None and "=" in line and not line.startswith("script"):
            name, _, value = line.partition(" = ")
            # multi-line strings
            if value.startswith('"') and (value.count('"') - value.count('\\"')) % 2 == 1:
                while True:
                    i += 1
                    value += "\n" + lines[i]
                    if lines[i].rstrip().endswith('"'):
                        break
            current[name.strip()] = parse_value(value)
        i += 1
    main = blocks.get("__main__", {})
    for k, v in list(main.items()):
        if isinstance(v, list) and v and isinstance(v[0], dict) and "sub" in v[0]:
            main[k] = [blocks[s["sub"]] for s in v]
    return main


saved = {}
saved_path = os.path.join(src, "saved_settings.json")
if os.path.exists(saved_path):
    saved = json.load(open(saved_path))

sections = {"weapons": "weapon", "grenades": "grenade", "abilities": "ability",
            "frame_parts": "frame", "flora": "flora", "traits": "trait"}
result = {}
for folder, prefix in sections.items():
    items = []
    d = os.path.join(src, "data", folder)
    for name in sorted(os.listdir(d)):
        if not name.endswith(".tres"):
            continue
        item = parse_tres(os.path.join(d, name))
        tuned = saved.get(prefix + "/" + item.get("id", ""), {})
        for k, v in tuned.items():
            m = re.match(r"^mod(\d+)$", k)
            if m:
                item["mods"][int(m.group(1))]["value"] = v
            else:
                item[k] = v
        items.append(item)
    items.sort(key=lambda x: x.get("order", 0))
    result[folder] = items

# Enemies / allies: each type's .tres plus its Global settings tab.
for kind, tab in (("soldier", "Soldier"), ("swarm", "Swarm"), ("tank", "Tank")):
    item = parse_tres(os.path.join(src, "data", "enemies", kind + ".tres"))
    item.update(saved.get("global/" + tab, {}))
    result[kind] = item
ally = parse_tres(os.path.join(src, "data", "allies", "ally.tres"))
ally.update(saved.get("global/Allies", {}))
result["ally"] = ally

# Every Global tab, for the C++ defaults.
result["global"] = {k[7:]: v for k, v in saved.items() if k.startswith("global/")}
json.dump(result, open(out, "w"), indent=1)
print("wrote", out, {k: len(v) for k, v in result.items() if isinstance(v, list)})
