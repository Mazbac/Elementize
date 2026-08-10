# Elementize Design Intelligence

## Purpose

Elementize should not merely assemble technically valid Elementor/Pixfort pages. It should help WP Builder behave like a competent web designer: explore the catalogue broadly, choose sections deliberately, preserve a coherent visual system, detect weak design decisions, and verify the finished page before claiming completion.

This work lives on `fastbuild/design-intelligence` and builds on the proven guarded builder/onboarding work from `fastbuild/pixfort-library`.

## Core split of responsibilities

### WP Builder / Custom GPT = creative director

The GPT should reason about:
- visual direction and reference-page character
- page hierarchy and conversion flow
- section sequence and storytelling
- whether a design should feel premium, editorial, technical, playful, minimal, etc.
- which legitimate design alternative best matches the brief
- when deliberate variation or deliberate consistency is appropriate

### Elementize plugin = eyes, measurements, catalogue, history, and guardrails

The plugin should provide or enforce:
- complete catalogue coverage and pagination
- template metadata and structural descriptors
- visual-probe evidence
- template-family diversity and recent-use history
- measurable color/contrast/typography/spacing information
- page-level visual consistency checks
- deterministic design-audit findings
- guarded design-setting writes
- final completion gates

The plugin should not attempt to replace aesthetic reasoning with a rigid universal style. It should provide facts, constraints, and measurable quality signals so the GPT can make informed design decisions.

## Problem to solve

Current autonomous builds can become generic because the GPT can succeed after inspecting only a small familiar subset of the Pixfort catalogue. Reusing a known-good section is cheap and safe, so the model has little incentive to prove broad exploration.

The current quality audit is also weighted toward technical/completion correctness: placeholders, proof, links, dependencies, and render issues. A page can pass those checks while still having weak visual hierarchy, repetitive layouts, inconsistent spacing, poor contrast, or generic section selection.

## Target workflow

For a substantial landing-page build, WP Builder should follow this pipeline:

1. **Design brief**
   - Translate the business brief/reference page into a compact visual direction.
   - Define visual character, color roles, typography character, density, imagery style, CTA hierarchy, and section rhythm.

2. **Page architecture**
   - Define the purpose of every section before searching templates.
   - Avoid adding sections merely because a template exists.

3. **Broad catalogue exploration**
   - Search multiple relevant categories/keywords per section role.
   - Gather a broad candidate pool rather than accepting the first plausible result.
   - Track catalogue coverage for the current decision.

4. **Diversity pass**
   - Group near-duplicate/template-family candidates.
   - Prefer a shortlist containing meaningfully different compositions.
   - Apply a soft penalty to templates repeatedly used in recent builds unless reuse is justified.

5. **Visual inspection**
   - Use visual probes on the serious shortlist.
   - Never select an important section from metadata/title alone when visual proof is available.

6. **Selection scoring**
   - Judge candidates against the design brief using explicit criteria such as reference fit, hierarchy, content fit, brand compatibility, distinctiveness, and page-flow compatibility.

7. **Build and adapt**
   - Insert the selected template with fresh guarded state.
   - Rewrite copy, imagery, links, and supported visuals.
   - Preserve a page-wide design system rather than treating each section independently.

8. **Design audit**
   - Audit measurable visual quality after assembly.
   - Repair deterministic issues before completion.

9. **Final visual critique**
   - GPT performs a final composition critique using rendered evidence when available.
   - Completion requires content quality + technical quality + design quality.

## Phase 1 — Catalogue exploration intelligence

### Goals
- Make it difficult for the GPT to repeatedly choose the same small set of templates without evidence.
- Give the GPT a compact understanding of the breadth of the current Pixfort catalogue.
- Preserve reuse when it is genuinely the best choice.

### Planned capabilities

#### Catalogue summary
A read-only endpoint should expose:
- total sections
- total pages
- counts per category
- counts per subtype when available
- enough metadata to tell the GPT what search space exists

#### Candidate exploration evidence
For an important section decision, expose or compute:
- candidate count considered
- search queries/categories used
- unique template families represented
- visually inspected template IDs

This is evidence for the reasoning workflow; it does not need to become a hard blocker for every small edit.

#### Template-family diversity
Derive a conservative family signature from available metadata such as:
- normalized template title prefix
- subtype
- category set
- structural signature once Phase 2 exists

The goal is to avoid a shortlist containing four cosmetic variants of the same layout.

#### Reuse history
Track template usage in Elementize-managed builds.
Return:
- current-page usage count
- recent managed-page usage count
- last-used page/time where safely available

Use this as a soft novelty signal, never as an automatic prohibition.

## Phase 2 — Structural template intelligence

For shortlisted Pixfort sections, Elementize should expose a compact structure summary after safely obtaining template content without inserting it.

Desired descriptors include:
- top-level container/section count
- approximate column/container composition
- widget-type counts
- heading/text/button/image/icon counts
- CTA count
- form/shortcode/dependency indicators
- image prominence
- alignment tendency
- background/color hints
- container-based vs legacy section
- approximate density
- layout signature/family signature

The GPT should be able to distinguish a split hero, centered hero, dashboard hero, card grid, editorial content section, etc. without relying only on a thumbnail title.

## Phase 3 — Design-system model

For each substantial page build, maintain a page-local design brief containing roles rather than arbitrary replacement colors:
- primary background
- alternate background
- surface/card background
- primary text
- secondary text
- accent
- accent hover
- border/subtle separator

Also track:
- heading scale
- body size/line-height target
- corner-radius character
- shadow character
- spacing scale
- preferred container width
- image treatment
- CTA hierarchy

This should remain page-local. Elementize must not silently modify global theme settings for one landing page.

## Phase 4 — Deterministic design audit

Add a read-only design audit separate from the existing completion-quality audit.

Initial checks should focus on measurable high-confidence issues:

### Contrast/readability
- text/background contrast where resolvable
- button-label/button-background contrast
- muted text contrast
- obvious text-over-image risks when enough data exists

### Typography consistency
- excessive number of font sizes/weights
- broken heading-size hierarchy
- body text below configured safe threshold
- inconsistent line-height where resolvable

### Spacing/rhythm
- excessive number of unrelated spacing values
- extreme section-spacing outliers
- cramped sections next to oversized sections
- repeated dense sections without visual relief

### Color-system consistency
- excessive distinct accent colors
- CTA color inconsistency
- unexpected palette drift between sections

### Composition repetition
- repeated layout signatures in consecutive sections
- too many centered-text sections in succession
- repeated card-grid patterns

### CTA hierarchy
- multiple visually competing primary CTA styles
- inconsistent primary CTA treatment

### Imagery consistency
- dominant off-topic imagery (existing semantic rule)
- inconsistent image treatment where detectable
- too many image-heavy sections or too little visual relief where confidence is high

### Accessibility basics
- heading-level/order warnings when derivable
- missing alt text on meaningful images when exposed
- tiny control/text targets where measurable

Every finding should include:
- severity
- confidence
- affected top-level element(s)
- rule ID
- human-readable explanation
- whether it blocks design completion by default

Avoid pretending subjective preferences are universal failures. Low-confidence aesthetic observations should be advisory, not blockers.

## Phase 5 — Guarded design writers

Only after the read/audit model is reliable, add guarded writes for carefully selected visual settings such as:
- spacing/padding/gap
- typography size/weight/line-height
- border radius/border
- background/surface colors
- alignment
- width/max-width
- supported responsive variants

Writers must follow the existing Elementize safety model:
- managed draft restrictions where appropriate
- exact element ID + setting path
- fresh content hash
- exact expected value
- allowlisted setting families
- verification after save
- revisions/rollback where applicable
- no arbitrary raw Elementor JSON mutation endpoint

## Phase 6 — Reference-aware visual evaluation

When the user supplies a public reference page or image, WP Builder should extract a design brief from it rather than attempting a pixel clone.

Evaluate similarities in:
- dark/light balance
- section density
- typography character
- content width
- visual rhythm
- card usage
- imagery style
- color roles
- CTA treatment
- section composition

The goal is to reproduce design principles and visual character using available Pixfort/Elementor primitives, not copy protected content or blindly duplicate layout.

## Completion model

Long term, clean autonomous completion should require:

`content_quality_pass && technical_quality_pass && design_quality_pass`

Where:
- **content quality** = no demo leftovers, unsupported proof, broken/placeholder CTAs, or known irrelevant dominant imagery
- **technical quality** = no render/dependency/structure failures
- **design quality** = no high-confidence design blockers from the deterministic design audit

The GPT may still report advisory aesthetic findings, but it must not call a page design-clean when a default design blocker remains.

## Implementation order

1. Catalogue summary + exploration/diversity/reuse signals.
2. Template structural inspection.
3. Update WP Builder instructions to require design brief + evidence-based template selection.
4. Add initial read-only design audit: contrast, palette, typography, repetition, CTA consistency.
5. Runtime-test against deliberately weak and strong pages.
6. Add guarded design-setting writers only for rules proven useful by the audit.
7. Add reference-aware final visual critique/render workflow.

## Success criteria

A successful first release of Design Intelligence should demonstrate that:
- WP Builder explores materially more of the Pixfort catalogue for new landing pages.
- Important section choices are visually inspected rather than repeatedly selected from a familiar subset.
- repeated-template use is visible and deliberate rather than accidental.
- the final page has an explicit visual direction and coherent section rhythm.
- obvious contrast/readability/palette/repetition problems are detected deterministically.
- existing Elementize safety guarantees remain intact.
