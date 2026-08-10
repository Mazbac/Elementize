<?php
/**
 * Plugin Name: Elementize
 * Description: Guarded content editing for existing Elementor and Pixfort pages.
 * Version: 0.30.0
 * Requires at least: 6.5
 * Requires PHP: 8.0
 * Requires Plugins: elementor
 * Author: Mazbac
 * License: GPL-2.0-or-later
 */

if ( ! defined( 'ABSPATH' ) ) exit;

define( 'ELEMENTIZE_VERSION', '0.30.0' );
define( 'ELEMENTIZE_FILE', __FILE__ );
define( 'ELEMENTIZE_DIR', __DIR__ );

require_once __DIR__ . '/includes/elementize-bootstrap.inc';

register_activation_hook( __FILE__, [ Elementize_Onboarding::class, 'activate' ] );
