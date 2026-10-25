//! Compact sketches for approximate candidate selection.
//! MinHash and SimHash over sparse feature sets.

use crate::signature::{FeatureId, SparseSignature};
use std::collections::HashSet;

const MINHASH_PERMUTATIONS: usize = 128;
const SIMHASH_BITS: usize = 64;

#[derive(Debug, Clone)]
pub struct MinHash {
    pub signature: Vec<u64>,
}

impl MinHash {
    pub fn from_signature(sig: &SparseSignature) -> Self {
        let features: HashSet<FeatureId> = sig.features.keys().copied().collect();
        let mut sketch = vec![u64::MAX; MINHASH_PERMUTATIONS];
        for (i, &seed) in (0..MINHASH_PERMUTATIONS).enumerate() {
            let seed = seed as u64 + 0x9E3779B97F4A7C15;
            for &fid in &features {
                let h = fid.wrapping_mul(seed).wrapping_add(seed);
                if h < sketch[i] {
                    sketch[i] = h;
                }
            }
        }
        Self { signature: sketch }
    }

    pub fn jaccard_estimate(&self, other: &MinHash) -> f32 {
        let matches = self.signature.iter()
            .zip(other.signature.iter())
            .filter(|(a, b)| a == b)
            .count();
        matches as f32 / MINHASH_PERMUTATIONS as f32
    }
}

#[derive(Debug, Clone)]
pub struct SimHash {
    pub bits: u64,
}

impl SimHash {
    pub fn from_signature(sig: &SparseSignature) -> Self {
        let mut acc = [0i64; SIMHASH_BITS];
        for (fid, weight) in &sig.features {
            for bit in 0..SIMHASH_BITS {
                let mask = 1u64 << bit;
                let contribution = if (fid & mask) != 0 { *weight as i64 } else { -(*weight as i64) };
                acc[bit] += contribution;
            }
        }
        let mut bits = 0u64;
        for bit in 0..SIMHASH_BITS {
            if acc[bit] > 0 {
                bits |= 1u64 << bit;
            }
        }
        Self { bits }
    }

    pub fn hamming_distance(&self, other: &SimHash) -> u32 {
        (self.bits ^ other.bits).count_ones()
    }

    pub fn similarity(&self, other: &SimHash) -> f32 {
        1.0 - (self.hamming_distance(other) as f32 / SIMHASH_BITS as f32)
    }
}
