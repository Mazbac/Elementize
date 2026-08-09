<?php
/**
 * Plugin Name: Elementize
 * Description: Controlled REST access to WordPress, Elementor, and Pixfort.
 * Version: 0.5.16
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
