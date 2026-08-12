<?php
if ( ! defined( 'ABSPATH' ) ) exit;

require_once __DIR__ . '/elementize-core.inc';
require_once __DIR__ . '/elementize-pixfort-icons.inc';
require_once __DIR__ . '/elementize-pixfort-transport.inc';
require_once __DIR__ . '/elementize-pixfort-theme-tokens.inc';
require_once __DIR__ . '/elementize-content.inc';
require_once __DIR__ . '/elementize-context.inc';
require_once __DIR__ . '/elementize-capabilities.inc';
require_once __DIR__ . '/elementize-design.inc';
require_once __DIR__ . '/elementize-tree.inc';
require_once __DIR__ . '/elementize-templates.inc';
require_once __DIR__ . '/elementize-creative.inc';
require_once __DIR__ . '/elementize-media-library.inc';
require_once __DIR__ . '/elementize-media-import.inc';
require_once __DIR__ . '/elementize-activity.inc';
require_once __DIR__ . '/elementize-connections.inc';
require_once __DIR__ . '/elementize-onboarding.inc';

Elementize_Core::init();
Elementize_Pixfort_Icons::init();
Elementize_Pixfort_Transport::init();
Elementize_Content::init();
Elementize_Context::init();
Elementize_Capabilities::init();
Elementize_Design::init();
Elementize_Templates::init();
Elementize_Creative::init();
Elementize_Media_Library::init();
Elementize_Media_Import::init();
Elementize_Activity::init();
Elementize_Connections::init();
Elementize_Onboarding::init();