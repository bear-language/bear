#!/usr/bin/python3

# use to more easily write variant visitors:

types = """
    ExecBlock, ExecBranch, ExecReturn, ExecYield, ExecJump,

    ExecUnionInit, ExecVariantInit, ExecStructInit, ExecAssignable, ExecComptConstant,
    ExecListLiteral, ExecAssignment, ExecMemberAccess, ExecBinary, ExecCast, ExecSubscript,
    ExecFnCall, ExecBorrow, ExecAddrOf, ExecDeref, ExecMatch, ExecMatchBranch, ExecFnPtr,
    ExecVariantFieldInit, ExecRange
"""

names = [t.strip() for t in types.replace("\n", "").split(",") if t.strip()]

template = """\
[](const {name}& d) -> {ret} {{
    // todo
    return {val};
}},"""

RETURN_TYPE = "OptId<TypeId>"

print("auto vs = Ovld{")
for n in names:
    print("    " + template.format(name=n, ret=RETURN_TYPE, val="{}"))
print("};")
