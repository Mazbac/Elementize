<?php
if ( ! defined( 'ABSPATH' ) ) exit;

/**
 * Keeps the Setup-page GPT Action schema aligned with the native ChatGPT
 * vision handoff without changing the existing Action operation surface.
 *
 * OpenAI only promotes openaiFileResponse entries to conversation files when
 * that response field is declared in the Action's OpenAPI response schema.
 */
final class Elementize_ChatGPT_Action_Schema {
    private const OLD_RESPONSE = "      responses:\n        '200': { description: Render verification, bounded screenshot metadata, and optional advisory local visual critique }\n        '403': { description: Page is not the active Creative Control page }";

    private const NEW_RESPONSE = "      responses:\n        '200':\n          description: Render verification plus optional native ChatGPT vision file handoff\n          content:\n            application/json:\n              schema:\n                type: object\n                additionalProperties: true\n                properties:\n                  openaiFileResponse:\n                    type: array\n                    maxItems: 1\n                    items:\n                      type: object\n                      required: [name, mime_type, content]\n                      properties:\n                        name: { type: string }\n                        mime_type: { type: string }\n                        content: { type: string, format: byte }\n        '403': { description: Page is not the active Creative Control page }";

    public static function init(): void {
        add_action( 'admin_enqueue_scripts', [ self::class, 'patch_setup_schema' ], 30 );
    }

    public static function patch_schema( string $schema ): string {
        if ( '' === $schema || false !== strpos( $schema, 'openaiFileResponse:' ) ) return $schema;
        $count = 0;
        $patched = str_replace( self::OLD_RESPONSE, self::NEW_RESPONSE, $schema, $count );
        return 1 === $count ? $patched : $schema;
    }

    public static function patch_setup_schema( string $hook ): void {
        if ( 'toplevel_page_elementize' !== $hook ) return;
        if ( ! wp_script_is( 'elementize-admin', 'enqueued' ) ) return;

        $old = wp_json_encode( self::OLD_RESPONSE );
        $new = wp_json_encode( self::NEW_RESPONSE );
        if ( ! is_string( $old ) || ! is_string( $new ) ) return;

        $script = '(function(){'
            . 'var c=window.ElementizeAdminConfig;'
            . 'if(!c||typeof c.schema!=="string"||c.schema.indexOf("openaiFileResponse:")!==-1)return;'
            . 'var oldBlock=' . $old . ';var newBlock=' . $new . ';'
            . 'if(c.schema.indexOf(oldBlock)===-1)return;'
            . 'c.schema=c.schema.replace(oldBlock,newBlock);'
            . '})();';

        wp_add_inline_script( 'elementize-admin', $script, 'before' );
    }
}
