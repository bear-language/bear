### todos

#### priority 
- [ ] evaluate non-taken compt paths for basic validity 

#### function body resolution / runtime eval:
- [ ] see `TODO`s
    - [ ] move checker structures / scopes in runtime functions should be composed of a `ScopeId` and `MoveMapId`
        - [x] `ScopeId`: same as current impl, for symbol look-up
        - [ ] `MoveMapId`: same idea as a scope, but:
            - [x] tracks DefId -> ExecId and DefId -> ExecIdSliceId tracking where defs were moved (for good diagnostics)
            - [x] after child(ren) are made, iterate through common moves (across branches if applicable) and mark as moved in current, pointing to moves
            - [ ] actually record moves (needs a copy vs. move rule per type first)
    - [ ] finish impl `RunTimeSolver`
        - [ ] control-flow stmts, mirroring `handle_match` (per-branch `InProgressBlock`, child move maps + `write_back_moves`)
            - [ ] `yield` in nested positions inside match branch blocks (only top-level yields are handled)
            - [ ] `is` w/ variant decomposition, bindings scoped to the `if`'s then-branch
            - [ ] `for in` (blocked on an iteration protocol/contract + `std::Range`)
        - [ ] closures (rust-like capture semantics)
        - [ ] warn on unused expression statements (fine for assignments/mutations/discardable or void fns)
        - [ ] `ExecMatchBranch` patterns keep their solved type (e.g. `i32` literal patterns on a `u8` scrutinee); either convert them in HIR or cast when lowering the `switch`
        - [ ] use deduction guides for 
            -  [x] functions
            -  [ ] variants/structs?
    - [ ] fix mut semantics on struct members 
        - [ ] warn on `mut` declared members
        - [x] make it so the struct instance's mutability entirely determines the member mutability
    - [ ] drop/destructor flags, tracked by block/lexical scope (always emit, LLVM folds the unconditional ones)
        - blocked on recording moves
    - [ ] static variable guard variables (thread safe, ideally or eventually)
        - only needed if LHS is not knowable at compt (in which case it can be an LLVM global w/ a constant initializer)
    - [ ] `.start` / `.end` access for builtin ranges `...` & `...=`, figure out how to handle inclusivity here
    - [ ] impl proper runtime Exec hashing so parallel match/if-else branches can be hashed and compared for duplicativity

- [ ] tighten up mention/mutation tracking 
    - improve/fix `unused variable: foo` diagnostics (for non top-levels)
    - add `foo is never mutated, consider not declaring as mutable`

- [ ] handle debug-build dependent panicking on array/slice out-of-bounds

- [ ] handle existence of main / lack of existence (have a `--lib`/`-l` flag to compile as a lib) 

- [ ] just find main thru top-level scope; only require it in non-lib builds 

- [ ] finalize `extern {}` and `extern C {}` semantics for cross-TU and FFI compilation respectively
    - [ ] hand out errors for C-incompatible functions when under a C abi extern, like no references, generics, etc.
    - [ ] scrap the `import C "foo.h";` construct

#### top-level resol / compt improvements:
- [ ] deduction guides
    - [ ] struct deduction guide use (building them is impl'd but untested)
    - [ ] variant deduction guides (building and use)

- [ ] generic deftypes e.g. `deftype MutDecay<T> = mut decay T;` 

- [ ] reflection improvements
        - [ ] implement use `foo.@id(str_val)` or `foo.@id(str_val)()` to compile-time reflect on members (relatively easy but tedious on some special-casing inside the compile-time solver)

- [ ] reduce redundant diagnostics (particularly revolving around compt eval)
    - [ ] wrap `context.emplace_diagnostic` / `context.emplace_diagnostic_with_message_value` in ComptExprSolver
    - [ ] make it so non-`compt` functions evaluated at compt have their bodies evaluated in a disabled diagnostic mode because their bodies get lowered later as runtime funcs, should fix (most) broken tests


#### queries
- [ ] better Span queries: index Span -> Exec and Span -> Type when `ctx.register_spans` is set
    - [ ] registration lives entirely in the emplacers (`emplace_exec`, `emplace_compt_exec`, `register_exec`, `emplace_type`), Context owns all the state
        - skip generated spans and skip while diagnostics are disabled (speculative solves), so do the diagnostic toggle bullet first
    - [ ] one entry per span, keyed by `{FileId, start, len}` -> `{id, compt_dirty, generic_dirty}`
        - memory is bounded by distinct source spans, no matter how many generic instances / compt evals
    - [ ] on emplace:
    ```
        if span not in index:
            insert {id}
        else if new is compt:
            mark compt_dirty   # compt re-eval (compt fib, loops, etc.)
        else:
            mark generic_dirty # runtime bodies are solved once per def, so this is an instantiation
    ```
        - no type comparisons, a second emplacement is enough to mark dirty
    - [ ] on query: lazily flatten into a sorted per-file vec and reuse the `scope_for_span` search (innermost containing span)
        - dirty entries -> `value depends on compt/generic parameters` instead of a concrete type

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

- [ ] bear-tree-sitter
    - parser implemented with tree-sitter for complex syntax highlighing 

- [ ] VSCode
    - [x] highlighing, lsp hooks 
    - [ ] basic cave/bearc integration (run button)
    - [ ] eventually include builds of bearls / prompt download

- [ ] Verify/implement debugger compatibility 

#### chores
- [ ] fix highlighting of "\\\\" in bear.nvim

tools
----- 

lexer & parser 
--------------
- [ ] improve numerical literal handling 
    - [ ] fix implicit `NaN` / `inf` shenanigans
    - [ ] probably just replace strtoll and friends with hand-rolled impls 
    - [ ] add binary integer literals `0b1010101` (keeping dec, hex, and float that we currently already have)
    - [ ] set a tkn to TOK_OVERSIZED_INT_ERR if there's no decimal and it's greater than u64 max or less than i64 min
    - [ ] suffixes?
- [ ] issue better diagnostics (in parse_expr) for unterminated string literals
    - [ ] set a special ERR_UNTERM_STR as the token_type when lexing and then report during parsing

hir & later 
----------- 

#### diagnostics
- [ ] using a scope iterator, use Levenshtein distance to make a `help: did you mean:` `...`

#### debugging 
- [x] make a scope iterator
- [ ] debug logger to display context and scope contents
