lsp compatibility plan 
---------------------- 
- example request:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "textDocument/definition",
  "params": {
    "textDocument": {
      "uri": "file:///home/user/project/src/main.br"
    },
    "position": {
      "line": 14,
      "character": 8
    }
  }
}
```
- note: line and character are 0-indexed

inside `hir::Context`
--------------------

- URI for file paths is completely handled by FileIds (file and ast interning system)

incoming `{line, character}` request handling
---------------------------------------------
- [x] spans -> scope is naturally sorted:
    - [x] build a flat sorted SmallVector<(Span, ScopeId)> during lowering, ordered by span start
    - [x] binary search to find candidates containing (line, col)
    - [x] amongst candidates, pick the one with the smallest len (innermost)

- [ ] handle turning request into a parsable symbol
    - given a line/character re-tokenization will be necssary (walk forward/backward to try to find a valid symbol)
    - needs to consider encoding (utf-8/utf-16), this will require rewalking the entire line to map the encoded character idx into compiler ASCII character idx
    - the current lexer can probably be entirely reused for the actual tokenization if refactored, but this certainly not be necssary if we're just looking for symbols (regex or manual regex equiv is fine for scoped symbols)
    - arbitrary re-lexing of a given line would allow arbitrary reparsing for small snippets, thus allowing massively faster Context queries (maybe do later)

- [ ] handle request -> node
    - make sure to consider encoding as well (see above bullet)
    - search thru ast to find node at (line, character):
        - binary search thru linear nodes (slice_stmts, slice_of_exprs, etc.)
        - recursively search thru structured nodes (var_decl, fn_decl, if_stmt, etc.)

outgoing `{line, character}` diagnostics handling
------------------------------------------------- 
- already cleanly handled with diagnostics that track source Spans that contain `{FileId, start, len line, col}` which can easily be converted to `{uri, line, col, len}`
    - any necssary info can be attached to diagnostics at those locations
    - code actions will be limited to `replace with` or `remove` to begin with, and it needs to work with:
        - `DiagnosticSymbolAfterMessage` -> just rip the `SymbolId`'s string value
        - `DiagnosticTypeAfterMessage` -> just get the type_to_str_as_mentioned 
