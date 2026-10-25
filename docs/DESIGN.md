# RSL Design Notes

## Recursive partition rule

Δ(R) = C(R) - Σ_i C(R_i)

Split region R when the reduction in retrieval cost from children outweighs additional indexing and traversal overhead. Cost estimated from actual query workloads (feature cardinality, segment count, depth), not token counts alone.

## Signature

Deterministic sparse:

S(d) = {(h(t), w_t) | t ∈ features(d)}

- h(t): stable 64-bit SHA-256 truncated hash of feature
- Features: tokens, n-grams, AST symbols, entity references, relational keys
- Weights: frequency × inverse document frequency × structural importance

Followed by MinHash (128 permutations) or SimHash (64-bit) for approximate candidate selection. Exact sparse scoring and structural verification after candidate pruning.

## Strengths

- Exact symbols and lexical relationships
- No pretrained embedding model
- Deterministic, auditable
- Content-addressed regions
- SQL-native predicates before similarity

## Weaknesses (acknowledged)

- Weaker on pure paraphrases until semantic expansion layer is added
- Requires workload-driven threshold tuning for boundaries and partitions
