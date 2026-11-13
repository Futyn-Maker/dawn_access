"""Prints the widget tree of a widget Blueprint converted to JSON: every widget export with its
class, the texts it carries, its visibility and its named parent.

python -I tools/assets/widget_tree.py <WBP_*.json> [...]
"""
import json
import sys


def name_of(doc, index):
    if not isinstance(index, int) or index == 0:
        return None
    if index < 0:
        imports = doc.get("Imports", [])
        return imports[-index - 1].get("ObjectName") if -index - 1 < len(imports) else None
    exports = doc.get("Exports", [])
    return exports[index - 1].get("ObjectName") if index - 1 < len(exports) else None


def class_name(doc, export):
    return name_of(doc, export.get("ClassIndex"))


def props(export):
    data = export.get("Data")
    return {p.get("Name"): p for p in data} if isinstance(data, list) else {}


def text_value(prop):
    v = prop.get("Value")
    if isinstance(v, dict):
        return v.get("Value") or v.get("CultureInvariantString") or ""
    if isinstance(v, str):
        return v
    return ""


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        doc = json.load(open(path, encoding="utf-8-sig"))
        exports = doc.get("Exports", [])
        print("==", path.split("/")[-1].split("\\")[-1])
        # parent of each widget: the slot's Parent, else the owner of a named slot/content.
        parent = {}
        for i, e in enumerate(exports, 1):
            p = props(e)
            cls = class_name(doc, e) or ""
            if "Slot" in cls or cls.endswith("Slot"):
                par = p.get("Parent", {}).get("Value")
                content = p.get("Content", {}).get("Value")
                if isinstance(content, int) and content > 0:
                    parent[content] = par
        for i, e in enumerate(exports, 1):
            cls = class_name(doc, e) or "?"
            if cls.endswith("Slot") or cls in ("WidgetTree", "WidgetBlueprintGeneratedClass", "Function") or "Delegate" in cls or cls.endswith("_C") and e.get("ObjectName", "").startswith("Default__"):
                continue
            p = props(e)
            texts = []
            for key in ("Text", "ButtonText", "SettingName", "FooterText", "ContextText", "Title", "CheckEnabledText", "CheckDisabledText", "TextWhenZero"):
                if key in p:
                    t = text_value(p[key])
                    if t:
                        texts.append("%s=%r" % (key, t[:60]))
            vis = p.get("Visibility", {}).get("Value")
            par = name_of(doc, parent.get(i))
            if cls in ("NormalExport",) or not cls:
                continue
            if any(k in p for k in ("Slot", "WidgetTree", "Visibility")) or cls.startswith("WBP_") or cls.endswith("Widget") or "Text" in cls or "Button" in cls or "Image" in cls or "Box" in cls or "Panel" in cls or "Slider" in cls or "Scroll" in cls:
                print("  %-28s %-34s parent=%-26s vis=%-24s %s" % (cls, e.get("ObjectName"), par, vis, " ".join(texts)))


if __name__ == "__main__":
    main()
