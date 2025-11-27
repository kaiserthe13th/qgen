# qgen

QGen is a automated optimal (heuristic-based) parser generator for binary
patterns up to 128 bits long.

The parsers it generates are trees with bucketed leafs, where each leaf holds
up to 16 patterns to match, and the tree is a binary decision tree where each
intermediate node holds which bit to look at then branch to the left or right
depending on the value of the bit.
