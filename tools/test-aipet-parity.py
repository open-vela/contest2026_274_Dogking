# SPDX-License-Identifier: Apache-2.0
"""Compare portable parser with supplied Linux source; never load credentials."""
import contextlib
import importlib.util
import io
import itertools
import pathlib
import subprocess
import sys

binary, goal = sys.argv[1:3]


def load(name):
    spec = importlib.util.spec_from_file_location(name, pathlib.Path(goal) / "src" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


action = load("action_parser").ActionParser
emotion = load("emotion_parser").EmotionParser
cases = [
    "好的！[ACTION:motor.forward(2)][开心]",
    "看！[ACTION:motor.turn_left(4)][ACTION:servo.nod()]转完了！[开心]",
    "危险！[ACTION:rm.rf()][ACTION:motor.stop()]",
    "[ACTION:servo.nod()][ACTION:servo.nod()][难过][开心]",
    "[ACTION:motor.forward(2)", "[ACTION:Motor.forward(2)]",
    "[未知]保留[ACTION:servo.nod(abc)]", "", "\u3000你好\u00a0世界\u2003[开心]\u3000",
]
for text, tag, spacing in itertools.product(
    ["Hello world", "中文🙂", "转完啦！", ""],
    ["", "[开心]", "[安慰][生气]", "[ACTION:led.on(255,0,0)][普通]", "[ACTION:unknown.x()]"],
    ["", " \t\n ", "\u3000\u00a0"],
):
    cases.append(spacing + text + spacing + tag + spacing)
encoded = "".join(s.encode().hex() + "\n" for s in cases)
output = subprocess.run([binary, "--parse-lines"], input=encoded, text=True,
                        stdout=subprocess.PIPE, check=True).stdout.splitlines()
assert len(output) == len(cases)
for source, result in zip(cases, output):
    with contextlib.redirect_stdout(io.StringIO()):
        clean1, actions = action.parse(source)
        clean2, label = emotion.parse(clean1)
    actual = [bytes.fromhex(s).decode() for s in result.split("\t")]
    assert actual == [clean2, label, *actions], (source, actual, clean2, label, actions)
print(f"Linux parser parity passed: {len(cases)} cases; credentials not loaded")
