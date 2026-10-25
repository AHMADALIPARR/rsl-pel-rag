//! Structural segmentation into sentences, sections, functions, AST nodes, logical records.

use unicode_segmentation::UnicodeSegmentation;
use regex::Regex;
use std::sync::OnceLock;

#[derive(Debug, Clone)]
pub struct Segment {
    pub id: u64,
    pub kind: SegmentKind,
    pub text: String,
    pub start: usize,
    pub end: usize,
    pub parent: Option<u64>,
    pub children: Vec<u64>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SegmentKind {
    Document,
    Section,
    Paragraph,
    Sentence,
    Function,
    AstNode,
    LogicalRecord,
}

pub struct Segmenter {
    sentence_re: Regex,
}

impl Segmenter {
    pub fn new() -> Self {
        Self {
            sentence_re: Regex::new(r"[.!?]+[\s\n]+").unwrap(),
        }
    }

    /// Parse document into hierarchical segments.
    /// Sentences, paragraphs, and coarse sections by headings.
    pub fn segment(&self, text: &str) -> Vec<Segment> {
        let mut segments = Vec::new();
        let mut next_id = 1u64;

        // Root document
        segments.push(Segment {
            id: 0,
            kind: SegmentKind::Document,
            text: text.to_string(),
            start: 0,
            end: text.len(),
            parent: None,
            children: Vec::new(),
        });

        // Split on double newlines for paragraphs/sections
        let paragraphs: Vec<&str> = text.split("\n\n").collect();
        let mut offset = 0usize;
        for para in paragraphs {
            if para.trim().is_empty() {
                offset += para.len() + 2;
                continue;
            }
            let is_heading = para.lines().next().map(|l| l.starts_with('#') || l.chars().all(|c| c.is_uppercase() || c.is_whitespace())).unwrap_or(false);
            let kind = if is_heading { SegmentKind::Section } else { SegmentKind::Paragraph };
            let id = next_id;
            next_id += 1;
            let start = offset;
            let end = offset + para.len();
            segments.push(Segment {
                id,
                kind,
                text: para.to_string(),
                start,
                end,
                parent: Some(0),
                children: Vec::new(),
            });
            // Sentences inside
            let sentences = self.sentence_re.split(para);
            let mut s_offset = start;
            for sent in sentences {
                let sent = sent.trim();
                if sent.is_empty() { continue; }
                let sid = next_id;
                next_id += 1;
                let s_start = s_offset;
                let s_end = s_offset + sent.len();
                segments.push(Segment {
                    id: sid,
                    kind: SegmentKind::Sentence,
                    text: sent.to_string(),
                    start: s_start,
                    end: s_end,
                    parent: Some(id),
                    children: Vec::new(),
                });
                s_offset = s_end + 1;
            }
            offset = end + 2;
        }
        // Wire children
        self.wire_hierarchy(&mut segments);
        segments
    }

    fn wire_hierarchy(&self, segments: &mut [Segment]) {
        let mut children_map: hashbrown::HashMap<u64, Vec<u64>> = hashbrown::HashMap::new();
        for seg in segments.iter() {
            if let Some(p) = seg.parent {
                children_map.entry(p).or_default().push(seg.id);
            }
        }
        for seg in segments.iter_mut() {
            if let Some(kids) = children_map.get(&seg.id) {
                seg.children = kids.clone();
            }
        }
    }
}

impl Default for Segmenter {
    fn default() -> Self {
        Self::new()
    }
}
