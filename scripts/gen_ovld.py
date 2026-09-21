#!/usr/bin/python3

# use to more easily write variant visitors:

types = """
    ExecBlock, ExecBranch, ExecReturn, ExecYield, ExecJump,

    ExecUnionInit, ExecVariantInit, ExecStructInit, ExecVariable, ExecComptConstant,
    ExecListLiteral, ExecAssignment, ExecMemberAccess, ExecBinary, ExecCast, ExecSubscript,
    ExecFnCall, ExecBorrow, ExecAddrOf, ExecDeref, ExecMatch, ExecMatchBranch, ExecFnPtr,
    ExecVariantFieldInit, ExecRange
"""

names = [t.strip() for t in types.replace("\n", "").split(",") if t.strip()]

template = """\
[](const {name}& d) -> {ret} {{
    return {val};
}},"""

RETURN_TYPE = "bool"

print("const auto vs = Ovld{")
for n in names:
    print("    " + template.format(name=n, ret=RETURN_TYPE, val="{}"))
print("};")
