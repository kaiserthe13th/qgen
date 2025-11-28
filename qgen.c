#define QGEN_NON_OPAQUE
#define QGEN_INTERNAL
#include "qgen.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

qgen_bitpattern_t qgen_str2bp(size_t length, const char *pattern) {
    qgen_bitpattern_t bp = {0};

    for (size_t i = 0; i < length; i++) {
        int u64_pos = bp.width >= 64; // 1 for high 64 bits, 0 for low 64 bits

        switch (pattern[i]) {
            case '1':
                // Set both active and mask bits
                if (u64_pos) {
                    bp.active_high <<= 1;
                    bp.mask_high <<= 1;
                    bp.active_high |= bp.active_low >> 63;
                    bp.mask_high |= bp.mask_low >> 63;
                }
                bp.active_low <<= 1;
                bp.mask_low <<= 1;

                bp.active_low |= 1;
                bp.mask_low |= 1;
                break;
            case '0':
                // Set only the MASK bit (active bit remains 0 from initialization)
                if (u64_pos) {
                    bp.active_high <<= 1;
                    bp.mask_high <<= 1;
                    bp.active_high |= bp.active_low >> 63;
                    bp.mask_high |= bp.mask_low >> 63;
                }
                bp.active_low <<= 1;
                bp.mask_low <<= 1;
                
                bp.mask_low |= 1;
                break;
            case 'x':
            case 'X':
            case '*':
                if (u64_pos) {
                    bp.active_high <<= 1;
                    bp.mask_high <<= 1;
                    bp.active_high |= bp.active_low >> 63;
                    bp.mask_high |= bp.mask_low >> 63;
                }
                bp.active_low <<= 1;
                bp.mask_low <<= 1;
                break;
            default: 
                // Ignored other characters (including '\0')
                continue;
        }
        bp.width++;

        // Safety check for bit width
        if (bp.width >= 128) break;
    }
    return bp;
}

qgen_bitpattern_t qgen_strz2bp(const char *pattern) {
    return qgen_str2bp(strlen(pattern), pattern);
}

void *qgen_new_array_list(size_t initial_capacity, size_t elem_size) {
    qgen_array_list_t *al = malloc(sizeof(qgen_array_list_t) + initial_capacity * elem_size);
    if (al == NULL) return NULL;
    al->capacity = initial_capacity;
    al->elem_size = elem_size;
    al->length = 0;
    return &al[1];
}

int qgen_al_push(void **arr, void *item) {
    qgen_array_list_t *al = QGEN_ARRAY_HEADER(*arr);
    if (al->length >= al->capacity) {
        al->capacity = al->capacity * 2 + 1;
        void *new_arr = realloc(al, sizeof(qgen_array_list_t) + al->capacity * al->elem_size);
        if (new_arr == NULL) return errno = ENOMEM;
        *arr = &((qgen_array_list_t *)new_arr)[1];
    }
    uint8_t *dest = &((uint8_t *) *arr)[al->elem_size * (al->length++)];
    memcpy(dest, item, al->elem_size);
    return 0;
}

int qgen_al_pop(void **arr, void *item) {
    qgen_array_list_t *al = QGEN_ARRAY_HEADER(*arr);
    if (al->length <= 0) return QGEN_ENOTFOUND;
    uint8_t *src = &((uint8_t *) *arr)[al->elem_size * (--al->length)];
    memcpy(item, src, al->elem_size);
    return 0;
}

void qgen_al_clear(void *arr) {
    qgen_array_list_t *al = QGEN_ARRAY_HEADER(arr);
    al->length = 0;
}

void qgen_free_array_list(void *arr) {
    qgen_array_list_t *al = QGEN_ARRAY_HEADER(arr);
    free(al);
}

void qgen_find_cared_bits(qgen_otree_gen_t *tree) {
    tree->mask_low = 0;
    tree->mask_high = 0;
    for (size_t i = 0; i < tree->tree.pattern_count; i++) {
        tree->mask_high |= tree->tree.patterns[i].mask_high;
        tree->mask_low |= tree->tree.patterns[i].mask_low;
    }
}

#define QGEN_BUCKET_MAX_LENGTH 16

// Helper to free lists INSIDE buckets if we have to abort generation
void qgen_discard_partial_tree(qgen_otree_t *tree) {
    if (tree->buckets) {
        for (size_t i = 0; i < tree->bucket_count; i++) {
            if (tree->buckets[i].pattern_ids) {
                qgen_free_array_list(tree->buckets[i].pattern_ids);
            }
        }
        free(tree->buckets);
        tree->buckets = NULL;
    }
    if (tree->nodes) {
        free(tree->nodes);
        tree->nodes = NULL;
    }
}

int qgen_generate_tree_helper(qgen_otree_gen_t *gentree, size_t *patterns) {
    int err = 0;
    qgen_otree_t *tree = &gentree->tree;
    qgen_array_list_t *patterns_head = QGEN_ARRAY_HEADER(patterns);
    if (patterns_head->length <= QGEN_BUCKET_MAX_LENGTH) {
        if (tree->bucket_count >= gentree->bucket_capacity) {
            gentree->bucket_capacity = gentree->bucket_capacity * 2 + 1;
            qgen_bucket_t *new_buckets = realloc(tree->buckets, sizeof(qgen_bucket_t) * gentree->bucket_capacity);
            if (new_buckets == NULL) {
                qgen_free_array_list(patterns);
                return errno = ENOMEM;
            }
            tree->buckets = new_buckets;
        }
        size_t bucket_id = tree->bucket_count;
        tree->buckets[tree->bucket_count++] = (qgen_bucket_t) {
            .pattern_count = patterns_head->length,
            .pattern_ids = patterns,
        };
        if (tree->node_count >= gentree->node_capacity) {
            gentree->node_capacity = gentree->node_capacity * 2 + 1;
            qgen_otree_node_t *new_nodes = realloc(tree->nodes, sizeof(qgen_otree_node_t) * gentree->node_capacity);
            if (new_nodes == NULL) return errno = ENOMEM;
            tree->nodes = new_nodes;
        }
        tree->nodes[tree->node_count++] = QGEN_NODE_LEAF(patterns_head->length, bucket_id);
        return 0;
    }

    size_t *zero_patterns = qgen_new_array_list(patterns_head->length, sizeof(*patterns));
    size_t *one_patterns = qgen_new_array_list(patterns_head->length, sizeof(*patterns));

    uint64_t best_bit_idx = -1;
    uint64_t gini_nom = 0xffffffffffffffffULL;
    uint64_t gini_denom = 1;
    for (uint64_t bit_idx = 0; bit_idx < tree->width; bit_idx++) {
        uint64_t low_mask = 0;
        uint64_t high_mask = 0;

        if (bit_idx >= 64) {
            high_mask = 1ULL << (bit_idx - 64);
            // Skip if NO pattern cares about it
            if (!(gentree->mask_high & high_mask)) continue;
        } else {
            low_mask = 1ULL << bit_idx;
            // Skip if NO pattern cares about it
            if (!(gentree->mask_low & low_mask)) continue;
        }

        uint64_t n0 = 0; // qgen_pat(0)
        uint64_t n1 = 0; // qgen_pat(1)

        for (size_t p = 0; p < patterns_head->length; p++) {
            qgen_bitpattern_t *pat = &tree->patterns[patterns[p]];

            // Check if this pattern CARES about the bit (mask is 1)
            int cares = bit_idx >= 64 ? (pat->mask_high & high_mask) : (pat->mask_low & low_mask);

            if (cares) {
                int is_one = bit_idx >= 64 ? (pat->active_high & high_mask) : (pat->active_low & low_mask);
                if (is_one) n1++;
                else n0++;
            }
        }

        uint64_t cur_nom = n0 + n1;
        if (cur_nom == 0) continue;
        uint64_t cur_denom = n0 * n1;
        if (cur_nom * gini_denom < gini_nom * cur_denom) {
            gini_nom = cur_nom;
            gini_denom = cur_denom;
            best_bit_idx = bit_idx;
        }
    }

    // There is no best bit, meaning all patterns are completely don't cares, which is an error case.
    if (best_bit_idx == -1) {
        err = EINVAL;
        goto cleanup;
    }

    uint64_t split_high = (best_bit_idx >= 64) ? (1ULL << (best_bit_idx - 64)) : 0;
    uint64_t split_low  = (best_bit_idx < 64)  ? (1ULL << best_bit_idx) : 0;

    for (size_t p = 0; p < patterns_head->length; p++) {
        qgen_bitpattern_t *pat = &tree->patterns[patterns[p]];
        
        // Re-check the best bit logic to push to lists
        int cares = (pat->mask_high & split_high) || (pat->mask_low & split_low);
        
        if (cares) {
            int is_one = (pat->active_high & split_high) || (pat->active_low & split_low);
            
            qgen_al_push(is_one ? ((void **) &one_patterns) : ((void **) &zero_patterns), &patterns[p]);
        } else {
            // It's an x, so it must go down both paths in the tree
            qgen_al_push((void **) &one_patterns, &patterns[p]);
            qgen_al_push((void **) &zero_patterns, &patterns[p]);
        }
    }

    // Free original patterns we are done now
    qgen_free_array_list(patterns);
    patterns = NULL;

    // Left side
    err = qgen_generate_tree_helper(gentree, zero_patterns);
    zero_patterns = NULL; // justification: if needed to be freed, it should have been freed by the recursive call
    if (err) goto cleanup;
    size_t zero_branch = tree->node_count - 1;
    size_t zero_weight = QGEN_NODE_WEIGHT(tree->nodes[zero_branch]);

    // Right side
    err = qgen_generate_tree_helper(gentree, one_patterns);
    one_patterns = NULL; // justification: if needed to be freed, it should have been freed by the recursive call
    if (err) goto cleanup;
    size_t one_branch = tree->node_count - 1;
    size_t one_weight = QGEN_NODE_WEIGHT(tree->nodes[one_branch]);
    
    if (tree->node_count >= gentree->node_capacity) {
        gentree->node_capacity = gentree->node_capacity * 2 + 1;
        qgen_otree_node_t *new_nodes = realloc(tree->nodes, sizeof(qgen_otree_node_t) * gentree->node_capacity);
        if (new_nodes == NULL) return errno = ENOMEM;
        tree->nodes = new_nodes;
    }
    tree->nodes[tree->node_count++] = QGEN_NODE_INTERMEDIATE(best_bit_idx, zero_weight + one_weight, zero_branch, one_branch);

cleanup:
    if (patterns) qgen_free_array_list(patterns);
    if (zero_patterns) qgen_free_array_list(zero_patterns);
    if (one_patterns) qgen_free_array_list(one_patterns);
    if (err != 0) {
        errno = err;
    }
    return err;
}

qgen_otree_t *qgen_generate_tree(qgen_bitpattern_t *patterns) {
    int err = 0;
    qgen_array_list_t *patterns_head = QGEN_ARRAY_HEADER(patterns);
    qgen_otree_gen_t gentree = {0};
    gentree.tree.patterns = patterns;
    gentree.tree.pattern_count = patterns_head->length;

    // Find max width
    gentree.tree.width = 0;
    for (size_t i = 0; i < gentree.tree.pattern_count; i++) {
        gentree.tree.width = gentree.tree.width >= gentree.tree.patterns[i].width ? gentree.tree.width : gentree.tree.patterns[i].width;
    }

    for (size_t i = 0; i < patterns_head->length; i++) {
        uint64_t shift = gentree.tree.width - patterns[i].width;
        uint64_t backshift = 64 - shift;

        patterns[i].active_high <<= shift;
        patterns[i].mask_high <<= shift;
        patterns[i].active_high |= patterns[i].active_low >> backshift;
        patterns[i].mask_high |= patterns[i].mask_low >> backshift;
        patterns[i].active_low <<= shift;
        patterns[i].mask_low <<= shift;
    }
    qgen_find_cared_bits(&gentree);
    
    size_t *pats = qgen_new_array_list(patterns_head->length, sizeof(size_t));
    // no need to set errno as qgen_new_array_list already sets it
    if (pats == NULL) return NULL;

    for (size_t i = 0; i < patterns_head->length; i++) {
        pats[i] = i;
    }
    QGEN_ARRAY_HEADER(pats)->length = patterns_head->length;

    err = qgen_generate_tree_helper(&gentree, pats);

    if (err != 0) {
        qgen_discard_partial_tree(&gentree.tree);
        errno = err;
        return NULL;
    }
    
    qgen_otree_t *result = malloc(sizeof(gentree.tree));
    if (result == NULL) {
        qgen_discard_partial_tree(&gentree.tree);
        errno = ENOMEM;
        return NULL;
    }
    *result = gentree.tree;
    return result;
}

static void qgen_export_dot_helper(FILE *f, qgen_otree_t *tree, size_t node_idx) {
    qgen_otree_node_t node = tree->nodes[node_idx];

    if (QGEN_NODE_IS_LEAF(node)) {
        size_t bucket_id = QGEN_NODE_BUCKET(node);
        qgen_bucket_t *bucket = &tree->buckets[bucket_id];
        
        // Render Leaf Node (Box shape)
        fprintf(
            f,
            "    node_%"PRIuPTR" [shape=record, style=filled, fillcolor=lightgrey, "
            "label=\"{LEAF | Pat Count: %"PRIuPTR" | Bucket ID: %"PRIuPTR"}\"];\n", 
            (uintptr_t) node_idx,
            (uintptr_t) bucket->pattern_count,
            (uintptr_t) bucket_id
        );
        return;
    }

    // Render Intermediate Node (Circle/Ellipse)
    // Display the Bit Index being tested
    uint64_t bit = QGEN_NODE_SPLIT_BIT(node);
    fprintf(
        f,
        "    node_%"PRIuPTR" [shape=ellipse, style=filled, fillcolor=white, label=\"Bit %" PRIu64 "\"];\n", 
        (uintptr_t) node_idx,
        (uintptr_t) bit
    );

    // Edges
    size_t zero_idx = QGEN_NODE_LEFT(node);
    size_t one_idx  = QGEN_NODE_RIGHT(node);

    // 0 Edge (False/Low) - Dotted line
    fprintf(f, "    node_%"PRIuPTR" -> node_%"PRIuPTR" [label=\"0\", style=dashed];\n", (uintptr_t) node_idx, (uintptr_t) zero_idx);
    
    // 1 Edge (True/High) - Solid line
    fprintf(f, "    node_%"PRIuPTR" -> node_%"PRIuPTR" [label=\"1\", style=bold];\n", (uintptr_t) node_idx, (uintptr_t) one_idx);

    // Recurse
    qgen_export_dot_helper(f, tree, zero_idx);
    qgen_export_dot_helper(f, tree, one_idx);
}

void qgen_export_to_dot(qgen_otree_t *tree, const char *filename) {
    if (!tree || tree->node_count == 0) return;

    FILE *f = NULL;
    errno = fopen_s(&f, filename, "w");
    if (!f) {
        perror("Failed to open dot file");
        return;
    }

    fprintf(f, "digraph QGenTree {\n");
    fprintf(f, "    rankdir=TB;\n"); // Top-to-Bottom layout
    fprintf(f, "    node [fontname=\"Helvetica\"];\n");
    fprintf(f, "    edge [fontname=\"Helvetica\", fontsize=10];\n");

    // Start recursion from the root
    // In your generation logic, the root is the last node added.
    size_t root_idx = tree->node_count - 1;
    qgen_export_dot_helper(f, tree, root_idx);

    fprintf(f, "}\n");
    fclose(f);
    printf("Tree exported to %s\n", filename);
}

void qgen_free_tree(qgen_otree_t *tree) {
    if (tree) {
        qgen_discard_partial_tree(tree);
        free(tree);
    }
}

intptr_t qgen_tree_dispatch(qgen_otree_t *tree, uint64_t high, uint64_t low) {
    size_t node_id = tree->node_count - 1;

    while (1) {
        qgen_otree_node_t node = tree->nodes[node_id];

        if (QGEN_NODE_IS_LEAF(node)) {
            size_t bucket_id = QGEN_NODE_BUCKET(node);
            qgen_bucket_t bucket = tree->buckets[bucket_id];

            // Linear Scan inside the Cache Line
            for (size_t i = 0; i < bucket.pattern_count; i++) {
                size_t pat_idx = bucket.pattern_ids[i];
                qgen_bitpattern_t pat = tree->patterns[pat_idx];
                if ((low & pat.mask_low) == pat.active_low && (high & pat.mask_high) == pat.active_high) {
                    return pat_idx;
                }
            }
            // No match in bucket
            return -1;
        }
        uint64_t split_bit = QGEN_NODE_SPLIT_BIT(node);
        int bit_set = split_bit >= 64 ? ((high >> (split_bit - 64)) & 1) : ((low >> split_bit) & 1);
        node_id = bit_set ? QGEN_NODE_RIGHT(node): QGEN_NODE_LEFT(node);
    }
}

uint8_t qgen_tree_max_width(qgen_otree_t *tree) {
    return tree->width;
}
