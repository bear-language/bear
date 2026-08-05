#!/usr/bin/python3
name = "Foo"
ty = "u8"

print(f"variant {name}" + "{")

for i in range(66000):
    print(f"    _{i}({ty} a),")

print("}")
