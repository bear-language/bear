### todos

#### misc/priority 
- [ ] finish `def_to_str`
- [ ] wrap `context.emplace_diagnostic` / `context.emplace_diagnostic_with_message` in `ComptExprSolver`, `RunTimeExprSolver`, and `TypeResolver` so that diagnostic emissions can be toggled with `.disable_diagnostics()` / `.enable_diagnostics()`
- [ ] make the ast query-able (walk and search for best node at a given span). this will be a bit less in complex than the pretty printer which also walks every node type

#### function body resolution / runtime eval:
- [ ] see `TODO`s
    - [ ] move checker structures / scopes in runtime functions should be composed of a `ScopeId` and `MoveMapId`
        - [x] `ScopeId`: same as current impl, for symbol look-up
        - [ ] `MoveMapId`: same idea as a scope, but:
            - [x] tracks DefId -> ExecId and DefId -> ExecIdSliceId tracking where defs were moved (for good diagnostics)
            - [ ] after child(ren) are made, iterate through common moves (across branches if applicable) and mark as moved in current, pointing to moves
    - [ ] impl `RunTimeExprSolver`
        - [ ] maximally desugar things to get rid of unneeded Exec types 
            - [ ] preunary, postunary
            - [ ] improve how blocks work (desugar loops/if branches/matches)
        - [ ] use deduction guides for functions/(variants/structs?)
        - [ ] make sure assignment type checking is properly rigid around mutable types (especially references).
            - [ ] this is already impl'd: see `Context::assignable_from_type_to_type`, but a version of this basic on inferable types (with `var` inference) is needed for variable decls
        - [ ] break up assign inits into the def (variable loc) and the exec (initializer), use a flag to track single-init of non-mut typed variables
    
- [ ] note: (impl. detail) the way mutable references are strucutured is that HIR stores all references types as mut/immut on the reference layer and then the next inner value type is always stored as immut since the mutability only binds to the reference logically. So, be sure to take this into account. 
    - [ ] ensure these work:
        - [ ] T       -> mut T
        - [ ] mut T   -> T
        - [ ] &T      -> &mut T         (fail)
        - [ ] &mut T  -> &T 
        - [ ] * T       -> *mut T       (fail)
        - [ ] * mut T   -> *T        
        - [ ] \[&]T      -> \[&] T mut  (fail)
        - [ ] \[&] T mut -> \[&]T       
- [ ] "borrow checker":
    - [ ] allow mutiple immutable and mutable borrows
    - [ ] no lifetimes
    - [ ] strictly ban returning a reference to a local variable directly out of a function
- [ ] remember: run-time values that are immutable references and have compile-time initializers can just reference static variables that store that compile-time value

- [ ] tighten up mention/mutation tracking for better `unused variable: foo` diagnostics (and top level decls when not a lib build)
- [ ] handle existence of main / lack of existence (have a `--lib`/`-l` flag to compile as a lib) 

- [ ] consider queuing structure declarations (as is done for functions) for better LLVM lowering, or just do it lazily as needed

- [ ] just find main thru top-level scope; only require it in non-lib builds 

- [ ] finalize `extern {}` and `extern C {}` semantics for cross-TU and FFI compilation respectively
    - [ ] hand out errors for C-incompatible functions when under a C abi extern, like no references, generics, etc.
    - [ ] scrap the `import C "foo.h";` construct

#### top-level resol / compt improvements:
- [ ] deduction guides
    - [ ] struct deduction guide use (building them is impl'd but untested)
    - [ ] variant deduction guides (building and use)

- [ ] variadic functions? 
    - [ ] only allow in functions as last param like this: `fn foo(i32 a, i32 b, ...) {}` or pass through some kind of anonymous struct, like Zig
    - [ ] get variadic params/args working at compt w/ callable functions

- [ ] reflection improvements
        - [ ] implement use `foo.@id(str_val)` or `foo.@id(str_val)()` to compile-time reflect on members (relatively easy but tedious on some special-casing inside the compile-time solver)


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

#### chores
- [ ] fix highlighting of "\\\\" in bear.nvim
- [ ] fix undefined arithmetic behavior in `ComptExprSolver` inherited from C++ (signed overflow, etc.)

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
- [ ] allow arbitrarily ordered struct members inits, will require mini symbol hashmaps
- [ ] add Exec Stringifier for run-time execs (currently only compt-able values are implemented)
- [ ] add a Def Stringifier (tedious)  
- [ ] arbitrary source code reconstruction from hir::Context

#### diagnostics
- [ ] using a scope iterator, use Levenshtein distance to make a `help: did you mean:` `...`

#### debugging 
- [x] make a scope iterator
- [ ] debug logger to display context and scope contents

#### lsp-friendly features
- [lsp compatibility plan here](docs/lsp-compat.md)
