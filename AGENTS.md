
## C++ Code Rules

1. Put each struct and enum in its own file.
2. Put shared data types in dedicated module (e.g. VoxelLandData).
3. Do not use anonymous namespaces. Use static class functions.
4. Keep .cpp files small. Split large ones into several .cpp files.
5. Forward declare instead of including headers where possible.
6. Do not use long parameter lists. Group parameters into structs.
7. Keep functions short. Extract logic into small helpers.
8. Avoid lambdas. Use named service functions.
9. Do not use global variables. Use static class members, accessed explicitly as Class::Variable.
10. Use `using` aliases for scalar types.
11. Always use braces for if, for, while, even for single statement.
12. Format code with clang-format.
13. Write one statement per line.
14. Use `const` for variables, parameters, and methods that do not change state.
15. Pass non-trivial types by const reference. Use value for scalars.
16. Use early returns to avoid deep nesting. Max 3 nesting levels.
17. Do not use magic numbers. Use named constants or enums.
18. Use `enum class` instead of plain enums.
19. Use `nullptr`. Never `NULL`  `0` for pointers.
20. Use `override`  `final` on virtual overrides.
21. Mark single-argument constructors `explicit`. Do not apply this rule for Unreal classes.
22. Do not use `auto`
23. Name functions as verbs, types as nouns, bools as `bIsX`  `bHasX`.
24. Follow one naming convention for whole project (UE: F, U, E, I prefixes).
25. One class per header/cpp pair. File name matches class name.
26. Use `#pragma once`.
27. Include order: own header, project headers, engine headers, std headers.
28. Do not put logic in headers except trivial inline and templates.
29. Validate pointers and inputs at function entry. Use `check` `ensure`or asserts for invariants.
30. Do not ignore return values. Mark with `[[nodiscard]]` where needed.
31. Do not use raw `new`/`delete`. Use smart pointers or engine-managed types.
32. Do not copy large containers. Reserve capacity before filling.
33. Do not add comments that restate code. Comment only reason (why).
34. Do not change code outside task scope. No unrelated refactors.
35. Do not rename or move existing public API without being asked.
36. Match style of surrounding code when it conflicts with rule.
37. Do not leave dead code, commented-out code, or unused includes.
38. Use static_cast instead of legacy C-cast style