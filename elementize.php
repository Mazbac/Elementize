<?php
/**
 * Plugin Name: Elementize
 * Description: Controlled REST access to WordPress, Elementor, and Pixfort.
 * Version: 0.26.0
 * Requires at least: 6.5
 * Requires PHP: 8.0
 * Requires Plugins: elementor
 * Author: Mazbac
 * License: GPL-2.0-or-later
 */
if ( ! defined( 'ABSPATH' ) ) exit;

require_once __DIR__ . '/includes/elementize-core.inc';
require_once __DIR__ . '/includes/elementize-visual-writes.inc';
require_once __DIR__ . '/includes/elementize-visual-rendering.inc';
require_once __DIR__ . '/includes/elementize-lifecycle.inc';
require_once __DIR__ . '/includes/elementize-visual-filters.inc';
require_once __DIR__ . '/includes/elementize-page-layout.inc';
require_once __DIR__ . '/includes/elementize-media-library.inc';
require_once __DIR__ . '/includes/elementize-chat-media.inc';
require_once __DIR__ . '/includes/elementize-text-audit.inc';
require_once __DIR__ . '/includes/elementize-render-audit.inc';
require_once __DIR__ . '/includes/elementize-post-content-sync.inc';
require_once __DIR__ . '/includes/elementize-render-cache-audit.inc';
require_once __DIR__ . '/includes/elementize-post-identity.inc';
require_once __DIR__ . '/includes/elementize-embedded-safe.inc';
require_once __DIR__ . '/includes/elementize-page-quality.inc';
require_once __DIR__ . '/includes/elementize-page-quality-hardening.inc';
require_once __DIR__ . '/includes/elementize-admin-display.inc';
require_once __DIR__ . '/includes/elementize-onboarding.inc';
require_once __DIR__ . '/includes/elementize-onboarding-visual-ai.inc';
require_once __DIR__ . '/includes/elementize-design-intelligence.inc';
require_once __DIR__ . '/includes/elementize-template-structure.inc';
require_once __DIR__ . '/includes/elementize-design-intelligence-hardening.inc';
require_once __DIR__ . '/includes/elementize-design-audit.inc';
require_once __DIR__ . '/includes/elementize-design-audit-hardening.inc';
require_once __DIR__ . '/includes/elementize-design-audit-calibration.inc';
require_once __DIR__ . '/includes/elementize-design-settings.inc';
require_once __DIR__ . '/includes/elementize-gpt-control-plane.inc';
require_once __DIR__ . '/includes/elementize-design-settings-response-budget.inc';
require_once __DIR__ . '/includes/elementize-design-writes.inc';
require_once __DIR__ . '/includes/elementize-design-write-discovery.inc';
require_once __DIR__ . '/includes/elementize-signed-preview.inc';
require_once __DIR__ . '/includes/elementize-visual-audit.inc';
require_once __DIR__ . '/includes/elementize-visual-localization.inc';
require_once __DIR__ . '/includes/elementize-visual-localization-hardening.inc';
require_once __DIR__ . '/includes/elementize-visual-repair-discovery.inc';
require_once __DIR__ . '/includes/elementize-visual-repair-discovery-hardening.inc';
require_once __DIR__ . '/includes/elementize-render-metrics.inc';
require_once __DIR__ . '/includes/elementize-render-metrics-hardening.inc';
require_once __DIR__ . '/includes/elementize-render-metrics-cdp.inc';
require_once __DIR__ . '/includes/elementize-render-metrics-cdp-stability.inc';
require_once __DIR__ . '/includes/elementize-rendered-repair-correlation.inc';
require_once __DIR__ . '/includes/elementize-rendered-repair-correlation-hardening.inc';
require_once __DIR__ . '/includes/elementize-bounded-repair-planning.inc';
require_once __DIR__ . '/includes/elementize-bounded-repair-planning-diagnostics.inc';
require_once __DIR__ . '/includes/elementize-rendered-observations.inc';
require_once __DIR__ . '/includes/elementize-rendered-observations-hardening.inc';
require_once __DIR__ . '/includes/elementize-rendered-observation-convergence.inc';
require_once __DIR__ . '/includes/elementize-convergence-exact-element-recovery.inc';
require_once __DIR__ . '/includes/elementize-focused-candidate-diversity.inc';
require_once __DIR__ . '/includes/elementize-focused-visual-verification.inc';
require_once __DIR__ . '/includes/elementize-focused-incomplete-recovery.inc';
require_once __DIR__ . '/includes/elementize-rendered-observation-convergence-hardening.inc';
require_once __DIR__ . '/includes/elementize-rendered-observation-convergence-diagnostics.inc';
require_once __DIR__ . '/includes/elementize-focused-visual-verification-hardening.inc';
require_once __DIR__ . '/includes/elementize-focused-section-verification.inc';
require_once __DIR__ . '/includes/elementize-section-convergence-backstop.inc';
require_once __DIR__ . '/includes/elementize-section-convergence-backstop-hardening.inc';
require_once __DIR__ . '/includes/elementize-converged-bounded-repair-planning.inc';
require_once __DIR__ . '/includes/elementize-aesthetic-brain.inc';
require_once __DIR__ . '/includes/elementize-status-version.inc';

Elementize_Visual_Writes::init();
Elementize_Visual_Rendering::init();
Elementize_Lifecycle::init();
Elementize_Visual_Filters::init();
Elementize_Page_Layout::init();
Elementize_Media_Library::init();
Elementize_Chat_Media::init();
Elementize_Text_Audit::init();
Elementize_Render_Audit::init();
Elementize_Post_Content_Sync::init();
Elementize_Render_Cache_Audit::init();
Elementize_Post_Identity::init();
Elementize_Embedded_Safe::init();
Elementize_Page_Quality::init();
Elementize_Admin_Display::init();
Elementize_Onboarding::init();
Elementize_Onboarding_Visual_AI::init();
Elementize_Design_Intelligence::init();
Elementize_Template_Structure::init();
Elementize_Design_Audit::init();
Elementize_Design_Settings::init();
Elementize_GPT_Control_Plane::init();
Elementize_Design_Settings_Response_Budget::init();
Elementize_Design_Writes::init();
Elementize_Design_Write_Discovery::init();
Elementize_Signed_Preview::init();
Elementize_Visual_Audit::init();
Elementize_Visual_Localization::init();
Elementize_Visual_Localization_Hardening::init();
Elementize_Visual_Repair_Discovery::init();
Elementize_Visual_Repair_Discovery_Hardening::init();
Elementize_Render_Metrics::init();
Elementize_Render_Metrics_Hardening::init();
Elementize_Render_Metrics_CDP::init();
Elementize_Render_Metrics_CDP_Stability::init();
Elementize_Rendered_Repair_Correlation::init();
Elementize_Rendered_Repair_Correlation_Hardening::init();
Elementize_Bounded_Repair_Planning::init();
Elementize_Bounded_Repair_Planning_Diagnostics::init();
Elementize_Rendered_Observations::init();
Elementize_Rendered_Observations_Hardening::init();
Elementize_Rendered_Observation_Convergence::init();
Elementize_Convergence_Exact_Element_Recovery::init();
Elementize_Focused_Candidate_Diversity::init();
Elementize_Focused_Visual_Verification::init();
Elementize_Focused_Incomplete_Recovery::init();
Elementize_Rendered_Observation_Convergence_Hardening::init();
Elementize_Rendered_Observation_Convergence_Diagnostics::init();
Elementize_Focused_Visual_Verification_Hardening::init();
Elementize_Focused_Section_Verification::init();
Elementize_Section_Convergence_Backstop::init();
Elementize_Section_Convergence_Backstop_Hardening::init();
Elementize_Converged_Bounded_Repair_Planning::init();
Elementize_Aesthetic_Brain::init();
Elementize_Status_Version::init();
