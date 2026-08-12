<?php
/**
 * Plugin Name: Elementize
 * Description: Guarded Elementor editing with optional page-scoped Creative Control.
 * Version: 0.40.0
 * Requires at least: 6.5
 * Requires PHP: 8.0
 * Requires Plugins: elementor
 * Author: Mazbac
 * License: GPL-2.0-or-later
 */

if ( ! defined( 'ABSPATH' ) ) exit;

define( 'ELEMENTIZE_VERSION', '0.40.0' );
define( 'ELEMENTIZE_FILE', __FILE__ );
define( 'ELEMENTIZE_DIR', __DIR__ );

require_once __DIR__ . '/includes/elementize-bootstrap.inc';
require_once __DIR__ . '/includes/runtime/elementize-chatgpt-action-schema.inc';
Elementize_ChatGPT_Action_Schema::init();
require_once __DIR__ . '/includes/runtime/elementize-chatgpt-vision.inc';
Elementize_ChatGPT_Vision::init();
require_once __DIR__ . '/includes/runtime/elementize-chatgpt-vision-dispatch.inc';
Elementize_ChatGPT_Vision_Dispatch::init();
require_once __DIR__ . '/includes/runtime/elementize-chatgpt-vision-bounds.inc';
Elementize_ChatGPT_Vision_Bounds::init();
require_once __DIR__ . '/includes/runtime/elementize-template-response.inc';
Elementize_Template_Response::init();

register_activation_hook( __FILE__, [ Elementize_Onboarding::class, 'activate' ] );