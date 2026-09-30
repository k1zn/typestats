"""Refreshes i18n/typestats_en.ts: runs lupdate over src/ and fills the translations from i18n/en.json
(Russian source text -> English). Prints the texts that have no translation yet.

    source env.sh && python i18n/update.py
"""
import json
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
TS = HERE / "typestats_en.ts"

subprocess.run(["lupdate", str(HERE.parent / "src"), "-ts", str(TS), "-source-language", "ru_RU",
                "-target-language", "en_US", "-no-obsolete", "-locations", "none"], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
words = json.loads((HERE / "en.json").read_text(encoding="utf-8"))
tree = ET.parse(TS)
missing = []
for message in tree.getroot().iter("message"):
    source = message.find("source").text
    translation = message.find("translation")
    if source in words:
        translation.text = words[source]
        translation.attrib.pop("type", None)
    else:
        missing.append(source)
tree.write(TS, encoding="utf-8", xml_declaration=True)
text = TS.read_text(encoding="utf-8").replace("<TS ", "<!DOCTYPE TS>\n<TS ", 1)
TS.write_text(text, encoding="utf-8", newline="\n")
print(f"{len(words)} translations, {len(missing)} missing")
for source in missing:
    print("  ", source)
