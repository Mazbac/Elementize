export type PageKey = 'home' | 'setup' | 'activity' | 'connection' | 'settings';

export interface ActivityPageIdentity {
  id: number;
  title: string;
  status: string;
  modified_gmt: string;
  permalink?: string | null;
  preview_url?: string | null;
  elementor_edit_url?: string | null;
}

export interface ActivityItem {
  id: string;
  created_gmt: string;
  page_id: number;
  page_title: string;
  page_status: string;
  user_id: number;
  user_name: string;
  revision_id: number;
  before_hash: string;
  after_hash: string;
  updated_count: number;
  kinds: Record<string, number>;
  changes: Array<{
    kind: string;
    element_id: string;
    setting_path: Array<string | number>;
  }>;
  undone_at_gmt: string;
  undo_revision_id: number;
  snapshot_available: boolean;
  undo_available: boolean;
  page: ActivityPageIdentity | null;
}

export interface ActivityResponse {
  items: ActivityItem[];
  returned_count: number;
  max_history: number;
}

export interface ConnectionItem {
  uuid: string;
  name: string;
  created: number;
  lastSuccessfulCall: number;
}

export interface ConnectionSummary {
  connected: boolean;
  activeCount: number;
  lastSuccessfulCall: number;
  connections: ConnectionItem[];
}

export interface ElementizeAdminConfig {
  version: string;
  requirements: {
    elementor: boolean;
    elementorDetail: string;
    pixfort: boolean;
    pixfortDetail: string;
    revisions: boolean;
    applicationPasswords: boolean;
  };
  environment: {
    siteOrigin: string;
    sitePublicHttps: boolean;
    storedPublic: string;
    effectiveOrigin: string;
    connectionReady: boolean;
  };
  materialsReady: boolean;
  allReady: boolean;
  instructions: string;
  schema: string;
  notice: string;
  testPrompt: string;
  urls: {
    gptBuilder: string;
    ajax: string;
    adminPost: string;
    setup: string;
  };
  nonces: {
    generateKey: string;
    saveConnection: string;
  };
}

declare global {
  interface Window {
    ElementizeAdminConfig?: ElementizeAdminConfig;
  }
}
