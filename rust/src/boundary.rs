//! Information-boundary detection.
//! Split only when semantic novelty, entropy, or dependency changes exceed thresholds.

use crate::signature::SparseSignature;
use crate::segment::Segment;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BoundaryReason {
    EntropyDelta,
    Novelty,
    DependencyShift,
    Structural,
    Forced,
}

#[derive(Debug, Clone)]
pub struct Boundary {
    pub after_segment: u64,
    pub reason: BoundaryReason,
    pub score: f32,
}

pub struct BoundaryDetector {
    pub entropy_threshold: f32,
    pub novelty_threshold: f32,
}

impl BoundaryDetector {
    pub fn new() -> Self {
        Self {
            entropy_threshold: 0.35,
            novelty_threshold: 0.55,
        }
    }

    /// Detect boundaries between consecutive segments using signature deltas.
    pub fn detect(&self, segments: &[Segment], signatures: &[SparseSignature]) -> Vec<Boundary> {
        let mut boundaries = Vec::new();
        if segments.len() < 2 || signatures.len() != segments.len() {
            return boundaries;
        }
        for i in 0..(segments.len() - 1) {
            let a = &signatures[i];
            let b = &signatures[i + 1];
            let score = 1.0 - a.score(b);
            let mut reason = None;
            if score > self.novelty_threshold {
                reason = Some(BoundaryReason::Novelty);
            } else if self.entropy_delta(a, b) > self.entropy_threshold {
                reason = Some(BoundaryReason::EntropyDelta);
            } else if matches!(segments[i].kind, crate::segment::SegmentKind::Section) {
                reason = Some(BoundaryReason::Structural);
            }
            if let Some(r) = reason {
                boundaries.push(Boundary {
                    after_segment: segments[i].id,
                    reason: r,
                    score,
                });
            }
        }
        boundaries
    }

    fn entropy_delta(&self, a: &SparseSignature, b: &SparseSignature) -> f32 {
        // Simple proxy: difference in feature cardinality normalized
        let ca = a.features.len() as f32;
        let cb = b.features.len() as f32;
        (ca - cb).abs() / (ca.max(cb).max(1.0))
    }
}

impl Default for BoundaryDetector {
    fn default() -> Self {
        Self::new()
    }
}
