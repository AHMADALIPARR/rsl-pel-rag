# Recursive Semantic Lattice (RSL) for PEL × PQDialect × RAG

AGPLv3 licensed secondary retrieval mechanism behind hand-rolled lattice.

Implements hierarchical, information-boundary-driven semantic lattice retrieval without dense embedding models or Python glue.

## Components

- **Rust** (`rust/`): Signature engine, sparse hash64, MinHash/SimHash sketches, structural segmentation, recursive lattice construction, information-boundary detection, cost-based recursive partition.
- **Elixir/OTP** (`elixir/`): PEL-style agent scheduler, query interpreter for native `RAG LATTICE` syntax, traversal orchestration, SQL predicate application.
- **SQL** (`sql/`): PostgreSQL schema for source registry, evidence regions, provenance, and lattice indexes.

## Native Query Syntax

```
LET evidence = RAG LATTICE documents
  SEGMENT BY INFORMATION_BOUNDARY
  SIGNATURE SPARSE_HASH64
  TRAVERSE RECURSIVE
  WHERE provenance.verified = TRUE
  MATCH :question
  TOP 16;
LET related = SQL {
  SELECT *
  FROM evidence
  JOIN source_registry
    ON evidence.source_id = source_registry.id
};
RETURN AGENT researcher
  REASON OVER related;
```

Implemented in Elixir grammar; no external DSL assumed.

## Build

### Rust core
```bash
cd rust
cargo build --release
```

### Elixir
```bash
cd elixir
mix deps.get
mix compile
```

## Benchmark targets

Recall@10, MRR, p95 latency, index size vs BM25, fixed-chunk dense, hybrid.

No Python. No vector DB. Deterministic sparse signatures + structural verification.
