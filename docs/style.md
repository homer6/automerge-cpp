# automerge-cpp Style Guide

This document defines the coding style for automerge-cpp. Every contributor
(human or AI) should read this before writing code. The style draws heavily
from Ben Deane's talks on modern C++ — in particular *Easy to Use, Hard to
Misuse*, *Using Types Effectively*, and *Composable C++*.

The guiding question is always: **can the compiler reject the mistake?**

---

## 1. Types Model the Domain

### 1.1 Make Illegal States Unrepresentable

If two fields are mutually exclusive, they belong in a `std::variant`, not in
a struct with a tag and two optionals.

```cpp
// BAD — caller can construct { .state = disconnected, .connection_id = 7 }
struct Connection {
    enum State { disconnected, connected, error };
    State state;
    int connection_id;      // only valid when connected
    std::string error_msg;  // only valid when error
};

// GOOD — each state carries exactly its own data
struct Disconnected {};
struct Connected { int connection_id; };
struct ConnectionError { std::string message; };

using ConnectionState = std::variant<Disconnected, Connected, ConnectionError>;
```

### 1.2 Strong Types for Identifiers

Never pass raw `int`, `uint64_t`, or `std::string` where the value has domain
meaning. Wrap it in a distinct type so the compiler catches mix-ups.

```cpp
// BAD — which argument is which?
void apply(uint64_t actor, uint64_t counter, uint64_t op);

// GOOD — compile error if you swap them
struct ActorId  { std::array<std::byte, 16> bytes; };
struct OpId     { std::uint64_t counter; ActorId actor; };
```

When creating a strong type wrapper, provide:
- Defaulted `operator<=>` for ordering
- `operator==` for equality
- A `std::hash` specialization if the type will be used in unordered containers
- A `std::format` specialization or `friend operator<<` for debugging

### 1.3 Closed Variant Sets

Use `std::variant` for values that have a fixed, known set of alternatives.
Do not use inheritance hierarchies or `void*` or `std::any` for this purpose.

```cpp
// The set of scalar values is closed — no extension point needed
using ScalarValue = std::variant<
    Null, bool, std::int64_t, std::uint64_t, double,
    Counter, Timestamp, std::string, Bytes
>;
```

### 1.4 Vocabulary Types

Use the standard vocabulary types for their intended purpose:

| Situation | Type | Not this |
|-----------|------|----------|
| Value might be absent | `std::optional<T>` | `T*`, `-1`, `""` |
| Operation might fail | `std::expected<T, Error>` | exceptions, error codes |
| One of N alternatives | `std::variant<A, B, C>` | `union`, `enum` + `void*` |
| Non-owning view of data | `std::span<T>` | `T*, size_t` |
| Non-owning view of string | `std::string_view` | `const char*` |

---

## 2. No Raw Loops

Every traversal in library code is expressed through algorithms, ranges
pipelines, or fold/reduce operations. This makes intent explicit, eliminates
off-by-one errors, and enables future parallelism.

### 2.1 Prefer Named Algorithms

```cpp
// BAD — what is this loop doing? You have to read the body.
bool found = false;
for (const auto& item : items) {
    if (item.id == target) {
        found = true;
        break;
    }
}

// GOOD — intent is declared in the name
bool found = std::ranges::any_of(items, [&](const auto& item) {
    return item.id == target;
});
```

### 2.2 Ranges Pipelines for Composition

```cpp
// BAD — imperative accumulation
std::vector<std::string> result;
for (const auto& entry : entries) {
    if (entry.is_active()) {
        result.push_back(to_upper(entry.name()));
    }
}

// GOOD — declarative pipeline
auto result = entries
    | std::views::filter(&Entry::is_active)
    | std::views::transform([](const auto& e) { return to_upper(e.name()); })
    | std::ranges::to<std::vector>();
```

### 2.3 Folds for Accumulation

Recognize when an operation is a fold (reduce). If the binary operation is
associative with an identity, it is a monoid — and can be parallelized.

```cpp
// BAD — manual accumulation
int total = 0;
for (auto x : values) total += x;

// GOOD — declares the structure of the computation
auto total = std::ranges::fold_left(values, 0, std::plus<>{});

// BETTER (if parallelism matters) — monoid enables std::reduce
auto total = std::reduce(std::execution::par, values.begin(), values.end());
```

### 2.4 When a Loop Is Acceptable

Raw loops are permitted in:
- **Performance-critical internal code** where the algorithm has no standard
  equivalent and the loop body is complex enough that a lambda would obscure
  intent more than the loop does.
- **Test code** where clarity matters more than purity.

Even then, prefer extracting the loop body into a named function.

---

## 3. Error Handling

### 3.1 `std::expected` for Expected Failures

Any operation that can fail in a way the caller should handle returns
`std::expected<T, Error>`. This includes I/O, parsing, validation, and
any operation on external data.

```cpp
auto load(std::span<const std::byte> data) -> std::expected<Document, Error>;

// Caller must handle the error — it cannot accidentally ignore it
auto doc = Document::load(data);
if (!doc) {
    log("failed: {}", doc.error().message);
    return;
}
use(*doc);
```

### 3.2 `std::optional` for Absence

When a value might not exist but that is not an error (e.g., looking up a key
that may not be present), return `std::optional<T>`.

```cpp
auto get(ObjId obj, Prop key) const -> std::optional<Value>;
```

### 3.3 Preconditions and Logic Errors

For programmer errors (violated preconditions, invariant violations), use
assertions or contracts. These should never happen in correct code and are
not something callers "handle."

```cpp
void set(ObjId obj, std::size_t index, ScalarValue val) {
    assert(index < length(obj) && "index out of bounds");
    // ...
}
```

### 3.4 No Exceptions in Library Code

Exceptions are not used for control flow. The only exceptions that may
propagate are `std::bad_alloc` (out of memory) — and only because the
standard library throws it.

---

## 4. Value Semantics and Immutability

### 4.1 Prefer Value Types

Types should be copyable and movable (regular types). Prefer value semantics
over reference semantics. If a type is large and copying is expensive,
make it movable and document that.

```cpp
// Good — value type, cheap to copy
struct OpId {
    std::uint64_t counter;
    ActorId actor;
    auto operator<=>(const OpId&) const = default;
};
```

### 4.2 `const` by Default

Every variable that doesn't need to change should be `const`. Use the
immediately-invoked lambda pattern to initialize complex `const` values.

```cpp
// BAD — result is mutable for no reason
auto result = compute_something();
// ... 50 lines later someone accidentally writes: result = other_thing;

// GOOD — frozen at initialization
const auto result = compute_something();

// GOOD — complex initialization, still const
const auto config = [&] {
    if (use_defaults) return Config::defaults();
    return Config::from_file(path);
}();
```

### 4.3 Explicit Mutation Boundaries

Documents are conceptually immutable outside of transactions. All mutations
go through `transact()`, which takes a callable that receives a mutable
`Transaction&`:

```cpp
doc.transact([](auto& tx) {
    tx.put(root, "key", 42);
});
// doc is conceptually frozen again
```

---

## 5. Composition Over Inheritance

### 5.1 No Deep Hierarchies

Do not build class hierarchies with virtual dispatch for domain modeling.
Use composition (a struct that *contains* components) and `std::variant`
(a type that *is one of* several alternatives).

```cpp
// BAD — polymorphism for data modeling
class Value {
    virtual ~Value() = default;
};
class IntValue : public Value { int val; };
class StrValue : public Value { std::string val; };

// GOOD — closed set, stack-allocated, visitable
using Value = std::variant<int, std::string>;
```

### 5.2 Concepts for Generic Interfaces

When you need polymorphic behavior (multiple types that share an interface),
prefer concepts over virtual functions. This gives zero-cost abstraction and
compile-time checking.

```cpp
template <typename T>
concept Readable = requires(const T& doc, ObjId obj, Prop key) {
    { doc.get(obj, key) } -> std::same_as<std::optional<Value>>;
    { doc.keys(obj) };
    { doc.length(obj) } -> std::same_as<std::size_t>;
};
```

### 5.3 Small, Focused Components

Each class or module does one thing. The internal `OpSet` stores operations.
The `ChangeGraph` manages causality. `Document` composes them. No god classes.

---

## 6. Naming

### 6.1 Conventions

| Entity | Style | Examples |
|--------|-------|---------|
| Namespace | `snake_case` | `automerge_cpp` |
| Type / Class / Struct | `PascalCase` | `Document`, `ActorId`, `ScalarValue` |
| Function / Method | `snake_case` | `put_object`, `splice_text`, `get_changes` |
| Variable | `snake_case` | `actor_id`, `change_hash`, `op_count` |
| Constant / constexpr | `snake_case` | `root`, `max_actor_id_bytes` |
| Enum values | `snake_case` | `ObjType::map`, `OpType::put` |
| Template parameter | `PascalCase` | `template <typename Fn>` |
| Macro (avoid) | `SCREAMING_SNAKE_CASE` | `AUTOMERGE_CPP_ASSERT` |
| File | `snake_case` | `document.hpp`, `op_set.cpp` |

### 6.2 Naming Principles

- **Names describe what, not how**: `active_users`, not `filtered_list`
- **Predicates read as questions**: `is_empty()`, `has_key()`, `contains()`
- **Mutators use verbs**: `put()`, `insert()`, `delete_key()`, `merge()`
- **Accessors are nouns**: `length()`, `keys()`, `actor_id()`
- **No abbreviations** unless universally understood: `doc` is fine, `chg` is not
- **No Hungarian notation**: no `m_`, `p_`, `s_` prefixes

### 6.3 Namespace Usage

All public symbols live in `namespace automerge_cpp`. Users alias it:

```cpp
namespace am = automerge_cpp;
auto doc = am::Document{};
```

Internal implementation details live in `namespace automerge_cpp::detail`.

---

## 7. File Organization

### 7.1 Header Structure

```cpp
#pragma once

// project headers (alphabetical)
#include <automerge-cpp/error.hpp>
#include <automerge-cpp/types.hpp>

// standard library headers (alphabetical)
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace automerge_cpp {

// declarations

}  // namespace automerge_cpp
```

### 7.2 Include Order

1. Corresponding header (in `.cpp` files — `#include <automerge-cpp/foo.hpp>`)
2. Project headers (`#include <automerge-cpp/...>`)
3. Third-party headers (if any)
4. Standard library headers

Each group separated by a blank line, sorted alphabetically within.

### 7.3 Header vs Source

- **Public headers** (`include/automerge-cpp/*.hpp`): declarations only. No
  implementation except `constexpr`, `inline`, and templates.
- **Internal headers** (`src/*.hpp`): implementation-detail types and functions
  not exposed to users. These are not installed.
- **Source files** (`src/*.cpp`): all non-template implementation.

### 7.4 One Concept Per Header

Each public header contains one primary type or a small cohesive group of
related types. `types.hpp` contains all identifier types because they are
interdependent. `value.hpp` contains all value types for the same reason.

---

## 8. API Design

### 8.1 Easy to Use, Hard to Misuse

Every public API should be evaluated against this question. If a caller can
accidentally misuse the API in a way that compiles, the API should be
redesigned.

```cpp
// BAD — easy to swap the arguments
void splice(ObjId obj, int start, int delete_count, std::string_view text);
doc.splice(text_obj, 5, 0, "hello");  // is 5 the start? the count?

// BETTER — but still positional
void splice_text(ObjId obj, std::size_t pos, std::size_t del,
                 std::string_view text);

// Could also consider named parameters or a builder for complex signatures
```

### 8.2 Non-Member Non-Friend Functions

Prefer free functions over member functions when the operation does not need
access to private state. This keeps the class interface minimal.

```cpp
// Instead of: doc.to_json()
// Prefer:     to_json(doc)
auto to_json(const Document& doc) -> std::string;
```

### 8.3 Return Types Encode Semantics

| Semantics | Return type |
|-----------|-------------|
| Always succeeds, returns value | `T` |
| Value might not exist | `std::optional<T>` |
| Might fail with error info | `std::expected<T, Error>` |
| Mutates in place, no return value | `void` |
| Returns a lazy view | Range / `std::generator<T>` |

### 8.4 Parameter Passing

| Parameter use | Passing style |
|---------------|---------------|
| Read-only, cheap to copy (≤ 16 bytes) | By value |
| Read-only, expensive to copy | `const T&` |
| Read-only, contiguous elements | `std::span<const T>` |
| Read-only, string | `std::string_view` |
| Sink (will be moved into) | By value (caller moves in) |
| Output (mutated in place) | `T&` |

---

## 9. Testing Style

### 9.1 Test Naming

```cpp
TEST(TypeName, descriptive_behavior_under_test) {
    // arrange
    // act
    // assert
}
```

Examples:
- `TEST(ActorId, default_constructed_is_all_zeros)`
- `TEST(ActorId, ordering_is_lexicographic_on_bytes)`
- `TEST(ScalarValue, int64_and_uint64_are_distinct_alternatives)`

### 9.2 One Assertion Per Logical Property

Each test should verify one logical property. Multiple `EXPECT_*` calls are
fine if they all relate to the same property.

### 9.3 No Test Helpers That Hide Logic

If a helper makes a test harder to understand, inline the code. Tests are
documentation — clarity trumps DRY.

### 9.4 Property-Based Tests for Algebraic Laws

For CRDT operations, test the algebraic properties:

```cpp
TEST(Document, merge_is_commutative) {
    // merge(a, b) produces the same state as merge(b, a)
}

TEST(Document, merge_is_associative) {
    // merge(merge(a, b), c) == merge(a, merge(b, c))
}

TEST(Document, merge_is_idempotent) {
    // merge(a, a) == a
}
```

---

## 10. Performance Considerations

### 10.1 Measure, Don't Guess

Never optimize without a benchmark proving the need. The benchmark suite
exists for this purpose.

### 10.2 Prefer Algorithms to Clever Code

Standard algorithms are heavily optimized by compiler vendors. A "clever"
hand-rolled loop is unlikely to beat `std::ranges::sort` and is certainly
harder to maintain.

### 10.3 Small Buffer Optimization

For types that are frequently allocated and usually small (like `ActorId`
at 16 bytes), keep them on the stack. For types that vary widely in size
(like `std::string` within `ScalarValue`), let the standard library handle
it — `std::string` already does SSO.

### 10.4 Move Semantics

Ensure all types are efficiently movable. For types with heap-allocated
internals, provide `noexcept` move constructors and move assignment operators.

---

## 11. Summary Checklist

Before submitting code, verify:

- [ ] No raw loops in library code (algorithms, ranges, or folds instead)
- [ ] No exceptions for expected failure paths (`std::expected` or `std::optional`)
- [ ] No implicit conversions between domain types
- [ ] No mutable state that could be `const`
- [ ] No `void*`, `std::any`, or raw pointer ownership
- [ ] Every variant alternative is handled (no `std::get` without checking)
- [ ] Every public type has comparison operators
- [ ] Every public function has a clear return type encoding its semantics
- [ ] Tests exist and pass
- [ ] No warnings at `-Wall -Wextra`
