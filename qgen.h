#ifndef QGEN_H
#define QGEN_H

#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>

/*
    About impl flags:
    - QGEN_NON_OPAQUE: use open struct definitions
    - QGEN_INTERNAL: show internal defines etc.

    - QGEN_SHARED: Use as a shared library.
    - QGEN_BUILD: Library is being compiled.
*/

#ifndef QGEN_SHARED
    #define QGEN_EXPORT
#else
    #if defined(_WIN32) && !defined(_MSC_VER)
        #if defined(QGEN_BUILD)
            #define QGEN_EXPORT __attribute__((dllexport))
        #else
            #define QGEN_EXPORT __attribute__((dllimport))
        #endif
    #elif defined(_WIN32)
        #if defined(QGEN_BUILD)
            #define QGEN_EXPORT __declspec(dllexport)
        #else
            #define QGEN_EXPORT __declspec(dllimport)
        #endif
    #elif defined(__GNUC__) || defined(__clang__)
        #if defined(QGEN_BUILD)
            #define QGEN_EXPORT __attribute__((visibility("default")))
        #else
            #define QGEN_EXPORT
        #endif
    #else
        #define QGEN_EXPORT
    #endif
#endif

#define QGEN_DEFAULT_ARRAY_LIST_CAP 0x1000

#define __QGEN_STR(x) #x
#define QGEN_STR(x) __QGEN_STR(x)
#define __QGEN_CONCAT(x, y) x##y
#define QGEN_CONCAT(x, y) __QGEN_CONCAT(x, y)

typedef struct qgen_bucket qgen_bucket_t;
typedef struct qgen_otree qgen_otree_t;

typedef struct qgen_bitpattern {
    uint8_t width;
    uint8_t __Reserved0[7];
    uint64_t __Reserved1;
    uint64_t mask_low, mask_high;
    uint64_t active_low, active_high;
} qgen_bitpattern_t;

inline static size_t qgen_get_bitpattern_size() {
    return sizeof(qgen_bitpattern_t);
}

#define QGEN_BP_FMT "1\"%*" PRIX64 "%*" PRIX64 "\" | m\"%*" PRIX64 "%*" PRIX64 "\""
#define QGEN_BP_DISPLAY(bp) \
    bp.width > 64 ? (int)(bp.width - 64) / 4 : 0, bp.active_high,\
    bp.width > 64 ? 16 : (int)bp.width / 4, bp.active_low,\
    bp.width > 64 ? (int)(bp.width - 64) / 4 : 0, bp.mask_high,\
    bp.width > 64 ? 16 : (int)bp.width / 4, bp.mask_low

typedef uint64_t qgen_otree_node_t;

#define QGEN_NODE_INTERMEDIATE(split_bit_index, weight, left_child, right_child) (uint64_t) ((((uint64_t) (split_bit_index) & 0x7fULL) << 56ULL) | (((uint64_t) (weight) & 0xffffULL) << 32ULL) | (((uint64_t) (left_child) & 0xffffULL) << 16) | ((uint64_t) (right_child) & 0xffffULL))
#define QGEN_NODE_LEAF(weight, bucket_index) (uint64_t) ((1ULL << 63ULL) | (((uint64_t) (weight) & 0xffffULL) << 32ULL) | ((uint64_t) (bucket_index) & 0xffffULL))

#define QGEN_NODE_TYPE_MASK (1ULL << 63ULL)
#define QGEN_NODE_IS_LEAF(node) ((node) & QGEN_NODE_TYPE_MASK)
#define QGEN_NODE_IS_INTERMEDIATE(node) (!QGEN_NODE_IS_LEAF(node))

#define QGEN_NODE_SPLIT_BIT(node) (((node) >> 56ULL) & 0x7fULL)
#define QGEN_NODE_LEFT(node) (((node) >> 16ULL) & 0xffffULL)
#define QGEN_NODE_RIGHT(node) ((node) & 0xffffULL)
#define QGEN_NODE_WEIGHT(node) (((node) >> 32ULL) & 0xffffULL)
#define QGEN_NODE_BUCKET(node) ((node) & 0xffffULL)

#ifdef QGEN_NON_OPAQUE
struct qgen_bucket {
    size_t pattern_count;
    size_t *pattern_ids;
};

struct qgen_otree {
    uint8_t width;
    uint8_t __Reserved0[7];
    size_t node_count;
    qgen_otree_node_t *nodes;
    size_t pattern_count;
    qgen_bitpattern_t *patterns;
    size_t bucket_count;
    qgen_bucket_t *buckets;
};
#endif

extern const uint8_t QGEN_FILE_MAGIC[];

#ifdef QGEN_INTERNAL
#define QGEN_FILE_MIN_VERSION_SUPPORTED 0
#define QGEN_FILE_MAX_VERSION_SUPPORTED 0

const uint8_t QGEN_FILE_MAGIC[] = {0x07, 0x12, 0xEE, 0x2E};
#endif

#define QGEN_FILE_VERSION(flags) (flags >> 24)
#define QGEN_FILE_CHECKSUM_METHOD(flags) (flags & 3)

typedef struct qgen_file_header {
    uint8_t magic[4]; // Magic number: 0x07, 0x12, 0xEE, 0x2E, 0 712EE part is hexspeak for O TREE, 2E is just an ascii dot
    uint8_t checksum[16]; // Checksum: default is fletcher's checksum on the patterns, but more space is reserved for change via flags
    uint32_t flags;
    uint32_t node_count;
    uint32_t node_offset;
    uint32_t pattern_count;
    uint32_t pattern_offset;
    uint32_t bucket_count;
    uint32_t bucket_offset;
    uint8_t __Reserved0[16];
} qgen_file_header_t;

#define qgen_pat(lit) qgen_str2bp(sizeof(QGEN_STR(lit)) / sizeof(char), QGEN_STR(lit))
QGEN_EXPORT qgen_bitpattern_t qgen_str2bp(size_t length, const char *pattern);
QGEN_EXPORT qgen_bitpattern_t qgen_strz2bp(const char *pattern);

typedef struct qgen_array_list qgen_array_list_t;

#define QGEN_ENOTFOUND (-1)

#ifdef QGEN_NON_OPAQUE
struct qgen_array_list {
    size_t elem_size;
    size_t capacity;
    size_t length;
};
#define QGEN_ARRAY_HEADER(arr) (&((qgen_array_list_t *) arr)[-1])
#endif

QGEN_EXPORT void *qgen_new_array_list(size_t initial_capacity, size_t elem_size);
QGEN_EXPORT int qgen_al_push(void **arr, void *item);
QGEN_EXPORT int qgen_al_pop(void **arr, void *item);
QGEN_EXPORT void qgen_al_clear(void *arr);
QGEN_EXPORT void qgen_free_array_list(void *arr);

#ifdef QGEN_INTERNAL
typedef struct qgen_otree_gen qgen_otree_gen_t;

struct qgen_otree_gen {
    uint8_t __Reserved0[8];
    size_t node_capacity, bucket_capacity;
    uint64_t mask_low, mask_high;
    qgen_otree_t tree;
};

void qgen_discard_partial_tree(qgen_otree_t *tree);
int qgen_generate_tree_helper(qgen_otree_gen_t *gentree, size_t *patterns);
void qgen_find_cared_bits(qgen_otree_gen_t *tree);
#endif

QGEN_EXPORT qgen_otree_t *qgen_generate_tree(qgen_bitpattern_t *patterns);
QGEN_EXPORT intptr_t qgen_tree_dispatch(qgen_otree_t *tree, uint64_t high, uint64_t low);
QGEN_EXPORT void qgen_export_to_dot(qgen_otree_t *tree, const char *filename);
QGEN_EXPORT uint8_t qgen_tree_max_width(qgen_otree_t *tree);
QGEN_EXPORT void qgen_free_tree(qgen_otree_t *tree);

#endif // QGEN_H