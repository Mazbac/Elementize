<?php
if ( ! defined( 'ABSPATH' ) ) exit;

require_once __DIR__ . '/elementize-core.inc';
require_once __DIR__ . '/elementize-pixfort-icons.inc';
require_once __DIR__ . '/elementize-content.inc';
require_once __DIR__ . '/elementize-media-library.inc';
require_once __DIR__ . '/elementize-media-import.inc';
require_once __DIR__ . '/elementize-activity.inc';
require_once __DIR__ . '/elementize-onboarding.inc';

Elementize_Core::init();
Elementize_Pixfort_Icons::init();
Elementize_Content::init();
Elementize_Media_Library::init();
Elementize_Media_Import::init();
Elementize_Activity::init();
Elementize_Onboarding::init();
