use rsl_core::{Segmenter, BoundaryDetector, Lattice, SparseSignature, Feature};

fn main() {
    let text = r#"
# Formal Specification

The adder must preserve I5 invariant across carry chain.

## Implementation

module Adder {
  fn add(a: BitVec, b: BitVec) -> BitVec { ... }
}
"#;

    let segmenter = Segmenter::new();
    let segments = segmenter.segment(text);
    println!("Segments: {}", segments.len());

    let mut signatures = Vec::new();
    for seg in &segments {
        let mut sig = SparseSignature::new();
        for token in seg.text.split_whitespace() {
            sig.add_token(token, 1.0);
        }
        if seg.text.contains("invariant") {
            sig.add_feature(Feature::AstSymbol("invariant".into()), 2.5);
        }
        signatures.push(sig);
    }

    let detector = BoundaryDetector::new();
    let boundaries = detector.detect(&segments, &signatures);
    println!("Boundaries: {}", boundaries.len());

    let mut lattice = Lattice::new();
    lattice.build_from_segments(&segments, &signatures, &boundaries);
    println!("Regions: {}", lattice.regions.len());

    let mut qsig = SparseSignature::new();
    qsig.add_token("carry", 1.0);
    qsig.add_token("invariant", 1.5);
    let candidates = lattice.traverse_candidates(&qsig, 5);
    println!("Top candidates: {:?}", candidates);
}
