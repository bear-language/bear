### todos

#### priority
- [ ] fix array to slice conversions for runtime solving
- [ ] make compt fn ptr compatibility less strict: generic fn calls now check args against the instantiated param types, so unannotated closures (`|i32 x| => x % 2 == 0` is `*fn(i32)`, no return type) and `var`-param compt fns (`*fn(var)->bool`) can't be passed to `*fn(T)->bool` params anymore (a65/a75/projects/04 were updated to annotate/genericize around this)
    - [ ] infer closure return types
    - [ ] decide whether compt fn ptrs w/ `var` params / missing return types should be compatible with concrete fn ptr types (do it for both generic and non-generic calls)

#### function body resolution / runtime eval:
- [ ] see `TODO`s
    - [ ] move checker structures / scopes in runtime functions should be composed of a `ScopeId` and `MoveMapId`
        - [x] `ScopeId`: same as current impl, for symbol look-up
        - [ ] `MoveMapId`: same idea as a scope, but:
            - [x] tracks DefId -> ExecId and DefId -> ExecIdSliceId tracking where defs were moved (for good diagnostics)
            - [x] after child(ren) are made, iterate through the union of moves (across branches if applicable) and mark as moved in current, pointing to moves
            - [x] skip diverging children in `write_back_moves`, `break`/`continue`/`yield` snapshot their path's moves to where they land
            - [ ] actually record moves
                - [ ] allow re-initialization: `x = foo()` where `x` is previously moved clears the `MoveMap` entry (consider a tombstone value)
                    - [x] lhs of plain `=` isn't checked for use-after-move
                    - [ ] tombstone has to stop lookup into parent maps
                    - [ ] merges: only live if every path reaching the join reinits (incl. implicit else / zero-iteration loops)
                    - [ ] snapshots: inner map wins over outer instead of a union
                - [ ] `x = foo()` where `x` is unmoved must destruct old value first (drop glue call before the store)
                - [x] ban partial moves of structs / variants / unions / arrays
                - [x] `match`/`is` on a `Destruct` scrutinee moves it: yes except for refs
                - [ ] handle `f(make_thing())` destructing temporaries after the end of the statement if they're not moved out (currently won't work easily w/ `MoveMap` which has `DefId` keys, maybe create temp `DefId`s?)
                - [x] ban `foo.destruct()` but provide `drop(foo)` which just moves foo out

- [ ] finish runtime solving 
    - `RuntimeSolver` 
        - [ ] closures (rust-like capture semantics): delayable until after backend
        - [ ] `ExecMatchBranch` patterns keep their solved type (e.g. `i32` literal patterns on a `u8` scrutinee): convert them in HIR or cast when lowering the `switch`
    - [ ] replace `dt self()` with a builtin `Destruct` contract w/ `mt mut destruct()`
        - [x] remove `TOK_DT`, `dt` from `token_is_mt_or_dt` & co., special case in `context.cpp`, `DestructorDeclaration` in syntax.md, update parser tests 30 & 44
        - [x] `Destruct` is intrinsic
        - [x] opt-in only thru `has(Destruct)`
        - [x] ban explicit `x.destruct()` calls 
        - [x] ban moving out of fields of a `Destruct` type (moving fields out will prob have to start out banned anyway)
        - [x] `Destruct` types are never implicitly copyable (feeds the copy vs. move rule)
        - [ ] compiler-inserted calls ignore mutability
        - [ ] members are auto-destructed after the user's `destruct()`(reverse decl order)
        - [ ] variants: destruct the active field only; unions: ban `Destruct` members or make it manual
        - [ ] perhaps warn on `T has Destruct` generic bounds (meaningless since everything gets destructed)
    - [ ] destruction in HIR via drop glue (plain `ExecFnCall`s, so compt eval could run destructors too)
        - [ ] synthesize a drop glue fn per type that needs destruction, cached per type (generic instantiations get their own)
            - [ ] structs: user's `destruct()` (if `has Destruct`), then glue for fields in reverse decl order
            - [ ] variants: `match` on the active field, glue for its payload
            - [ ] arrays: loop over elements
            - [ ] mark glue defs (e.g. `is_drop_glue` or special chars in name) so analyses can tell drops apart from user calls
        - [ ] insert glue calls (built directly, not thru `solve_fn_call_impl`, borrows ignore mutability)
            - [ ] block end: block's defs in reverse decl order
            - [ ] `return`/`break`/`continue`/`yield`: live defs of every scope being exited (needs a scope stack in `RuntimeSolver`)
            - [ ] reassignment of a live def
            - [ ] unmoved temporaries at the end of the statement
        - [ ] drop flags: defs w/ any move in the fn's move map get a synthesized `bool` def
            - init sets it, `ExecMove` clears it, drops become `if flag { glue(&mut x) }`
            - never-moved defs drop unconditionally, LLVM can fold as needed
    - [ ] `.start` / `.end` access for builtin ranges `...` & `...=`, figure out how to handle inclusivity here
    - [ ] impl proper runtime Exec hashing so parallel match/if-else branches can be hashed and compared for duplicativity

- [ ] better main 
    - [ ] detect/report wrong parameters for main
    - [ ] resolve empty args vs. `[&]str args` or something similar
    - [ ] juicy? or maybe just a bit juicy

- [ ] function purity recording, see TODO in `may_have_side_effects`
    - [ ] automatic detection 
    - [ ] enforce user defined `pure` functions (new keyword)

- [ ] handle debug-build dependent panicking on array/slice out-of-boundss 

- [ ] finalize `extern {}` and `extern C {}` semantics for cross-TU and FFI compilation respectively
    - [ ] hand out errors for C-incompatible functions when under a C abi extern basically w/ generics, etc.
    - [ ] scrap the `import C "foo.h";` construct or have it automatically translate-c (halfway between old/new Zig)

- [ ] allow contract satisfaction `CanonicalTypeId`-wise instead of `DefId`-wise (to allow non-structs to have contracts)
    - [ ] will require new `satisfy Foo for Bar {}` syntax
    - [ ] `DefId` -> `DefId` contract maps will need to be use `CanonicalTypeId` keys instead 
    - [ ] will complicate method look up (look up type's contract scope, then on miss check current scope)

- [ ] make a way to reserve the root FileId so intrinsics can be registered first (better diagnostic order for redefinitions)

#### top-level resol / compt improvements / features:
- [ ] generic deftypes e.g. `deftype MutDecay<T> = mut decay T;` 

- [ ] add `@in_compt` builtin which is true during compt-eval, else false

- [ ] cache results of `@members_of` / `@statics_of`

- [ ] reflection improvements
        - [ ] implement use `foo.@id(str_val)` or `foo.@id(str_val)()` to compile-time reflect on members (relatively easy but tedious on some special-casing inside the compile-time solver)

- [ ] `requires` clauses in contracts (optional single-bool compt predicates per contract)

- [ ] `variant` stuff
    - [ ] niches, at least for `Opt`
    - [ ] make `Opt` (and `Err`) intrinsic
    - [ ] impl `try foo` syntax sugar for `Opt` and `Err`

- [ ] `@volatile_load(src)` / `@volatile_store(dst, src)`

- [ ] intrinsic atomics (not builtin types but special-cased structs)

#### language server / ContextDatabase stuff
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
