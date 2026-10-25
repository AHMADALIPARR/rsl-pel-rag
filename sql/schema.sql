-- PostgreSQL schema for RSL evidence regions and source registry.
-- Supports SQL predicates in the native query dialect.

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

CREATE TABLE source_registry (
    id          BIGSERIAL PRIMARY KEY,
    uri         TEXT NOT NULL UNIQUE,
    content_hash BYTEA,
    verified    BOOLEAN NOT NULL DEFAULT FALSE,
    provenance  JSONB NOT NULL DEFAULT '{}',
    inserted_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE lattice_regions (
    id              BIGSERIAL PRIMARY KEY,
    parent_id       BIGINT REFERENCES lattice_regions(id),
    source_id       BIGINT NOT NULL REFERENCES source_registry(id),
    depth           INTEGER NOT NULL DEFAULT 0,
    segment_ids     BIGINT[] NOT NULL,
    signature_hash  BYTEA,          -- SimHash or MinHash sketch
    sparse_features JSONB,          -- compressed sparse map
    estimated_cost  REAL,
    cross_links     JSONB,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_regions_parent ON lattice_regions(parent_id);
CREATE INDEX idx_regions_source ON lattice_regions(source_id);
CREATE INDEX idx_regions_signature ON lattice_regions USING gin (sparse_features);
CREATE INDEX idx_source_verified ON source_registry(verified) WHERE verified = TRUE;

-- Example query matching the native dialect's SQL clause:
-- SELECT * FROM evidence JOIN source_registry ON evidence.source_id = source_registry.id
-- WHERE provenance.verified = TRUE;
