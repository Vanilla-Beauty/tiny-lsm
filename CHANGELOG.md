# CHANGELOG

## [v0.0.1] - 2026-02-28

### Bug Fixes

#### 1. Compaction discards MVCC versions (`keep_all_versions` flag)

**Problem**: The compaction path reused query-path iterators. Those iterators skip older `tranc_id` entries for the same key in `operator++` (MVCC dedup logic), permanently dropping historical versions during compaction.

**Impact**: After compaction, querying a key with an earlier `tranc_id` may return no result even though that version was written, breaking snapshot-read semantics.

**Fix**: Added a `bool keep_all_versions = false` parameter to every layer of the iterator chain. Defaults to `false` so all existing query-path call sites are unaffected. Compaction call sites pass `true` to suppress same-key dedup:

- `BlockIterator` (`include/block/block_iterator.h`, `src/block/block_iterator.cpp`): guard the same-key skip `while` loop in `operator++` with `keep_all_versions_`.
- `HeapIterator` (`include/iterator/iterator.h`, `src/iterator/iterator.cpp`): guard the same-key pop loops in `operator++` and in the constructor's tombstone-skip logic.
- `SstIterator` (`include/sst/sst_iterator.h`, `src/sst/sst_iterator.cpp`): propagate `keep_all_versions` to `BlockIterator`. Also fixed a bug in `merge_sst_iterator` where the `tranc_id` pushed onto the heap was hard-coded to `0`; it now reads each record's own `get_cur_tranc_id()`.
- `TwoMergeIterator` (`include/lsm/two_merge_iterator.h`, `src/lsm/two_merge_iterator.cpp`): `skip_it_b()` returns immediately in `keep_all_versions` mode; `choose_it_a()` orders same-key entries by descending `tranc_id` in that mode so block entries stay in the order expected by `Block::adjust_idx_by_tranc_id`.
- `ConcactIterator` (`include/sst/concact_iterator.h`, `src/sst/concact_iterator.cpp`): propagate `keep_all_versions` to `SST::begin`.
- `SST::begin` (`include/sst/sst.h`, `src/sst/sst.cpp`): accept and forward `keep_all_versions` to `SstIterator`.
- `LSMEngine::full_l0_l1_compact` / `full_common_compact` (`src/lsm/engine.cpp`): pass `keep_all_versions = true` at every iterator construction site.

#### 2. Tombstone lost after compaction

**Problem**: In `gen_sst_from_iter`, when the SST size threshold is reached immediately after writing a DELETE entry, the iterator has already advanced to the matching PUT entry (lower `tranc_id`). The flush fires, putting DELETE in SST-N and PUT in SST-N+1. Both SSTs cover the same key in their range, violating the L1+ non-overlapping invariant. `LSMEngine::get`'s binary search may land on SST-N+1 (PUT) and return the value, never reaching SST-N (DELETE).

**Impact**: `LSMTest.BigPersistence2` and `LSMTest.SmallConfigLargeDataPersistent` failed — keys deleted via `remove()` remained visible after compaction.

**Fix** (`src/lsm/engine.cpp`, `gen_sst_from_iter`): before deciding whether to flush the current SST, check whether the next iterator entry shares the same key. If so, defer the flush until all versions of that key have been added:

```cpp
bool next_is_same_key =
    iter.is_valid() && !iter.is_end() && (*iter).first == cur_key;
if (!next_is_same_key &&
    new_sst_builder.estimated_size() >= target_sst_size) {
    // flush SST ...
}
```

### Tests

Four new `CompactTest` cases added in `test/test_compact.cpp`:

| Test | What it verifies |
|------|-----------------|
| `MultiVersionSurvivedAfterL0Compact` | After L0→L1 compaction, three MVCC versions (V1/V2/V3) of the same key are each readable with the corresponding `tranc_id` |
| `MultiVersionSurvivedAfterL1Compact` | MVCC versions remain readable after L1→L2 compaction |
| `TombstoneSurvivedAfterL0Compact` | With small memory config (mirrors `SmallConfigLargeDataPersistent`), deleted keys are not visible after compaction |
| `TombstoneDefaultConfig` | With default config (mirrors `BigPersistence2`, 1 M keys), deleted keys are not visible after compaction |

### Build

- Fixed a typo in `xmake.lua`: `$(builddir)` → `$(buildir)`, allowing the `run-all-tests` aggregate target to locate test binaries correctly.
