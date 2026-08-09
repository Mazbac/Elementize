<?php
/**
 * Plugin Name: Elementize
 * Description: Controlled REST access to WordPress, Elementor, and Pixfort.
 * Version: 0.4.0
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

Elementize_Visual_Writes::init();
Elementize_Visual_Rendering::init();
Elementize_Lifecycle::init();
