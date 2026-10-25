//! Recursive Semantic Lattice core.
//! Deterministic sparse signatures, information-boundary segmentation,
//! recursive lattice construction. No embeddings, no Python.

pub mod signature;
pub mod segment;
pub mod lattice;
pub mod sketch;
pub mod boundary;

pub use signature::{SparseSignature, Feature, hash64};
pub use segment::{Segmenter, Segment};
pub use lattice::{Lattice, Region, RegionId};
pub use sketch::{MinHash, SimHash};
pub use boundary::{BoundaryDetector, BoundaryReason};
