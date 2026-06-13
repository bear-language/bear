# use to more easily write variant visitors:

types = """
    ExecBlock, ExecExprStmt, ExecBreakStmt, ExecContinueStmt, ExecIfStmt, ExecLoopStmt,
    ExecReturnStmt, ExecYieldStmt,

    ExecExprUnionInit, ExecExprVariantInit, ExecExprStructInit, ExecExprStructMemberInit,
    ExecExprVariable, ExecExprComptConstant, ExecExprListLiteral, ExecExprAssignMove,
    ExecExprAssignEqual, ExecExprIs, ExecExprMemberAccess, ExecExprPointerMemberAccess,
    ExecExprBinary, ExecExprCast, ExecExprPreUnary, ExecExprPostUnary, ExecExprSubscript,
    ExecExprFnCall, ExecExprBorrow, ExecExprDeref, ExecExprClosure, ExecExprVariantDecomp,
    ExecExprMatch, ExecExprMatchBranch, ExecFnPtr, ExecVariantFieldInit"""

RETURN_TYPE = "size_t"

names = [t.strip() for t in types.replace("\n", "").split(",") if t.strip()]

template = """\
[&ctx](const {name}& t) -> {ret} {{
    // todo
    return {val};
}},"""

print("auto vs = Ovld{")
for n in names:
    print("    " + template.format(name=n, ret=RETURN_TYPE, val="{}"))
print("};")
