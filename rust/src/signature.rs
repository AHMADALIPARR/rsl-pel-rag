//! Sparse semantic signatures.
//! S(d) = {(h(t), w_t) | t ∈ features(d)}
//! 64-bit stable feature hashes, weights from frequency/rarity/structure.

use hashbrown::HashMap;
use sha2::{Sha256, Digest};
use std::hash::{Hash, Hasher};
use std::collections::hash_map::DefaultHasher;

pub type FeatureId = u64;
pub type Weight = f32;

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Feature {
    Token(String),
    NGram(Vec<String>),
    AstSymbol(String),
    Entity(String),
    RelationalKey(String),
}

impl Feature {
    pub fn hash64(&self) -> FeatureId {
        let mut hasher = Sha256::new();
        match self {
            Feature::Token(t) => hasher.update(b"tok:"),
            Feature::NGram(ng) => {
                hasher.update(b"ng:");
                for g in ng {
                    hasher.update(g.as_bytes());
                    hasher.update(b"|");
                }
            }
            Feature::AstSymbol(s) => hasher.update(b"ast:"),
            Feature::Entity(e) => hasher.update(b"ent:"),
            Feature::RelationalKey(k) => hasher.update(b"rel:"),
        }
        match self {
            Feature::Token(t) | Feature::AstSymbol(t) | Feature::Entity(t) | Feature::RelationalKey(t) => {
                hasher.update(t.as_bytes());
            }
            Feature::NGram(_) => {}
        }
        let result = hasher.finalize();
        u64::from_le_bytes(result[0..8].try_into().unwrap())
    }
}

pub fn hash64(feature: &Feature) -> FeatureId {
    feature.hash64()
}

#[derive(Debug, Clone)]
pub struct SparseSignature {
    pub features: HashMap<FeatureId, Weight>,
    pub token_count: usize,
    pub structural_depth: u32,
}

impl SparseSignature {
    pub fn new() -> Self {
        Self {
            features: HashMap::new(),
            token_count: 0,
            structural_depth: 0,
        }
    }

    pub fn add_feature(&mut self, feature: Feature, weight: Weight) {
        let id = feature.hash64();
        *self.features.entry(id).or_insert(0.0) += weight;
    }

    pub fn add_token(&mut self, token: &str, weight: Weight) {
        self.add_feature(Feature::Token(token.to_lowercase()), weight);
        self.token_count += 1;
    }

    /// Exact sparse cosine-like score against another signature.
    pub fn score(&self, other: &SparseSignature) -> f32 {
        let mut dot = 0.0;
        let mut norm_a = 0.0;
        let mut norm_b = 0.0;
        for (id, wa) in &self.features {
            norm_a += wa * wa;
            if let Some(wb) = other.features.get(id) {
                dot += wa * wb;
            }
        }
        for wb in other.features.values() {
            norm_b += wb * wb;
        }
        if norm_a == 0.0 || norm_b == 0.0 {
            0.0
        } else {
            dot / (norm_a.sqrt() * norm_b.sqrt())
        }
    }

    pub fn is_empty(&self) -> bool {
        self.features.is_empty()
    }
}

impl Default for SparseSignature {
    fn default() -> Self {
        Self::new()
    }
}
