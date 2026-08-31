<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://github.com/bear-language/bear-vscode/blob/main/images/bear-logo-text-dark.png?raw=true">
    <source media="(prefers-color-scheme: light)" srcset="https://github.com/bear-language/bear-vscode/blob/main/images/bear-logo-text-light.png?raw=true">
    <img alt="The Bear Programming Language"
         src="https://raw.githubusercontent.com/zachMahan64/bear-vscode/refs/heads/main/images/bear-logo.png"
         width="50%">
  </picture>

</div>

-------------
- A modern, powerful, C-like language with simple syntax.
- Bear is statically-typed and compiled using LLVM as the backend.
- Codegen is still a work-in-progress.

## Documentation
- [Grammar](docs/syntax.md)
- [References](docs/references.md)

## Set up
#### Build
- See the [build docs](docs/build.md) for more detailed instructions, including how to build a custom LLVM distribution (if wanted) and building on Windows.
- You will need git, gcc or clang, and CMake installed.
- The build also works using MinGW on Windows.
```bash
# installing LLVM development packages:

# Ubuntu / Debian
sudo apt install llvm-dev 

# Fedora / RHEL 
sudo dnf install llvm-devel

# Arch 
sudo pacman -S llvm

# macOS
brew install llvm # follow instructions after install to put this on your path

# manual cmake build
cmake -B build -S .
cmake --build build

# convient scripts:
./scripts/clean-all.sh [Release|Debug] # builds bearc and libbearc
./scripts/clean.sh     [Release|Debug] # builds bearc
./scripts/build-tests.sh               # build and run tests
```
- Also, you will probably want to put `/path/to/bearc/build/bearc/` on your path.
#### Run
```
bearc --help          # to see CLI usage
```
#### Tooling 
- [A Neovim plugin is availible](https://github.com/bear-language/bear.nvim), which easily can be used in Vim as well.
- [A VSCode extension is availible](https://github.com/bear-language/bear-vscode), which includes syntax highlighting. Right now, local install is required, directions are in the linked repo.
- More to come in the future

#### Previews
- Bear is comfortably turing complete at compile-time, with many composable reflection and type-inference features
``` bear
// tests/hir/59.br

// let's show off some reflection, compile-time execution, 
// and static duck-typing

mod demo; 

compt fn square_val(var a) => a * a

compt fn is_integer(var i) => {
    @same_type(typeof i, i64) || @same_type(typeof i, u64) ||
    @same_type(typeof i, i32) || @same_type(typeof i, u32) ||
    @same_type(typeof i, i16) || @same_type(typeof i, u16) ||
    @same_type(typeof i, i8) || @same_type(typeof i, u8) 
}

struct IntWrapper {

    deftype ValType = i32;

    hid ValType stored_val;

    fn new(ValType val) => Self{.stored_val = val}

    mt copy() => Self{.stored_val = self.stored_val}

    mt val() -> ValType => self.stored_val

    compt mt add_with(var a) =>
        Self..new(self.val() + a) if @same_type(decay typeof a, Self)
            else Self..new(self.val() + a as typeof stored_val)

    compt mt square() => Self..new(square_val(self.val()))

    // this can take in any list thru compile-time reflection
    compt mt val_in_list(var list) -> bool => { 
        self.val_in_list_helper(list, list.len - 1)
    }
    
    // recursive helper
    hid compt mt val_in_list_helper(var list, var curr) -> bool => { 
        false if curr == 0 
            else (list[curr] == self.val()) || self.val_in_list_helper(list, curr - 1)
    }

}

//                  tests
// -------------------------------------------------
// these are all evaluatable at compile-time
// note: @static_assert is a compile-time expression 
// that returns a bool, so that's why we're writing
// our tests like this:

deftype Int = IntWrapper; // we can define a type alias like this

var _0 = @static_assert Int..new(12).val() == 12; 

var _1 = @static_assert @same_type(Int..ValType, i32); // ensure our type alias is working as intended

compt Int my_int = Int..new(42); // we can check equalities of structs at compile-time too

var _2 = @static_assert my_int.add_with(1000) == Int..new(1000 + 42);

use Int..ValType; // use Int aka IntWrapper's ValType in current scope

var _3 = @static_assert @same_type(ValType, i32);

var _4 = @static_assert @same_type(decay &mut ValType, i32); // decay discards qualifiers, so this should hold!

var _5 = @static_assert Int..new(12).square().square().val() == 12 * 12 * 12 * 12;

compt Int my_int1 = Int..new(10);

var _6 = @static_assert my_int1.val_in_list([1, 2, 3, 4, 7, 10, 8, 9, 1000]);

var _7 = @static_assert Int..new(12).copy() == Int..new(12); // copy called as a method

var _8 = @static_assert Int..copy(Int..new(12)) == Int{.stored_val = 12}; // methods can also take Self as a 
                                                                          // first regular argument

// some mistakes to demo diagnostics:

var _9 = @static_assert my_int1.val_in_list([1, 2, 3, 4, 7, 11, 8, 9, 1000]); // oops

compt ValType v = my_int1.val(0) == Int..new(); // oops, too many and too few arguments

compt i32 v1 = Int..new(42); // oops, not convertible here
```
- Diagnostics output from the above test source code (less pretty here than normal tty output since there's no bold/color here)
```
error: static assertion failed 
 --> /Users/zachmahan/dev/bear-lang/tests/hir/59.br:86:25 
      | 
  86  | var _9 = @static_assert my_int1.val_in_list([1, 2, 3, 4, 7, 11, 8, 9, 1000]); // oops
      |                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ condition is false
      | 

error: function `val` expected 0 arguments but got 1 
 --> /Users/zachmahan/dev/bear-lang/tests/hir/59.br:88:31 
      | 
  88  | compt ValType v = my_int1.val(0) == Int..new(); // oops, too many and too few arguments
      |                               ^ 
note: `val` declared here 
      | 
  27  |     mt val() -> ValType => self.stored_val
      |        ^^^ 

error: function `new` expected 1 arguments but got 0 
 --> /Users/zachmahan/dev/bear-lang/tests/hir/59.br:88:46 
      | 
  88  | compt ValType v = my_int1.val(0) == Int..new(); // oops, too many and too few arguments
      |                                              ^ 
note: `new` declared here 
      | 
  23  |     fn new(ValType val) => Self{.stored_val = val}
      |        ^^^ 

error: cannot convert value of type `IntWrapper` to `i32` 
 --> /Users/zachmahan/dev/bear-lang/tests/hir/59.br:90:16 
      | 
  90  | compt i32 v1 = Int..new(42); // oops, not convertible here
      |                ^^^^^^^^^^^^ 

4 errors generated.
2 notes generated.
compilation terminated: '/Users/zachmahan/dev/bear-lang/tests/hir/59.br'
```
