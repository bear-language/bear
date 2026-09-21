#!/usr/bin/python3

# use to more easily write variant visitors:

types = """
DefModule, DefFunction, DefGenericFunction, DefFunctionPrototype, DefVariable,
                   DefStruct, DefGenericStruct, DefVariant, DefGenericVariant, DefVariantField,
                   DefUnion, DefGenericContract, DefContract, DefDeftype, DefScopeWrapper,
                   DefUnevaluated, DefMalformed
"""

names = [t.strip() for t in types.replace("\n", "").split(",") if t.strip()]

template = """\
[&ctx](const {name}& d) -> {ret} {{
    // todo
    return {val};
}},"""

RETURN_TYPE = "std::string"

print("const auto vs = Ovld{")
for n in names:
    print("    " + template.format(name=n, ret=RETURN_TYPE, val="{}"))
print("};")
