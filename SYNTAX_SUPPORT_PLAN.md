# Luv Language Syntax Support & Implementation Plan

This document outlines the comprehensive strategy for restoring full feature parity to the Luv compiler's CodeGen and Visitor systems.

## 1. Core Language Elements

### Expressions
- **Literals**: Full support for `Int`, `Float`, `String`, `Bool`, `Null`, and `Char`.
- **Binary/Unary Ops**: Implement float vs. integer dispatch for all standard operators.
- **Ternary Expr**: Use LLVM `phi` nodes for result selection.
- **Comparison Chains**: Transform `a < b < c` into `(a < b) && (b < c)` using short-circuiting logic.

### Control Flow
- **If/Else**: Standard branching with `phi` nodes for expressions.
- **Match**: Implement as a series of comparisons or a `switch` instruction for integer patterns.
- **Loops**: 
    - `while`: Standard loop with condition and body blocks.
    - `for-in`: Iterator-based loop (calls `.next()` or handles array indexing).
    - `for i in 0..10`: Range-based loop with inclusive/exclusive handling.
    - **Labeled Loops**: Support `![label] for ...` syntax for multi-level `break`/`continue`.
- **Break/Continue**: Manage a `loopStack` to jump to correct exit/continue blocks. Supports labels.

## 2. Declarations & Types

### Literals
- Full support for `Int`, `Float`, `String`, `Bool`, `Null` (including `nen`), and `Char`.

### Variables
- **Implicit Declaration**: Support `a = expr` in scripting mode by auto-generating `alloca` or global variables.
- **Tuple Destructuring**: `mut a, b = (1, 2)` - Extract elements from the tuple struct and store in individual variables.
- **Inference**: Use the `semanticType` provided by the analyzer to resolve LLVM types.

### Functions
- **Arrow vs. Block**: Both mapped to LLVM `Function`.
- **Default Parameters**: Generate wrapper functions or handle default values at the call site.
- **Method Binding**: Functions with `boundStruct` set will be mangled as `TypeName_MethodName` and receive `self` as the first argument.

## 3. Object-Oriented Programming (OOP)

### Structs
- Mapped to LLVM `StructType` (packed or unpacked based on attributes).
- Support field access via `getelementptr`.

### Classes
- **Memory Layout**: LLVM `StructType` containing all fields.
- **Inheritance**: Base class struct is embedded as the first field of the derived class struct.
- **Methods**: Virtual methods will be handled via a VTable (global pointer array) stored in the class instance.
- **Constructor (`init`)**: Standard function call that initializes fields and sets the VTable pointer.
- **`super` calls**: Explicitly call the base class version of a method (bypassing VTable).

### Interfaces
- Dynamic dispatch via Interface Tables (ITables). Each class implementing an interface will generate an ITable entry.

## 4. Data Structures

### Arrays
- **Implementation**: A struct `{ T* data, i64 len, i64 cap }`.
- **Repeat Syntax**: `[val; count]` - Allocate memory and use a loop to initialize.
- **Properties**: `.len` and `.cap` map to struct fields.
- **Slicing**: Returns a view struct `{ T* data, i64 len }`.

### Tuples
- Mapped to anonymous LLVM structs.
- Indexed access (`tup.0`) via `getelementptr`.

## 5. Advanced Features & Intrinsics

### Intrinsics (`@name`)
- **`@println`**: Map to `printf`. Handle multiple arguments by creating a format string.
- **`@max`, `@min`, `@abs`**: Use LLVM intrinsics (`llvm.smax`, etc.) or branching logic.
- **`@sizeof`, `@popcount`**: Map to LLVM `size_of` and `llvm.ctpop`.

### Attributes & Memory Hints
- **`@SoA`**: Transform Array of Structs to Struct of Arrays in memory layout.
- **`@stack`, `@heap`**: Direct allocation strategy (`alloca` vs `malloc`).
- **Inline Assembly**: Support via `llvm::InlineAsm`.

### Scripting Mode
- Detect top-level statements. Wrap them in a `script_init` function.
- Generate a `main` wrapper that calls `script_init` if a user-defined `main` doesn't already exist.

## 6. Infrastructure Fixes

### Visitor Improvements
- **`cast_any`**: Ensure no more `bad_any_cast` by always returning base pointers (`Node*`, `Expr*`, `Stmt*`) and using a robust helper for retrieval.
- **Memory Management**: Strictly use the `Arena` allocator for all AST nodes during visitation.

## 7. Verification Strategy
- Run all `tests/*.lv` files in sequence.
- Compare output against `.bin` or expected stdout.
- Ensure no regressions in FFI demos and scripting examples.
