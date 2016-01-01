#ifndef wren_h
#define wren_h

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

// A single virtual machine for executing Wren code.
//
// Wren has no global state, so all state stored by a running interpreter lives
// here.
typedef struct WrenVM WrenVM;

// A handle to a Wren object.
//
// This lets code outside of the VM hold a persistent reference to an object.
// After a value is acquired, and until it is released, this ensures the
// garbage collector will not reclaim it.
typedef struct WrenValue WrenValue;

// A generic allocation function that handles all explicit memory management
// used by Wren. It's used like so:
//
// - To allocate new memory, [memory] is NULL and [newSize] is the desired
//   size. It should return the allocated memory or NULL on failure.
//
// - To attempt to grow an existing allocation, [memory] is the memory, and
//   [newSize] is the desired size. It should return [memory] if it was able to
//   grow it in place, or a new pointer if it had to move it.
//
// - To shrink memory, [memory] and [newSize] are the same as above but it will
//   always return [memory].
//
// - To free memory, [memory] will be the memory to free and [newSize] will be
//   zero. It should return NULL.
typedef void* (*WrenReallocateFn)(void* memory, size_t newSize);

// A function callable from Wren code, but implemented in C.
typedef void (*WrenForeignMethodFn)(WrenVM* vm);

// A finalizer function for freeing resources owned by an instance of a foreign
// class. Unlike most foreign methods, finalizers do not have access to the VM
// and should not interact with it since it's in the middle of a garbage
// collection.
typedef void (*WrenFinalizerFn)(void* data);

// Loads and returns the source code for the module [name].
typedef char* (*WrenLoadModuleFn)(WrenVM* vm, const char* name);

// Returns a pointer to a foreign method on [className] in [module] with
// [signature].
typedef WrenForeignMethodFn (*WrenBindForeignMethodFn)(WrenVM* vm,
                                                       const char* module,
                                                       const char* className,
                                                       bool isStatic,
                                                       const char* signature);

// Displays a string of text to the user.
typedef void (*WrenWriteFn)(WrenVM* vm, const char* text);

typedef struct
{
  // The callback invoked when the foreign object is created.
  //
  // This must be provided. Inside the body of this, it must call
  // [wrenAllocateForeign] exactly once.
  WrenForeignMethodFn allocate;

  // The callback invoked when the garbage collector is about to collecto a
  // foreign object's memory.
  //
  // This may be `NULL` if the foreign class does not need to finalize.
  WrenFinalizerFn finalize;
} WrenForeignClassMethods;

// Returns a pair of pointers to the foreign methods used to allocate and
// finalize the data for instances of [className] in [module].
typedef WrenForeignClassMethods (*WrenBindForeignClassFn)(
    WrenVM* vm, const char* module, const char* className);

typedef struct
{
  // The callback Wren will use to allocate, reallocate, and deallocate memory.
  //
  // If `NULL`, defaults to a built-in function that uses `realloc` and `free`.
  WrenReallocateFn reallocateFn;

  // The callback Wren uses to load a module.
  //
  // Since Wren does not talk directly to the file system, it relies on the
  // embedder to phyisically locate and read the source code for a module. The
  // first time an import appears, Wren will call this and pass in the name of
  // the module being imported. The VM should return the soure code for that
  // module. Memory for the source should be allocated using [reallocateFn] and
  // Wren will take ownership over it.
  //
  // This will only be called once for any given module name. Wren caches the
  // result internally so subsequent imports of the same module will use the
  // previous source and not call this.
  //
  // If a module with the given name could not be found by the embedder, it
  // should return NULL and Wren will report that as a runtime error.
  WrenLoadModuleFn loadModuleFn;

  // The callback Wren uses to find a foreign method and bind it to a class.
  //
  // When a foreign method is declared in a class, this will be called with the
  // foreign method's module, class, and signature when the class body is
  // executed. It should return a pointer to the foreign function that will be
  // bound to that method.
  //
  // If the foreign function could not be found, this should return NULL and
  // Wren will report it as runtime error.
  WrenBindForeignMethodFn bindForeignMethodFn;

  // The callback Wren uses to find a foreign class and get its foreign methods.
  //
  // When a foreign class is declared, this will be called with the class's
  // module and name when the class body is executed. It should return the
  // foreign functions uses to allocate and (optionally) finalize the bytes
  // stored in the foreign object when an instance is created.
  WrenBindForeignClassFn bindForeignClassFn;

  // The callback Wren uses to display text when `System.print()` or the other
  // related functions are called.
  //
  // If this is `NULL`, Wren discards any printed text.
  WrenWriteFn writeFn;

  // The number of bytes Wren will allocate before triggering the first garbage
  // collection.
  //
  // If zero, defaults to 10MB.
  size_t initialHeapSize;

  // After a collection occurs, the threshold for the next collection is
  // determined based on the number of bytes remaining in use. This allows Wren
  // to shrink its memory usage automatically after reclaiming a large amount
  // of memory.
  //
  // This can be used to ensure that the heap does not get too small, which can
  // in turn lead to a large number of collections afterwards as the heap grows
  // back to a usable size.
  //
  // If zero, defaults to 1MB.
  size_t minHeapSize;

  // Wren will grow (and shrink) the heap automatically as the number of bytes
  // remaining in use after a collection changes. This number determines the
  // amount of additional memory Wren will use after a collection, as a
  // percentage of the current heap size.
  //
  // For example, say that this is 50. After a garbage collection, Wren there
  // are 400 bytes of memory still in use. That means the next collection will
  // be triggered after a total of 600 bytes are allocated (including the 400
  // already in use.
  //
  // Setting this to a smaller number wastes less memory, but triggers more
  // frequent garbage collections.
  //
  // If zero, defaults to 50.
  int heapGrowthPercent;
} WrenConfiguration;

typedef enum {
  WREN_RESULT_SUCCESS,
  WREN_RESULT_COMPILE_ERROR,
  WREN_RESULT_RUNTIME_ERROR
} WrenInterpretResult;

// Initializes [configuration] with all of its default values.
//
// Call this before setting the particular fields you care about.
void wrenInitConfiguration(WrenConfiguration* configuration);

// Creates a new Wren virtual machine using the given [configuration]. Wren
// will copy the configuration data, so the argument passed to this can be
// freed after calling this. If [configuration] is `NULL`, uses a default
// configuration.
WrenVM* wrenNewVM(WrenConfiguration* configuration);

// Disposes of all resources is use by [vm], which was previously created by a
// call to [wrenNewVM].
void wrenFreeVM(WrenVM* vm);

// Immediately run the garbage collector to free unused memory.
void wrenCollectGarbage(WrenVM* vm);

// Runs [source], a string of Wren source code in a new fiber in [vm].
WrenInterpretResult wrenInterpret(WrenVM* vm, const char* source);

// Creates a handle that can be used to invoke a method with [signature] on
// using a receiver and arguments that are set up on the stack.
//
// This handle can be used repeatedly to directly invoke that method from C
// code using [wrenCall].
//
// When you are done with this handle, it must be released using
// [wrenReleaseValue].
WrenValue* wrenMakeCallHandle(WrenVM* vm, const char* signature);

// Calls [method], using the receiver and arguments previously set up on the
// stack.
//
// [method] must have been created by a call to [wrenMakeCallHandle]. The
// arguments to the method must be already on the stack. The receiver should be
// in slot 0 with the remaining arguments following it, in order. It is an
// error if the number of arguments provided does not match the method's
// signature.
//
// After this returns, you can access the return value from slot 0 on the stack.
WrenInterpretResult wrenCall(WrenVM* vm, WrenValue* method);

// Releases the reference stored in [value]. After calling this, [value] can no
// longer be used.
void wrenReleaseValue(WrenVM* vm, WrenValue* value);

// The following functions are intended to be called from foreign methods or
// finalizers. The interface Wren provides to a foreign method is like a
// register machine: you are given a numbered array of slots that values can be
// read from and written to. Values always live in a slot (unless explicitly
// captured using wrenGetSlotValue(), which ensures the garbage collector can
// find them.
//
// When your foreign function is called, you are given one slot for the receiver
// and each argument to the method. The receiver is in slot 0 and the arguments
// are in increasingly numbered slots after that. You are free to read and
// write to those slots as you want. If you want more slots to use as scratch
// space, you can call wrenEnsureSlots() to add more.
//
// When your function returns, every slot except slot zero is discarded and the
// value in slot zero is used as the return value of the method. If you don't
// store a return value in that slot yourself, it will retain its previous
// value, the receiver.
//
// While Wren is dynamically typed, C is not. This means the C interface has to
// support the various types of primitive values a Wren variable can hold: bool,
// double, string, etc. If we supported this for every operation in the C API,
// there would be a combinatorial explosion of functions, like "get a
// double-valued element from a list", "insert a string key and double value
// into a map", etc.
//
// To avoid that, the only way to convert to and from a raw C value is by going
// into and out of a slot. All other functions work with values already in a
// slot. So, to add an element to a list, you put the list in one slot, and the
// element in another. Then there is a single API function wrenInsertInList()
// that takes the element out of that slot and puts it into the list.
//
// The goal of this API is to be easy to use while not compromising performance.
// The latter means it does not do type or bounds checking at runtime except
// using assertions which are generally removed from release builds. C is an
// unsafe language, so it's up to you to be careful to use it correctly. In
// return, you get a very fast FFI.

// TODO: Generalize this to look up a foreign class in any slot and place the
// object in a desired slot.
// This must be called once inside a foreign class's allocator function.
//
// It tells Wren how many bytes of raw data need to be stored in the foreign
// object and creates the new object with that size. It returns a pointer to
// the foreign object's data.
void* wrenAllocateForeign(WrenVM* vm, size_t size);

// Returns the number of slots available to the current foreign method.
int wrenGetSlotCount(WrenVM* vm);

// Ensures that the foreign method stack has at least [numSlots] available for
// use, growing the stack if needed.
//
// Does not shrink the stack if it has more than enough slots.
//
// It is an error to call this from a finalizer.
void wrenEnsureSlots(WrenVM* vm, int numSlots);

// Reads a boolean value from [slot].
//
// It is an error to call this if the slot does not contain a boolean value.
bool wrenGetSlotBool(WrenVM* vm, int slot);

// Reads a byte array from [slot].
//
// The memory for the returned string is owned by Wren. You can inspect it
// while in your foreign method, but cannot keep a pointer to it after the
// function returns, since the garbage collector may reclaim it.
//
// Returns a pointer to the first byte of the array and fill [length] with the
// number of bytes in the array.
//
// It is an error to call this if the slot does not contain a string.
const char* wrenGetSlotBytes(WrenVM* vm, int slot, int* length);

// Reads a number from [slot].
//
// It is an error to call this if the slot does not contain a number.
double wrenGetSlotDouble(WrenVM* vm, int slot);

// Reads a foreign object from [slot] and returns a pointer to the foreign data
// stored with it.
//
// It is an error to call this if the slot does not contain an instance of a
// foreign class.
void* wrenGetSlotForeign(WrenVM* vm, int slot);

// Reads a string from [slot].
//
// The memory for the returned string is owned by Wren. You can inspect it
// while in your foreign method, but cannot keep a pointer to it after the
// function returns, since the garbage collector may reclaim it.
//
// It is an error to call this if the slot does not contain a string.
const char* wrenGetSlotString(WrenVM* vm, int slot);

// Creates a handle for the value stored in [slot].
//
// This will prevent the object that is referred to from being garbage collected
// until the handle is released by calling [wrenReleaseValue()].
WrenValue* wrenGetSlotValue(WrenVM* vm, int slot);

// The following functions provide the return value for a foreign method back
// to Wren. Like above, they may only be called during a foreign call invoked
// by Wren.
//
// If none of these is called by the time the foreign function returns, the
// method implicitly returns `null`. Within a given foreign call, you may only
// call one of these once. It is an error to access any of the foreign calls
// arguments after one of these has been called.

// Stores the boolean [value] in [slot].
void wrenSetSlotBool(WrenVM* vm, int slot, bool value);

// Stores the array [length] of [bytes] in [slot].
//
// The bytes are copied to a new string within Wren's heap, so you can free
// memory used by them after this is called.
void wrenSetSlotBytes(WrenVM* vm, int slot, const char* bytes, size_t length);

// Stores the numeric [value] in [slot].
void wrenSetSlotDouble(WrenVM* vm, int slot, double value);

// Stores a new empty list in [slot].
void wrenSetSlotNewList(WrenVM* vm, int slot);

// Stores null in [slot].
void wrenSetSlotNull(WrenVM* vm, int slot);

// Stores the string [text] in [slot].
//
// The [text] is copied to a new string within Wren's heap, so you can free
// memory used by it after this is called. The length is calculated using
// [strlen()]. If the string may contain any null bytes in the middle, then you
// should use [wrenSetSlotBytes()] instead.
void wrenSetSlotString(WrenVM* vm, int slot, const char* text);

// Stores the value captured in [value] in [slot].
//
// This does not release the handle for the value.
void wrenSetSlotValue(WrenVM* vm, int slot, WrenValue* value);

// Takes the value stored at [elementSlot] and inserts it into the list stored
// at [listSlot] at [index].
//
// As in Wren, negative indexes can be used to insert from the end. To append
// an element, use `-1` for the index.
void wrenInsertInList(WrenVM* vm, int listSlot, int index, int elementSlot);

// Looks up the top level variable with [name] in [module] and stores it in
// [slot].
void wrenGetVariable(WrenVM* vm, const char* module, const char* name,
                     int slot);

#endif
