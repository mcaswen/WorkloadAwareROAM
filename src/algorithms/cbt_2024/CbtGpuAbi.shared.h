#ifndef PARALLEL_ROAM_CBT_GPU_ABI_SHARED_H
#define PARALLEL_ROAM_CBT_GPU_ABI_SHARED_H

// This macro-only header is consumed by both MSVC and DXC.
// Keep values as language-neutral integer tokens and expose typed names in each language adapter.
#define CBT_GPU_INVALID_INDEX 0xffffffffu
#define CBT_GPU_BASE_BISECTOR_COUNT 6u
#define CBT_GPU_BASE_DEPTH 4u

#define CBT_GPU_VISIBLE_FLAG 0x1u
#define CBT_GPU_MODIFIED_FLAG 0x2u

// Debug metadata shares Flags without changing the operational low bits.
// The hold window makes every-frame CBT changes perceptible in the UI.
#define CBT_GPU_SPLIT_EVENT_FLAG 0x4u
#define CBT_GPU_MERGE_EVENT_FLAG 0x8u
#define CBT_GPU_DEBUG_EVENT_MASK 0xcu
#define CBT_GPU_DEBUG_EVENT_LIFETIME_SHIFT 4u
#define CBT_GPU_DEBUG_EVENT_LIFETIME_MASK 0x1f0u
#define CBT_GPU_DEBUG_EVENT_HOLD_FRAMES 16u
#define CBT_GPU_ACTIVE_DEPTH_SHIFT 9u
#define CBT_GPU_ACTIVE_DEPTH_MASK 0x7e00u

#define CBT_GPU_UNCHANGED_ELEMENT 0u
#define CBT_GPU_BISECT_ELEMENT 1u
#define CBT_GPU_SIMPLIFY_ELEMENT 2u
#define CBT_GPU_MERGED_ELEMENT 3u

#define CBT_GPU_CLASSIFICATION_BACK_FACE_CULLED (-3)
#define CBT_GPU_CLASSIFICATION_FRUSTUM_CULLED (-2)
#define CBT_GPU_CLASSIFICATION_TOO_SMALL (-1)
#define CBT_GPU_CLASSIFICATION_UNCHANGED 0
#define CBT_GPU_CLASSIFICATION_BISECT 1

#define CBT_GPU_NO_SPLIT_PATTERN 0x00u
#define CBT_GPU_CENTER_SPLIT_PATTERN 0x01u
#define CBT_GPU_RIGHT_SPLIT_PATTERN 0x02u
#define CBT_GPU_LEFT_SPLIT_PATTERN 0x04u

// CbtBisectorData is eight tightly packed uint words in both C++ and HLSL.
#define CBT_GPU_BISECTOR_DATA_WORD_COUNT 8u
#define CBT_GPU_BISECTOR_SUBDIVISION_PATTERN_WORD 0u
#define CBT_GPU_BISECTOR_INDICES_WORD 1u
#define CBT_GPU_BISECTOR_PROBLEMATIC_NEIGHBOR_WORD 4u
#define CBT_GPU_BISECTOR_STATE_WORD 5u
#define CBT_GPU_BISECTOR_FLAGS_WORD 6u
#define CBT_GPU_BISECTOR_PROPAGATION_ID_WORD 7u

// Two four-word draw commands are followed by modified-position and active-bisector counts.
#define CBT_GPU_DRAW_STATE_WORD_COUNT 10u
#define CBT_GPU_DRAW_ACTIVE_VERTEX_COUNT_WORD 0u
#define CBT_GPU_DRAW_ACTIVE_INSTANCE_COUNT_WORD 1u
#define CBT_GPU_DRAW_ACTIVE_START_VERTEX_WORD 2u
#define CBT_GPU_DRAW_ACTIVE_START_INSTANCE_WORD 3u
#define CBT_GPU_DRAW_VISIBLE_VERTEX_COUNT_WORD 4u
#define CBT_GPU_DRAW_VISIBLE_INSTANCE_COUNT_WORD 5u
#define CBT_GPU_DRAW_VISIBLE_START_VERTEX_WORD 6u
#define CBT_GPU_DRAW_VISIBLE_START_INSTANCE_WORD 7u
#define CBT_GPU_DRAW_MODIFIED_POSITION_COUNT_WORD 8u
#define CBT_GPU_DRAW_ACTIVE_BISECTOR_COUNT_WORD 9u

// GeometryDispatch stores three consecutive D3D12_DISPATCH_ARGUMENTS records.
#define CBT_GPU_DISPATCH_ARGUMENT_WORD_COUNT 3u
#define CBT_GPU_GEOMETRY_DISPATCH_WORD_COUNT 9u
#define CBT_GPU_ACTIVE_DISPATCH_OFFSET_WORD 0u
#define CBT_GPU_ACTIVE_POSITION_DISPATCH_OFFSET_WORD 3u
#define CBT_GPU_MODIFIED_DISPATCH_OFFSET_WORD 6u

// Validation UAV layout shared by shader writes and delayed CPU readback:
// [0] first validation error code;
// [1] physical slot associated with the first error;
// [2] duplicate split claims rejected by the planner;
// [3] compatibility nodes shared by multiple split chains;
// [4] total compatibility-chain traversal steps;
// [5] longest compatibility chain observed this frame;
// [6] dynamic slots committed by Bisect;
// [7] neighbor rewrites emitted by split propagation;
// [8..11] center, right, left, and triple Bisect template counts;
// [12] legal pair or facing-pair merge groups;
// [13] dynamic sibling slots returned to the OCBT;
// [14] neighbor rewrites emitted by merge propagation;
// [15] two-node merge groups committed;
// [16] four-node merge groups committed;
// [17] active dynamic OCBT root captured before this frame mutates it;
// [18] maximum bit-length depth among the active heap IDs after indexation.
// Keep this count synchronized with D3D12CbtDiagnostics readback sizing.
#define CBT_GPU_VALIDATION_MAX_ACTIVE_DEPTH_WORD 18u
#define CBT_GPU_VALIDATION_WORD_COUNT 19u

#endif
