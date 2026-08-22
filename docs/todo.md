### todos

main quest
----------

#### hir phase 2.a:
- all while in the process of *resolving* top-level declarations:

- [ ] generic argument deduction guides for fn calls, variant inits, and struct inits
    - [ ] struct deduction guide use (building them is impl'd but untested)
    - [ ] variant deduction guides (building and use)

- [ ] variadic functions? 
    - [ ] only allow in functions as last param like this: `fn foo(i32 a, i32 b, ...) {}` or pass through some kind of anonymous struct, like Zig
    - [ ] get variadic params/args working at compt w/ callable functions

- [ ] reflection improvements
        - [ ] implement use `foo.@id(str_val)` or `foo.@id(str_val)()` to compile-time reflect on members (relatively easy but tedious on some special-casing inside the compile-time solver)


#### hir phase 2.b (function body resolution):
- [ ] impl `RunTimeExprSolver`
    - [ ] use deduction guides for functions/(variants/structs?)
- [ ] build up runtime statements into structured blocks
- [ ] make sure assignment type checking is properly rigid around mutable types (especially references).
    - note: (impl. detail) the way mutable references are strucutured is that HIR stores all references types as mut/immut on the reference layer and then the next inner value type is always stored as immut since the mutability only binds to the reference logically. So, be sure to take this into account. 
    - [ ] when calling a function, disregard the outer-most mutability of the type (unless it's a reference)
    - [ ] assignment requires recursively that thru references or within pointers/slices: immut <- mut but not mut <- immut 
    - [ ] ensure these work:
        - [ ] T       -> mut T
        - [ ] mut T   -> T
        - [ ] &T      -> &mut T         (fail)
        - [ ] &mut T  -> &T 
        - [ ] * T       -> *mut T       (fail)
        - [ ] * mut T   -> *T        
        - [ ] \[&]T      -> \[&] T mut  (fail)
        - [ ] \[&] T mut -> \[&]T       
```
how it works (save this for later type system docs)
 Mutability is a capability attached to the layer through which the value is accessed. When comparing nested types, mutability may be weakened (mut -> immut) but never strengthened (immut -> mut). Top-level value mutability is erased for by-value expressions and at function-call boundaries, whereas reference-layer mutability is preserved.

```
- [ ] make a system to etch ExecId into a structured linear form within blocks to be naturally connected in a CFG 
    - [ ] this should be directly conducive to 3AC for all `hir::Exec`s
- [ ] move checker
    - [ ] Scopes in runtime functions should be composed of a `ScopeId` and `MoveMapId`
        - [ ] `ScopeId`: same as current impl, for symbol look-up
        - [ ] `MoveMapId`: same idea as a scope, but:
            - [ ] tracks DefId -> ExecId tracking where defs were moved (for good diagnostics)
            - [ ] parent MoveMaps should track their children so that each child can confirm that its siblings also move the same definitions 
- [ ] "borrow checker":
    - [ ] allow mutiple immutable and mutable borrows
    - [ ] no lifetimes
    - [ ] strictly ban returning a reference to a local variable directly out of a function
- [ ] remember: run-time values that are immutable references and have compile-time initializers can just reference static variables that store that compile-time value
- [ ] LLVM lowering prep:
    - [ ] tighten up mention/mutation tracking for better `unused variable: foo` diagnostics (and top level decls when not a lib build)
    - [ ] either queue structure declarations (as is done for functions) for better LLVM lowering 
    - [ ] just find main thru top-level scope; only require it in non-lib builds 
    - [ ] add a flag for exec/lib build to track diagnostics slightly different (described above)
    - [ ] finalize `extern {}` and `extern C {}` semantics for cross-TU and FFI compilation respectively
        - [ ] hand out errors for C-incompatible functions when under a C abi extern, like no references, generics, etc.

#### optimizations
- [ ] `hir::Context` ctor that takes a stale context and a list of updated files, and then based on the stale context's files (necessary for above flag and also AST reuse for the future LSP):
```
    for every file in stale context:
        if red: # stale and already marked as such 
            continue 
        if not red && stale: 
            color it and its dependents red
            continue 
        # since it's not stale:
        move file and it's data (buffer, token, ast) from stale context into new context

    delete the stale context and replace it with the new context 
    # note make sure dtor of files inside context properly handle being moved (no double frees, etc.)
```
#### long term (compiler)
- [ ] LLVM IR
- [ ] automatic extern functions and struct definition exports for static libraries
- [ ] C header parsing -> extern functions

#### tools 
- [ ] cave package-manager
    - run, init, build, check, and other nice-to-haves

- [ ] language server 
    - using the LSP (for VSCode/IDE/text-editor portability)
    - implemented in C++ (or Rust) using libbearc

- [ ] bear-tree-sitter
    - parser implemented with tree-sitter for complex syntax highlighing 

- [ ] VSCode
    - [ ] update highlighting to have parity with `bear.nvim`
    - [ ] basic cave/bearc integration (run button)

- [ ] Verify/implement debugger compatibility 

side quests
-----------

#### chores
- [ ] fix highlighting of "\\\\" in bear.nvim

tools
----- 
- [ ] highlighing for isize

lexer & parser 
--------------
- [ ] improve numerical literal handling 
    - [ ] probably just replace strtoll and friends with hand-rolled impls 
    - [ ] add binary integer literals `0b1010101` (keeping dec, hex, and float that we currently already have)
    - [ ] set a tkn to TOK_OVERSIZED_INT_ERR if there's no decimal and it's greater than u64 max or less than i64 min
    - [ ] suffixes?
    

hir & later 
----------- 
- [ ] allow arbitrarily ordered struct members inits, will require mini symbol hashmaps
- [ ] add Exec Stringifier for run-time execs (currently only compt-able values are implemented)
- [ ] add a Def Stringifier (tedious)  
- [ ] arbitrary source code reconstruction from hir::Context

#### diagnostics
- [ ] fully allow cyclical imports, add a flag to enable warnings instead of always warning for it
- [ ] using a scope iterator, use Levenshtein distance to make a `help: did you mean:` `...`

#### debugging 
- [x] make a scope iterator
- [ ] debug logger to display context and scope contents

#### lsp-friendly features
- [lsp compatibility plan here](docs/lsp-compat.md)
