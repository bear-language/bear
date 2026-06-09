//     /                              /
//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/type.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec_ops.hpp"
#include "compiler/hir/indexing.hpp"
#include "utils/hashing.hpp"
#include <iostream>
#include <utility>

namespace hir {

template <ConsiderMut C> bool TypeComparator<C>::operator()(const Type& t1, const Type& t2) const {
    auto vs = Ovld{
        [&t2](const TypeBuiltin& t) -> bool {
            if (!t2.holds<TypeBuiltin>()) {
                return false;
            }
            return t.type == t2.as<TypeBuiltin>().type;
        },
        [&t2](const TypeStruct& t) -> bool {
            if (!t2.holds<TypeStruct>()) {
                return false;
            }
            return t.def_id == t2.as<TypeStruct>().def_id;
        },
        [&t2](const TypeVariant& t) -> bool {
            if (!t2.holds<TypeVariant>()) {
                return false;
            }
            return t.def_id == t2.as<TypeVariant>().def_id;
        },
        [&t2](const TypeUnion& t) -> bool {
            if (!t2.holds<TypeUnion>()) {
                return false;
            }
            return t.def_id == t2.as<TypeUnion>().def_id;
        },
        [&t2](const TypeDeftype&) -> bool { return t2.holds<TypeDeftype>(); },
        [&t2](const TypeArr& t) -> bool {
            if (!t2.holds<TypeArr>()) {
                return false;
            }
            return t.canonical_size == t2.as<TypeArr>().canonical_size;
        },
        [&t2](const TypeSlice&) -> bool { return t2.holds<TypeSlice>(); },
        [&t2](const TypeRef&) -> bool { return t2.holds<TypeRef>(); },
        [&t2](const TypePtr&) -> bool { return t2.holds<TypePtr>(); },
        [this, &t2](const TypeFnPtr& t) -> bool {
            if (!t2.holds<TypeFnPtr>()) {
                return false;
            }
            bool rt_match = false;
            if (t.return_type.has_value() && t2.as<TypeFnPtr>().return_type.has_value()) {
                rt_match = context.equivalent_type(t.return_type.as_id(),
                                                   t2.as<TypeFnPtr>().return_type.as_id());
            } else {
                rt_match = t.return_type.empty() && t2.as<TypeFnPtr>().return_type.empty();
            }
            bool arity_match = t.param_types.len() == t2.as<TypeFnPtr>().param_types.len();
            if (!rt_match || !arity_match) {
                return false;
            }
            auto t_start = t.param_types.begin();
            auto t2_start = t2.as<TypeFnPtr>().param_types.begin();
            auto len
                = t.param_types.len(); // guranteed to match since we already passed arity check
            // iterate thru params and return false on mismatch
            for (HirSize i = 0; i < len; i++) {
                auto tid = context.type_id(t_start.at(i));
                auto tid2 = context.type_id(t2_start.at(i));
                if (!TypeTransformer<TypeComparator>{context}(tid, tid2)) {
                    return false;
                }
            }
            // all matched, so return true
            return true;
        },
        [&t2](const TypeVariadic&) -> bool { return t2.holds<TypeVariadic>(); },
        [&t2](const TypeVar&) -> bool { return t2.holds<TypeVar>(); },

    };
    if constexpr (considers_mut()) {
        if (t1.mut != t2.mut) {
            return false;
        }
    }
    return t1.visit(vs);
}

template <ConsiderMut C> size_t TypeHasher<C>::operator()(const Type& t1) const {
    auto vs = Ovld{
        [&](const TypeBuiltin& t) -> size_t { return mix(0x01ULL ^ static_cast<size_t>(t.type)); },
        [&](const TypeStruct& t) -> size_t {
            return mix(0x02ULL ^ static_cast<size_t>(t.def_id.val()));
        },
        [&](const TypeDeftype&) -> size_t {
            std::cout << ("tried to directly hash a deftype, do not use `as_mentioned` invocations "
                          "when hashing!")
                      << '\n';
            std::unreachable();
            return 0ULL;
        },
        [&](const TypeArr& t) -> size_t {
            return mix(0x04ULL ^ static_cast<size_t>(t.canonical_size));
        },
        [&](const TypeSlice&) -> size_t { return mix(0x05ULL); },
        [&](const TypeRef&) -> size_t { return mix(0x06ULL); },
        [&](const TypePtr&) -> size_t { return mix(0x07ULL); },
        [&](const TypeFnPtr& t) -> size_t {
            size_t h = 0;
            if (t.return_type.has_value()) {
                h = mix(0x08ULL ^ TypeTransformer<TypeHasher>{context}(t.return_type.as_id()));
            }
            h ^= static_cast<size_t>(t.param_types.len()) * 0x9e3779b97f4a7c15ULL;
            for (auto tidx = t.param_types.begin(); tidx != t.param_types.end(); tidx++) {
                auto tid = context.type_id(tidx);
                h = transform(h, TypeTransformer<TypeHasher>{context}(tid));
            }
            return mix(h);
        },
        [&](const TypeVariadic&) -> size_t { return mix(0x09ULL); },
        [&](const TypeVar&) -> size_t { return mix(0x10ULL); },
        [&](const TypeUnion& t) -> size_t {
            return mix(0x11ULL ^ static_cast<size_t>(t.def_id.val()));
        },
        [&](const TypeVariant& t) -> size_t {
            return mix(0x12ULL ^ static_cast<size_t>(t.def_id.val()));
        }};

    size_t h = t1.visit(vs);

    if constexpr (considers_mut()) {
        if (t1.mut) {
            h ^= 0x9e3779b97f4a7c15ULL;
        }
    }

    return mix(h);
}
template <ConsiderMut C> size_t TypeHasher<C>::transform(size_t res1, size_t res2) {
    // high entropy hash transform
    // https://stackoverflow.com/questions/35985960/c-why-is-boosthash-combine-the-best-way-to-combine-hash-values
    return res1 ^ (res2 + 0x9e3779b97f4a7c15ULL + (res1 << 6) + (res1 >> 2));
}

template <ConsiderMut C> TypeToStringValue TypeToString<C>::operator()(const Type& t1) const {
    TypeToStringValue val{.str = std::string{}};
    val.str.reserve(64); // decently sized
    std::string& str = val.str;
    auto vs = Ovld{
        [&](const TypeBuiltin& t) {
            str += builtin_type_to_cstr(t.type);
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
        },
        [&](const TypeStruct& t) {
            str += context.symbol_id_to_cstr(context.def(t.def_id).name);
            if (t.maybe_canon_gen_args_id.has_value()) {
                str += "::<>"; // TODO properly handle gen args
            }
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
        },
        [&](const TypeVariant& t) {
            str += context.symbol_id_to_cstr(context.def(t.def_id).name);
            if (t.maybe_canon_gen_args_id.has_value()) {
                str += "::<>"; // TODO properly handle gen args
            }
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
        },
        [&](const TypeUnion& t) {
            str += context.symbol_id_to_cstr(context.def(t.def_id).name);
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
        },
        [&](const TypeDeftype& t) {
            str += context.symbol_id_to_cstr(context.def(t.definition).name);
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
        },
        [&](const TypeArr& t) {
            str += '[';
            str += std::to_string(t.canonical_size);
            str += ']';
        },
        [&](const TypeSlice&) {
            str += '[';
            str += '&';
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += "mut";
                }
            }
            str += ']';
        },
        [&](const TypeRef&) {
            str += '&';
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += "mut ";
                }
            }
        },
        [&](const TypePtr&) {
            str += '*';
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += "mut ";
                }
            }
        },
        [&](const TypeFnPtr& t) {
            str += "*fn";
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
            str += '(';
            for (auto tidx = t.param_types.begin(); tidx != t.param_types.end(); tidx++) {
                auto tid = context.type_id(tidx);
                str += TypeTransformer<TypeToString>{context}(tid).str;
                if (tidx != t.param_types.last_elem()) {
                    str += ", ";
                }
            }
            str += ')';
            if (t.return_type.has_value()) {
                str += "->";
                str += TypeTransformer<TypeToString>{context}(t.return_type.as_id()).str;
            }
        },
        [&](const TypeVariadic&) { str += "..."; },
        [&](const TypeVar&) {
            str += "var";
            if constexpr (considers_mut()) {
                if (t1.mut) {
                    str += " mut";
                }
            }
        },

    };

    t1.visit(vs);

    return val;
}

template <ConsiderMut C>
TypeToStringValue TypeToString<C>::transform(TypeToStringValue res1, TypeToStringValue res2) {
    return TypeToStringValue{.str = std::move(res1.str) + std::move(res2.str)};
}

template <TypeTransformerFunctor F> OptId<TypeId> TypeTransformer<F>::try_inner(const Type& type) {
    using OTid = OptId<TypeId>;
    auto vs = Ovld{
        [&](const TypeBuiltin&) -> OTid { return OTid{}; },
        [&](const TypeStruct&) -> OTid { return OTid{}; },
        [&](const TypeVariant&) -> OTid { return OTid{}; },
        [&](const TypeUnion&) -> OTid { return OTid{}; },
        [&](const TypeDeftype&) -> OTid { return OTid{}; },
        [&](const TypeArr& t) -> OTid { return t.inner; },
        [&](const TypeSlice& t) -> OTid { return t.inner; },
        [&](const TypeRef& t) -> OTid { return t.inner; },
        [&](const TypePtr& t) -> OTid { return t.inner; },
        [&](const TypeFnPtr&) -> OTid { return OTid{}; },
        [&](const TypeVariadic& t) -> OTid { return t.inner; },
        [&](const TypeVar&) -> OTid { return OTid{}; },
    };
    return type.visit(vs);
}
template <TypeTransformerFunctor F> Type TypeTransformer<F>::get_type(TypeId tid) const noexcept {
    return context.type(tid);
};

template <TypeTransformerFunctor F>
Type TypeTransformer<F>::get_type_as_mentioned(TypeId tid) const noexcept {
    return context.type_as_mentioned(tid);
};

template <TypeTransformerFunctor F>
typename F::value_type TypeTransformer<F>::invoke(TypeId tid, auto get_type_functor) {
    auto t = get_type_functor(tid);

    const auto invoker = F{context};

    // get first value up-front (prevents collection bugs from a default initialized collector)
    typename F::value_type collector = invoker(t);

    OptId<TypeId> maybe_tid{tid};

    while (true) {
        maybe_tid = try_inner(t);
        if (maybe_tid.has_value()) {
            t = get_type_functor(maybe_tid.as_id());
        }
        if (!maybe_tid.has_value()) {
            break;
        }
        collector = invoker.transform(collector, invoker(t));
    }
    return collector;
}

template <TypeTransformerFunctor F>
typename F::value_type TypeTransformer<F>::invoke(TypeId tid1, TypeId tid2, auto get_type_functor) {

    Type t1 = get_type_functor(tid1);
    Type t2 = get_type_functor(tid2);

    const auto invoker = F{context};

    // get first value up-front (prevents collection bugs from a default initialized collector)
    typename F::value_type collector = invoker(t1, t2);

    OptId<TypeId> maybe_tid1{tid1};
    OptId<TypeId> maybe_tid2{tid2};

    while (true) {
        maybe_tid1 = try_inner(t1);
        maybe_tid2 = try_inner(t2);
        if (maybe_tid1.has_value()) {
            t1 = get_type_functor(maybe_tid1.as_id());
        }
        if (maybe_tid2.has_value()) {
            t2 = get_type_functor(maybe_tid2.as_id());
        }
        if (!maybe_tid1.has_value() || !maybe_tid2.has_value()) {
            break;
        }
        collector = invoker.transform(collector, invoker(t1, t2));
    }
    // neither have a value, so return the collector
    if (!maybe_tid1.has_value() && !maybe_tid2.has_value()) {
        return collector;
    }
    // return conditional single-invocation
    return maybe_tid1.has_value() ? invoker.transform(collector, invoker(t1))
                                  : invoker.transform(collector, invoker(t2));
}

template <TypeTransformerFunctor F>
typename F::value_type TypeTransformer<F>::operator()(TypeId tid1, TypeId tid2) {
    return invoke(tid1, tid2, [this](TypeId tid) { return get_type(tid); });
}
template <TypeTransformerFunctor F>
typename F::value_type TypeTransformer<F>::operator()(TypeId tid) {
    return invoke(tid, [this](TypeId tid) { return get_type(tid); });
}

template <TypeTransformerFunctor F>
typename F::value_type TypeTransformer<F>::invoke_as_mentioned(TypeId tid) {
    return invoke(tid, [this](TypeId tid) { return get_type_as_mentioned(tid); });
}

template <TypeTransformerFunctor F>
typename F::value_type TypeTransformer<F>::invoke_as_mentioned(TypeId tid1, TypeId tid2) {
    return invoke(tid1, tid2, [this](TypeId tid) { return get_type_as_mentioned(tid); });
}

// explicit instatiantiations for the TypeTransformer
template class TypeTransformer<TypeHasher<DoConsiderMut>>;
template class TypeTransformer<TypeHasher<DoNotConsiderMut>>;

template class TypeTransformer<TypeComparator<DoConsiderMut>>;
template class TypeTransformer<TypeComparator<DoNotConsiderMut>>;

template class TypeTransformer<TypeInferer<DoConsiderMut>>;
template class TypeTransformer<TypeInferer<DoNotConsiderMut>>;

template class TypeTransformer<TypeToString<DoConsiderMut>>;
template class TypeTransformer<TypeToString<DoNotConsiderMut>>;

template class TypeTransformer<TypeContainsVar>;
template class TypeTransformer<TypeContainsDeftype>;

CanonicalTypeTable::CanonicalTypeTable(Context& context, DataArena& arena, HirSize capacity)
    : context(context), arena(arena), count{0} {
    this->capacity = (capacity > DEFAULT_CAP) ? capacity : DEFAULT_CAP;
    buckets = arena.alloc_as<Entry**>(this->capacity * sizeof(Entry*));

    // zero-init buckets
    memset(static_cast<void*>(buckets), 0, this->capacity * sizeof(Entry*));
}

size_t CanonicalTypeTable::hash(TypeId type) const {
    return TypeTransformer<TypeHasher<considerer_type>>{context}(type);
}

bool CanonicalTypeTable::same_structure(TypeId tid1, TypeId tid2) const {
    return TypeTransformer<TypeComparator<considerer_type>>{context}(tid1, tid2);
}

size_t CanonicalTypeTable::index(size_t hash, size_t cap) { return hash & (cap - 1); }
void CanonicalTypeTable::put_new_head_on_chain(Entry** chain, Entry* new_entry) {
    assert(chain);
    new_entry->next = *chain;
    *chain = new_entry;
}
void CanonicalTypeTable::rehash(size_t new_capacity) {
    Entry** new_buckets = arena.alloc_as<Entry**>(sizeof(Entry*) * new_capacity);
    memset(static_cast<void*>(new_buckets), 0, new_capacity * sizeof(Entry*));
    for (size_t i = 0; i < this->capacity; i++) {
        Entry* curr = this->buckets[i];
        while (curr) {
            Entry* next = curr->next;
            // just move curr into the new chain
            put_new_head_on_chain(new_buckets + index(curr->hash, new_capacity), curr);
            curr = next;
        }
    }
    this->capacity = new_capacity;
    this->buckets = new_buckets; // don't delete old buckets since arena will clean up later
}

OptId<CanonicalTypeId> CanonicalTypeTable::at(TypeId tid) const {
    size_t hash_val = hash(tid);
    Entry* curr = this->buckets[index(hash_val, this->capacity)];
    while (curr) {
        if (hash_val == curr->hash && same_structure(curr->key_id, tid)) {
            return curr->val;
        }
        curr = curr->next;
    }
    return OptId<CanonicalTypeId>{};
}

void CanonicalTypeTable::insert(TypeId tid, CanonicalTypeId cid) {
    size_t hash_val = hash(tid);
    Entry** chain = this->buckets + index(hash_val, this->capacity);
    Entry* new_entry = arena.alloc_type<Entry>();
    ::new (new_entry) Entry{tid, cid, hash_val, nullptr};
    put_new_head_on_chain(chain, new_entry);
    ++this->count;
}
CanonicalTypeId CanonicalTypeTable::canonical(TypeId tid) {
    // already cid
    OptId<CanonicalTypeId> maybe_cid = this->at(tid);
    if (maybe_cid.has_value()) {
        return maybe_cid.as_id();
    }
    if (static_cast<double>(this->count + 1) > LOAD_FACTOR * static_cast<double>(this->capacity)) {
        this->rehash(static_cast<size_t>(GROWTH_FACTOR * static_cast<double>(this->capacity)));
    }
    // get new cid and set backward/forward pointing:
    // forward: tid -> cid (in this table)
    // backward: cid -> tid (first mention, for structural reversal of any abitrary cid)
    CanonicalTypeId new_cid = context.emplace_and_get_canonical_type_id(tid);
    this->insert(tid, new_cid);
    return new_cid;
}

const char* builtin_type_to_cstr(builtin_type t) {
    switch (t) {
    case builtin_type::u8:
        return "u8";
    case builtin_type::i8:
        return "i8";
    case builtin_type::u16:
        return "u16";
    case builtin_type::i16:
        return "i16";
    case builtin_type::u32:
        return "u32";
    case builtin_type::i32:
        return "i32";
    case builtin_type::u64:
        return "u64";
    case builtin_type::i64:
        return "i64";
    case builtin_type::usize:
        return "usize";
    case builtin_type::charr:
        return "char";
    case builtin_type::f32:
        return "f32";
    case builtin_type::f64:
        return "f64";
    case builtin_type::voidd:
        return "void";
    case builtin_type::str:
        return "str";
        break;
    case builtin_type::nullpointer:
        return "nullptr";
        break;
    case builtin_type::boolean:
        return "bool";
        break;
    }
    std::unreachable();
    return nullptr;
}
bool Type::is_same(const Context& ctx, TypeId tid1, TypeId tid2) {
    return ctx.type(tid1).canonical == ctx.type(tid2).canonical;
    // to check structurally:
    // return TypeTransformer<TypeComparator<DoConsiderMut>>{ctx}(tid1, tid2);
}

std::optional<builtin_type> id_tkn_slice_to_maybe_builtin(token_ptr_slice_t tkn_slice) {
    if (tkn_slice.len != 1) {
        return std::optional<builtin_type>{};
    }
    switch (tkn_slice.start[0]->type) {
    case TOK_I8:
        return builtin_type::i8;
    case TOK_U8:
        return builtin_type::u8;
    case TOK_I16:
        return builtin_type::i16;
    case TOK_U16:
        return builtin_type::u16;
    case TOK_I32:
        return builtin_type::i32;
    case TOK_U32:
        return builtin_type::u32;
    case TOK_I64:
        return builtin_type::i64;
    case TOK_U64:
        return builtin_type::u64;
    case TOK_USIZE:
        return builtin_type::usize;
    case TOK_CHAR:
        return builtin_type::charr;
    case TOK_F32:
        return builtin_type::f32;
    case TOK_F64:
        return builtin_type::f64;
    case TOK_STR:
        return builtin_type::str;
    case TOK_BOOL:
        return builtin_type::boolean;
    case TOK_VOID:
        return builtin_type::voidd;
    default:
        break;
    }
    return std::optional<builtin_type>{};
}

bool contains_mut(const Context& ctx, TypeId tid) {
    return TypeTransformer<TypeContainsMut>{ctx}(tid);
}

bool contains_deftype(const Context& ctx, TypeId tid) {
    return TypeTransformer<TypeContainsDeftype>{ctx}.invoke_as_mentioned(tid);
}

bool TypeContainsMut::operator()(const Type& t1) const { return t1.mut; }

template <ConsiderMut C> std::string type_to_string_impl_with_akas(const Context& ctx, TypeId tid) {
    std::string str_with_true_type{};
    str_with_true_type.reserve(128); // decently sized
    str_with_true_type += TypeTransformer<TypeToString<C>>{ctx}(tid).str;
    if (TypeTransformer<TypeContainsDeftype>{ctx}.invoke_as_mentioned(tid)) {
        std::string str_with_aka{};
        str_with_aka.reserve(128);
        str_with_aka += TypeTransformer<TypeToString<C>>{ctx}.invoke_as_mentioned(tid).str;
        str_with_aka += " aka ";
        str_with_aka += str_with_true_type;
        return str_with_aka;
    }
    return str_with_true_type;
}

std::string type_to_string_with_akas(const Context& ctx, TypeId tid) {
    return type_to_string_impl_with_akas<DoConsiderMut>(ctx, tid);
}

std::string type_to_string_with_akas_without_muts(const Context& ctx, TypeId tid) {
    return type_to_string_impl_with_akas<DoNotConsiderMut>(ctx, tid);
}

// converts a TypeId to a string
std::string type_to_string(const Context& ctx, TypeId tid) {
    return TypeTransformer<TypeToString<DoConsiderMut>>{ctx}(tid).str;
}
// converts a TypeId to a string without any muts
std::string type_to_string_without_muts(const Context& ctx, TypeId tid) {
    return TypeTransformer<TypeToString<DoNotConsiderMut>>{ctx}(tid).str;
}

// converts a TypeId to a string
std::string type_to_string_as_mentioned(const Context& ctx, TypeId tid) {
    return TypeTransformer<TypeToString<DoConsiderMut>>{ctx}.invoke_as_mentioned(tid).str;
}
// converts a TypeId to a string without any muts
std::string type_to_string_as_mentioned_without_muts(const Context& ctx, TypeId tid) {
    return TypeTransformer<TypeToString<DoNotConsiderMut>>{ctx}.invoke_as_mentioned(tid).str;
}

bool builtin_type_has_binary_op(builtin_type type, binary_op op) {
    switch (type) {
    case builtin_type::str:
        switch (op) {
        case binary_op::plus:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return true;
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::right_shift_arithmetic:
        case binary_op::bool_or:
        case binary_op::bool_and:
            return false;
        }
        break;
    // i8
    case builtin_type::i8:
    // i16
    case builtin_type::i16:
    // i32
    case builtin_type::i32:
    // i64
    case builtin_type::i64: {
        switch (op) {
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::left_bitshift:
        case binary_op::right_shift_arithmetic:
        case binary_op::bool_or:
        case binary_op::bool_and:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return true;
        case binary_op::right_shift_logical:
            return false;
        }
        break;
    }
    // u8
    case builtin_type::u8:
    // u16
    case builtin_type::u16:
    // u32
    case builtin_type::u32:
    // u64
    case builtin_type::usize:
    case builtin_type::u64: {
        switch (op) {
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::bool_or:
        case binary_op::bool_and:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return true;
        case binary_op::right_shift_arithmetic:
            return false;
        }
        break;
    }
    // char
    case builtin_type::charr: {
        switch (op) {
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::right_shift_arithmetic:
            return false;
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::bool_or:
        case binary_op::bool_and:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return true;
        }

        break;
    }
    case builtin_type::f32:
    case builtin_type::f64: {
        switch (op) {
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return true;
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::right_shift_arithmetic:
        case binary_op::bool_or:
        case binary_op::bool_and:
            return false;
        }
        break;
    }
    // nullptr
    case builtin_type::nullpointer: {
        switch (op) {
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::right_shift_arithmetic:
        case binary_op::bool_or:
        case binary_op::bool_and:
            return false;
        }
        break;
    }
    // bool
    case builtin_type::boolean: {
        switch (op) {
        case binary_op::bool_or:
        case binary_op::bool_and:
        case binary_op::bool_equal:
        case binary_op::bool_not_equal:
            return true;
        case binary_op::plus:
        case binary_op::minus:
        case binary_op::multiply:
        case binary_op::divide:
        case binary_op::greater_than:
        case binary_op::less_than:
        case binary_op::greater_than_or_equal:
        case binary_op::less_than_or_equal:
        case binary_op::modulo:
        case binary_op::bit_or:
        case binary_op::bit_and:
        case binary_op::bit_xor:
        case binary_op::left_bitshift:
        case binary_op::right_shift_logical:
        case binary_op::right_shift_arithmetic:
            return false;
        }
        break;
    }
    case builtin_type::voidd:
        return false;
    }
    return false;
}

bool builtin_type_has_unary_op(builtin_type type, unary_op op) {
    switch (type) {
    // str
    case builtin_type::str:
        return false;

    case builtin_type::i8:
    case builtin_type::i16:
    case builtin_type::i32:
    case builtin_type::i64:
        return op != unary_op::bool_not;

    case builtin_type::u8:
    case builtin_type::u16:
    case builtin_type::u32:
    case builtin_type::u64:
        return op != unary_op::minus && op != unary_op::bool_not;

    // char
    case builtin_type::charr:
        return false;
    // f32
    case builtin_type::f32:
    // f64
    case builtin_type::f64: {
        switch (op) {
        case unary_op::plus:
        case unary_op::minus:
            return true;
        case unary_op::inc:
        case unary_op::dec:
        case unary_op::bool_not:
        case unary_op::bit_not:
            return false;
        }
        break;
    }
    // nullptr
    case builtin_type::nullpointer: {
        return false;
        break;
    }
    // bool
    case builtin_type::boolean: {
        return op == unary_op::bool_not;
        break;
    }
    case builtin_type::usize:
    case builtin_type::voidd:
        return false;
    }
    return false;
}

template <ConsiderMut C> bool TypeInferer<C>::operator()(const Type& t1, const Type& t2) const {

    TypeComparator<C> comparator{context};

    auto vs = Ovld{
        [&](const TypeBuiltin& t) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeStruct& t) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeVariant& t) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeUnion& t) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeDeftype&) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeArr& t) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeSlice&) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeRef&) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypePtr&) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [this, &t2](const TypeFnPtr& t) -> bool {
            if (!t2.holds<TypeFnPtr>()) {
                return false;
            }
            bool rt_match = false;
            if (t.return_type.has_value() && t2.as<TypeFnPtr>().return_type.has_value()) {
                rt_match = context.type_inferrable_as(t.return_type.as_id(),
                                                      t2.as<TypeFnPtr>().return_type.as_id())
                           || context.equivalent_type(t.return_type.as_id(),
                                                      t2.as<TypeFnPtr>().return_type.as_id());
            } else {
                rt_match = t.return_type.empty() && t2.as<TypeFnPtr>().return_type.empty();
            }
            bool arity_match = t.param_types.len() == t2.as<TypeFnPtr>().param_types.len();
            if (!rt_match || !arity_match) {
                return false;
            }
            auto t_start = t.param_types.begin();
            auto t2_start = t2.as<TypeFnPtr>().param_types.begin();
            auto len = t.param_types.len();
            for (HirSize i = 0; i < len; i++) {
                auto tid = context.type_id(t_start.at(i));
                auto tid2 = context.type_id(t2_start.at(i));
                if (!TypeTransformer<TypeInferer<C>>{context}(tid, tid2)) {
                    return false;
                }
            }
            // all matched, so return true
            return true;
        },
        [&](const TypeVariadic&) -> bool { return t2.holds<TypeVar>() || comparator(t1, t2); },
        [&](const TypeVar&) -> bool { return true; },

    };
    if constexpr (considers_mut()) {
        if (t1.mut != t2.mut) {
            return false;
        }
    }
    return t1.visit(vs);
}

} // namespace hir
