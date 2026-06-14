//    /                              /
//   /_____  _____  _____  _____    /  _____   _  _  _____
//  /     / /____  /____/ /____/   /  /____/  /\  / /  __
// /_____/ /____  /    / /   \    /  /    /  /  \/ /____/
// Copyright (C) 2025-2026 Zachary Mahan
// Licensed under the GNU GPL v3. See LICENSE for details.

#include "compiler/hir/generic.hpp"
#include "compiler/hir/context.hpp"
#include "compiler/hir/exec_proving.hpp"
#include "compiler/hir/indexing.hpp"
#include "compiler/hir/type.hpp"
#include "utils/hashing.hpp"
#include <cstddef>

namespace hir {

size_t hash_gen_arg(const Context& ctx, GenericArgId gen_arg_id) {
    Ovld vs{
        [&ctx](TypeId tid) -> size_t { return mix(ctx.type(tid).canonical.val()); },
        [&ctx](ExecId eid) -> size_t { return hash_exec(ctx, eid); },
    };
    return ctx.gen_arg(gen_arg_id).visit(vs);
}

bool equivalent_gen_arg(const Context& ctx, GenericArgId gid1, GenericArgId gid2) {
    const GenericArg g2 = ctx.gen_arg(gid2);
    Ovld vs{
        [&ctx, g2](TypeId tid) -> bool {
            if (!g2.holds<TypeId>()) {
                return false;
            }
            return ctx.equivalent_type(tid, g2.as<TypeId>());
        },
        [&ctx, g2](ExecId eid) -> bool {
            if (!g2.holds<ExecId>()) {
                return false;
            }
            return equivalent_exec(ctx, eid, g2.as<ExecId>());
        },
    };
    return ctx.gen_arg(gid1).visit(vs);
}

size_t hash_gen_arg_id_slice(const Context& ctx, GenericArgIdSliceId gid) {
    IdSlice<GenericArgId> slice = ctx.gen_arg_id_slice(gid);
    size_t h = 0;
    for (auto gidx = slice.begin(); gidx != slice.end(); ++gidx) {
        h = transform(h, hash_gen_arg(ctx, ctx.gen_arg_id(gidx)));
    }
    return h;
}

bool equivalent_gen_arg_id_slice(const Context& ctx, GenericArgIdSliceId sid1,
                                 GenericArgIdSliceId sid2) {
    const IdSlice<GenericArgId> s1 = ctx.gen_arg_id_slice(sid1);
    const IdSlice<GenericArgId> s2 = ctx.gen_arg_id_slice(sid2);
    if (s1.len() != s2.len()) {
        return false;
    }
    for (size_t i = 0; i < s1.len(); ++i) {
        if (!equivalent_gen_arg(ctx, ctx.gen_arg_id(s1.get(i)), ctx.gen_arg_id(s2.get(i)))) {
            return false;
        }
    }
    return true;
}

void CanonicalComptArgsTable::rehash(size_t new_capacity) {
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

bool CanonicalComptArgsTable::same_structure(GenericArgIdSliceId gid1,
                                             GenericArgIdSliceId gid2) const {
    return equivalent_gen_arg_id_slice(context, gid1, gid2);
}

size_t CanonicalComptArgsTable::hash(GenericArgIdSliceId id) const {
    return hash_gen_arg_id_slice(context, id);
}

size_t CanonicalComptArgsTable::index(size_t hash, size_t cap) { return hash & (cap - 1); }

void CanonicalComptArgsTable::put_new_head_on_chain(Entry** chain, Entry* new_entry) {
    assert(chain);
    new_entry->next = *chain;
    *chain = new_entry;
}

void CanonicalComptArgsTable::insert(GenericArgIdSliceId id, CanonicalGenericArgsId cid) {
    size_t hash_val = hash(id);
    Entry** chain = this->buckets + index(hash_val, this->capacity);
    Entry* new_entry = arena.alloc_type<Entry>();
    ::new (new_entry) Entry{id, cid, hash_val, nullptr};
    put_new_head_on_chain(chain, new_entry);
    ++this->count;
}

OptId<CanonicalGenericArgsId> CanonicalComptArgsTable::at(GenericArgIdSliceId id) const {
    size_t hash_val = hash(id);
    Entry* curr = this->buckets[index(hash_val, this->capacity)];
    while (curr) {
        if (hash_val == curr->hash && same_structure(curr->key_id, id)) {
            return curr->val;
        }
        curr = curr->next;
    }
    return {};
}

CanonicalGenericArgsId CanonicalComptArgsTable::canonical(GenericArgIdSliceId id) {
    // already cid
    OptId<CanonicalGenericArgsId> maybe_cid = this->at(id);
    if (maybe_cid.has_value()) {
        return maybe_cid.as_id();
    }
    if (static_cast<double>(this->count + 1) > LOAD_FACTOR * static_cast<double>(this->capacity)) {
        this->rehash(static_cast<size_t>(GROWTH_FACTOR * static_cast<double>(this->capacity)));
    }
    // get new cid and set backward/forward pointing:
    // forward: tid -> cid (in this table)
    // backward: cid -> tid (first mention, for structural reversal of any abitrary cid)
    const CanonicalGenericArgsId new_cid = context.emplace_and_get_canonical_gen_args_slice_id(id);
    this->insert(id, new_cid);
    return new_cid;
}

} // namespace hir
