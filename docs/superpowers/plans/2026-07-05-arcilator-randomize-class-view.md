# Arcilator Randomize Class View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose SystemVerilog randomized class values to C++ testbenches through arcilator-style generated `Layout` / `View` / wrapper classes.

**Architecture:** Keep the user-facing API aligned with existing arcilator module ports: generated C++ exposes `Packet pkt; pkt.randomize(); pkt.view.len`, and the testbench drives `dut.view.len = pkt.view.len`. MooreToCore remains responsible for class object layout and generated randomize helpers; arcilator exports class layout metadata and the existing header generator renders C++ class views. To avoid breaking existing state-file users, the first implementation uses a separate `--class-info-file` JSON alongside the current `--state-file` JSON, then `arcilator-header-cpp.py --class-info` combines both into one header.

**Tech Stack:** Moore dialect class declarations, MooreToCore class layout cache, Arc/arcilator model-info export, `tools/arcilator/arcilator-header-cpp.py`, JSON, C++ AOT testbench, Bitwuzla-backed Arc runtime.

## Global Constraints

- Do not use C out-parameters as the primary user API.
- Do not expose raw SV class handles as the C++ testbench interface.
- Match arcilator's existing `dut.view.port` style for randomizable class fields.
- Keep existing `--state-file` JSON schema compatible with existing users.
- The first implementation only needs scalar integral class properties in generated class views.
- Keep 1-d unpacked array class-view support as a follow-up unless scalar view support is already green.
- Keep SV `initial` and generic `llhd.process` out of the arcilator demo path.
- Use the existing `build-bitwuzla` build directory and no more than `-j14`.
- Run `yapf` on Python changes and `clang-format` on C++ changes when those tools are available.

---

## Design Confirmation

This plan matches the newly agreed direction:

- The previous temporary goal, "make a source-to-runtime flow run", is complete and no longer drives API design.
- The new goal is arcilator-native C++ usability.
- Module ports currently become Arc storage entries and generated `View` references.
- Randomizable classes should similarly become heap-backed class storage plus generated `View` references.
- C++ testbench code should look like this:

```cpp
#include "PacketSink-arc.h"

int main() {
  PacketSink dut;
  Packet pkt;

  if (!pkt.randomize())
    return 1;

  dut.view.len = pkt.view.len;
  dut.view.addr = pkt.view.addr;
  dut.eval();

  return dut.view.ok ? 0 : 2;
}
```

The class object storage is not Arc model state storage. It is MooreToCore heap object storage, but the C++ access pattern should be intentionally parallel to arcilator model storage.

## File Structure

- Modify `tools/arcilator/arcilator-header-cpp.py`
  - Parse optional class-info JSON.
  - Generate `ClassField`, `<Class>Layout`, `<Class>View`, and `<Class>` wrapper.
  - Reuse the existing name cleaning and `Bytes<N>` support.
- Modify `include/circt/Dialect/Arc/ModelInfo.h`
  - Add `ClassFieldInfo` and `ClassInfo`.
  - Declare class-info collection and JSON serialization helpers.
- Modify `lib/Dialect/Arc/ModelInfo.cpp`
  - Parse MooreToCore-emitted module metadata into `ClassInfo`.
  - Serialize class info to JSON.
- Modify `tools/arcilator/arcilator.cpp`
  - Add `--class-info-file`.
  - Write class-info JSON after the Arc conversion pipeline has run, mirroring `--state-file`.
- Modify `lib/Conversion/MooreToCore/MooreToCore.cpp`
  - Emit module class-info metadata from the existing `ClassTypeCache`.
  - Emit exported lifecycle functions `__circt_new_<Class>` and `__circt_delete_<Class>`.
  - Make generated `__circt_randomize_<Class>` callable from the C++ header.
- Modify `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
  - Adjust helper visibility if it currently emits `__circt_randomize_<Class>` as private.
- Create `test/arcilator/Inputs/randomize-class-view/`
  - Fixture JSON for header-generator tests.
- Create `test/arcilator/randomize-class-header.test`
  - Tests generated C++ header text from fixture state JSON plus class-info JSON.
- Create `test/Conversion/MooreToCore/class-info.mlir`
  - Tests MooreToCore class metadata and exported lifecycle helpers.
- Modify `test/arcilator/Inputs/randomize-sv-cpp-aot/packet.sv`
  - Add a tiny module with ports driven by randomized class values.
- Modify `test/arcilator/Inputs/randomize-sv-cpp-aot/testbench.cpp`
  - Use generated `Packet` wrapper and `PacketSink` model view.
- Modify `test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`
  - Generate state JSON, class-info JSON, generated header, LLVM, executable, and run it.
- Modify `test/arcilator/randomize-sv-cpp-aot.sv`
  - Keep as the lit wrapper for the script.
- Modify `docs/SystemVerilogConstraintSolving.md`
  - Record that the desired C++ TB API is generated class views, not out-params.

## Public Interfaces

### Class-Info JSON

The first version uses a separate JSON file to avoid changing existing `--state-file` consumers:

```json
[
  {
    "name": "Packet",
    "numBytes": 32,
    "newFn": "__circt_new_Packet",
    "deleteFn": "__circt_delete_Packet",
    "randomizeFn": "__circt_randomize_Packet",
    "fields": [
      {
        "name": "len",
        "offset": 16,
        "numBits": 32,
        "cppType": "int32_t"
      },
      {
        "name": "addr",
        "offset": 20,
        "numBits": 32,
        "cppType": "int32_t"
      }
    ]
  }
]
```

### Generated C++ Shape

The generated header should expose this shape for each supported class:

```cpp
struct ClassField {
  const char *name;
  unsigned offset;
  unsigned numBits;
};

extern "C" {
void *__circt_new_Packet();
void __circt_delete_Packet(void *object);
bool __circt_randomize_Packet(void *object);
}

class PacketLayout {
public:
  static const char *name;
  static const unsigned numBytes;
  static const std::array<ClassField, 2> fields;
};

class PacketView {
public:
  int32_t &len;
  int32_t &addr;
  uint8_t *object;

  explicit PacketView(uint8_t *object)
      : len(*reinterpret_cast<int32_t *>(object + 16)),
        addr(*reinterpret_cast<int32_t *>(object + 20)), object(object) {}
};

class Packet {
private:
  uint8_t *object;

public:
  Packet()
      : object(static_cast<uint8_t *>(__circt_new_Packet())), view(object) {}
  ~Packet() { __circt_delete_Packet(object); }

  Packet(const Packet &) = delete;
  Packet &operator=(const Packet &) = delete;
  Packet(Packet &&) = delete;
  Packet &operator=(Packet &&) = delete;

  bool randomize() { return __circt_randomize_Packet(object); }

  PacketView view;
};
```

---

### Task 1: Header Generator Supports Class-Info Fixtures

**Files:**
- Modify: `tools/arcilator/arcilator-header-cpp.py`
- Create: `test/arcilator/Inputs/randomize-class-view/state.json`
- Create: `test/arcilator/Inputs/randomize-class-view/class-info.json`
- Create: `test/arcilator/randomize-class-header.test`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-class-header.test`

**Interfaces:**
- Consumes: existing state JSON array accepted by `arcilator-header-cpp.py`.
- Consumes: optional class-info JSON array passed with `--class-info`.
- Produces: generated C++ declarations for `ClassField`, `PacketLayout`, `PacketView`, and `Packet`.

- [x] **Step 1: Add fixture state JSON**

Create `test/arcilator/Inputs/randomize-class-view/state.json`:

```json
[
  {
    "name": "PacketSink",
    "numStateBytes": 32,
    "initialFnSym": "",
    "finalFnSym": "",
    "states": [
      {
        "name": "len",
        "offset": 16,
        "numBits": 32,
        "type": "input"
      },
      {
        "name": "addr",
        "offset": 20,
        "numBits": 32,
        "type": "input"
      },
      {
        "name": "ok",
        "offset": 24,
        "numBits": 1,
        "type": "output"
      }
    ]
  }
]
```

- [x] **Step 2: Add fixture class-info JSON**

Create `test/arcilator/Inputs/randomize-class-view/class-info.json`:

```json
[
  {
    "name": "Packet",
    "numBytes": 32,
    "newFn": "__circt_new_Packet",
    "deleteFn": "__circt_delete_Packet",
    "randomizeFn": "__circt_randomize_Packet",
    "fields": [
      {
        "name": "len",
        "offset": 16,
        "numBits": 32,
        "cppType": "int32_t"
      },
      {
        "name": "addr",
        "offset": 20,
        "numBits": 32,
        "cppType": "int32_t"
      }
    ]
  }
]
```

- [x] **Step 3: Add failing header-generator lit test**

Create `test/arcilator/randomize-class-header.test`:

```text
# RUN: %PYTHON% %S/../../tools/arcilator/arcilator-header-cpp.py \
# RUN:   %S/Inputs/randomize-class-view/state.json \
# RUN:   --class-info %S/Inputs/randomize-class-view/class-info.json \
# RUN:   | FileCheck %s
# RUN: %PYTHON% %S/../../tools/arcilator/arcilator-header-cpp.py \
# RUN:   %S/Inputs/randomize-class-view/state.json \
# RUN:   --class-info %S/Inputs/randomize-class-view/class-info.json \
# RUN:   | FileCheck %s --check-prefix=ORDER

# CHECK-DAG: struct ClassField
# CHECK-DAG: class PacketSinkView
# CHECK-DAG: uint32_t &len;
# CHECK-DAG: uint32_t &addr;
# CHECK-DAG: uint8_t &ok;

# CHECK-DAG: extern "C" {
# CHECK-DAG: void *__circt_new_Packet();
# CHECK-DAG: void __circt_delete_Packet(void *object);
# CHECK-DAG: bool __circt_randomize_Packet(void *object);
# CHECK-DAG: }

# CHECK-DAG: class PacketLayout
# CHECK-DAG: static const char *name;
# CHECK-DAG: static const unsigned numBytes;
# CHECK-DAG: static const std::array<ClassField, 2> fields;

# CHECK-DAG: const char *PacketLayout::name = "Packet";
# CHECK-DAG: const unsigned PacketLayout::numBytes = 32;
# CHECK-DAG: ClassField{"len", 16, 32}
# CHECK-DAG: ClassField{"addr", 20, 32}

# CHECK-DAG: class PacketView
# CHECK-DAG: int32_t &len;
# CHECK-DAG: int32_t &addr;
# CHECK-DAG: explicit PacketView(uint8_t *object)
# CHECK-DAG: len(*reinterpret_cast<int32_t *>(object + 16))
# CHECK-DAG: addr(*reinterpret_cast<int32_t *>(object + 20))

# CHECK-DAG: class Packet
# CHECK-DAG: Packet()
# CHECK-DAG: __circt_new_Packet()
# CHECK-DAG: ~Packet()
# CHECK-DAG: __circt_delete_Packet(object)
# CHECK-DAG: Packet(const Packet &) = delete;
# CHECK-DAG: Packet &operator=(const Packet &) = delete;
# CHECK-DAG: Packet(Packet &&) = delete;
# CHECK-DAG: Packet &operator=(Packet &&) = delete;
# CHECK-DAG: bool randomize()
# CHECK-DAG: return __circt_randomize_Packet(object);

# ORDER-LABEL: class Packet {
# ORDER-NEXT: private:
# ORDER-NEXT: uint8_t *object;
# ORDER: public:
# ORDER: Packet() :
# ORDER-NEXT: object(static_cast<uint8_t *>(__circt_new_Packet())),
# ORDER-NEXT: view(object) {}
```

- [x] **Step 4: Run the test and confirm failure**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-class-header.test
```

Expected: FAIL because `arcilator-header-cpp.py` does not accept `--class-info`.

- [x] **Step 5: Extend `arcilator-header-cpp.py` data model**

In `tools/arcilator/arcilator-header-cpp.py`, add dataclasses after `ModelInfo`:

```python
@dataclass
class ClassFieldInfo:
  name: str
  offset: int
  numBits: int
  cppType: str

  def decode(d: dict) -> "ClassFieldInfo":
    return ClassFieldInfo(d["name"], d["offset"], d["numBits"], d["cppType"])


@dataclass
class ClassInfo:
  name: str
  numBytes: int
  newFn: str
  deleteFn: str
  randomizeFn: str
  fields: List[ClassFieldInfo]

  def decode(d: dict) -> "ClassInfo":
    return ClassInfo(d["name"], d["numBytes"], d["newFn"], d["deleteFn"],
                     d["randomizeFn"],
                     [ClassFieldInfo.decode(f) for f in d["fields"]])
```

Add loader:

```python
def load_classes(class_info_json: Optional[str]) -> List[ClassInfo]:
  if not class_info_json:
    return []
  with open(class_info_json, "r") as f:
    return [ClassInfo.decode(d) for d in json.load(f)]
```

- [x] **Step 6: Extend the header template**

In `header_cpp_template`, add this block before `{% for model in models %}`:

```jinja2
{% if classes %}
struct ClassField {
  const char *name;
  unsigned offset;
  unsigned numBits;
};

{% for cls in classes %}
extern "C" {
void *{{ cls.newFn }}();
void {{ cls.deleteFn }}(void *object);
bool {{ cls.randomizeFn }}(void *object);
}

class {{ cls.name }}Layout {
public:
  static const char *name;
  static const unsigned numBytes;
  static const std::array<ClassField, {{ cls.fields|length }}> fields;
};

const char *{{ cls.name }}Layout::name = "{{ cls.name }}";
const unsigned {{ cls.name }}Layout::numBytes = {{ cls.numBytes }};
const std::array<ClassField, {{ cls.fields|length }}> {{ cls.name }}Layout::fields = {
{% for field in cls.fields %}
  ClassField{"{{ field.name }}", {{ field.offset }}, {{ field.numBits }}},
{% endfor %}
};

class {{ cls.name }}View {
public:
{% for field in cls.fields %}
  {{ field.cppType }} &{{ clean_name(field.name) }};
{% endfor %}
  uint8_t *object;

  explicit {{ cls.name }}View(uint8_t *object) :
{% for field in cls.fields %}
    {{ clean_name(field.name) }}(*reinterpret_cast<{{ field.cppType }} *>(object + {{ field.offset }})),
{% endfor %}
    object(object) {}
};

class {{ cls.name }} {
private:
  uint8_t *object;

public:
  {{ cls.name }}() :
    object(static_cast<uint8_t *>({{ cls.newFn }}())),
    view(object) {}

  ~{{ cls.name }}() {
    {{ cls.deleteFn }}(object);
    object = nullptr;
  }

  {{ cls.name }}(const {{ cls.name }} &) = delete;
  {{ cls.name }} &operator=(const {{ cls.name }} &) = delete;
  {{ cls.name }}({{ cls.name }} &&) = delete;
  {{ cls.name }} &operator=({{ cls.name }} &&) = delete;

  bool randomize() { return {{ cls.randomizeFn }}(object); }

  {{ cls.name }}View view;
};
{% endfor %}
{% endif %}
```

Update `render_header_cpp` signature and render call:

```python
def render_header_cpp(models, classes, view_depth):
```

Pass `classes=classes` and `clean_name=clean_name` into `template.render(...)`.

Add parser argument:

```python
parser.add_argument("--class-info",
                    metavar="CLASS_INFO_JSON",
                    default=None,
                    help="randomizable class description file to process")
```

Load and render:

```python
models_data = load_models(args.state_json)
classes_data = load_classes(args.class_info)
print(render_header_cpp(models_data, classes_data, args.view_depth))
```

- [x] **Step 7: Format Python**

Run:

```sh
yapf -i tools/arcilator/arcilator-header-cpp.py
```

Expected: exits 0. If `yapf` is not on PATH, report that and run the tests anyway.

Observed: `yapf` was not on PATH; continued with the focused test.

- [x] **Step 8: Run the header-generator test**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-class-header.test
```

Expected: PASS.

### Task 2: MooreToCore Emits Class Metadata and Lifecycle Helpers

**Files:**
- Modify: `lib/Conversion/MooreToCore/MooreToCore.cpp`
- Modify: `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
- Create: `test/Conversion/MooreToCore/class-info.mlir`
- Test: `build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/class-info.mlir`

**Interfaces:**
- Consumes: `moore.class.classdecl @Packet` with scalar integral properties.
- Produces: module attribute `circt.arc.class_info`.
- Produces: public `func.func @__circt_new_Packet() -> !llvm.ptr`.
- Produces: public `func.func @__circt_delete_Packet(%arg0: !llvm.ptr)`.
- Produces: public callable `func.func @__circt_randomize_Packet(%arg0: !llvm.ptr) -> i1`.

- [x] **Step 1: Add failing MooreToCore class-info test**

Create `test/Conversion/MooreToCore/class-info.mlir`:

```mlir
// RUN: circt-opt --convert-moore-to-core %s | FileCheck %s

moore.class.classdecl @Packet {
  moore.class.propertydecl @len : !moore.i32 {isRand}
  moore.class.propertydecl @addr : !moore.i32 {isRand}
  moore.class.constraintdecl @c {
  ^bb0(%this: !moore.class<@Packet>):
    %zero = moore.constant 0 : i32
    %len_ref = moore.class.property_ref %this[@len] : <@Packet> -> !moore.ref<!moore.i32>
    %len = moore.read %len_ref : <i32>
    %ok = moore.sgt %len, %zero : i32 -> i1
    moore.yield %ok : i1
  }
}

func.func @smoke() -> !moore.i1 {
  %object = moore.class.new : !moore.class<@Packet>
  %ok = moore.class.randomize %object : !moore.class<@Packet>
  return %ok : !moore.i1
}

// CHECK: circt.arc.class_info
// CHECK: name = "Packet"
// CHECK: newFn = "__circt_new_Packet"
// CHECK: deleteFn = "__circt_delete_Packet"
// CHECK: randomizeFn = "__circt_randomize_Packet"
// CHECK: name = "len"
// CHECK: cppType = "int32_t"
// CHECK: name = "addr"
// CHECK: cppType = "int32_t"

// CHECK-LABEL: func.func @__circt_delete_Packet
// CHECK-SAME: (%[[OBJECT:.*]]: !llvm.ptr)
// CHECK: call @free(%[[OBJECT]])
// CHECK: return

// CHECK-LABEL: func.func @__circt_new_Packet() -> !llvm.ptr
// CHECK: call @malloc
// CHECK: llvm.store
// CHECK: return

// CHECK-LABEL: func.func @__circt_randomize_Packet
// CHECK-SAME: (%{{.*}}: !llvm.ptr) -> i1
// CHECK: call @arcRuntimeSolverCheck
```

- [x] **Step 2: Run the test and confirm failure**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/class-info.mlir
```

Expected: FAIL because class-info metadata and lifecycle helpers do not exist yet.

- [x] **Step 3: Add helper-name functions**

In `lib/Conversion/MooreToCore/MooreToCore.cpp`, near `getRandomizeHelperName`, add:

```cpp
static std::string getClassNewHelperName(SymbolRefAttr classSym) {
  return (Twine("__circt_new_") + classSym.getRootReference().getValue()).str();
}

static std::string getClassDeleteHelperName(SymbolRefAttr classSym) {
  return (Twine("__circt_delete_") + classSym.getRootReference().getValue())
      .str();
}
```

- [x] **Step 4: Factor class allocation into a reusable helper**

In `lib/Conversion/MooreToCore/MooreToCore.cpp`, extract the allocation body from `ClassNewOpConversion::matchAndRewrite` into:

```cpp
static FailureOr<Value>
createClassAllocation(Location loc, SymbolRefAttr sym, ModuleOp mod,
                      const TypeConverter &typeConverter, ClassTypeCache &cache,
                      FunctionCache &funcCache,
                      ConversionPatternRewriter &rewriter);
```

The helper must:

- call `resolveClassStructBody(mod, sym, typeConverter, cache)`;
- compute `DataLayout dl(mod)` and `dl.getTypeSize(structTy)`;
- call or declare `malloc`;
- store the class type-info pointer into the object header;
- return the allocated `!llvm.ptr`.

Then update `ClassNewOpConversion::matchAndRewrite` to call `createClassAllocation(...)` and replace the op with the returned pointer.

- [x] **Step 5: Emit public new/delete functions from `ClassDeclOpConversion`**

Change `ClassDeclOpConversion` constructor to also receive `FunctionCache &funcCache`.

In `ClassDeclOpConversion::matchAndRewrite`, after `resolveClassStructBody(...)`, create:

```cpp
func.func @__circt_new_Packet() -> !llvm.ptr
func.func @__circt_delete_Packet(%object: !llvm.ptr)
```

Use the class declaration symbol directly:

```cpp
auto classSym = SymbolRefAttr::get(op.getSymNameAttr());
```

For `@__circt_new_Packet`, insert a block, call `createClassAllocation(...)`, and return the pointer.

For `@__circt_delete_Packet`, declare or create `free` through `FunctionCache`:

```cpp
auto ptrTy = LLVM::LLVMPointerType::get(op.getContext());
auto freeFn = funcCache.getOrCreate(rewriter, "free", {ptrTy}, {});
func::CallOp::create(rewriter, loc, freeFn, ValueRange{objectArg});
func::ReturnOp::create(rewriter, loc);
```

Do not add destructor semantics in this milestone.

- [x] **Step 6: Emit class-info module metadata**

In `ClassDeclOpConversion::matchAndRewrite`, build or append to a module attribute named `circt.arc.class_info`.

Use `DictionaryAttr` entries with these keys:

```text
name: StringAttr
numBytes: IntegerAttr i64
newFn: StringAttr
deleteFn: StringAttr
randomizeFn: StringAttr
fields: ArrayAttr<DictionaryAttr>
```

Each exposed field dictionary must contain:

```text
name: StringAttr
offset: IntegerAttr i64
numBits: IntegerAttr i64
cppType: StringAttr
```

For first version, expose only scalar integral class properties:

- `!moore.i8` -> `"int8_t"`
- `!moore.i16` -> `"int16_t"`
- `!moore.i32` -> `"int32_t"`
- `!moore.i64` -> `"int64_t"`

Observed: Moore `IntType` carries width/domain but not source signedness, so the
first implementation emits signed `intN_t` C++ view types.

Skip hidden `rand_mode` and `constraint_mode` fields. They are not `ClassPropertyDeclOp`s in the source class view and must not appear in generated C++ public view.

Use `ClassTypeCache::ClassStructInfo::propertyPath` to find the property GEP path, and compute the byte offset from the LLVM struct layout. If offset computation is awkward for nested paths, add a small helper:

```cpp
static FailureOr<uint64_t>
computeClassPropertyByteOffset(DataLayout &layout,
                               LLVM::LLVMStructType classBody,
                               ArrayRef<unsigned> gepPath);
```

The helper must reject non-constant or unsupported paths with `emitError`, not silently omit source properties.

- [x] **Step 7: Make randomize helper externally callable**

In `lib/Conversion/MooreToCore/LowerClassRandomize.cpp`, find where `func.func @__circt_randomize_<Class>` is created.

If it is currently created with private visibility, change it to public unless there is already a public wrapper. The desired emitted symbol is:

```mlir
func.func @__circt_randomize_Packet(%arg0: !llvm.ptr) -> i1
```

Update existing tests that expected:

```mlir
func.func private @__circt_randomize_Packet
```

to accept the public symbol.

- [x] **Step 8: Register updated conversion pattern constructor**

In `populateOpConversion(...)`, update:

```cpp
patterns.add<ClassDeclOpConversion>(typeConverter, patterns.getContext(),
                                    classCache);
```

to:

```cpp
patterns.add<ClassDeclOpConversion>(typeConverter, patterns.getContext(),
                                    classCache, funcCache);
```

- [x] **Step 9: Format C++**

Run:

```sh
clang-format -i lib/Conversion/MooreToCore/MooreToCore.cpp lib/Conversion/MooreToCore/LowerClassRandomize.cpp
```

Expected: exits 0. If `clang-format` is not available, report that and continue with build/test.

Observed: `clang-format` was not on PATH; continued with build/test.

- [x] **Step 10: Build `circt-opt`**

Run:

```sh
ninja -C build-bitwuzla -j14 bin/circt-opt
```

Expected: exits 0.

- [x] **Step 11: Run the MooreToCore test**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/class-info.mlir
```

Expected: PASS.

- [x] **Step 12: Run existing randomize lowering tests**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/randomize-call.mlir test/Conversion/ImportVerilog/randomize-flow.sv test/Conversion/MooreToCore/class-randomize-solver-calls.mlir test/Conversion/MooreToCore/class-randomize-crosscheck.mlir
```

Expected: PASS.

### Task 3: Arcilator Exports Class-Info JSON

**Files:**
- Modify: `include/circt/Dialect/Arc/ModelInfo.h`
- Modify: `lib/Dialect/Arc/ModelInfo.cpp`
- Modify: `tools/arcilator/arcilator.cpp`
- Test: covered by Task 4's end-to-end `randomize-sv-cpp-aot.sv` demo

**Interfaces:**
- Consumes: module attribute `circt.arc.class_info`.
- Produces: `arcilator --class-info-file <path>` JSON matching the class-info schema from Task 1.

- [x] **Step 1: Add `ClassFieldInfo` and `ClassInfo` structs**

In `include/circt/Dialect/Arc/ModelInfo.h`, after `StateInfo`, add:

```cpp
struct ClassFieldInfo {
  std::string name;
  uint64_t offset;
  uint64_t numBits;
  std::string cppType;
};

struct ClassInfo {
  std::string name;
  uint64_t numBytes;
  std::string newFn;
  std::string deleteFn;
  std::string randomizeFn;
  llvm::SmallVector<ClassFieldInfo> fields;
};
```

Add declarations:

```cpp
mlir::LogicalResult collectClassInfo(mlir::ModuleOp module,
                                     llvm::SmallVector<ClassInfo> &classes);

void serializeClassInfoToJson(llvm::raw_ostream &outputStream,
                              llvm::ArrayRef<ClassInfo> classes);
```

- [x] **Step 2: Implement class-info collection**

In `lib/Dialect/Arc/ModelInfo.cpp`, implement:

```cpp
LogicalResult circt::arc::collectClassInfo(ModuleOp module,
                                           SmallVector<ClassInfo> &classes);
```

Read:

```cpp
auto attr = module->getAttrOfType<ArrayAttr>("circt.arc.class_info");
```

For each class dictionary, require all keys:

- `name`
- `numBytes`
- `newFn`
- `deleteFn`
- `randomizeFn`
- `fields`

For each field dictionary, require all keys:

- `name`
- `offset`
- `numBits`
- `cppType`

If an attribute is malformed, emit an error on `module`:

```cpp
return module.emitError("malformed circt.arc.class_info attribute");
```

If the attribute is absent, return success with an empty class list.

- [x] **Step 3: Implement class-info JSON serialization**

In `lib/Dialect/Arc/ModelInfo.cpp`, implement:

```cpp
void circt::arc::serializeClassInfoToJson(raw_ostream &outputStream,
                                          ArrayRef<ClassInfo> classes);
```

Use `llvm::json::OStream` and emit the schema from Task 1:

```json
[
  {
    "name": "...",
    "numBytes": 0,
    "newFn": "...",
    "deleteFn": "...",
    "randomizeFn": "...",
    "fields": [
      {
        "name": "...",
        "offset": 0,
        "numBits": 0,
        "cppType": "..."
      }
    ]
  }
]
```

- [x] **Step 4: Add arcilator command-line option**

In `tools/arcilator/arcilator.cpp`, near `stateFile`, add:

```cpp
static llvm::cl::opt<std::string>
    classInfoFile("class-info-file",
                  llvm::cl::desc("Randomizable class info file"),
                  llvm::cl::value_desc("filename"), llvm::cl::init(""),
                  llvm::cl::cat(mainCategory));
```

After the existing `--state-file` block, add:

```cpp
if (!classInfoFile.empty()) {
  std::error_code ec;
  llvm::ToolOutputFile outputFile(classInfoFile, ec,
                                  llvm::sys::fs::OpenFlags::OF_None);
  if (ec) {
    llvm::errs() << "unable to open class info file: " << ec.message() << '\n';
    return failure();
  }

  SmallVector<arc::ClassInfo> classes;
  if (failed(arc::collectClassInfo(module.get(), classes))) {
    llvm::errs() << "failed to collect class info\n";
    return failure();
  }
  arc::serializeClassInfoToJson(outputFile.os(), classes);
  outputFile.keep();
}
```

- [x] **Step 5: Format C++**

Run:

```sh
clang-format -i include/circt/Dialect/Arc/ModelInfo.h lib/Dialect/Arc/ModelInfo.cpp tools/arcilator/arcilator.cpp
```

Expected: exits 0. If `clang-format` is not available, report that and continue with build/test.

Observed: `clang-format` was not on PATH; continued with build/test.

- [x] **Step 6: Build arcilator**

Run:

```sh
ninja -C build-bitwuzla -j14 bin/arcilator
```

Expected: exits 0.

Observed smoke:

```sh
build-bitwuzla/bin/circt-opt --convert-moore-to-core test/Conversion/MooreToCore/class-info.mlir -o /tmp/circt-class-info-core.mlir
build-bitwuzla/bin/arcilator /tmp/circt-class-info-core.mlir --class-info-file /tmp/circt-classes.json --disable-output -o /tmp/circt-arcilator-out.mlir
```

The generated `/tmp/circt-classes.json` matched the Task 1 class-info schema.

### Task 4: End-to-End Generated Header Demo Uses `PacketView`

**Files:**
- Modify: `test/arcilator/Inputs/randomize-sv-cpp-aot/packet.sv`
- Modify: `test/arcilator/Inputs/randomize-sv-cpp-aot/testbench.cpp`
- Modify: `test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`
- Modify: `test/arcilator/randomize-sv-cpp-aot.sv`
- Modify: `docs/SystemVerilogConstraintSolving.md`
- Test: `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv`

**Interfaces:**
- Consumes: `circt-verilog --ir-moore`.
- Consumes: `circt-opt --convert-moore-to-core`.
- Consumes: `arcilator --state-file --class-info-file --emit-llvm --no-runtime`.
- Produces: a native executable whose C++ testbench uses `Packet pkt; pkt.randomize(); pkt.view.len`.
- Produces: project documentation that says the C++ TB API is generated class views.

- [x] **Step 1: Replace demo SV with class plus tiny module**

Update `test/arcilator/Inputs/randomize-sv-cpp-aot/packet.sv` to:

```systemverilog
class Packet;
  rand int len;
  rand int addr;

  constraint len_c {
    len > 0;
  }

  constraint addr_c {
    addr == 16;
  }
endclass

module PacketSink(input int len, input int addr, output bit ok);
  assign ok = (len > 0) && (addr == 16);
endmodule
```

Observed: the first version intentionally keeps demo constraints within the
currently supported solver subset (`sgt` and `eq`). `<=`, bit-select, and
logical `and` lowering remain follow-up solver-expression work.

- [x] **Step 2: Replace C++ testbench with generated view usage**

Update `test/arcilator/Inputs/randomize-sv-cpp-aot/testbench.cpp` to:

```cpp
#include "randomize-arc.h"

#include <cstdio>

int main() {
  PacketSink dut;
  Packet pkt;

  if (!pkt.randomize()) {
    std::fprintf(stderr, "Packet randomize failed\n");
    return 1;
  }

  std::printf("packet: len=%d addr=0x%x\n", pkt.view.len, pkt.view.addr);

  dut.view.len = pkt.view.len;
  dut.view.addr = pkt.view.addr;
  dut.eval();

  if (!dut.view.ok) {
    std::fprintf(stderr, "PacketSink rejected randomized packet\n");
    return 2;
  }

  return 0;
}
```

- [x] **Step 3: Update run script to generate state/class/header files**

In `test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`, after `circt-opt --convert-moore-to-core`, add state and class-info paths:

```sh
state_json="$OUT_DIR/randomize-state.json"
class_json="$OUT_DIR/randomize-classes.json"
header="$OUT_DIR/randomize-arc.h"
```

Replace the arcilator command with:

```sh
"$arcilator" "$OUT_DIR/randomize-core.mlir" --emit-llvm --no-runtime \
  --state-file "$state_json" --class-info-file "$class_json" \
  -o "$OUT_DIR/randomize.ll"
```

Generate the header:

```sh
"$PYTHON" "$repo_root/tools/arcilator/arcilator-header-cpp.py" \
  "$state_json" --class-info "$class_json" > "$header"
```

Add default Python at the top:

```sh
PYTHON="${PYTHON:-python3}"
```

Compile with the generated header include path:

```sh
"$CXX" "$OUT_DIR/randomize.ll" "$script_dir/testbench.cpp" "$arc_runtime_lib" \
  $BITWUZLA_RANDOMIZE_LINK_FLAGS -I"$OUT_DIR" -I"$BUILD_DIR/tools/arcilator" \
  -I"$repo_root/tools/arcilator" -Wno-override-module -fuse-ld=lld \
  -o "$OUT_DIR/randomize.exe"
```

- [x] **Step 4: Run the demo script and confirm failure if earlier tasks are missing**

Run:

```sh
OUT_DIR=/tmp/circt-randomize-class-view-demo \
  test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh
```

Expected before Tasks 1-3 are complete: FAIL due missing class info, missing generated header support, or missing lifecycle symbols. Expected after Tasks 1-3 are complete: PASS and output contains:

```text
packet: len=
randomize demo executable passed: /tmp/circt-randomize-class-view-demo/randomize.exe
```

Observed after Tasks 1-3 were complete:

```text
packet: len=2147483647 addr=0x10
randomize demo executable passed: /tmp/circt-randomize-class-view-demo/randomize.exe
```

- [x] **Step 5: Update lit wrapper**

Keep `test/arcilator/randomize-sv-cpp-aot.sv` as a wrapper:

```systemverilog
// REQUIRES: slang
// REQUIRES: bitwuzla-randomize
// RUN: env OUT_DIR=%t %S/Inputs/randomize-sv-cpp-aot/run.sh
```

- [x] **Step 6: Run the lit wrapper**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv
```

Expected: PASS and command output includes `randomize demo executable passed`.

- [x] **Step 7: Update status documentation**

In `docs/SystemVerilogConstraintSolving.md`, update the current executable demo paragraph to say:

```markdown
The current arcilator-oriented C++ API direction is generated views. Module
ports remain exposed through the existing `dut.view.port` API, and
randomizable classes are exposed through generated heap-backed wrappers such as
`Packet pkt; pkt.randomize(); pkt.view.len`. The class object is not Arc model
state storage, but the generated C++ access pattern intentionally mirrors
arcilator's module view style.
```

- [x] **Step 8: Run formatting**

Run:

```sh
yapf -i tools/arcilator/arcilator-header-cpp.py
clang-format -i include/circt/Dialect/Arc/ModelInfo.h lib/Dialect/Arc/ModelInfo.cpp tools/arcilator/arcilator.cpp lib/Conversion/MooreToCore/MooreToCore.cpp lib/Conversion/MooreToCore/LowerClassRandomize.cpp
```

Expected: both commands exit 0. If one formatter is unavailable, report the missing formatter and continue with build/test.

Observed: both `yapf` and `clang-format` were not on PATH; continued with
build/test.

- [x] **Step 9: Build tools**

Run:

```sh
ninja -C build-bitwuzla -j14 bin/circt-opt bin/arcilator
```

Expected: exits 0.

- [x] **Step 10: Run focused tests**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a \
  test/arcilator/randomize-class-header.test \
  test/Conversion/MooreToCore/class-info.mlir \
  test/arcilator/randomize-sv-cpp-aot.sv
```

Expected: PASS for all listed tests.

- [x] **Step 11: Run existing randomize and arcilator subsets**

Run:

```sh
build-bitwuzla/bin/llvm-lit -a \
  test/Conversion/ImportVerilog/randomize-flow.sv \
  test/Conversion/MooreToCore/randomize-call.mlir \
  test/arcilator
```

Expected: PASS.

- [x] **Step 12: Check whitespace**

Run:

```sh
git diff --check
```

Expected: exits 0.

## Verification Matrix

- `yapf -i tools/arcilator/arcilator-header-cpp.py`
- `clang-format -i include/circt/Dialect/Arc/ModelInfo.h lib/Dialect/Arc/ModelInfo.cpp tools/arcilator/arcilator.cpp lib/Conversion/MooreToCore/MooreToCore.cpp lib/Conversion/MooreToCore/LowerClassRandomize.cpp`
- `ninja -C build-bitwuzla -j14 bin/circt-opt bin/arcilator`
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-class-header.test`
- `build-bitwuzla/bin/llvm-lit -a test/Conversion/MooreToCore/class-info.mlir`
- `OUT_DIR=/tmp/circt-randomize-class-view-demo test/arcilator/Inputs/randomize-sv-cpp-aot/run.sh`
- `build-bitwuzla/bin/llvm-lit -a test/arcilator/randomize-sv-cpp-aot.sv`
- `build-bitwuzla/bin/llvm-lit -a test/Conversion/ImportVerilog/randomize-flow.sv test/Conversion/MooreToCore/randomize-call.mlir test/arcilator`
- `git diff --check`

## Deferred Work

- 1-d unpacked array views using `Memory<T, Stride, Depth>` or a small `ArrayView<T, N>`.
- Inheritance-aware public view generation beyond properties directly represented in the computed class layout.
- SV destructor semantics for class wrapper destruction.
- Runtime `rand_mode` and `constraint_mode` control from the generated C++ wrapper.
- Merging class info into the existing state JSON once the separate `--class-info-file` path is stable.
