//! Recursive lattice construction.
//! Parent/child regions with cross-links for shared entities, symbols, references.
//! Cost-based recursive partition: split only when Δ(R) justifies the overhead.

use crate::signature::SparseSignature;
use crate::segment::{Segment, SegmentKind};
use crate::boundary::Boundary;
use hashbrown::{HashMap, HashSet};
use serde::{Serialize, Deserialize};

pub type RegionId = u64;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Region {
    pub id: RegionId,
    pub parent: Option<RegionId>,
    pub children: Vec<RegionId>,
    pub segments: Vec<u64>,
    pub signature: SparseSignature,
    pub cross_links: HashMap<String, Vec<RegionId>>, // entity/symbol -> regions
    pub depth: u32,
    pub estimated_cost: f32,
}

#[derive(Debug, Clone)]
pub struct Lattice {
    pub regions: HashMap<RegionId, Region>,
    pub root: RegionId,
    next_id: RegionId,
}

impl Lattice {
    pub fn new() -> Self {
        let root = 0;
        let mut regions = HashMap::new();
        regions.insert(root, Region {
            id: root,
            parent: None,
            children: Vec::new(),
            segments: Vec::new(),
            signature: SparseSignature::new(),
            cross_links: HashMap::new(),
            depth: 0,
            estimated_cost: 0.0,
        });
        Self {
            regions,
            root,
            next_id: 1,
        }
    }

    pub fn build_from_segments(
        &mut self,
        segments: &[Segment],
        signatures: &[SparseSignature],
        boundaries: &[Boundary],
    ) {
        // Group segments into initial regions by boundaries
        let mut current = Vec::new();
        let mut current_sig = SparseSignature::new();
        let boundary_set: HashSet<u64> = boundaries.iter().map(|b| b.after_segment).collect();

        for (i, seg) in segments.iter().enumerate() {
            if seg.kind == SegmentKind::Document { continue; }
            current.push(seg.id);
            // merge signature
            for (fid, w) in &signatures[i].features {
                *current_sig.features.entry(*fid).or_insert(0.0) += w;
            }
            if boundary_set.contains(&seg.id) || i == segments.len() - 1 {
                self.add_region(current, current_sig, None);
                current = Vec::new();
                current_sig = SparseSignature::new();
            }
        }
        self.recursive_partition();
    }

    fn add_region(&mut self, segments: Vec<u64>, signature: SparseSignature, parent: Option<RegionId>) -> RegionId {
        let id = self.next_id;
        self.next_id += 1;
        let depth = parent.map(|p| self.regions[&p].depth + 1).unwrap_or(1);
        let cost = signature.features.len() as f32 * 0.01 + segments.len() as f32 * 0.1;
        let region = Region {
            id,
            parent,
            children: Vec::new(),
            segments,
            signature,
            cross_links: HashMap::new(),
            depth,
            estimated_cost: cost,
        };
        if let Some(p) = parent {
            if let Some(pr) = self.regions.get_mut(&p) {
                pr.children.push(id);
            }
        } else if let Some(root) = self.regions.get_mut(&self.root) {
            root.children.push(id);
        }
        self.regions.insert(id, region);
        id
    }

    /// Recursive partition rule: Δ(R) = C(R) - Σ C(R_i)
    /// Split when children reduce retrieval cost enough to outweigh indexing overhead.
    fn recursive_partition(&mut self) {
        let ids: Vec<RegionId> = self.regions.keys().copied().filter(|id| *id != self.root).collect();
        for id in ids {
            if let Some(region) = self.regions.get(&id).cloned() {
                if region.segments.len() < 4 { continue; }
                // Hypothetical split into two halves
                let mid = region.segments.len() / 2;
                let left_cost = (mid as f32) * 0.15;
                let right_cost = ((region.segments.len() - mid) as f32) * 0.15;
                let delta = region.estimated_cost - (left_cost + right_cost);
                // Overhead proxy
                let overhead = 0.05 * region.depth as f32;
                if delta > overhead && delta > 0.2 {
                    // Perform split (simplified: create children)
                    let left_segs = region.segments[..mid].to_vec();
                    let right_segs = region.segments[mid..].to_vec();
                    let left_sig = region.signature.clone(); // in real: recompute
                    let right_sig = region.signature.clone();
                    let left = self.add_region(left_segs, left_sig, Some(id));
                    let right = self.add_region(right_segs, right_sig, Some(id));
                    if let Some(r) = self.regions.get_mut(&id) {
                        r.children.push(left);
                        r.children.push(right);
                        r.estimated_cost = left_cost + right_cost + overhead;
                    }
                }
            }
        }
    }

    pub fn region(&self, id: RegionId) -> Option<&Region> {
        self.regions.get(&id)
    }

    /// Traverse for query: select regions matching signature + structural filters.
    pub fn traverse_candidates(&self, query_sig: &SparseSignature, top_k: usize) -> Vec<(RegionId, f32)> {
        let mut scored: Vec<(RegionId, f32)> = self.regions.iter()
            .filter(|(id, _)| **id != self.root)
            .map(|(id, r)| (*id, r.signature.score(query_sig)))
            .collect();
        scored.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
        scored.truncate(top_k);
        scored
    }
}

impl Default for Lattice {
    fn default() -> Self {
        Self::new()
    }
}
