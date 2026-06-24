### Bear syntax
- **Work In Progress:** This document is the primary grammar reference for use during the development of Bear.

#### Grammar Diagrams
- Bear is comprised of a few distinct syntax constructs: Expressions, Statements, Types, (and Clauses, which are subcomponents of certain statements)

#### Types
- Type: 
    - MutType: `mut`? Type `mut`? *** `mut`s bind leftward, unless nothing is to the left, where they will then bind rightward
    - | FnPtr: `*` `fn` `mut`? `(` Parameter(s) `)` `->` ReturnType
    - | BaseType:
        - Signed integers: `i8, i16, i32, i64`
        - | Unsigned integers: `u8, u16, u32, u64, usize`
        - | Floating points: `f32, f64`
        - | Primitive strings: `str`
        - | Boolean: `bool`
        - | Variant: (implicit: `variant`) Identifier
        - | Struct: (implicit: `struct`) Identifier
        - | Union: (implicit: `union`) Identifier
    - | Generic: \[BaseType `::`? `<` GenericParam(s) `>`] | BaseType `::` GenericParam
    - | Array: `[` CompileTimeExpression `]` Type 
    - | Slice: `[` `&` `mut`? `]` Type
    - | Reference: `&` Type
    - | Pointer: `*` Type
    - | Variadic: Type `...`
    - | TypeOf: `typeof` Expression 
    - | Decay: `decay` Type

#### Expressions
- Expression:
    - Identifier: \[contains `A`-`Z`, `a`-`z`, `0`-`9`, and `_`; cannot begin with `0`-`9`] 
    - | ScopableIdentier: Identifier \[`..` Identifier...]?
    - | IntegerLiteral: \[follows C conventions for decimal and hexidecimal literals, minus suffixes]
    - | FloatingPointLiteral: \[follows C conventions for floating point literals, minus suffixes]
    - | StringLiteral: \[follows C conventions for string literals]
    - | ListLiteral: `[` Expression `,` Expression `,` ... `]`
    - | BinaryExpression: `Expression` BinaryOp `Expression`
        - BinaryOp: a full precedence chart is in the works, for now refer to the maps in the [parser rules file](bearc/src/compiler/parser/rules.c) 
    - | Preunary: `--` | `++` 
    - | SizeOf: `sizeof` `(` Type `)`
    - | AlignOf: `alignof` `(` Type `)`
    - | Postunary: Expression `++` | `--`
    - | Subscript: Expression `[` Expression `]`
    - | Grouping: `(` Expression `)`
    - | FunctionCall: Expression `(` Argument(s) `)`
        - Arguments: Expression `,` Expression `,` ...
    - | Borrow: `&` `mut`? Expression
    - | Dereference: `*` Expression
    - | StructInitialization: Identifier `{` StructFieldInitialization(s) `}`
       - StructFieldInitialization: `.` Identifier `=` | `<-` Expression 
    - | Closure: `move`? `|` Parameter(s) `|` \[`->` Type]? \[`=>` Expression] | BlockExpression 
    - | VariantDecomposition: Identifier `(` Parameter(s) `)`
    - | Match: `match` `(` Expression `)` `{` MatchBranch(s) `}`
    - | TernaryIf: Expression `if` `compt`? Expression `else` Expression
    - | MatchBranch: VariantDecomposition | Expression `=>` Expression | BlockExpression
       - BlockExpression: `{` Statement(s) | YieldStatement(s) `}` 
            - YieldStatement: `yield` Expression
    - Reflection:
        - `@same_type` `(` Type `,` Type `)` 
        - `@type_to_str` `(` Type `)` , parentheses may be elided
        - `@static_assert` `(` Expression \[, Expression]? `)`, parentheses may be elided 
        - `@defined` `(` Identifier | MemberAccess(es)... `)`, parentheses may be elided
        - `@has_contract` `(` Identifier, Identifer `)`
        - `@inferable_as` `(` Type, Type `)`
        - `@err` | `@warn` | `@note` | `@help` `(` Expression \[`,` Expression]? `)`, parentheses may be elided

#### Statements
- *File*: TopLevelStatement(s)
- Visibility: \[`pub` | `hid`]?
- *TopLevelStatement*: 
    - | *ModuleDeclaration*: Visibility `mod` Identifier `;` | `{` TopLevelStatement(s) `}` 
    - | ImportStatement: `import` ExternalLanguage? -> PathLiteral `;` 
    - | *UseStatement*: `use` `mod`? Identifier `;`
    - | *FunctionDeclaration*: Visibility `fn` Identifier GenericParams? `(` Parameters `)` [`->` Type]?  `;` | `{` BodyStatement(s) `}` | `=>` Expression | `=>` `{` Expression `}`
    - | GenericParams: `<` Identifier -> HasClause? `,` ... `>` 
        - *HasClause*: `has` `(` Contract `+`? Contract(s) `)`
    - | *StructDeclaration*: Visibility `struct` Identifier -> GenericParams? -> HasClause? -> `{` TopLevelStatement(s) | MethodDeclaration(s) | DestructorDeclaration(s) `}` 
    - | *MethodDeclaration*: Visibility `mt` `mut`? Identifier -> GenericParams? -> `(` -> Parameter(s) -> `)` -> `;` | `{` BodyStatement(s) `}`
    - | *DestructorDeclaration*: `dt` `self` GenericParams? -> `(` -> Parameters -> `)` -> `;` | `{` BodyStatement(s) `}`
    - | *VariantDeclaration*: Visibility `variant` Identifier -> GenericParams? `{` VariantFieldDeclaration(s) `}`
    - | *VariantFieldDeclaration*: Identifier `(` Parameter(s) `)`
    - | *ContractDeclaration*: Visibility `contract` Identifier GenericParams? `{` MethodPrototypes `}`
    - | *UnionDeclaration*: Visibility `union` Identifier `{` MemberVariableDeclaration(s) `}`
    - | *ExternBlock*: `extern` ExternalLanguage `{` -> FunctionDeclaration(s) `}`
    - | *DefTypeDeclaration*: Visibility `deftype` Identifier `=` Type `;`
    - | *TopLevelVariableDeclaration*: Visibility \[Type | `var`] Identifier `;` | \[`=` | `<-`] Expression `;`

- *BodyStatement*: 
    - | VariableDeclaration: `static`? `compt`? \[`alignas` `(` IntegerLiteral `)`]? Type | `var` Identifier `;` | \[`=` | `<-`] Expression `;` 
    - | *UseStatement*: `use` Identifier `;`
    - | BlockStatement: `{` BodyStatement(s) `}`
    - | IfStatement: `if` `compt`? Expression `{` BodyStatement(s) `}` \[`else` `{` BodyStatement(s) `}`]?
    - | WhileStatement: `while` Expression `{` LoopBodyStatement(s) `}`
    - | ForLoopStatement: `for` `(` BodyStatement `;` Expression `;` Expression `)` `{` LoopBodyStatement(s) `}`
    - | ForInLoopStatement: `for` `var` Identifier `in` Expression `{` LoopBodyStatement(s) `}`
    - | ReturnStatement: `return` Expression? `;`
    - | EmptyStatement: `;`
    - | ExpressionStatement: Expression `;`

- *LoopBodyStatement*: 
    - BodyStatement
    - | BreakStatement: `break` `;`
    - | ContinueStatement: `continue` `;`


