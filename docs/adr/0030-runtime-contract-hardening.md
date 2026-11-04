# ADR 0030: Runtime handle isolation and transactional ownership

Status: Proposed; implementation under review. Owner request: complete the runtime review follow-ups.

## Contract

Runtime handles include an opaque, nonzero, process-local owner token as well as slot and generation. Foreign handles are refused by every lookup and mutation. Moving an unlinked ECS world transfers its token; a reused moved-from world obtains a new token. Tokens never enter persisted data, replay hashes, or ordering. Persistent entity and schema identity are a separate scene contract.

ECS components must have nonthrowing move construction, move assignment and destruction. Constructors and allocation may throw. Add stages construction and allocation before publishing; failed add may invalidate pointers but leaves component membership unchanged. Entity creation reserves all slot and free-list storage before publishing, so destruction does not allocate. Removal callbacks must not throw. Callback order follows pool registration in that world, independent of process-wide type lookup order. Moves transfer storage with allocation-free swaps. MSVC Debug fault tests preconstruct empty component pools because its noexcept vector constructors allocate debug iterator proxies; Release/Linux exercise cold pool allocation failures too.

Subsystem-owned removal hooks are claimed as a pair, exclusively and atomically. Ordinary hook replacement cannot erase them, and worlds with owned hooks cannot move. The physics world must outlive its linked ECS world; the helper may die earlier. Constructing another link, linking while locked, or linking over preexisting ownership components fails explicitly. A physics component has exactly one owner and cannot be copied. Body and character ownership are mutually exclusive on an entity. Attachment restores prior entity state and destroys the pending physics object if construction fails.

A deferred creation rolls back its new entity if setup throws. Other setup side effects remain the caller's responsibility. Applying a buffer consumes the failing command and preserves later commands before commands appended during application; already applied commands stay applied. Exceptions propagate without silently losing the remaining work.

## Consequences and checks

This intentionally changes public runtime handle layout and tightens component requirements. These handles have no serialized compatibility promise. Tests must cover two simultaneous worlds, moved-from reuse, locked/duplicate link creation, exclusive cleanup hooks, throwing construction and allocation failure. Existing physics state hashes must remain unchanged.
