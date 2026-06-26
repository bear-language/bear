### todos

main quest
----------

#### hir phase 2.a:
- all while in the process of *resolivng* top-level declarations:

- [ ] use canonical generic args canonicalization to memoize compt function args -> values
    - have FunctionDefs store something analogous to `OptId<CanonicalGenericArgsIdMapId>` or which will be used as its results cache

- [ ] contract diagnostics for per-param disagreements

- [ ] `compt` improvements:
    - [ ] compt closures (pure-expr only)
    - [ ] allow capturing compt variables 
    - [ ] compt slices `[&]` so we can work with any dimensional lists at compt, also tighten up list handling generally
    - [ ] compt ranges `1...4` and make then work with compt matching, better type-checking/reporting for scalar matched values

- [ ] tighten up variadic type handling, consider making it part of the function decl node, not the type

- [ ] struct member reflection 
     - [ ] add `@members_of` -> list of strs, which can be iterated over
     - [ ] add `foo.@member(str_val)` to compile-time reflect on members 


- [ ] tighten up abi related stuff with hir::LayoutRules or something similar
    - [ ] properly impl `@sizeof` and `@alignof`, make them generalized as a primitive query in `hir::Context` and then plug into `hir::ComptExprSolver` and later the runtime expr solver

- [ ] some basic [lsp-compat](/docs/lsp-compat.md), mostly thru building span -> scope search trees (only build these when a flag is enabled, tho; this will need to be added)

- [ ] generic argument deduction guides for fn calls, variant inits, and struct inits
    - [ ] use positional type inference for each decayed argument

#### hir phase 2.b (function body resolution):
- [ ] make sure assignment type checking is properly rigid especially around mutable references.
    - the way mutable references are strucutured is that HIR stores all references types as mut/immut on the reference layer and then the next inner value type is always stored as immut since the mutability only binds to the reference logically. So, be sure to take this into account. 
- [ ] make a system to etch ExecId into a structured linear form within blocks to be naturally connected in a CFG 
    - [ ] this should be directly conducive to 3AC for all `hir::Exec`s
- [ ] move checker
- [ ] a borrow checker should be trivial, however:
    - [ ] allow mutiple immutable and mutable borrows
    - [ ] no lifetimes
    - [ ] strictly ban returning a reference to a local variable
- [ ] remember: run-time values that are immutable references and have compile-time initializers can just reference static variables that store that compile-time value
- [ ] LLVM lowering prep:
    - [ ] tighten up mention/mutation tracking for better `unused variable: foo` diagnostics (and top level decls when not a lib build)
    - [ ] either queue struct and function declarations (cheaper linear lowering to LLVM) or use a scope iterator 
    - [ ] just find main thru top-level scope; only require it in non-lib builds 
    - [ ] add a flag for exec/lib build to track diagnostics slightly different (described above)
    - [ ] finalize `extern {}` and `extern C {}` semantics for cross-TU and FFI compilation respectively
        - [ ] hand out errors for C-incompatible functions when under a C abi extern, like no references, generics, etc.

#### optimizations
- [ ] add a `-i` flag to specify specific files for parallel builds
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
    - [x] At minimum, a regex-based syntax highlighter
    - [ ] basic cave/bearc integration (run button)

- [ ] Improve debugger compatibility 

side quests
-----------

#### chores

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
- [ ] verify correctness of escape sequences in char and string literals

hir & later 
----------- 
- [ ] allow arbitrarily ordered struct members inits, will require mini symbol hashmaps
- [ ] add an Exec Stringifier (tedious)
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
