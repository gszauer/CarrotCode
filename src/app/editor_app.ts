import {
  AI_COMPACT_PROMPT_DOC_PATH,
  AI_HARMONY_TOOL_PROMPT_DOC_PATH,
  AI_SETTINGS_DOC_PATH,
  AI_SYSTEM_PROMPT_DOC_PATH,
  AI_TAG_TOOL_PROMPT_DOC_PATH,
  ChatHarness,
  DEFAULT_AI_ENDPOINT_CONFIG,
  DEFAULT_AI_RUNTIME_SETTINGS,
  checkOpenAICompatibleServer,
  loadAiCompactPrompt,
  loadAiEndpointConfig,
  loadAiHarmonyToolPrompt,
  loadAiSystemPrompt,
  loadAiTagToolPrompt,
  resetAiPromptStorage,
  resolveAiContextTokens,
  saveAiCompactPrompt,
  saveAiEndpointConfig,
  saveAiHarmonyToolPrompt,
  saveAiSystemPrompt,
  saveAiTagToolPrompt,
  type AiModelInfo,
  type AiServerCheckResult,
  type AiWorkspaceChange,
  type ChatMessage,
  type ContextBundle,
  type DuplicateToolCallDecision,
  type DuplicateToolCallInfo,
  type AiToolCallFormat,
  type AiRuntimeSettings,
  type ToolCallLimitDecision
} from "../assistant/chat";
import { syntaxFromPath, TextDocument, type Position, type Selection } from "../editor/document";
import { DocumentStore } from "../editor/document_store";
import { isUnsupportedFilePath, UNSUPPORTED_FILE_TEXT } from "../editor/file_types";
import { Highlighter, TokenType, type Token } from "../editor/highlighter";
import { importFileList } from "../platform/drag_drop";
import { IndexedVfs } from "../platform/indexed_vfs";
import { InputBridge, TextInputTarget } from "../platform/input_bridge";
import { ViewportService } from "../platform/viewport";
import { clamp, Color, rectContains, Rect } from "../shared/types";
import type { VfsNode } from "../vfs/types";
import { basename, dirname, joinPath, normalizePath } from "../vfs/path";
import { Point, WebglRenderer, type FontName, type FontSource } from "../renderer/webgl_renderer";
import { applyTheme, theme, type ThemeName } from "../renderer/theme";
import { MiniBuffer } from "./mini_buffer";
import JSZip from "jszip";

type SidebarMode = "files" | "search" | "chat" | "settings";
type DockZone = "center" | "left" | "right" | "top" | "bottom";
type SplitDirection = "row" | "column";
type SettingTextKey = "aiBaseUrl" | "aiApiKey" | "aiModel" | "aiMaxContextTokens";
type TextFieldKey = "search" | "projectReplace" | "find" | "findReplace" | SettingTextKey;
type SettingNumberKey = "fontSize" | "uiScale" | "tabSpaces" | "aiMaxToolCalls" | "aiCompactFreePercent";
type SettingCheckboxKey = "monospacedFont" | "useTabStops" | "showWhitespace" | "showThinking" | "renameOnDoubleClick" | "showLineNumbers" | "rememberOpenFiles" | "aiModelManual" | "aiDetectDuplicateToolCalls" | "aiInsertEditorContext";
type SettingDropdownKey = "theme" | "aiProvider" | "aiToolCallFormat" | "aiModel";
type SettingButtonAction = "resetAll" | "clearFileSystem" | "editSystemPrompt" | "editTagToolPrompt" | "editHarmonyToolPrompt" | "editCompactPrompt" | "checkAiServer" | "probeLmStudioModels" | "probeLmStudioMaxTokens";
type SettingHeaderId = "visual" | "interface" | "ai" | "danger";
type AiProvider = "local" | "openai";
type AiConnectionStatus = {
  state: "idle" | "checking" | "ok" | "error";
  message: string;
  baseUrl?: string | undefined;
  checkedAt?: number | undefined;
};
type AiEndpointFieldState = "ok" | "error" | null;
type EditorContextMenuCommand = "cut" | "copy" | "paste" | "systemCopy" | "systemPaste" | "undo" | "redo";
type FileContextMenuCommand = "rename" | "duplicate" | "delete";
type FolderContextMenuCommand = "rename" | "delete" | "createFile" | "createFolder" | "uploadFile";
type TabContextMenuCommand = "save" | "findInFile" | "close" | "closeOthers" | "resetSettings";
type TabBarContextMenuCommand = "newFile" | "uploadFile" | "closeAll";
type TabOverflowContextMenuCommand = `selectTab:${string}`;
type HighlightContextMenuCommand = `highlight:${string}`;
type GutterContextMenuCommand = "toggleLineNumbers";
type SettingContextMenuCommand = "themeDark" | "themeLight" | "aiProviderLocal" | "aiProviderOpenAI" | "aiToolFormatNone" | "aiToolFormatTag" | "aiToolFormatHarmony" | `aiModel:${string}`;
type ChatContextMenuCommand = "exportChat" | "debugChat" | "clearChat" | "compactChat" | "copyBubble" | "copyChat" | "systemCopyBubble" | "systemCopyChat";
type ContextMenuCommand = EditorContextMenuCommand | FileContextMenuCommand | FolderContextMenuCommand | TabContextMenuCommand | TabBarContextMenuCommand | TabOverflowContextMenuCommand | HighlightContextMenuCommand | GutterContextMenuCommand | SettingContextMenuCommand | ChatContextMenuCommand;
type ModalAction = "save" | "discard" | "cancel" | "delete" | "download" | "replace" | "append" | "clearChat" | "allowMore" | "allowAll" | "stopToolCalls" | "allowDuplicateTool" | "breakDuplicateTool";
type ModalButtonVariant = "primary" | "secondary" | "danger";
type DockTarget = { groupId: string; zone: DockZone; polygon: Point[]; previewRect: Rect };
type EditorGroup = {
  id: string;
  tabs: string[];
  activeDocId: string | null;
  frameRect: Rect;
  editorRect: Rect;
};
type DockNode =
  | { type: "leaf"; group: EditorGroup }
  | { type: "split"; id: string; direction: SplitDirection; children: DockNode[]; weights: number[] };
type DockPreview = { groupId: string; zone: DockZone; rect: Rect; polygon: Point[] };
type TabLayoutItem = { docId: string; label: string; width: number; start: number; end: number };
type TabLayout = { items: TabLayoutItem[]; stripRect: Rect; overflowButtonRect: Rect | null; scroll: number; maxScroll: number; totalWidth: number };
type TabInsertionPreview = { groupId: string; index: number; rect: Rect };
type ContextMenuItem = { kind: "item"; command: ContextMenuCommand; label: string; rect: Rect; enabled: boolean };
type ContextMenuSeparator = { kind: "separator"; rect: Rect };
type ContextMenuEntry = ContextMenuItem | ContextMenuSeparator;
type ContextMenuSeed = Omit<ContextMenuItem, "kind" | "rect"> | { separator: true };
type ContextMenuScope =
  | { type: "editor"; groupId: string; docId: string }
  | { type: "file"; path: string }
  | { type: "folder"; path: string }
  | { type: "root"; path: "/" }
  | { type: "tab"; groupId: string; docId: string }
  | { type: "tabBar"; groupId: string }
  | { type: "tabOverflow"; groupId: string }
  | { type: "highlightDropdown"; groupId: string; docId: string }
  | { type: "gutter"; groupId: string; docId: string }
  | { type: "settingsRoot" }
  | { type: "settingsDropdown"; key: SettingDropdownKey }
  | { type: "settingsNumber"; key: SettingNumberKey }
  | { type: "textField"; field: TextFieldKey }
  | { type: "chatInput" }
  | { type: "chatRoot" }
  | { type: "chatBubble"; messageId: string }
  | { type: "rename"; path: string }
  | { type: "search" };
type ContextMenuState = { scope: ContextMenuScope; rect: Rect; items: ContextMenuEntry[] };
type ModalButton = { action: ModalAction; label: string; variant: ModalButtonVariant; rect: Rect; enabled: boolean };
type ZipWorkspaceEntry = { entry: JSZip.JSZipObject; path: string };
type ModalState =
  | {
      kind: "dirtyClose";
      title: string;
      message: string;
      detail: string;
      docId: string;
      savePath?: string | undefined;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "dirtyDownload";
      title: string;
      message: string;
      detail: string;
      docId: string;
      savePath?: string | undefined;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "deleteFolder";
      title: string;
      message: string;
      detail: string;
      path: string;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "clearFileSystem";
      title: string;
      message: string;
      detail: string;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "clearChat";
      title: string;
      message: string;
      detail: string;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "zipImport";
      title: string;
      message: string;
      detail: string;
      file: File;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "zipProgress";
      title: string;
      message: string;
      detail: string;
      progress: number;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "compactProgress";
      title: string;
      message: string;
      detail: string;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "downloadReady";
      title: string;
      message: string;
      detail: string;
      url: string;
      filename: string;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "toolCallLimit";
      title: string;
      message: string;
      detail: string;
      limit: number;
      used: number;
      resolve: (decision: ToolCallLimitDecision) => void;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    }
  | {
      kind: "duplicateToolCall";
      title: string;
      message: string;
      detail: string;
      call: DuplicateToolCallInfo;
      resolve: (decision: DuplicateToolCallDecision) => void;
      defaultAction: ModalAction;
      cancelAction: ModalAction;
      pending: boolean;
      buttons: ModalButton[];
    };
type FileTreeEntry =
  | { type: "dir"; path: string; name: string; children: FileTreeEntry[] }
  | { type: "file"; path: string; name: string };
type TabDragState = {
  docId: string;
  sourceGroupId: string;
  sourceIndex: number;
  restoreRoot: DockNode;
  restoreActiveGroupId: string;
  restoreActiveDocId: string | null;
  pointer: Point;
};
type PendingTabDragState = { docId: string; groupId: string; startPoint: Point };
type DockResizeState = {
  splitId: string;
  index: number;
  direction: SplitDirection;
  startPoint: number;
  startWeights: number[];
  splitRect: Rect;
};
type EditorScrollState = { x: number; y: number };
type FindWidgetState = { open: boolean; replaceExpanded: boolean; findBuffer: MiniBuffer; replaceBuffer: MiniBuffer };
type ScrollbarAxis = "vertical" | "horizontal";
type SidebarScrollPanel = "files" | "search" | "settings";
type ChatScrollbarPanel = "chatTranscript" | "chatInput";
type EditorOverflow = { vertical: boolean; horizontal: boolean };
type SelectionHandleEdge = "start" | "end";
type DocumentWidthCacheEntry = { revision: number; layoutKey: string; width: number };
type LineWidthCacheEntry = { layoutKey: string; text: string; width: number };
type HighlightCacheEntry = { syntaxId: string; text: string; tokens: Token[] };
type ChatLineCacheEntry = { key: string; lines: string[] };
type ScrollbarDragState = {
  axis: ScrollbarAxis;
  groupId: string;
  docId: string;
  startPoint: number;
  startScroll: number;
  trackRect: Rect;
  thumbRect: Rect;
};
type ScrollbarHoverState = { axis: ScrollbarAxis; groupId: string; docId: string; overThumb: boolean };
type SettingsScrollbarDragState = { startPoint: number; startScroll: number; viewportRect: Rect; trackRect: Rect; thumbRect: Rect };
type SettingsScrollbarHoverState = { overThumb: boolean };
type SidebarScrollbarDragState = { panel: SidebarScrollPanel; startPoint: number; startScroll: number; trackRect: Rect; thumbRect: Rect; viewportRect: Rect; contentHeight: number };
type SidebarScrollbarHoverState = { panel: SidebarScrollPanel; overThumb: boolean };
type ChatScrollbarDragState = { panel: ChatScrollbarPanel; startPoint: number; startScroll: number; trackRect: Rect; thumbRect: Rect; viewportRect: Rect; contentHeight: number };
type ChatScrollbarHoverState = { panel: ChatScrollbarPanel; overThumb: boolean };
type TapState = { time: number; point: Point; key: string };
type DeferredTouchHit = { hit: HitItem; point: Point };
type TouchKeyboardHit = Extract<HitItem, { type: "editor" | "fileRenameInput" | "searchInput" | "chatInput" | "textField" | "settingsNumber" }>;
type PendingTouchKeyboardFocus = { pointerId: number; hit: TouchKeyboardHit; expiresAt: number };
type PendingTouchDoubleTap = { pointerId: number; hit: TouchKeyboardHit; point: Point; key: string };
type TextSelectionHandleTarget =
  | { type: "rename"; path: string }
  | { type: "textField"; field: TextFieldKey }
  | { type: "settingsNumber"; key: SettingNumberKey }
  | { type: "chatInput" };
type TouchLongPressState =
  | { type: "editor"; pointerId: number; groupId: string; docId: string; point: Point }
  | { type: "text"; pointerId: number; target: TextSelectionHandleTarget; inputRect: Rect; point: Point };
type SelectionHandleDragState =
  | { type: "editor"; pointerId: number; groupId: string; docId: string; edge: SelectionHandleEdge; fixed: { line: number; col: number }; point: Point }
  | { type: "mini"; pointerId: number; target: Exclude<TextSelectionHandleTarget, { type: "chatInput" }>; inputRect: Rect; edge: SelectionHandleEdge; fixed: number; point: Point }
  | { type: "chatInput"; pointerId: number; inputRect: Rect; edge: SelectionHandleEdge; fixed: { line: number; col: number }; point: Point };
type TouchScrollState =
  | {
      type: "editor";
      pointerId: number;
      groupId: string;
      docId: string;
      rect: Rect;
      startPoint: Point;
      startScroll: EditorScrollState;
      originalSelection: Selection;
      active: boolean;
    }
  | {
      type: "settings";
      pointerId: number;
      groupId: string;
      rect: Rect;
      startPoint: Point;
      startScrollY: number;
      active: boolean;
    }
  | {
      type: "sidebar";
      pointerId: number;
      panel: SidebarScrollPanel;
      rect: Rect;
      startPoint: Point;
      startScrollY: number;
      active: boolean;
    }
  | {
      type: "chat";
      pointerId: number;
      panel: ChatScrollbarPanel;
      rect: Rect;
      startPoint: Point;
      startScrollY: number;
      active: boolean;
    };
type ChatInputVisualLine = { line: number; start: number; end: number; text: string };
type AppSettings = {
  theme: ThemeName;
  fontSize: number;
  uiScale: number;
  monospacedFont: boolean;
  tabSpaces: number;
  useTabStops: boolean;
  showWhitespace: boolean;
  showThinking: boolean;
  renameOnDoubleClick: boolean;
  showLineNumbers: boolean;
  rememberOpenFiles: boolean;
  aiProvider: AiProvider;
  aiModelManual: boolean;
  aiMaxToolCalls: number;
  aiDetectDuplicateToolCalls: boolean;
  aiToolCallFormat: AiToolCallFormat;
  aiCompactFreePercent: number;
  aiInsertEditorContext: boolean;
};
type PersistedSession = {
  version: 1;
  activePath: string | null;
  activeGroupId: string | null;
  sidebarMode: SidebarMode;
  sidebarWidth: number;
  lastSidebarWidth: number;
  dockRoot: PersistedDockNode;
  scrollStates?: Record<string, EditorScrollState>;
};
type PersistedDockNode =
  | { type: "leaf"; group: { id: string; paths: string[]; activePath: string | null } }
  | { type: "split"; id: string; direction: SplitDirection; children: PersistedDockNode[]; weights: number[] };
type HitItem =
  | { type: "activity"; mode: SidebarMode; rect: Rect }
  | { type: "downloadActivity"; rect: Rect }
  | { type: "settingsActivity"; rect: Rect }
  | { type: "filesRoot"; rect: Rect }
  | { type: "settingsRoot"; rect: Rect }
  | { type: "chatRoot"; rect: Rect }
  | { type: "folder"; path: string; expanded: boolean; rect: Rect }
  | { type: "file"; path: string; rect: Rect }
  | { type: "fileRenameInput"; path: string; rect: Rect }
  | { type: "tab"; docId: string; groupId: string; rect: Rect }
  | { type: "tabClose"; docId: string; groupId: string; rect: Rect }
  | { type: "tabBar"; groupId: string; rect: Rect }
  | { type: "tabOverflow"; groupId: string; rect: Rect }
  | { type: "editorGutter"; groupId: string; docId: string; rect: Rect }
  | { type: "statusWhitespace"; rect: Rect }
  | { type: "statusHighlight"; groupId: string; docId: string; rect: Rect }
  | { type: "sidebarResize"; rect: Rect }
  | { type: "dockResize"; splitId: string; index: number; direction: SplitDirection; rect: Rect; splitRect: Rect }
  | { type: "editorScrollbar"; axis: ScrollbarAxis; groupId: string; docId: string; rect: Rect; trackRect: Rect; thumbRect: Rect }
  | { type: "selectionHandle"; edge: SelectionHandleEdge; groupId: string; docId: string; rect: Rect }
  | { type: "textSelectionHandle"; edge: SelectionHandleEdge; target: TextSelectionHandleTarget; inputRect: Rect; rect: Rect }
  | { type: "settingsScrollbar"; rect: Rect; trackRect: Rect; thumbRect: Rect; viewportRect: Rect }
  | { type: "sidebarScrollbar"; panel: SidebarScrollPanel; rect: Rect; trackRect: Rect; thumbRect: Rect; viewportRect: Rect; contentHeight: number }
  | { type: "contextMenu"; command: ContextMenuCommand; rect: Rect; enabled: boolean }
  | { type: "modalButton"; action: ModalAction; rect: Rect; enabled: boolean }
  | { type: "settingsHeader"; id: SettingHeaderId; rect: Rect }
  | { type: "settingsCheckbox"; key: SettingCheckboxKey; rect: Rect }
  | { type: "settingsDropdown"; key: SettingDropdownKey; rect: Rect }
  | { type: "settingsNumber"; key: SettingNumberKey; rect: Rect }
  | { type: "settingsButton"; action: SettingButtonAction; rect: Rect; enabled: boolean }
  | { type: "textField"; field: TextFieldKey; rect: Rect }
  | { type: "searchReplaceToggle"; rect: Rect }
  | { type: "searchRefresh"; rect: Rect }
  | { type: "searchReplaceAll"; rect: Rect; enabled: boolean }
  | { type: "findToggle"; rect: Rect }
  | { type: "findPrevious"; rect: Rect; enabled: boolean }
  | { type: "findNext"; rect: Rect; enabled: boolean }
  | { type: "findClose"; rect: Rect }
  | { type: "findReplace"; rect: Rect; enabled: boolean }
  | { type: "findReplaceAll"; rect: Rect; enabled: boolean }
  | { type: "searchInput"; rect: Rect }
  | { type: "searchResult"; path: string; line: number; rect: Rect }
  | { type: "chatTranscript"; rect: Rect }
  | { type: "chatBubble"; messageId: string; rect: Rect; viewportRect: Rect }
  | { type: "chatInput"; rect: Rect }
  | { type: "chatSend"; rect: Rect; enabled: boolean; label: string }
  | { type: "chatShowThinking"; rect: Rect }
  | { type: "chatScrollbar"; panel: ChatScrollbarPanel; rect: Rect; trackRect: Rect; thumbRect: Rect; viewportRect: Rect; contentHeight: number }
  | { type: "editor"; groupId: string; rect: Rect };

const DOCK_SPLITTER_GAP = 1;
const DOCK_SPLITTER_HIT_SIZE = 9;
const DOCK_MIN_PANEL_SIZE = 140;
const DOCK_EDGE_TARGET_RATIO = 0.33;
const DOCK_CENTER_TARGET_RATIO = 0.34;
const EDITOR_SCROLLBAR_SIZE = 12;
const EDITOR_SCROLLBAR_THUMB_MIN = 24;
const EDITOR_GUTTER_MIN_DIGITS = 3;
const EDITOR_GUTTER_PAD_LEFT = 10;
const EDITOR_GUTTER_PAD_RIGHT = 12;
const EDITOR_TEXT_PAD_X = 8;
const EDITOR_TEXT_TRAILING_PAD_X = 20;
const PANEL_HEADER_H = 32;
const TAB_MIN_W = 128;
const TAB_MAX_W = 240;
const TAB_GAP = 1;
const TAB_OVERFLOW_BUTTON_W = 32;
const TAB_AUTOSCROLL_EDGE_W = 34;
const CONTEXT_MENU_WIDTH = 136;
const CONTEXT_MENU_ROW_H = 28;
const CONTEXT_MENU_SEPARATOR_H = 9;
const CONTEXT_MENU_PAD = 4;
const MODAL_WIDTH = 420;
const MODAL_BUTTON_H = 30;
const MODAL_BUTTON_GAP = 8;
const TAB_DRAG_THRESHOLD = 6;
const TOUCH_SCROLL_THRESHOLD = 10;
const TOUCH_DOUBLE_TAP_MS = 420;
const TOUCH_DOUBLE_TAP_DISTANCE = 28;
const TOUCH_LONG_PRESS_MS = 540;
const TOUCH_KEYBOARD_STABILIZE_MS = 900;
const SELECTION_HANDLE_TOUCH_SIZE = 26;
const SELECTION_HANDLE_AUTOSCROLL_EDGE = 42;
const SELECTION_HANDLE_AUTOSCROLL_MAX_STEP = 18;
const CARET_BLINK_HALF_MS = 530;
const HIGHLIGHT_OPTIONS = [
  { id: "plain", label: "Plain" },
  { id: "javascript", label: "JavaScript" },
  { id: "cpp", label: "C/C++" },
  { id: "json", label: "JSON" },
  { id: "markdown", label: "Markdown" },
  { id: "lua", label: "Lua" },
  { id: "python", label: "Python" }
] as const;
const SETTINGS_TAB_ID = "settings";
const SETTINGS_TAB_LABEL = "Settings";
const SETTINGS_STORAGE_KEY = "slug.settings";
const SESSION_STORAGE_KEY = "slug.session";
const DEFAULT_SETTINGS: AppSettings = {
  theme: "dark",
  fontSize: 14,
  uiScale: 100,
  monospacedFont: false,
  tabSpaces: 4,
  useTabStops: true,
  showWhitespace: false,
  showThinking: true,
  renameOnDoubleClick: true,
  showLineNumbers: true,
  rememberOpenFiles: true,
  aiProvider: "openai",
  aiModelManual: false,
  aiMaxToolCalls: DEFAULT_AI_RUNTIME_SETTINGS.maxToolCallsPerTurn,
  aiDetectDuplicateToolCalls: DEFAULT_AI_RUNTIME_SETTINGS.detectDuplicateToolCalls,
  aiToolCallFormat: DEFAULT_AI_RUNTIME_SETTINGS.toolCallFormat,
  aiCompactFreePercent: DEFAULT_AI_RUNTIME_SETTINGS.compactFreePercent,
  aiInsertEditorContext: true
};

export class EditorApp {
  readonly input: InputBridge;
  readonly viewport: ViewportService;
  readonly renderer: WebglRenderer;
  readonly docs: DocumentStore;
  readonly highlighter = new Highlighter();
  readonly chat: ChatHarness;
  readonly searchBuffer = new MiniBuffer();
  readonly projectReplaceBuffer = new MiniBuffer();
  readonly chatDraft = new TextDocument(undefined, "");
  readonly renameBuffer = new MiniBuffer();
  private readonly settingsTextBuffers: Record<SettingTextKey, MiniBuffer> = {
    aiBaseUrl: new MiniBuffer(),
    aiApiKey: new MiniBuffer(),
    aiModel: new MiniBuffer(),
    aiMaxContextTokens: new MiniBuffer()
  };
  sidebarMode: SidebarMode = "files";
  sidebarWidth = 280;
  private lastSidebarWidth = 280;
  files: VfsNode[] = [];
  private treeNodes: VfsNode[] = [];
  private readonly expandedFolders = new Set<string>();
  private readonly knownFolders = new Set<string>();
  searchResults: Array<{ path: string; line: number; text: string }> = [];
  openTabs: string[] = [];
  activeDocId: string | null = null;
  private activeGroupId = "group-main";
  private groups: EditorGroup[] = [makeGroup("group-main")];
  private dockRoot: DockNode = { type: "leaf", group: this.groups[0]! };
  private readonly scrollStates = new Map<string, EditorScrollState>();
  private readonly tabScrollStates = new Map<string, number>();
  private readonly pendingTabRevealIds = new Set<string>();
  private readonly documentWidthCache = new Map<string, DocumentWidthCacheEntry>();
  private readonly lineWidthCache = new Map<string, LineWidthCacheEntry>();
  private readonly highlightCache = new Map<string, HighlightCacheEntry>();
  private readonly chatLineCache = new Map<string, ChatLineCacheEntry>();
  statusText = "Ready";
  private readonly hits: HitItem[] = [];
  private raf = 0;
  private selecting = false;
  private resizingSidebar = false;
  private dockResize: DockResizeState | null = null;
  private scrollbarDrag: ScrollbarDragState | null = null;
  private hoveredScrollbar: ScrollbarHoverState | null = null;
  private settingsScrollY = 0;
  private settingsScrollbarDrag: SettingsScrollbarDragState | null = null;
  private hoveredSettingsScrollbar: SettingsScrollbarHoverState | null = null;
  private filesScrollY = 0;
  private searchScrollY = 0;
  private chatScrollY = 0;
  private chatInputScrollY = 0;
  private aiModels: AiModelInfo[] = [];
  private aiConnectionStatus: AiConnectionStatus = { state: "idle", message: "" };
  private aiEndpointFieldState: AiEndpointFieldState = null;
  private sidebarScrollbarDrag: SidebarScrollbarDragState | null = null;
  private hoveredSidebarScrollbar: SidebarScrollbarHoverState | null = null;
  private chatScrollbarDrag: ChatScrollbarDragState | null = null;
  private hoveredChatScrollbar: ChatScrollbarHoverState | null = null;
  private hoveredActivityButton: SidebarMode | "download" | "settings" | null = null;
  private hoveredButton: string | null = null;
  private selectedFileTreePath: string | null = null;
  private hoveredFileTreePath: string | null = null;
  private contextMenu: ContextMenuState | null = null;
  private contextMenuHover: ContextMenuCommand | null = null;
  private modal: ModalState | null = null;
  private modalHover: ModalAction | null = null;
  private renamePath: string | null = null;
  private renameSelecting = false;
  private searchSelecting = false;
  private chatInputSelecting = false;
  private textFieldSelecting: TextFieldKey | null = null;
  private searchReplaceExpanded = false;
  private readonly findStates = new Map<string, FindWidgetState>();
  private readonly inactiveFindBuffer = new MiniBuffer();
  private readonly inactiveFindReplaceBuffer = new MiniBuffer();
  private caretBlinkEpoch = performance.now();
  private caretBlinkTimer = 0;
  private pendingTabDrag: PendingTabDragState | null = null;
  private tabDrag: TabDragState | null = null;
  private dockPreview: DockPreview | null = null;
  private tabInsertionPreview: TabInsertionPreview | null = null;
  private lastTabDragPoint: Point | null = null;
  private tabDragAutoscrollTimer = 0;
  private lastTouchTap: TapState | null = null;
  private touchLongPress: TouchLongPressState | null = null;
  private touchLongPressTimer = 0;
  private touchScroll: TouchScrollState | null = null;
  private deferredTouchHit: DeferredTouchHit | null = null;
  private pendingTouchKeyboardFocus: PendingTouchKeyboardFocus | null = null;
  private pendingTouchDoubleTap: PendingTouchDoubleTap | null = null;
  private touchKeyboardStabilizeUntil = 0;
  private touchKeyboardStabilizeTimer = 0;
  private selectionHandleDrag: SelectionHandleDragState | null = null;
  private selectionHandleAutoscrollFrame = 0;
  private editorRect: Rect = { x: 0, y: 0, w: 0, h: 0 };
  private readonly settingsExpanded = new Set<SettingHeaderId>(["visual", "interface", "ai"]);
  private settings: AppSettings = loadSettings();
  private activeSettingsNumber: SettingNumberKey | null = null;
  private readonly settingsNumberBuffer = new MiniBuffer();
  private settingsNumberSelecting = false;
  private activeSettingsText: SettingTextKey | null = null;
  private settingsHitClip: Rect | null = null;
  private settingsViewportRect: Rect | null = null;
  private focusedSettingsInputRect: Rect | null = null;
  private pendingFocusedInputReveal = false;
  private localClipboard = "";
  private systemClipboardOverlay: HTMLDivElement | null = null;
  private systemClipboardViewportCleanup: (() => void) | null = null;
  private pendingCloseQueue: string[] = [];
  private pendingDownloadDirtyQueue: string[] = [];
  private downloadInProgress = false;
  private uploadInput: HTMLInputElement | null = null;
  private systemFileUploadOverlay: HTMLDivElement | null = null;
  private systemFileUploadViewportCleanup: (() => void) | null = null;
  private uploadTargetFolder = "/";
  private untitledCounter = 1;
  private readonly untitledLabels = new Map<string, string>();
  private readonly untitledPreferredNames = new Map<string, string>();
  private fileDragActive = false;
  private fileDragLabel = "Drop to upload";

  constructor(private readonly canvas: HTMLCanvasElement, readonly vfs: IndexedVfs, fontSources: FontSource[]) {
    this.viewport = new ViewportService(canvas);
    this.viewport.start();
    const gl = canvas.getContext("webgl2");
    if (!gl) throw new Error("WebGL2 is required");
    this.renderer = new WebglRenderer(canvas, this.viewport.get(), fontSources);
    this.applySettings();
    this.docs = new DocumentStore(vfs);
    this.input = new InputBridge(document.body);
    this.chat = new ChatHarness(vfs);
    this.installEvents();
  }

  async start(): Promise<void> {
    localStorage.removeItem("slug.aiHelperPrompts");
    await this.refreshFiles();
    await this.restoreEditorSession();
    this.draw();
    this.scheduleDraw();
  }

  activeDoc(): TextDocument | undefined {
    return this.activeDocId && !this.isSettingsTab(this.activeDocId) ? this.docs.get(this.activeDocId) : undefined;
  }

  private activeFindState(create = true): FindWidgetState | null {
    const doc = this.activeDoc();
    return doc ? this.findStateForDoc(doc.id, create) : null;
  }

  private findStateForDoc(docId: string | null | undefined, create = true): FindWidgetState | null {
    if (!docId || this.isSettingsTab(docId)) return null;
    let state = this.findStates.get(docId);
    if (!state && create) {
      state = { open: false, replaceExpanded: false, findBuffer: new MiniBuffer(), replaceBuffer: new MiniBuffer() };
      this.findStates.set(docId, state);
    }
    return state ?? null;
  }

  private isSettingsTab(id: string | null | undefined): boolean {
    return id === SETTINGS_TAB_ID;
  }

  private tabLabel(id: string): string {
    if (this.isSettingsTab(id)) return SETTINGS_TAB_LABEL;
    const doc = this.docs.get(id);
    return doc ? this.documentLabel(doc) : "(untitled)";
  }

  private documentLabel(doc: TextDocument): string {
    if (doc.path && this.isAiSpecialPath(doc.path)) return this.aiSpecialLabel(doc.path);
    return doc.path ?? this.untitledLabels.get(doc.id) ?? "Untitled";
  }

  private isAiSpecialPath(path: string | undefined): boolean {
    const normalized = path ? normalizePath(path) : "";
    return normalized === AI_SETTINGS_DOC_PATH
      || normalized === AI_SYSTEM_PROMPT_DOC_PATH
      || normalized === AI_TAG_TOOL_PROMPT_DOC_PATH
      || normalized === AI_HARMONY_TOOL_PROMPT_DOC_PATH
      || normalized === AI_COMPACT_PROMPT_DOC_PATH;
  }

  private isAiSpecialDoc(doc: TextDocument | undefined): boolean {
    return Boolean(doc?.path && this.isAiSpecialPath(doc.path));
  }

  private aiSpecialLabel(path: string): string {
    const normalized = normalizePath(path);
    if (normalized === AI_SETTINGS_DOC_PATH) return "AI Settings";
    if (normalized === AI_SYSTEM_PROMPT_DOC_PATH) return "System Prompt";
    if (normalized === AI_TAG_TOOL_PROMPT_DOC_PATH) return "Tag Tool Prompt";
    if (normalized === AI_HARMONY_TOOL_PROMPT_DOC_PATH) return "Harmony Tool Prompt";
    if (normalized === AI_COMPACT_PROMPT_DOC_PATH) return "Compact Prompt";
    return "AI Document";
  }

  async refreshFiles(): Promise<void> {
    this.treeNodes = await this.listTreeNodes("/");
    this.files = this.treeNodes.filter((node) => node.kind === "file");
    this.syncFileTreeFolders();
    this.syncFileTreeSelection();
  }

  private async listTreeNodes(path: string): Promise<VfsNode[]> {
    const children = (await this.vfs.listDir(path)).filter((node) => node.path !== "/" && !node.path.startsWith("/.slug-"));
    const result: VfsNode[] = [];
    for (const node of children) {
      result.push(node);
      if (node.kind === "dir") result.push(...await this.listTreeNodes(node.path));
    }
    return result;
  }

  async openFile(path: string, options: { focus?: boolean | undefined } = {}): Promise<void> {
    const doc = await this.docs.open(path);
    const existing = this.groupContaining(doc.id);
    const group = existing ?? this.activeGroup();
    if (!group.tabs.includes(doc.id)) group.tabs.push(doc.id);
    group.activeDocId = doc.id;
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.revealTabInGroup(group, doc.id);
    this.selectFileTreePath(doc.path ?? null);
    this.syncOpenTabs();
    this.statusText = doc.readOnly ? "File type not supported" : `Opened ${path}`;
    if (options.focus !== false && !this.renamePath) this.focusEditor();
    else if (options.focus === false) this.input.blur();
    this.scheduleDraw();
  }

  openUntitledDocument(groupId = this.activeGroupId, options: { label?: string | undefined; text?: string | undefined; preferredName?: string | undefined; dirty?: boolean | undefined; readOnly?: boolean | undefined } = {}): TextDocument {
    const group = this.groupById(groupId);
    const doc = this.docs.createUntitled(options.text ?? "");
    doc.readOnly = Boolean(options.readOnly);
    this.untitledLabels.set(doc.id, options.label || `Untitled-${this.untitledCounter++}`);
    if (options.preferredName) {
      this.untitledPreferredNames.set(doc.id, options.preferredName);
      doc.syntaxId = syntaxFromPath(options.preferredName);
    }
    if (options.dirty && !doc.readOnly) doc.revision = doc.savedRevision + 1;
    group.tabs.push(doc.id);
    group.activeDocId = doc.id;
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.revealTabInGroup(group, doc.id);
    this.syncOpenTabs();
    this.statusText = doc.readOnly ? "File type not supported" : `Opened ${this.documentLabel(doc)}`;
    this.focusEditor();
    this.scheduleDraw();
    return doc;
  }

  openSettingsTab(): void {
    this.sidebarMode = "settings";
    this.sidebarWidth = this.sidebarWidth > 0 ? this.sidebarWidth : this.lastSidebarWidth || 280;
    this.input.blur();
    this.scheduleDraw();
  }

  private openAiSettingsDocument(): void {
    this.openVirtualAiDocument(AI_SETTINGS_DOC_PATH, JSON.stringify(loadAiEndpointConfig(), null, 2));
  }

  private openSystemPromptDocument(): void {
    this.openVirtualAiDocument(AI_SYSTEM_PROMPT_DOC_PATH, loadAiSystemPrompt());
  }

  private openTagToolPromptDocument(): void {
    this.openVirtualAiDocument(AI_TAG_TOOL_PROMPT_DOC_PATH, loadAiTagToolPrompt());
  }

  private openHarmonyToolPromptDocument(): void {
    this.openVirtualAiDocument(AI_HARMONY_TOOL_PROMPT_DOC_PATH, loadAiHarmonyToolPrompt());
  }

  private openCompactPromptDocument(): void {
    this.openVirtualAiDocument(AI_COMPACT_PROMPT_DOC_PATH, loadAiCompactPrompt());
  }

  private openVirtualAiDocument(path: string, text: string): void {
    const doc = this.docs.getByPath(path) ?? this.docs.createVirtual(path, text);
    const existing = this.groupContaining(doc.id);
    const group = existing ?? this.activeGroup();
    if (!group.tabs.includes(doc.id)) group.tabs.push(doc.id);
    group.activeDocId = doc.id;
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.revealTabInGroup(group, doc.id);
    this.syncOpenTabs();
    this.statusText = `Opened ${this.aiSpecialLabel(path)}`;
    this.focusEditor();
    this.scheduleDraw();
  }

  scheduleDraw(): void {
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => {
      this.raf = 0;
      this.draw();
    });
  }

  private resetCaretBlink(): void {
    this.caretBlinkEpoch = performance.now();
    if (this.caretBlinkTimer) {
      window.clearTimeout(this.caretBlinkTimer);
      this.caretBlinkTimer = 0;
    }
    this.syncInputBridgeSelection();
    this.scheduleDraw();
  }

  private syncInputBridgeSelection(): void {
    const target = this.input.activeTarget;
    if (!target || this.input.composing) return;
    this.input.syncSelectionForClipboard(target.getSelectedText());
  }

  private saveAndApplySettings(): void {
    localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(this.settings));
    localStorage.setItem("slug.aiProvider", this.settings.aiProvider);
    this.applySettings();
    if (this.settings.rememberOpenFiles) this.persistEditorSession();
    else this.clearPersistedEditorSession();
    this.scheduleDraw();
  }

  private applySettings(): void {
    applyTheme(this.settings.theme);
    localStorage.setItem("slug.aiProvider", this.settings.aiProvider);
    this.renderer.configureText(this.settings.fontSize, this.settings.uiScale, this.settings.monospacedFont);
    this.documentWidthCache.clear();
    this.lineWidthCache.clear();
    this.highlightCache.clear();
  }

  private ui(value: number): number {
    return value * this.settings.uiScale / 100;
  }

  private isCaretBlinkOn(): boolean {
    return Math.floor((performance.now() - this.caretBlinkEpoch) / CARET_BLINK_HALF_MS) % 2 === 0;
  }

  private scheduleCaretBlinkFrame(): void {
    if (!this.hasBlinkingCaretOwner() || this.caretBlinkTimer) return;
    const elapsed = performance.now() - this.caretBlinkEpoch;
    const wait = CARET_BLINK_HALF_MS - (elapsed % CARET_BLINK_HALF_MS);
    this.caretBlinkTimer = window.setTimeout(() => {
      this.caretBlinkTimer = 0;
      this.scheduleDraw();
    }, Math.max(16, wait + 1));
  }

  getStateForTests(): unknown {
    const activeLabel = this.activeDocId ? this.tabLabel(this.activeDocId) : undefined;
    const findState = this.activeFindState(false);
    return {
      activePath: this.activeDoc() ? this.documentLabel(this.activeDoc()!) : (this.isSettingsTab(this.activeDocId) ? SETTINGS_TAB_LABEL : undefined),
      activeText: this.activeDoc()?.getText(),
      activeSyntaxId: this.activeDoc()?.syntaxId,
      selectedText: this.activeDoc()?.selectedText() ?? "",
      openTabs: this.openTabs.map((id) => this.tabLabel(id)),
      activeTab: activeLabel,
      sidebarMode: this.sidebarMode,
      sidebarVisible: this.sidebarWidth > 0,
      statusText: this.statusText,
      fileDragActive: this.fileDragActive,
      fileDragLabel: this.fileDragLabel,
      filesScrollY: this.filesScrollY,
      searchScrollY: this.searchScrollY,
      settings: { ...this.settings },
      settingsActivityTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "settingsActivity" }> => hit.type === "settingsActivity")?.rect ?? null,
      downloadActivityTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "downloadActivity" }> => hit.type === "downloadActivity")?.rect ?? null,
      settingsNumberText: this.settingsNumberBuffer.text,
      settingsNumberSelectedText: this.settingsNumberBuffer.selectedText(),
      settingsTextSelectedText: this.activeSettingsText ? this.settingsTextBuffers[this.activeSettingsText].selectedText() : "",
      activeSettingsNumber: this.activeSettingsNumber,
      settingsScrollY: this.settingsScrollY,
      settingsTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "settingsHeader" | "settingsCheckbox" | "settingsDropdown" | "settingsNumber" | "settingsButton" | "textField" }> =>
          hit.type === "settingsHeader" || hit.type === "settingsCheckbox" || hit.type === "settingsDropdown" || hit.type === "settingsNumber" || hit.type === "settingsButton" || (hit.type === "textField" && isSettingTextField(hit.field)))
        .map((hit) => ({ type: hit.type, key: "key" in hit ? hit.key : "id" in hit ? hit.id : "action" in hit ? hit.action : hit.field, rect: hit.rect, enabled: "enabled" in hit ? hit.enabled : true })),
      searchQuery: this.searchBuffer.text,
      searchScrollX: this.searchBuffer.scrollX,
      projectReplaceText: this.projectReplaceBuffer.text,
      searchReplaceExpanded: this.searchReplaceExpanded,
      searchSelectedText: this.searchBuffer.selectedText(),
      searchInputRect: this.searchInputRect(),
      projectReplaceInputRect: this.textFieldRect("projectReplace"),
      searchTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "textField" | "searchReplaceToggle" | "searchRefresh" | "searchReplaceAll" }> =>
          hit.type === "textField" || hit.type === "searchReplaceToggle" || hit.type === "searchRefresh" || hit.type === "searchReplaceAll")
        .filter((hit) => hit.type !== "textField" || hit.field === "search" || hit.field === "projectReplace")
        .map((hit) => ({ type: hit.type, key: "field" in hit ? hit.field : hit.type, rect: hit.rect, enabled: "enabled" in hit ? hit.enabled : true })),
      searchCaretVisible: this.isSearchCaretVisible(),
      searchResults: this.searchResults,
      searchResultTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "searchResult" }> => hit.type === "searchResult")
        .map((hit) => ({ path: hit.path, line: hit.line, rect: hit.rect })),
      findOpen: Boolean(findState?.open),
      findReplaceExpanded: Boolean(findState?.replaceExpanded),
      findQuery: findState?.findBuffer.text ?? "",
      findReplaceText: findState?.replaceBuffer.text ?? "",
      findSelectedText: findState?.findBuffer.selectedText() ?? "",
      findReplaceSelectedText: findState?.replaceBuffer.selectedText() ?? "",
      findTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "textField" | "findToggle" | "findPrevious" | "findNext" | "findClose" | "findReplace" | "findReplaceAll" }> =>
          hit.type === "textField" || hit.type === "findToggle" || hit.type === "findPrevious" || hit.type === "findNext" || hit.type === "findClose" || hit.type === "findReplace" || hit.type === "findReplaceAll")
        .filter((hit) => hit.type !== "textField" || hit.field === "find" || hit.field === "findReplace")
        .map((hit) => ({ type: hit.type, key: "field" in hit ? hit.field : hit.type, rect: hit.rect, enabled: "enabled" in hit ? hit.enabled : true })),
      chatMessages: this.chat.visibleMessages(),
      chatDisplayedMessages: this.chatDisplayMessages(),
      chatTokenUsage: this.chat.tokenUsage(),
      chatRootTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "chatRoot" }> => hit.type === "chatRoot")?.rect ?? null,
      chatBubbleTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "chatBubble" }> => hit.type === "chatBubble")
        .map((hit) => {
          const msg = this.chatDisplayMessages().find((candidate) => candidate.id === hit.messageId);
          return { id: hit.messageId, role: msg?.role ?? "", text: msg?.text ?? "", rect: hit.rect };
        }),
      activeInputKind: this.input.activeTarget?.kind ?? null,
      chatDraft: this.chatDraft.getText(),
      chatScrollY: this.chatScrollY,
      chatInputScrollY: this.chatInputScrollY,
      chatInputRect: this.hits.find((hit): hit is Extract<HitItem, { type: "chatInput" }> => hit.type === "chatInput")?.rect ?? null,
      chatSendTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "chatSend" }> => hit.type === "chatSend") ?? null,
      chatShowThinking: this.settings.showThinking,
      chatShowThinkingTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "chatShowThinking" }> => hit.type === "chatShowThinking")?.rect ?? null,
      chatRunning: this.chat.running,
      chatScrollbars: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "chatScrollbar" }> => hit.type === "chatScrollbar")
        .map((hit) => ({ panel: hit.panel, rect: hit.rect, trackRect: hit.trackRect, thumbRect: hit.thumbRect })),
      touchKeyboardStabilizing: this.isTouchKeyboardStabilizing(),
      visualViewportResizeDeferred: this.viewport.isVisualViewportCanvasResizeDeferred(),
      aiEndpointConfig: loadAiEndpointConfig(),
      aiModels: this.aiModels,
      aiConnectionStatus: { ...this.aiConnectionStatus },
      aiEndpointFieldState: this.aiEndpointFieldState,
      renamePath: this.renamePath,
      renameText: this.renameBuffer.text,
      renameSelectedText: this.renameBuffer.selectedText(),
      renameScrollX: this.renameBuffer.scrollX,
      renameInputRect: this.renameInputRect(),
      renameInvalid: this.renamePath ? !isValidFileName(this.renameBuffer.text.trim()) : false,
      renameInvalidCharacters: invalidFileNameCharacterRanges(this.renameBuffer.text).map((range) => ({ ...range, text: this.renameBuffer.text.slice(range.start, range.end) })),
      caretBlinkOn: this.isCaretBlinkOn(),
      renameCaretVisible: this.isRenameCaretVisible(),
      sidebarWidth: this.sidebarWidth,
      selectedFileTreePath: this.fileTreeSelectedPath(),
      hoveredFileTreePath: this.hoveredFileTreePath,
      fileTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "file" }> => hit.type === "file")
        .map((hit) => ({ path: hit.path, rect: hit.rect })),
      folderTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "folder" }> => hit.type === "folder")
        .map((hit) => ({ path: hit.path, expanded: hit.expanded, rect: hit.rect })),
      filesRootTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "filesRoot" }> => hit.type === "filesRoot")?.rect ?? null,
      tabTargets: this.tabHitState("tab"),
      tabCloseTargets: this.tabHitState("tabClose"),
      tabOverflowTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "tabOverflow" }> => hit.type === "tabOverflow")
        .map((hit) => ({ groupId: hit.groupId, rect: hit.rect })),
      editorGutterTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "editorGutter" }> => hit.type === "editorGutter")
        .map((hit) => ({ groupId: hit.groupId, path: this.docs.get(hit.docId)?.path ?? "(untitled)", rect: hit.rect })),
      tabBarTargets: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "tabBar" }> => hit.type === "tabBar")
        .map((hit) => ({ groupId: hit.groupId, rect: hit.rect })),
      editorGroups: this.groups.map((group) => {
        const doc = group.activeDocId ? this.docs.get(group.activeDocId) : undefined;
        return {
          id: group.id,
          activePath: doc?.path ?? (this.isSettingsTab(group.activeDocId) ? SETTINGS_TAB_LABEL : null),
          tabs: group.tabs.map((id) => this.tabLabel(id)),
          cursor: doc?.selection.head ?? null,
          caretVisible: doc ? this.isDocumentCaretVisible(group, doc.id) : false,
          scrollX: doc ? this.scrollForDoc(doc.id).x : 0,
          scrollY: doc ? this.scrollForDoc(doc.id).y : 0,
          gutterWidth: doc ? this.gutterWidthForDoc(doc) : 0,
          frameRect: group.frameRect,
          editorRect: group.editorRect
        };
      }),
      visibleCarets: this.groups.flatMap((group) => {
        const doc = group.activeDocId ? this.docs.get(group.activeDocId) : undefined;
        if (!doc || !this.isDocumentCaretVisible(group, doc.id)) return [];
        return [{ groupId: group.id, path: doc.path ?? "(untitled)", cursor: doc.selection.head, rect: this.caretRect(doc, group.editorRect) }];
      }),
      mobileSelectionHandles: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "selectionHandle" | "textSelectionHandle" }> => hit.type === "selectionHandle" || hit.type === "textSelectionHandle")
        .map((hit) => hit.type === "selectionHandle"
          ? { edge: hit.edge, groupId: hit.groupId, path: this.docs.get(hit.docId)?.path ?? "(untitled)", target: "editor", rect: hit.rect }
          : { edge: hit.edge, groupId: "", path: this.textSelectionTargetLabel(hit.target), target: hit.target.type, rect: hit.rect }),
      dockPreview: this.dockPreview,
      tabInsertionPreview: this.tabInsertionPreview,
      dragGhost: this.tabDrag ? this.dragGhostRect() : null,
      dockOverlayTargets: this.tabDrag ? this.allDockTargets().map((target) => ({ groupId: target.groupId, zone: target.zone, polygon: target.polygon, previewRect: target.previewRect })) : [],
      statusWhitespaceTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "statusWhitespace" }> => hit.type === "statusWhitespace")?.rect ?? null,
      statusHighlightTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "statusHighlight" }> => hit.type === "statusHighlight")?.rect ?? null,
      settingsRootTarget: this.hits.find((hit): hit is Extract<HitItem, { type: "settingsRoot" }> => hit.type === "settingsRoot")?.rect ?? null,
      sidebarResizeTarget: this.hits.find((hit) => hit.type === "sidebarResize")?.rect ?? null,
      dockSplitters: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "dockResize" }> => hit.type === "dockResize")
        .map((hit) => ({ splitId: hit.splitId, index: hit.index, direction: hit.direction, rect: hit.rect })),
      editorScrollbars: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "editorScrollbar" }> => hit.type === "editorScrollbar")
        .map((hit) => ({ axis: hit.axis, groupId: hit.groupId, path: this.docs.get(hit.docId)?.path ?? "(untitled)", rect: hit.rect, trackRect: hit.trackRect, thumbRect: hit.thumbRect })),
      settingsScrollbar: this.hits.find((hit): hit is Extract<HitItem, { type: "settingsScrollbar" }> => hit.type === "settingsScrollbar") ?? null,
      sidebarScrollbars: this.hits
        .filter((hit): hit is Extract<HitItem, { type: "sidebarScrollbar" }> => hit.type === "sidebarScrollbar")
        .map((hit) => ({ panel: hit.panel, rect: hit.rect, trackRect: hit.trackRect, thumbRect: hit.thumbRect })),
      hoveredScrollbar: this.hoveredScrollbar
        ? { axis: this.hoveredScrollbar.axis, groupId: this.hoveredScrollbar.groupId, path: this.docs.get(this.hoveredScrollbar.docId)?.path ?? "(untitled)", overThumb: this.hoveredScrollbar.overThumb }
        : null,
      contextMenu: this.contextMenu
        ? { scope: this.contextMenu.scope, rect: this.contextMenu.rect, items: this.contextMenu.items.filter(isContextMenuItem).map((item) => ({ command: item.command, label: item.label, rect: item.rect, enabled: item.enabled })) }
        : null,
      modal: this.modal
        ? {
            kind: this.modal.kind,
            title: this.modal.title,
            message: this.modal.message,
            detail: this.modal.detail,
            progress: this.modal.kind === "zipProgress" ? this.modal.progress : null,
            pending: this.modal.pending,
            buttons: this.modal.buttons.map((button) => ({ action: button.action, label: button.label, rect: button.rect, enabled: button.enabled }))
          }
        : null,
      renderer: this.renderer.diagnostics(),
      canvas: { width: this.canvas.width, height: this.canvas.height, cssWidth: this.viewport.get().cssWidth, cssHeight: this.viewport.get().cssHeight }
    };
  }

  private installEvents(): void {
    this.viewport.onChange(() => {
      this.requestFocusedInputReveal();
      this.scheduleDraw();
    });
    this.vfs.watch(() => {
      void this.refreshFiles().then(() => this.scheduleDraw());
    });
    this.canvas.addEventListener("pointerdown", (event) => this.onPointerDown(event));
    this.canvas.addEventListener("pointermove", (event) => this.onPointerMove(event));
    this.canvas.addEventListener("pointerleave", () => this.clearScrollbarHover());
    this.canvas.addEventListener("click", (event) => this.onClick(event));
    this.canvas.addEventListener("selectstart", (event) => event.preventDefault());
    this.canvas.addEventListener("dragstart", (event) => event.preventDefault());
    this.canvas.addEventListener("contextmenu", (event) => this.onContextMenu(event));
    this.canvas.addEventListener("dblclick", (event) => this.onDoubleClick(event));
    window.addEventListener("pointerup", (event) => this.onPointerUp(event));
    window.addEventListener("pointercancel", (event) => this.onPointerCancel(event));
    window.addEventListener("keydown", (event) => {
      if (this.modal) {
        const action = event.key === "Escape" ? this.modal.cancelAction : event.key === "Enter" ? this.modal.defaultAction : null;
        if (action) void this.runModalAction(action);
        if (action || event.key === "Tab") {
          event.preventDefault();
          event.stopPropagation();
        }
        return;
      }
      if (event.key !== "Escape" || !this.contextMenu) return;
      event.preventDefault();
      this.closeContextMenu();
    });
    this.canvas.addEventListener("wheel", (event) => {
      if (this.modal) {
        event.preventDefault();
        return;
      }
      const canvasRect = this.canvas.getBoundingClientRect();
      const point = { x: event.clientX - canvasRect.left, y: event.clientY - canvasRect.top };
      const tabGroup = this.tabGroupAtPoint(point);
      if (tabGroup && this.scrollTabGroupFromWheel(tabGroup, event, point)) {
        event.preventDefault();
        this.closeContextMenuForScroll();
        return;
      }
      const chatRegion = this.chatScrollRegionForPoint(point);
      if (chatRegion) {
        event.preventDefault();
        this.closeContextMenuForScroll();
        const deltaY = this.normalizedWheelDelta(event.deltaY, event.deltaMode, chatRegion.viewport);
        this.setChatPanelScrollY(chatRegion.panel, this.chatPanelScrollY(chatRegion.panel) + deltaY, chatRegion.viewport);
        this.scheduleDraw();
        return;
      }
      const sidebarRegion = this.sidebarScrollRegionForPoint(point);
      if (sidebarRegion) {
        event.preventDefault();
        this.closeContextMenuForScroll();
        const deltaY = this.normalizedWheelDelta(event.deltaY, event.deltaMode, sidebarRegion.viewport);
        this.scrollSidebarPanel(sidebarRegion.panel, deltaY, sidebarRegion.viewport);
        return;
      }
      const group = this.editorGroupAt(point.x, point.y);
      if (group && this.isSettingsTab(group.activeDocId)) {
        event.preventDefault();
        this.closeContextMenuForScroll();
        const deltaY = this.normalizedWheelDelta(event.deltaY, event.deltaMode, group.editorRect);
        this.settingsScrollY = clamp(this.settingsScrollY + deltaY, 0, this.maxSettingsScrollY(group.editorRect));
        this.scheduleDraw();
        return;
      }
      const doc = group?.activeDocId ? this.docs.get(group.activeDocId) : undefined;
      if (!group || !doc) return;
      event.preventDefault();
      this.closeContextMenuForScroll();
      const scroll = this.scrollForDoc(doc.id);
      const deltaY = this.normalizedWheelDelta(event.deltaY, event.deltaMode, group.editorRect);
      const deltaX = this.normalizedWheelDelta(event.deltaX, event.deltaMode, group.editorRect) + (event.shiftKey ? deltaY : 0);
      if (!event.shiftKey) scroll.y = clamp(scroll.y + deltaY, 0, this.maxScrollY(doc, group.editorRect));
      scroll.x = clamp(scroll.x + deltaX, 0, this.maxScrollX(doc, group.editorRect));
      this.persistEditorSession();
      this.scheduleDraw();
    }, { passive: false });
    this.canvas.addEventListener("dragenter", (event) => {
      event.preventDefault();
      if (this.modal) {
        if (event.dataTransfer) event.dataTransfer.dropEffect = "none";
        return;
      }
      this.updateFileDragState(event.dataTransfer);
      if (event.dataTransfer) event.dataTransfer.dropEffect = "copy";
    });
    this.canvas.addEventListener("dragover", (event) => {
      event.preventDefault();
      if (this.modal) {
        this.clearFileDragState();
        if (event.dataTransfer) event.dataTransfer.dropEffect = "none";
        return;
      }
      this.updateFileDragState(event.dataTransfer);
      if (event.dataTransfer) event.dataTransfer.dropEffect = "copy";
    });
    this.canvas.addEventListener("dragleave", (event) => {
      const rect = this.canvas.getBoundingClientRect();
      if (event.clientX >= rect.left && event.clientX <= rect.right && event.clientY >= rect.top && event.clientY <= rect.bottom) return;
      this.clearFileDragState();
    });
    this.canvas.addEventListener("drop", (event) => {
      event.preventDefault();
      this.clearFileDragState();
      if (this.modal) return;
      if (!event.dataTransfer) return;
      void this.handleFileDrop(event.dataTransfer);
    });
  }

  private ensureUploadInput(): HTMLInputElement {
    if (this.uploadInput) return this.uploadInput;
    const input = document.createElement("input");
    input.type = "file";
    input.multiple = true;
    input.style.position = "fixed";
    input.style.left = "-1000px";
    input.style.top = "0";
    input.style.width = "1px";
    input.style.height = "1px";
    input.style.opacity = "0";
    input.style.pointerEvents = "none";
    input.setAttribute("aria-hidden", "true");
    input.addEventListener("change", () => {
      const files = input.files ? Array.from(input.files) : [];
      const target = this.uploadTargetFolder;
      input.value = "";
      if (files.length === 0) return;
      void this.uploadFilesToFolder(files, target);
    });
    document.body.appendChild(input);
    this.uploadInput = input;
    return input;
  }

  private updateFileDragState(dataTransfer: DataTransfer | null): void {
    if (!dataTransfer || !dataTransferContainsFiles(dataTransfer)) return;
    const files = Array.from(dataTransfer.files ?? []);
    const zip = files.find(isZipFile);
    const label = zip ? "Drop to import workspace zip" : "Drop to open in memory";
    if (this.fileDragActive && this.fileDragLabel === label) return;
    this.fileDragActive = true;
    this.fileDragLabel = label;
    this.scheduleDraw();
  }

  private clearFileDragState(): void {
    if (!this.fileDragActive) return;
    this.fileDragActive = false;
    this.fileDragLabel = "Drop to upload";
    this.scheduleDraw();
  }

  private async handleFileDrop(dataTransfer: DataTransfer): Promise<void> {
    const files = Array.from(dataTransfer.files ?? []);
    if (files.length === 0) return;
    const zip = files.find(isZipFile);
    if (zip) {
      this.openZipImportModal(zip);
      return;
    }
    for (const file of files) await this.openDroppedFileInMemory(file);
    this.statusText = files.length === 1 ? `Opened ${files[0]!.name} in memory` : `Opened ${files.length} files in memory`;
    this.scheduleDraw();
  }

  private async openDroppedFileInMemory(file: File): Promise<void> {
    const unsupported = isUnsupportedFilePath(file.name);
    const text = unsupported ? UNSUPPORTED_FILE_TEXT : await file.text();
    this.openUntitledDocument(this.activeGroupId, {
      label: file.name || undefined,
      preferredName: file.name || undefined,
      text,
      dirty: !unsupported,
      readOnly: unsupported
    });
  }

  private onPointerDown(event: PointerEvent): void {
    const point = this.viewport.pointerToCanvasCss(event);
    const hit = this.hitAt(point.x, point.y);
    if (event.pointerType === "touch" && !this.isTouchKeyboardHit(hit)) {
      this.pendingTouchKeyboardFocus = null;
      this.pendingTouchDoubleTap = null;
    }
    if ((hit?.type === "selectionHandle" || hit?.type === "textSelectionHandle") && event.pointerType === "touch") {
      event.preventDefault();
      if (hit.type === "selectionHandle") this.startSelectionHandleDrag(hit, event.pointerId, point);
      else this.startTextSelectionHandleDrag(hit, event.pointerId, point);
      return;
    }
    if (this.activeSettingsNumber && event.pointerType === "touch" && hit?.type !== "settingsNumber" && this.handleActiveSettingsNumberTouchDoubleTap(event, point)) return;
    const switchingToTextInput = Boolean(hit && this.isTouchKeyboardHit(hit));
    if (this.activeSettingsNumber && hit?.type !== "settingsNumber") this.commitSettingsNumberInput(!switchingToTextInput);
    if (this.activeSettingsText && !(hit?.type === "textField" && hit.field === this.activeSettingsText)) this.commitSettingsTextInput(!switchingToTextInput);
    if (this.modal) {
      event.preventDefault();
      if (hit?.type === "modalButton" && hit.enabled) void this.runModalAction(hit.action);
      return;
    }
    if (this.contextMenu) {
      event.preventDefault();
      if (hit?.type === "contextMenu") {
        if (hit.enabled) void this.runContextMenuCommand(hit.command);
        return;
      }
      this.closeContextMenu();
      return;
    }
    if (this.renamePath && hit?.type !== "fileRenameInput") {
      event.preventDefault();
      void this.commitRename();
      return;
    }
    if (hit && this.handleTouchDoubleTap(event, point, hit)) return;
    const touchScroll = this.makeTouchScrollState(event, point, hit);
    if (!hit && !touchScroll) return;
    if (!this.shouldAllowNativeTouchFocus(event, hit)) event.preventDefault();
    this.queueTouchKeyboardFocus(event, hit);
    if (this.isContextMenuPointer(event)) return;
    this.touchScroll = touchScroll;
    if (touchScroll) this.capturePointer(event.pointerId);
    if (event.pointerType === "touch" && hit && this.isTouchKeyboardHit(hit)) this.startTouchLongPress(event.pointerId, point, hit);
    if (!hit) return;
    if (touchScroll && this.shouldDeferTouchHit(hit)) {
      this.deferredTouchHit = { hit, point: { ...point } };
      return;
    }
    this.updateScrollbarHover(hit, point);
    if (hit.type === "activity") {
      this.toggleActivityMode(hit.mode, event.pointerType !== "touch");
      this.draw();
    } else if (hit.type === "downloadActivity") {
      void this.requestWorkspaceDownload();
    } else if (hit.type === "settingsActivity") {
      this.toggleActivityMode("settings", event.pointerType !== "touch");
    } else if (hit.type === "sidebarResize") {
      this.resizingSidebar = true;
      this.canvas.style.cursor = "col-resize";
    } else if (hit.type === "dockResize") {
      this.startDockResize(hit, point);
    } else if (hit.type === "editorScrollbar") {
      this.startScrollbarDrag(hit, point);
    } else if (hit.type === "settingsScrollbar") {
      this.startSettingsScrollbarDrag(hit, point);
    } else if (hit.type === "sidebarScrollbar") {
      this.startSidebarScrollbarDrag(hit, point);
    } else if (hit.type === "chatScrollbar") {
      this.startChatScrollbarDrag(hit, point);
    } else if (hit.type === "folder") {
      this.selectFileTreePath(hit.path);
      this.toggleFolder(hit.path);
    } else if (hit.type === "filesRoot") {
      if (event.pointerType === "touch") this.input.blur();
      else this.focusEditor();
    } else if (hit.type === "file") {
      this.selectFileTreePath(hit.path);
      if (event.detail >= 2 && this.settings.renameOnDoubleClick) this.startRename(hit.path, hit.rect);
      else void this.openFile(hit.path, { focus: event.pointerType !== "touch" });
    } else if (hit.type === "fileRenameInput") {
      this.focusRename(hit.rect);
      this.setRenameCursorFromPoint(point.x, hit.rect, false);
      this.renameSelecting = true;
    } else if (hit.type === "tabClose") {
      void this.requestCloseTab(hit.docId);
    } else if (hit.type === "tabOverflow") {
      this.openTabOverflowMenu(hit.groupId, hit.rect);
    } else if (hit.type === "tab") {
      if (event.button === 1) {
        void this.requestCloseTab(hit.docId);
        return;
      }
      this.activateTabInGroup(this.groupById(hit.groupId), hit.docId, event.pointerType !== "touch");
      this.pendingTabDrag = { docId: hit.docId, groupId: hit.groupId, startPoint: { ...point } };
      this.scheduleDraw();
    } else if (hit.type === "textField") {
      this.focusTextField(hit.field, hit.rect);
      this.setTextFieldCursorFromPoint(hit.field, point.x, hit.rect, false);
      this.textFieldSelecting = hit.field;
    } else if (hit.type === "searchInput") {
      this.focusMiniTarget("search", hit.rect);
      this.setSearchCursorFromPoint(point.x, hit.rect, false);
      this.searchSelecting = true;
    } else if (hit.type === "searchResult") {
      const focus = event.pointerType !== "touch";
      void this.openFile(hit.path, { focus }).then(() => {
        const doc = this.activeDoc();
        if (doc) doc.setSelection({ line: hit.line, col: 0 });
        if (focus) this.revealEditorCaret();
        else if (doc) {
          this.ensureCaretVisible(doc, this.activeEditorRect());
          this.scheduleDraw();
        }
      });
    } else if (hit.type === "chatInput") {
      this.focusMiniTarget("chat", hit.rect, event.pointerType === "touch");
      this.setChatInputCursorFromPoint(point, hit.rect, false);
      this.chatInputSelecting = true;
    } else if (hit.type === "chatSend") {
      if (hit.enabled) void this.runChatSendControl();
    } else if (hit.type === "chatShowThinking") {
      this.toggleChatShowThinking();
    } else if (hit.type === "settingsHeader") {
      this.toggleSettingsHeader(hit.id);
    } else if (hit.type === "settingsCheckbox") {
      this.toggleSettingsCheckbox(hit.key);
    } else if (hit.type === "settingsDropdown") {
      this.openSettingsDropdown(hit.rect, hit.key);
    } else if (hit.type === "statusWhitespace") {
      this.toggleStatusWhitespace();
    } else if (hit.type === "statusHighlight") {
      this.openHighlightDropdown(hit);
    } else if (hit.type === "settingsNumber") {
      this.focusSettingsNumber(hit.key, hit.rect);
      this.setSettingsNumberCursorFromPoint(point.x, hit.rect, false);
      this.settingsNumberSelecting = true;
    } else if (hit.type === "settingsButton") {
      if (hit.enabled) void this.runSettingsButton(hit.action);
    } else if (hit.type === "searchReplaceToggle") {
      this.searchReplaceExpanded = !this.searchReplaceExpanded;
      this.scheduleDraw();
    } else if (hit.type === "searchRefresh") {
      void this.runSearch();
    } else if (hit.type === "searchReplaceAll") {
      if (hit.enabled) void this.replaceAllInWorkspace();
    } else if (hit.type === "findToggle") {
      const state = this.activeFindState(false);
      if (state) state.replaceExpanded = !state.replaceExpanded;
      this.scheduleDraw();
    } else if (hit.type === "findPrevious") {
      if (hit.enabled) this.selectDocumentFindMatch(-1);
    } else if (hit.type === "findNext") {
      if (hit.enabled) this.selectDocumentFindMatch(1);
    } else if (hit.type === "findClose") {
      this.closeFindWidget();
    } else if (hit.type === "findReplace") {
      if (hit.enabled) this.replaceCurrentFindMatch();
    } else if (hit.type === "findReplaceAll") {
      if (hit.enabled) this.replaceAllInActiveDocument();
    } else if (hit.type === "editorGutter") {
      const group = this.groupById(hit.groupId);
      const doc = this.docs.get(hit.docId);
      if (!doc) return;
      this.activeGroupId = group.id;
      this.activeDocId = doc.id;
      group.activeDocId = doc.id;
      this.selectActiveDocumentInFileTree();
      doc.setSelection(this.positionFromPointInEditor(doc, group.editorRect, point.x, point.y));
      this.focusEditor();
      this.resetCaretBlink();
      this.persistEditorSession();
    } else if (hit.type === "editor") {
      this.activeGroupId = hit.groupId;
      this.activeDocId = this.groupById(hit.groupId).activeDocId;
      const doc = this.activeDoc();
      if (!doc) return;
      this.selectActiveDocumentInFileTree();
      if (!this.isMobileContextMode() && event.detail >= 3) {
        this.selecting = false;
        this.selectEditorLineFromPoint(doc, hit.rect, point);
        this.focusEditor();
        return;
      }
      const pos = this.positionFromPoint(point.x, point.y);
      doc.setSelection(pos);
      this.selecting = true;
      this.focusEditor();
      this.resetCaretBlink();
      this.persistEditorSession();
    }
  }

  private onPointerMove(event: PointerEvent): void {
    const point = this.viewport.pointerToCanvasCss(event);
    if (this.modal) {
      event.preventDefault();
      const hover = this.hitAt(point.x, point.y);
      this.updateModalHover(hover);
      this.canvas.style.cursor = "";
      return;
    }
    if (this.selectionHandleDrag?.pointerId === event.pointerId) {
      event.preventDefault();
      this.updateSelectionHandleDrag(point);
      return;
    }
    this.cancelTouchLongPressIfMoved(event.pointerId, point);
    if (this.touchScroll && this.touchScroll.pointerId === event.pointerId) {
      event.preventDefault();
      this.updateTouchScroll(point);
      return;
    }
    if (this.renameSelecting) {
      event.preventDefault();
      const hit = this.hitAt(point.x, point.y);
      const rect = hit?.type === "fileRenameInput" ? hit.rect : this.renameInputRect();
      if (rect) this.setRenameCursorFromPoint(point.x, rect, true);
      return;
    }
    if (this.textFieldSelecting) {
      event.preventDefault();
      const hit = this.hitAt(point.x, point.y);
      const rect = hit?.type === "textField" && hit.field === this.textFieldSelecting ? hit.rect : this.textFieldRect(this.textFieldSelecting);
      if (rect) this.setTextFieldCursorFromPoint(this.textFieldSelecting, point.x, rect, true);
      return;
    }
    if (this.searchSelecting) {
      event.preventDefault();
      const hit = this.hitAt(point.x, point.y);
      const rect = hit?.type === "searchInput" ? hit.rect : this.searchInputRect();
      if (rect) this.setSearchCursorFromPoint(point.x, rect, true);
      return;
    }
    if (this.chatInputSelecting) {
      event.preventDefault();
      const hit = this.hitAt(point.x, point.y);
      const rect = hit?.type === "chatInput" ? hit.rect : this.chatInputRectForFocus();
      if (rect) this.setChatInputCursorFromPoint(point, rect, true);
      return;
    }
    if (this.settingsNumberSelecting) {
      event.preventDefault();
      const hit = this.hitAt(point.x, point.y);
      const rect = hit?.type === "settingsNumber" ? hit.rect : this.settingsNumberInputRect();
      if (rect) this.setSettingsNumberCursorFromPoint(point.x, rect, true);
      return;
    }
    if (this.pendingTabDrag) {
      const distance = Math.hypot(point.x - this.pendingTabDrag.startPoint.x, point.y - this.pendingTabDrag.startPoint.y);
      if (distance < TAB_DRAG_THRESHOLD) return;
      event.preventDefault();
      const pending = this.pendingTabDrag;
      this.pendingTabDrag = null;
      this.startTabDrag(pending.docId, pending.groupId, pending.startPoint);
      if (this.tabDrag) {
        this.tabDrag.pointer = point;
        this.updateDockPreview(point);
      }
      return;
    }
    if (this.resizingSidebar) {
      event.preventDefault();
      this.sidebarWidth = this.clampSidebarWidth(point.x - this.ui(48));
      this.statusText = `Sidebar ${Math.round(this.sidebarWidth)}px`;
      this.scheduleDraw();
      return;
    }
    if (this.scrollbarDrag) {
      event.preventDefault();
      this.dragScrollbar(point);
      this.canvas.style.cursor = "";
      return;
    }
    if (this.settingsScrollbarDrag) {
      event.preventDefault();
      this.dragSettingsScrollbar(point);
      this.canvas.style.cursor = "";
      return;
    }
    if (this.sidebarScrollbarDrag) {
      event.preventDefault();
      this.dragSidebarScrollbar(point);
      this.canvas.style.cursor = "";
      return;
    }
    if (this.chatScrollbarDrag) {
      event.preventDefault();
      this.dragChatScrollbar(point);
      this.canvas.style.cursor = "";
      return;
    }
    if (this.dockResize) {
      event.preventDefault();
      this.resizeDockSplit(point);
      return;
    }
    if (this.tabDrag) {
      event.preventDefault();
      this.tabDrag.pointer = point;
      this.updateDockPreview(point);
      return;
    }
    const hover = this.hitAt(point.x, point.y);
    this.updateContextMenuHover(hover);
    this.updateScrollbarHover(hover, point);
    this.updateActivityButtonHover(hover);
    this.updateButtonHover(hover);
    this.updateFileTreeHover(hover);
    this.canvas.style.cursor = this.cursorForHit(hover);
    if (!this.selecting) return;
    const doc = this.activeDoc();
    if (!doc) return;
    const pos = this.positionFromPoint(point.x, point.y);
    doc.setSelection(doc.selection.anchor, pos);
    this.resetCaretBlink();
  }

  private onPointerUp(event: PointerEvent): void {
    const point = this.viewport.pointerToCanvasCss(event);
    if (this.modal) {
      const hover = this.hitAt(point.x, point.y);
      this.updateModalHover(hover);
      this.canvas.style.cursor = "";
      return;
    }
    const touchScrollWasActive = this.touchScroll?.pointerId === event.pointerId && this.touchScroll.active;
    const deferredTouchHit = this.touchScroll?.pointerId === event.pointerId ? this.deferredTouchHit : null;
    const pendingTouchDoubleTap = this.pendingTouchDoubleTap?.pointerId === event.pointerId ? this.pendingTouchDoubleTap : null;
    if (this.touchScroll?.pointerId === event.pointerId) this.touchScroll = null;
    this.cancelTouchLongPress(event.pointerId);
    if (this.selectionHandleDrag?.pointerId === event.pointerId) this.stopSelectionHandleDrag();
    if (deferredTouchHit) this.deferredTouchHit = null;
    if (touchScrollWasActive || pendingTouchDoubleTap) event.preventDefault();
    const completedDockResize = Boolean(this.dockResize);
    if (this.tabDrag) {
      this.tabDrag.pointer = point;
      this.updateDockPreview(point);
      this.applyTabDrop();
    }
    this.selecting = false;
    this.resizingSidebar = false;
    this.dockResize = null;
    this.scrollbarDrag = null;
    this.settingsScrollbarDrag = null;
    this.sidebarScrollbarDrag = null;
    this.chatScrollbarDrag = null;
    this.renameSelecting = false;
    this.searchSelecting = false;
    this.chatInputSelecting = false;
    this.textFieldSelecting = null;
    this.settingsNumberSelecting = false;
    this.pendingTabDrag = null;
    this.tabDrag = null;
    this.dockPreview = null;
    this.tabInsertionPreview = null;
    this.lastTabDragPoint = null;
    this.stopTabDragAutoscroll();
    if (completedDockResize) this.persistEditorSession();
    if (touchScrollWasActive) {
      this.pendingTouchKeyboardFocus = null;
      if (pendingTouchDoubleTap) this.pendingTouchDoubleTap = null;
    } else if (pendingTouchDoubleTap) {
      this.pendingTouchDoubleTap = null;
      this.deferredTouchHit = null;
      this.finishTouchTextDoubleTap(pendingTouchDoubleTap);
      this.runPendingTouchKeyboardFocus(event.pointerId);
    } else {
      if (deferredTouchHit) this.runDeferredTouchHit(deferredTouchHit);
      this.runPendingTouchKeyboardFocus(event.pointerId);
    }
    const hover = this.hitAt(point.x, point.y);
    this.updateScrollbarHover(hover, point);
    this.updateActivityButtonHover(hover);
    this.updateButtonHover(hover);
    this.updateFileTreeHover(hover);
    this.canvas.style.cursor = this.cursorForHit(hover);
    this.draw();
  }

  private onPointerCancel(event: PointerEvent): void {
    if (this.touchScroll?.pointerId === event.pointerId) this.touchScroll = null;
    if (this.pendingTouchKeyboardFocus?.pointerId === event.pointerId) this.pendingTouchKeyboardFocus = null;
    if (this.pendingTouchDoubleTap?.pointerId === event.pointerId) this.pendingTouchDoubleTap = null;
    this.cancelTouchLongPress(event.pointerId);
    if (this.selectionHandleDrag?.pointerId === event.pointerId) this.stopSelectionHandleDrag();
    this.deferredTouchHit = null;
    this.selecting = false;
    this.resizingSidebar = false;
    this.dockResize = null;
    this.scrollbarDrag = null;
    this.settingsScrollbarDrag = null;
    this.sidebarScrollbarDrag = null;
    this.chatScrollbarDrag = null;
    this.renameSelecting = false;
    this.searchSelecting = false;
    this.chatInputSelecting = false;
    this.textFieldSelecting = null;
    this.settingsNumberSelecting = false;
    this.pendingTabDrag = null;
    this.tabDrag = null;
    this.dockPreview = null;
    this.tabInsertionPreview = null;
    this.lastTabDragPoint = null;
    this.hoveredButton = null;
    this.hoveredFileTreePath = null;
    this.stopTabDragAutoscroll();
    this.canvas.style.cursor = "";
    this.scheduleDraw();
  }

  private onContextMenu(event: MouseEvent): void {
    event.preventDefault();
    if (this.modal) return;
    const rect = this.canvas.getBoundingClientRect();
    const point = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    const hit = this.hitAt(point.x, point.y);
    if (hit?.type === "contextMenu") return;
    if (hit?.type === "tab" || hit?.type === "tabClose") {
      this.openTabContextMenu(point, hit.groupId, hit.docId);
      return;
    }
    if (hit?.type === "tabBar") {
      this.openTabBarContextMenu(point, hit.groupId);
      return;
    }
    if (hit?.type === "tabOverflow") {
      this.openTabOverflowMenu(hit.groupId, hit.rect);
      return;
    }
    if (hit?.type === "editorGutter") {
      this.openGutterContextMenu(point, hit.groupId, hit.docId);
      return;
    }
    if (hit?.type === "fileRenameInput") {
      this.focusRename(hit.rect);
      if (!this.pointHitsRenameSelection(point.x, hit.rect)) this.setRenameCursorFromPoint(point.x, hit.rect, false);
      this.openRenameTextContextMenu(point, hit.path);
      return;
    }
    if (hit?.type === "searchInput") {
      this.focusMiniTarget("search", hit.rect);
      if (!this.pointHitsSearchSelection(point.x, hit.rect)) this.setSearchCursorFromPoint(point.x, hit.rect, false);
      this.openSearchTextContextMenu(point);
      return;
    }
    if (hit?.type === "chatInput") {
      this.focusMiniTarget("chat", hit.rect);
      if (!this.pointHitsChatInputSelection(point, hit.rect)) this.setChatInputCursorFromPoint(point, hit.rect, false);
      this.openChatInputContextMenu(point);
      return;
    }
    if (hit?.type === "chatBubble") {
      this.openChatBubbleContextMenu(point, hit.messageId);
      return;
    }
    if (hit?.type === "textField") {
      this.focusTextField(hit.field, hit.rect);
      if (!this.pointHitsTextFieldSelection(hit.field, point.x, hit.rect)) this.setTextFieldCursorFromPoint(hit.field, point.x, hit.rect, false);
      this.openTextFieldContextMenu(point, hit.field);
      return;
    }
    if (hit?.type === "settingsNumber") {
      this.focusSettingsNumber(hit.key, hit.rect);
      if (!this.pointHitsSettingsNumberSelection(point.x, hit.rect)) this.setSettingsNumberCursorFromPoint(point.x, hit.rect, false);
      this.openSettingsNumberTextContextMenu(point, hit.key);
      return;
    }
    if (hit?.type === "file") {
      if (this.renamePath && this.renamePath !== hit.path) void this.commitRename();
      this.selectFileTreePath(hit.path);
      this.openFileContextMenu(point, hit.path);
      return;
    }
    if (hit?.type === "folder") {
      if (this.renamePath && this.renamePath !== hit.path) void this.commitRename();
      this.selectFileTreePath(hit.path);
      this.openFolderContextMenu(point, hit.path);
      return;
    }
    if (hit?.type === "filesRoot") {
      if (this.renamePath) void this.commitRename();
      this.openRootContextMenu(point);
      return;
    }
    if (hit?.type === "settingsRoot") {
      this.openSettingsRootContextMenu(point);
      return;
    }
    if (hit?.type === "chatRoot") {
      this.openChatRootContextMenu(point);
      return;
    }
    if (!hit || hit.type !== "editor") {
      this.closeContextMenu();
      return;
    }
    const group = this.groupById(hit.groupId);
    const docId = group.activeDocId;
    const doc = docId ? this.docs.get(docId) : undefined;
    if (!doc) {
      this.closeContextMenu();
      return;
    }
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.selectActiveDocumentInFileTree();
    if (!this.pointHitsSelection(doc, group.editorRect, point)) {
      doc.setSelection(this.positionFromPoint(point.x, point.y));
    }
    this.openEditorContextMenu(point, group, doc);
    this.focusEditor();
  }

  private onClick(event: MouseEvent): void {
    if (this.pendingTouchKeyboardFocus) {
      event.preventDefault();
      this.runPendingTouchKeyboardFocus(undefined, true);
      return;
    }
    if (event.detail < 3 || this.modal || this.contextMenu || this.isMobileContextMode()) return;
    const rect = this.canvas.getBoundingClientRect();
    const point = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    const hit = this.hitAt(point.x, point.y);
    if (hit?.type === "fileRenameInput") {
      event.preventDefault();
      this.focusRename(hit.rect);
      this.renameBuffer.selectAll();
      this.renameSelecting = false;
      this.resetCaretBlink();
      return;
    }
    if (hit?.type === "searchInput") {
      event.preventDefault();
      this.focusMiniTarget("search", hit.rect);
      this.searchBuffer.selectAll();
      this.searchSelecting = false;
      this.resetCaretBlink();
      return;
    }
    if (hit?.type === "chatInput") {
      event.preventDefault();
      this.focusMiniTarget("chat", hit.rect);
      this.chatDraft.selectAll();
      this.chatInputSelecting = false;
      this.resetCaretBlink();
      return;
    }
    if (hit?.type === "textField") {
      event.preventDefault();
      this.focusTextField(hit.field, hit.rect);
      this.bufferForTextField(hit.field).selectAll();
      this.textFieldSelecting = null;
      this.resetCaretBlink();
      return;
    }
    if (hit?.type === "settingsNumber") {
      event.preventDefault();
      this.focusSettingsNumber(hit.key, hit.rect);
      this.settingsNumberBuffer.selectAll();
      this.settingsNumberSelecting = false;
      this.resetCaretBlink();
      return;
    }
    if (hit?.type !== "editor") return;
    event.preventDefault();
    const group = this.groupById(hit.groupId);
    const docId = group.activeDocId;
    const doc = docId ? this.docs.get(docId) : undefined;
    if (!doc) return;
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.selectActiveDocumentInFileTree();
    this.selecting = false;
    this.selectEditorLineFromPoint(doc, group.editorRect, point);
    this.focusEditor();
    this.persistEditorSession();
  }

  private onDoubleClick(event: MouseEvent): void {
    event.preventDefault();
    if (this.modal) return;
    if (this.contextMenu && this.isMobileContextMode()) return;
    const rect = this.canvas.getBoundingClientRect();
    const point = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    const hit = this.hitAt(point.x, point.y);
    if (hit && this.isMobileContextMode()) {
      if (hit.type === "chatInput") {
        this.focusMiniTarget("chat", hit.rect, true);
        this.selectChatInputWordFromPoint(point, hit.rect);
        return;
      }
      this.openContextMenuForHit(point, hit, true);
      return;
    }
    if (hit?.type === "fileRenameInput") {
      this.focusRename(hit.rect);
      this.selectRenameWordFromPoint(point.x, hit.rect);
    } else if (hit?.type === "searchInput") {
      this.focusMiniTarget("search", hit.rect);
      this.selectSearchWordFromPoint(point.x, hit.rect);
    } else if (hit?.type === "chatInput") {
      this.focusMiniTarget("chat", hit.rect);
      this.selectChatInputWordFromPoint(point, hit.rect);
    } else if (hit?.type === "textField") {
      this.focusTextField(hit.field, hit.rect);
      this.selectTextFieldWordFromPoint(hit.field, point.x, hit.rect);
    } else if (hit?.type === "settingsNumber") {
      this.focusSettingsNumber(hit.key, hit.rect);
      this.selectSettingsNumberWordFromPoint(point.x, hit.rect);
    } else if (hit?.type === "file") {
      if (this.settings.renameOnDoubleClick) this.startRename(hit.path, hit.rect);
    } else if (hit?.type === "tabBar") {
      this.openUntitledDocument(hit.groupId);
    } else if (hit?.type === "editor") {
      const group = this.groupById(hit.groupId);
      const docId = group.activeDocId;
      const doc = docId ? this.docs.get(docId) : undefined;
      if (!doc) return;
      this.activeGroupId = group.id;
      this.activeDocId = doc.id;
      this.selectActiveDocumentInFileTree();
      this.selectEditorWordFromPoint(doc, group.editorRect, point);
      this.focusEditor();
      this.persistEditorSession();
    }
  }

  private focusEditor(): void {
    const doc = this.activeDoc();
    if (!doc) return;
    this.input.focusEditor(this.editorTarget(), this.caretRect(doc));
    this.resetCaretBlink();
    this.requestFocusedInputReveal();
  }

  private revealEditorCaret(): void {
    const doc = this.activeDoc();
    if (!doc) return;
    this.ensureCaretVisible(doc, this.activeEditorRect());
    this.focusEditor();
  }

  private focusMiniTarget(kind: "search" | "chat", rect: Rect, stabilizeTouchKeyboard = false): void {
    if (stabilizeTouchKeyboard) this.beginTouchKeyboardStabilization();
    this.input.focusEditor(this.miniTarget(kind), kind === "chat" ? this.chatInputCaretRect(rect) : rect);
    this.resetCaretBlink();
    this.requestFocusedInputReveal();
  }

  private focusTextField(field: TextFieldKey, rect: Rect): void {
    if (isSettingTextField(field)) {
      if (this.activeSettingsText !== field) this.syncSettingsTextBufferFromConfig(field);
      this.activeSettingsText = field;
    } else if (this.activeSettingsText) {
      this.commitSettingsTextInput(false);
    }
    this.input.focusEditor(this.textFieldTarget(field), rect);
    this.resetCaretBlink();
    this.requestFocusedInputReveal();
  }

  private focusActivityMode(mode: SidebarMode, focus = true): void {
    if (!focus) {
      this.input.blur();
      return;
    }
    const vp = this.viewport.get();
    const sidebarX = this.ui(48);
    if (mode === "search") {
      this.focusMiniTarget("search", { x: sidebarX + this.ui(10), y: this.ui(40), w: this.sidebarWidth - this.ui(20), h: this.ui(28) });
    } else if (mode === "chat") {
      this.focusMiniTarget("chat", this.chatInputRectForSidebar({ x: sidebarX, y: 0, w: this.sidebarWidth, h: vp.cssHeight - this.ui(24) }));
    } else if (mode === "settings") {
      this.input.blur();
    } else {
      this.focusEditor();
    }
  }

  private toggleActivityMode(mode: SidebarMode, focus = true): void {
    if (this.sidebarWidth > 0 && this.sidebarMode === mode) {
      this.lastSidebarWidth = this.sidebarWidth;
      this.sidebarWidth = 0;
      this.statusText = "Sidebar hidden";
      if (focus) this.focusEditor();
      else this.input.blur();
      return;
    }
    this.sidebarMode = mode;
    this.sidebarWidth = this.lastSidebarWidth || 280;
    this.statusText = `${mode[0]!.toUpperCase()}${mode.slice(1)} panel`;
    this.focusActivityMode(mode, focus);
  }

  private requestFocusedInputReveal(): void {
    if (!this.input.activeTarget && !this.activeSettingsNumber && !this.activeSettingsText && !this.renamePath) return;
    this.pendingFocusedInputReveal = true;
  }

  private applyPendingFocusedInputReveal(): void {
    if (!this.pendingFocusedInputReveal) return;
    this.pendingFocusedInputReveal = false;
    if (this.revealFocusedInputInScrollArea()) this.scheduleDraw();
  }

  private revealFocusedInputInScrollArea(): boolean {
    if (this.isTouchKeyboardStabilizing()) return false;
    let changed = false;
    const activeKind = this.input.activeTarget?.kind ?? null;
    if (activeKind === "editor") {
      const doc = this.activeDoc();
      const group = doc ? this.groupContaining(doc.id) : null;
      if (doc && group) {
        const scroll = this.scrollForDoc(doc.id);
        const beforeX = scroll.x;
        const beforeY = scroll.y;
        this.ensureCaretVisible(doc, group.editorRect);
        changed ||= Math.abs(scroll.x - beforeX) > 0.5 || Math.abs(scroll.y - beforeY) > 0.5;
      }
    } else if (activeKind === "chat") {
      const before = this.chatInputScrollY;
      const inputRect = this.chatInputRectForFocus();
      this.ensureChatInputCaretVisible(inputRect);
      this.input.refocus(this.chatInputCaretRect(inputRect));
      changed ||= Math.abs(this.chatInputScrollY - before) > 0.5;
    }
    if (this.activeSettingsNumber || this.activeSettingsText) changed ||= this.ensureFocusedSettingsInputVisible();
    return changed;
  }

  private beginTouchKeyboardStabilization(): void {
    const until = performance.now() + TOUCH_KEYBOARD_STABILIZE_MS;
    this.touchKeyboardStabilizeUntil = Math.max(this.touchKeyboardStabilizeUntil, until);
    this.viewport.deferVisualViewportCanvasResize(TOUCH_KEYBOARD_STABILIZE_MS);
    if (this.touchKeyboardStabilizeTimer) window.clearTimeout(this.touchKeyboardStabilizeTimer);
    this.touchKeyboardStabilizeTimer = window.setTimeout(() => {
      this.touchKeyboardStabilizeTimer = 0;
      this.requestFocusedInputReveal();
      this.scheduleDraw();
    }, Math.max(16, Math.ceil(this.touchKeyboardStabilizeUntil - performance.now()) + 16));
  }

  private isTouchKeyboardStabilizing(): boolean {
    return performance.now() < this.touchKeyboardStabilizeUntil;
  }

  private ensureFocusedSettingsInputVisible(): boolean {
    const viewport = this.settingsViewportRect;
    const input = this.focusedSettingsInputRect;
    if (!viewport || !input) return false;
    const margin = Math.min(this.ui(10), Math.max(0, (viewport.h - input.h) / 2));
    const top = viewport.y + margin;
    const bottom = viewport.y + viewport.h - margin;
    let next = this.settingsScrollY;
    if (input.y < top) next -= top - input.y;
    else if (input.y + input.h > bottom) next += input.y + input.h - bottom;
    next = clamp(next, 0, this.maxSettingsScrollY(viewport));
    if (Math.abs(next - this.settingsScrollY) <= 0.5) return false;
    this.settingsScrollY = next;
    return true;
  }

  private hitAt(x: number, y: number): HitItem | undefined {
    for (let i = this.hits.length - 1; i >= 0; i--) {
      const hit = this.hits[i]!;
      if ((hit.type === "selectionHandle" || hit.type === "textSelectionHandle") && rectContains(hit.rect, x, y)) return hit;
    }
    for (let i = this.hits.length - 1; i >= 0; i--) {
      const hit = this.hits[i]!;
      if (rectContains(hit.rect, x, y)) return hit;
    }
    return undefined;
  }

  private cursorForHit(hit: HitItem | undefined): string {
    if (!hit) return "";
    if (hit.type === "sidebarResize") return "col-resize";
    if (hit.type === "dockResize") return hit.direction === "row" ? "col-resize" : "row-resize";
    return "";
  }

  private updateScrollbarHover(hit: HitItem | undefined, point: Point): void {
    const next = hit?.type === "editorScrollbar"
      ? { axis: hit.axis, groupId: hit.groupId, docId: hit.docId, overThumb: rectContains(hit.thumbRect, point.x, point.y) }
      : null;
    const nextSettings = hit?.type === "settingsScrollbar"
      ? { overThumb: rectContains(hit.thumbRect, point.x, point.y) }
      : null;
    const nextSidebar = hit?.type === "sidebarScrollbar"
      ? { panel: hit.panel, overThumb: rectContains(hit.thumbRect, point.x, point.y) }
      : null;
    const nextChat = hit?.type === "chatScrollbar"
      ? { panel: hit.panel, overThumb: rectContains(hit.thumbRect, point.x, point.y) }
      : null;
    const changed = this.hoveredScrollbar?.axis !== next?.axis
      || this.hoveredScrollbar?.groupId !== next?.groupId
      || this.hoveredScrollbar?.docId !== next?.docId
      || this.hoveredScrollbar?.overThumb !== next?.overThumb
      || this.hoveredSettingsScrollbar?.overThumb !== nextSettings?.overThumb
      || this.hoveredSidebarScrollbar?.panel !== nextSidebar?.panel
      || this.hoveredSidebarScrollbar?.overThumb !== nextSidebar?.overThumb
      || this.hoveredChatScrollbar?.panel !== nextChat?.panel
      || this.hoveredChatScrollbar?.overThumb !== nextChat?.overThumb;
    if (!changed) return;
    this.hoveredScrollbar = next;
    this.hoveredSettingsScrollbar = nextSettings;
    this.hoveredSidebarScrollbar = nextSidebar;
    this.hoveredChatScrollbar = nextChat;
    this.scheduleDraw();
  }

  private clearScrollbarHover(): void {
    if ((!this.hoveredScrollbar && !this.hoveredSettingsScrollbar && !this.hoveredSidebarScrollbar && !this.hoveredChatScrollbar && !this.hoveredActivityButton && !this.hoveredButton && !this.hoveredFileTreePath) || this.scrollbarDrag || this.settingsScrollbarDrag || this.sidebarScrollbarDrag || this.chatScrollbarDrag) return;
    this.hoveredScrollbar = null;
    this.hoveredSettingsScrollbar = null;
    this.hoveredSidebarScrollbar = null;
    this.hoveredChatScrollbar = null;
    this.hoveredActivityButton = null;
    this.hoveredButton = null;
    this.hoveredFileTreePath = null;
    this.canvas.style.cursor = "";
    this.scheduleDraw();
  }

  private updateActivityButtonHover(hit: HitItem | undefined): void {
    const next = hit?.type === "activity" ? hit.mode : hit?.type === "downloadActivity" ? "download" : hit?.type === "settingsActivity" ? "settings" : null;
    if (this.hoveredActivityButton === next) return;
    this.hoveredActivityButton = next;
    this.scheduleDraw();
  }

  private updateButtonHover(hit: HitItem | undefined): void {
    const next = this.buttonHoverKeyForHit(hit);
    if (this.hoveredButton === next) return;
    this.hoveredButton = next;
    this.scheduleDraw();
  }

  private updateFileTreeHover(hit: HitItem | undefined): void {
    const next = hit?.type === "file" || hit?.type === "folder" ? normalizePath(hit.path) : null;
    if (this.hoveredFileTreePath === next) return;
    this.hoveredFileTreePath = next;
    this.scheduleDraw();
  }

  private buttonHoverKeyForHit(hit: HitItem | undefined): string | null {
    if (!hit) return null;
    if (hit.type === "tabClose") return this.buttonHoverKey("tabClose", hit.groupId, hit.docId);
    if (hit.type === "tabOverflow") return this.buttonHoverKey("tabOverflow", hit.groupId);
    if (hit.type === "statusWhitespace") return this.buttonHoverKey("statusWhitespace");
    if (hit.type === "statusHighlight") return this.buttonHoverKey("statusHighlight", hit.groupId, hit.docId);
    if (hit.type === "searchReplaceToggle") return this.buttonHoverKey("searchReplaceToggle");
    if (hit.type === "searchRefresh") return this.buttonHoverKey("searchRefresh");
    if (hit.type === "searchReplaceAll") return hit.enabled ? this.buttonHoverKey("searchReplaceAll") : null;
    if (hit.type === "findToggle") return this.buttonHoverKey("findToggle");
    if (hit.type === "findPrevious") return hit.enabled ? this.buttonHoverKey("findPrevious") : null;
    if (hit.type === "findNext") return hit.enabled ? this.buttonHoverKey("findNext") : null;
    if (hit.type === "findClose") return this.buttonHoverKey("findClose");
    if (hit.type === "findReplace") return hit.enabled ? this.buttonHoverKey("findReplace") : null;
    if (hit.type === "findReplaceAll") return hit.enabled ? this.buttonHoverKey("findReplaceAll") : null;
    if (hit.type === "chatSend") return hit.enabled ? this.buttonHoverKey("chatSend") : null;
    if (hit.type === "chatShowThinking") return this.buttonHoverKey("chatShowThinking");
    if (hit.type === "settingsButton") return hit.enabled ? this.buttonHoverKey("settingsButton", hit.action) : null;
    if (hit.type === "settingsCheckbox") return this.buttonHoverKey("settingsCheckbox", hit.key);
    if (hit.type === "settingsDropdown") return this.buttonHoverKey("settingsDropdown", hit.key);
    return null;
  }

  private buttonHoverKey(type: string, ...parts: string[]): string {
    return [type, ...parts].join(":");
  }

  private isButtonHovered(type: string, ...parts: string[]): boolean {
    return this.hoveredButton === this.buttonHoverKey(type, ...parts);
  }

  private isContextMenuPointer(event: PointerEvent): boolean {
    return event.button === 2 || (event.button === 0 && event.ctrlKey);
  }

  private isMobileContextMode(): boolean {
    return navigator.maxTouchPoints > 0 && window.matchMedia("(pointer: coarse)").matches;
  }

  private isMobileSelectionMode(): boolean {
    return navigator.maxTouchPoints > 0 || window.matchMedia("(pointer: coarse)").matches;
  }

  private shouldAllowNativeTouchFocus(event: PointerEvent, hit: HitItem | undefined): boolean {
    return event.pointerType === "touch" && this.isTouchKeyboardHit(hit);
  }

  private isTouchKeyboardHit(hit: HitItem | undefined): hit is TouchKeyboardHit {
    return Boolean(hit && (hit.type === "editor"
      || hit.type === "fileRenameInput"
      || hit.type === "searchInput"
      || hit.type === "chatInput"
      || hit.type === "textField"
      || hit.type === "settingsNumber"));
  }

  private queueTouchKeyboardFocus(event: PointerEvent, hit: HitItem | undefined): void {
    if (event.pointerType !== "touch") return;
    if (!this.isTouchKeyboardHit(hit)) {
      this.pendingTouchKeyboardFocus = null;
      return;
    }
    this.beginTouchKeyboardStabilization();
    this.pendingTouchKeyboardFocus = { pointerId: event.pointerId, hit, expiresAt: performance.now() + 800 };
  }

  private runPendingTouchKeyboardFocus(pointerId?: number, clear = false): boolean {
    const pending = this.pendingTouchKeyboardFocus;
    if (!pending) return false;
    if (pointerId !== undefined && pending.pointerId !== pointerId) return false;
    if (performance.now() > pending.expiresAt) {
      this.pendingTouchKeyboardFocus = null;
      return false;
    }
    this.beginTouchKeyboardStabilization();
    if (!(this.input.isFocused() && this.touchKeyboardHitMatchesActiveInput(pending.hit))) this.refocusTouchKeyboardHit(pending.hit);
    if (clear) this.pendingTouchKeyboardFocus = null;
    return true;
  }

  private refocusTouchKeyboardHit(hit: HitItem): void {
    if (!this.isTouchKeyboardHit(hit)) return;
    this.beginTouchKeyboardStabilization();
    if (hit.type === "editor") {
      const doc = this.activeDoc();
      this.input.refocus(doc ? this.caretRect(doc) : hit.rect);
      return;
    }
    if (hit.type === "chatInput") {
      const inputRect = this.chatInputRectForFocus();
      this.input.refocus(this.chatInputCaretRect(inputRect));
      return;
    }
    this.input.refocus(hit.rect);
  }

  private touchKeyboardHitMatchesActiveInput(hit: TouchKeyboardHit): boolean {
    const kind = this.input.activeTarget?.kind ?? null;
    if (hit.type === "editor") return kind === "editor";
    if (hit.type === "searchInput") return kind === "search";
    if (hit.type === "chatInput") return kind === "chat";
    if (hit.type === "fileRenameInput") return kind === "command" && this.renamePath === hit.path;
    if (hit.type === "settingsNumber") return kind === "command" && this.activeSettingsNumber === hit.key;
    return kind === hit.field;
  }

  private startTouchLongPress(pointerId: number, point: Point, hit: TouchKeyboardHit): void {
    let press: TouchLongPressState | null = null;
    if (hit.type === "editor") {
      const group = this.groupById(hit.groupId);
      const docId = group.activeDocId;
      const doc = docId ? this.docs.get(docId) : undefined;
      if (!doc || doc.readOnly) return;
      press = { type: "editor", pointerId, groupId: group.id, docId: doc.id, point: { ...point } };
    } else {
      const target = this.textSelectionTargetFromTouchHit(hit);
      if (!target) return;
      press = { type: "text", pointerId, target, inputRect: { ...hit.rect }, point: { ...point } };
    }
    this.cancelTouchLongPress();
    this.touchLongPress = press;
    this.touchLongPressTimer = window.setTimeout(() => this.completeTouchLongPress(pointerId), TOUCH_LONG_PRESS_MS);
  }

  private textSelectionTargetFromTouchHit(hit: TouchKeyboardHit): TextSelectionHandleTarget | null {
    if (hit.type === "fileRenameInput") return { type: "rename", path: hit.path };
    if (hit.type === "searchInput") return { type: "textField", field: "search" };
    if (hit.type === "chatInput") return { type: "chatInput" };
    if (hit.type === "textField") return { type: "textField", field: hit.field };
    if (hit.type === "settingsNumber") return { type: "settingsNumber", key: hit.key };
    return null;
  }

  private completeTouchLongPress(pointerId: number): void {
    const press = this.touchLongPress;
    if (!press || press.pointerId !== pointerId) return;
    if (this.pendingTouchDoubleTap?.pointerId === pointerId) this.pendingTouchDoubleTap = null;
    this.cancelTouchLongPress(pointerId);
    if (press.type === "text") {
      this.completeTextTouchLongPress(press);
      return;
    }
    const group = this.groupById(press.groupId);
    const doc = this.docs.get(press.docId);
    if (!doc || group.activeDocId !== doc.id || doc.readOnly) return;
    this.touchScroll = null;
    this.deferredTouchHit = null;
    this.selecting = false;
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.selectActiveDocumentInFileTree();
    this.selectEditorWordFromPoint(doc, group.editorRect, press.point);
    this.focusEditor();
    this.persistEditorSession();
    this.scheduleDraw();
  }

  private completeTextTouchLongPress(press: Extract<TouchLongPressState, { type: "text" }>): void {
    this.touchScroll = null;
    this.deferredTouchHit = null;
    this.selecting = false;
    this.renameSelecting = false;
    this.searchSelecting = false;
    this.chatInputSelecting = false;
    this.textFieldSelecting = null;
    this.settingsNumberSelecting = false;
    this.focusTextSelectionHandleTarget(press.target, press.inputRect);
    if (press.target.type === "rename") this.selectRenameWordFromPoint(press.point.x, press.inputRect);
    else if (press.target.type === "textField") this.selectTextFieldWordFromPoint(press.target.field, press.point.x, press.inputRect);
    else if (press.target.type === "settingsNumber") this.selectSettingsNumberWordFromPoint(press.point.x, press.inputRect);
    else this.selectChatInputWordFromPoint(press.point, press.inputRect);
    this.scheduleDraw();
  }

  private cancelTouchLongPress(pointerId?: number): void {
    if (pointerId !== undefined && this.touchLongPress?.pointerId !== pointerId) return;
    if (this.touchLongPressTimer) window.clearTimeout(this.touchLongPressTimer);
    this.touchLongPressTimer = 0;
    this.touchLongPress = null;
  }

  private cancelTouchLongPressIfMoved(pointerId: number, point: Point): void {
    const press = this.touchLongPress;
    const pendingTap = this.pendingTouchDoubleTap;
    if (pendingTap?.pointerId === pointerId && Math.hypot(point.x - pendingTap.point.x, point.y - pendingTap.point.y) >= this.ui(TOUCH_SCROLL_THRESHOLD)) this.pendingTouchDoubleTap = null;
    if (!press || press.pointerId !== pointerId) return;
    if (Math.hypot(point.x - press.point.x, point.y - press.point.y) >= this.ui(TOUCH_SCROLL_THRESHOLD)) this.cancelTouchLongPress(pointerId);
  }

  private startSelectionHandleDrag(hit: Extract<HitItem, { type: "selectionHandle" }>, pointerId: number, point: Point): void {
    const group = this.groupById(hit.groupId);
    const doc = this.docs.get(hit.docId);
    if (!doc || !doc.hasSelection()) return;
    const ordered = doc.getOrderedSelection();
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    group.activeDocId = doc.id;
    this.selectActiveDocumentInFileTree();
    this.selectionHandleDrag = {
      type: "editor",
      pointerId,
      groupId: group.id,
      docId: doc.id,
      edge: hit.edge,
      fixed: hit.edge === "start" ? { ...ordered.end } : { ...ordered.start },
      point: { ...point }
    };
    this.touchScroll = null;
    this.deferredTouchHit = null;
    this.selecting = false;
    this.capturePointer(pointerId);
    this.startSelectionHandleAutoscroll();
  }

  private startTextSelectionHandleDrag(hit: Extract<HitItem, { type: "textSelectionHandle" }>, pointerId: number, point: Point): void {
    if (hit.target.type === "chatInput") {
      if (!this.chatDraft.hasSelection()) return;
      const ordered = this.chatDraft.getOrderedSelection();
      this.selectionHandleDrag = {
        type: "chatInput",
        pointerId,
        inputRect: hit.inputRect,
        edge: hit.edge,
        fixed: hit.edge === "start" ? { ...ordered.end } : { ...ordered.start },
        point: { ...point }
      };
    } else {
      const buffer = this.bufferForTextSelectionHandleTarget(hit.target);
      if (!buffer.hasSelection()) return;
      this.focusTextSelectionHandleTarget(hit.target, hit.inputRect);
      const start = Math.min(buffer.anchor, buffer.cursor);
      const end = Math.max(buffer.anchor, buffer.cursor);
      this.selectionHandleDrag = {
        type: "mini",
        pointerId,
        target: hit.target,
        inputRect: hit.inputRect,
        edge: hit.edge,
        fixed: hit.edge === "start" ? end : start,
        point: { ...point }
      };
    }
    this.touchScroll = null;
    this.deferredTouchHit = null;
    this.selecting = false;
    this.renameSelecting = false;
    this.searchSelecting = false;
    this.chatInputSelecting = false;
    this.textFieldSelecting = null;
    this.settingsNumberSelecting = false;
    this.capturePointer(pointerId);
    this.startSelectionHandleAutoscroll();
  }

  private updateSelectionHandleDrag(point: Point): void {
    const drag = this.selectionHandleDrag;
    if (!drag) return;
    drag.point = { ...point };
    if (drag.type === "mini") {
      this.updateMiniSelectionHandleDrag(drag);
      return;
    }
    if (drag.type === "chatInput") {
      this.updateChatInputSelectionHandleDrag(drag);
      return;
    }
    const group = this.groupById(drag.groupId);
    const doc = this.docs.get(drag.docId);
    if (!doc) return;
    this.applySelectionHandleAutoscroll(drag, group, doc);
    const pos = this.positionFromPointInEditor(doc, group.editorRect, point.x, point.y);
    doc.setSelection(drag.fixed, pos);
    this.resetCaretBlink();
    this.persistEditorSession();
    this.scheduleDraw();
  }

  private updateMiniSelectionHandleDrag(drag: Extract<SelectionHandleDragState, { type: "mini" }>): void {
    const buffer = this.bufferForTextSelectionHandleTarget(drag.target);
    this.applyMiniSelectionHandleAutoscroll(drag, buffer);
    const col = this.miniBufferColumnFromPoint(buffer, drag.inputRect, this.textSelectionHandlePadX(drag.target), drag.point.x);
    buffer.anchor = drag.fixed;
    buffer.cursor = col;
    this.revealMiniBufferCaret(buffer, drag.inputRect, this.textSelectionHandlePadX(drag.target));
    this.resetCaretBlink();
    this.scheduleDraw();
  }

  private updateChatInputSelectionHandleDrag(drag: Extract<SelectionHandleDragState, { type: "chatInput" }>): void {
    this.applyChatInputSelectionHandleAutoscroll(drag);
    const pos = this.chatInputPositionFromPoint(drag.point, drag.inputRect);
    this.chatDraft.setSelection(drag.fixed, pos);
    this.ensureChatInputCaretVisible(drag.inputRect);
    this.resetCaretBlink();
    this.scheduleDraw();
  }

  private startSelectionHandleAutoscroll(): void {
    if (this.selectionHandleAutoscrollFrame) return;
    const tick = () => {
      this.selectionHandleAutoscrollFrame = 0;
      const drag = this.selectionHandleDrag;
      if (!drag) return;
      this.updateSelectionHandleDrag(drag.point);
      this.selectionHandleAutoscrollFrame = window.requestAnimationFrame(tick);
    };
    this.selectionHandleAutoscrollFrame = window.requestAnimationFrame(tick);
  }

  private stopSelectionHandleDrag(): void {
    this.selectionHandleDrag = null;
    if (this.selectionHandleAutoscrollFrame) window.cancelAnimationFrame(this.selectionHandleAutoscrollFrame);
    this.selectionHandleAutoscrollFrame = 0;
    this.persistEditorSession();
  }

  private applySelectionHandleAutoscroll(drag: Extract<SelectionHandleDragState, { type: "editor" }>, group: EditorGroup, doc: TextDocument): void {
    const scroll = this.scrollForDoc(doc.id);
    const rect = group.editorRect;
    const edge = this.ui(SELECTION_HANDLE_AUTOSCROLL_EDGE);
    const maxStep = this.ui(SELECTION_HANDLE_AUTOSCROLL_MAX_STEP);
    let dx = 0;
    let dy = 0;
    if (drag.point.y < rect.y + edge) dy = -maxStep * (1 - Math.max(0, drag.point.y - rect.y) / edge);
    else if (drag.point.y > rect.y + rect.h - edge) dy = maxStep * (1 - Math.max(0, rect.y + rect.h - drag.point.y) / edge);
    if (drag.point.x < rect.x + edge) dx = -maxStep * (1 - Math.max(0, drag.point.x - rect.x) / edge);
    else if (drag.point.x > rect.x + rect.w - edge) dx = maxStep * (1 - Math.max(0, rect.x + rect.w - drag.point.x) / edge);
    if (dx === 0 && dy === 0) return;
    scroll.x = clamp(scroll.x + dx, 0, this.maxScrollX(doc, rect));
    scroll.y = clamp(scroll.y + dy, 0, this.maxScrollY(doc, rect));
  }

  private applyMiniSelectionHandleAutoscroll(drag: Extract<SelectionHandleDragState, { type: "mini" }>, buffer: MiniBuffer): void {
    const content = this.miniBufferContentRect(drag.inputRect, this.textSelectionHandlePadX(drag.target));
    const maxScroll = Math.max(0, this.renderer.measureText(buffer.text, "ui") - content.w);
    if (maxScroll <= 0) return;
    const edge = this.ui(SELECTION_HANDLE_AUTOSCROLL_EDGE);
    const maxStep = this.ui(SELECTION_HANDLE_AUTOSCROLL_MAX_STEP);
    let dx = 0;
    if (drag.point.x < content.x + edge) dx = -maxStep * (1 - Math.max(0, drag.point.x - content.x) / edge);
    else if (drag.point.x > content.x + content.w - edge) dx = maxStep * (1 - Math.max(0, content.x + content.w - drag.point.x) / edge);
    if (dx === 0) return;
    buffer.scrollX = clamp(buffer.scrollX + dx, 0, maxScroll);
  }

  private applyChatInputSelectionHandleAutoscroll(drag: Extract<SelectionHandleDragState, { type: "chatInput" }>): void {
    const metrics = this.chatInputMetrics(drag.inputRect);
    const edge = this.ui(SELECTION_HANDLE_AUTOSCROLL_EDGE);
    const maxStep = this.ui(SELECTION_HANDLE_AUTOSCROLL_MAX_STEP);
    let dy = 0;
    if (drag.point.y < metrics.content.y + edge) dy = -maxStep * (1 - Math.max(0, drag.point.y - metrics.content.y) / edge);
    else if (drag.point.y > metrics.content.y + metrics.content.h - edge) dy = maxStep * (1 - Math.max(0, metrics.content.y + metrics.content.h - drag.point.y) / edge);
    if (dy === 0) return;
    this.chatInputScrollY = clamp(this.chatInputScrollY + dy, 0, Math.max(0, metrics.contentHeight - metrics.viewport.h));
  }

  private makeTouchScrollState(event: PointerEvent, point: Point, hit: HitItem | undefined): TouchScrollState | null {
    if (event.pointerType !== "touch") return null;
    if (hit?.type === "editorScrollbar" || hit?.type === "settingsScrollbar" || hit?.type === "sidebarScrollbar" || hit?.type === "chatScrollbar") return null;
    if (hit?.type === "chatTranscript" || hit?.type === "chatInput" || hit?.type === "chatBubble") {
      const panel: ChatScrollbarPanel = hit.type === "chatInput" ? "chatInput" : "chatTranscript";
      const rect = hit.type === "chatBubble" ? hit.viewportRect : hit.rect;
      const maxScroll = this.maxChatScrollY(panel, rect);
      if (maxScroll > 0) {
        return {
          type: "chat",
          pointerId: event.pointerId,
          panel,
          rect: { ...rect },
          startPoint: { ...point },
          startScrollY: this.chatPanelScrollY(panel),
          active: false
        };
      }
    }
    const sidebarRegion = this.sidebarScrollRegionForPoint(point);
    if (sidebarRegion) {
      const maxScroll = this.maxSidebarScrollY(sidebarRegion.panel, sidebarRegion.viewport);
      if (maxScroll > 0) {
        return {
          type: "sidebar",
          pointerId: event.pointerId,
          panel: sidebarRegion.panel,
          rect: { ...sidebarRegion.viewport },
          startPoint: { ...point },
          startScrollY: this.sidebarScrollY(sidebarRegion.panel),
          active: false
        };
      }
    }
    if (hit?.type === "editor" || hit?.type === "editorGutter") {
      const group = this.groupById(hit.groupId);
      const docId = group.activeDocId;
      const doc = docId ? this.docs.get(docId) : undefined;
      if (!doc) return null;
      if (this.maxScrollX(doc, group.editorRect) <= 0 && this.maxScrollY(doc, group.editorRect) <= 0) return null;
      return {
        type: "editor",
        pointerId: event.pointerId,
        groupId: group.id,
        docId: doc.id,
        rect: { ...group.editorRect },
        startPoint: { ...point },
        startScroll: { ...this.scrollForDoc(doc.id) },
        originalSelection: cloneSelectionState(doc.selection),
        active: false
      };
    }
    const group = this.editorGroupAt(point.x, point.y);
    if (!group || !this.isSettingsTab(group.activeDocId)) return null;
    if (this.maxSettingsScrollY(group.editorRect) <= 0) return null;
    return {
      type: "settings",
      pointerId: event.pointerId,
      groupId: group.id,
      rect: { ...group.editorRect },
      startPoint: { ...point },
      startScrollY: this.settingsScrollY,
      active: false
    };
  }

  private updateTouchScroll(point: Point): void {
    const scroll = this.touchScroll;
    if (!scroll) return;
    const dx = point.x - scroll.startPoint.x;
    const dy = point.y - scroll.startPoint.y;
    if (!scroll.active) {
      if (Math.hypot(dx, dy) < this.ui(TOUCH_SCROLL_THRESHOLD)) return;
      scroll.active = true;
      this.cancelTouchLongPress(scroll.pointerId);
      this.lastTouchTap = null;
      this.pendingTouchDoubleTap = null;
      this.deferredTouchHit = null;
      this.selecting = false;
      this.renameSelecting = false;
      this.searchSelecting = false;
      this.textFieldSelecting = null;
      this.settingsNumberSelecting = false;
      this.closeContextMenuForScroll();
      if (scroll.type === "editor") {
        const doc = this.docs.get(scroll.docId);
        if (doc) doc.selection = cloneSelectionState(scroll.originalSelection);
      }
    }
    if (scroll.type === "settings") {
      const group = this.groupById(scroll.groupId);
      this.settingsScrollY = clamp(scroll.startScrollY - dy, 0, this.maxSettingsScrollY(group.editorRect));
      this.scheduleDraw();
      return;
    }
    if (scroll.type === "sidebar") {
      this.setSidebarScrollY(scroll.panel, scroll.startScrollY - dy, scroll.rect);
      this.scheduleDraw();
      return;
    }
    if (scroll.type === "chat") {
      this.setChatPanelScrollY(scroll.panel, scroll.startScrollY - dy, scroll.rect);
      this.scheduleDraw();
      return;
    }
    const group = this.groupById(scroll.groupId);
    const doc = this.docs.get(scroll.docId);
    if (!doc) return;
    const docScroll = this.scrollForDoc(doc.id);
    docScroll.x = clamp(scroll.startScroll.x - dx, 0, this.maxScrollX(doc, group.editorRect));
    docScroll.y = clamp(scroll.startScroll.y - dy, 0, this.maxScrollY(doc, group.editorRect));
    this.persistEditorSession();
    this.scheduleDraw();
  }

  private capturePointer(pointerId: number): void {
    try {
      this.canvas.setPointerCapture(pointerId);
    } catch {
      // Safari may reject capture after synthetic or cancelled touch events.
    }
  }

  private shouldDeferTouchHit(hit: HitItem): boolean {
    return hit.type === "settingsHeader"
      || hit.type === "settingsCheckbox"
      || hit.type === "settingsDropdown"
      || hit.type === "statusWhitespace"
      || hit.type === "chatShowThinking"
      || hit.type === "statusHighlight"
      || hit.type === "settingsButton"
      || hit.type === "folder"
      || hit.type === "file"
      || hit.type === "filesRoot"
      || hit.type === "editorGutter"
      || hit.type === "searchResult";
  }

  private runDeferredTouchHit(deferred: DeferredTouchHit): void {
    const { hit, point } = deferred;
    if (hit.type === "settingsHeader") {
      this.toggleSettingsHeader(hit.id);
    } else if (hit.type === "settingsCheckbox") {
      this.toggleSettingsCheckbox(hit.key);
    } else if (hit.type === "settingsDropdown") {
      this.openSettingsDropdown(hit.rect, hit.key);
    } else if (hit.type === "statusWhitespace") {
      this.toggleStatusWhitespace();
    } else if (hit.type === "chatShowThinking") {
      this.toggleChatShowThinking();
    } else if (hit.type === "statusHighlight") {
      this.openHighlightDropdown(hit);
    } else if (hit.type === "editorGutter") {
      const group = this.groupById(hit.groupId);
      const doc = this.docs.get(hit.docId);
      if (!doc) return;
      this.activeGroupId = group.id;
      this.activeDocId = doc.id;
      group.activeDocId = doc.id;
      this.selectActiveDocumentInFileTree();
      doc.setSelection(this.positionFromPointInEditor(doc, group.editorRect, point.x, point.y));
      this.selecting = true;
      this.focusEditor();
      this.resetCaretBlink();
      this.persistEditorSession();
    } else if (hit.type === "settingsNumber") {
      this.focusSettingsNumber(hit.key, hit.rect);
      this.setSettingsNumberCursorFromPoint(point.x, hit.rect, false);
    } else if (hit.type === "textField") {
      this.focusTextField(hit.field, hit.rect);
      this.setTextFieldCursorFromPoint(hit.field, point.x, hit.rect, false);
    } else if (hit.type === "settingsButton") {
      if (hit.enabled) void this.runSettingsButton(hit.action);
    } else if (hit.type === "folder") {
      this.selectFileTreePath(hit.path);
      this.toggleFolder(hit.path);
    } else if (hit.type === "file") {
      this.selectFileTreePath(hit.path);
      void this.openFile(hit.path, { focus: false });
    } else if (hit.type === "filesRoot") {
      this.input.blur();
    } else if (hit.type === "searchResult") {
      void this.openFile(hit.path, { focus: false }).then(() => {
        const doc = this.activeDoc();
        if (doc) doc.setSelection({ line: hit.line, col: 0 });
        if (doc) {
          this.ensureCaretVisible(doc, this.activeEditorRect());
          this.scheduleDraw();
        }
      });
    }
  }

  private handleTouchDoubleTap(event: PointerEvent, point: Point, hit: HitItem): boolean {
    if (event.pointerType !== "touch") return false;
    const key = this.doubleTapKey(hit);
    if (!key) {
      this.lastTouchTap = null;
      return false;
    }
    const now = performance.now();
    const last = this.lastTouchTap;
    this.lastTouchTap = { time: now, point: { ...point }, key };
    if (!last || last.key !== key) return false;
    if (now - last.time > TOUCH_DOUBLE_TAP_MS) return false;
    if (Math.hypot(point.x - last.point.x, point.y - last.point.y) > TOUCH_DOUBLE_TAP_DISTANCE) return false;
    this.lastTouchTap = null;
    if (this.isTouchKeyboardHit(hit)) {
      this.pendingTouchDoubleTap = { pointerId: event.pointerId, hit, point: { ...point }, key };
      return false;
    }
    event.preventDefault();
    this.pendingTouchKeyboardFocus = null;
    this.openContextMenuForHit(point, hit, true);
    return true;
  }

  private handleActiveSettingsNumberTouchDoubleTap(event: PointerEvent, point: Point): boolean {
    const key = this.activeSettingsNumber;
    const last = this.lastTouchTap;
    if (!key || !last || last.key !== `settingsNumber:${key}`) return false;
    const now = performance.now();
    if (now - last.time > TOUCH_DOUBLE_TAP_MS) return false;
    this.lastTouchTap = null;
    event.preventDefault();
    this.focusSettingsNumber(key, this.settingsNumberInputRect(key) ?? { x: point.x, y: point.y, w: this.ui(72), h: this.ui(24) });
    this.settingsNumberBuffer.selectAll();
    this.openSettingsNumberTextContextMenu(point, key);
    return true;
  }

  private finishTouchTextDoubleTap(tap: PendingTouchDoubleTap): void {
    if (tap.hit.type === "chatInput") {
      this.focusMiniTarget("chat", tap.hit.rect, true);
      this.selectChatInputWordFromPoint(tap.point, tap.hit.rect);
      this.chatInputSelecting = false;
      this.scheduleDraw();
      return;
    }
    this.openContextMenuForHit(tap.point, tap.hit, true);
  }

  private doubleTapKey(hit: HitItem): string | null {
    if (hit.type === "file") return `file:${hit.path}`;
    if (hit.type === "folder") return `folder:${hit.path}`;
    if (hit.type === "filesRoot") return "filesRoot";
    if (hit.type === "settingsRoot") return "settingsRoot";
    if (hit.type === "chatRoot") return "chatRoot";
    if (hit.type === "chatBubble") return `chatBubble:${hit.messageId}`;
    if (hit.type === "fileRenameInput") return `rename:${hit.path}`;
    if (hit.type === "searchInput") return "searchInput";
    if (hit.type === "chatInput") return "chatInput";
    if (hit.type === "textField") return `textField:${hit.field}`;
    if (hit.type === "settingsNumber") return `settingsNumber:${hit.key}`;
    if (hit.type === "editor") return `editor:${hit.groupId}`;
    if (hit.type === "editorGutter") return `editorGutter:${hit.groupId}:${hit.docId}`;
    if (hit.type === "tab" || hit.type === "tabClose") return `tab:${hit.groupId}:${hit.docId}`;
    if (hit.type === "tabBar") return `tabBar:${hit.groupId}`;
    if (hit.type === "tabOverflow") return `tabOverflow:${hit.groupId}`;
    return null;
  }

  private openContextMenuForHit(point: Point, hit: HitItem, selectTextFirst = false): boolean {
    if (hit.type === "fileRenameInput") {
      this.focusRename(hit.rect);
      if (selectTextFirst) this.selectRenameWordFromPoint(point.x, hit.rect);
      else if (!this.pointHitsRenameSelection(point.x, hit.rect)) this.setRenameCursorFromPoint(point.x, hit.rect, false);
      this.openRenameTextContextMenu(point, hit.path);
      return true;
    }
    if (hit.type === "searchInput") {
      this.focusMiniTarget("search", hit.rect);
      if (selectTextFirst) this.selectSearchWordFromPoint(point.x, hit.rect);
      else if (!this.pointHitsSearchSelection(point.x, hit.rect)) this.setSearchCursorFromPoint(point.x, hit.rect, false);
      this.openSearchTextContextMenu(point);
      return true;
    }
    if (hit.type === "chatInput") {
      this.focusMiniTarget("chat", hit.rect);
      if (selectTextFirst) this.selectChatInputWordFromPoint(point, hit.rect);
      else if (!this.pointHitsChatInputSelection(point, hit.rect)) this.setChatInputCursorFromPoint(point, hit.rect, false);
      this.openChatInputContextMenu(point);
      return true;
    }
    if (hit.type === "textField") {
      this.focusTextField(hit.field, hit.rect);
      if (selectTextFirst) this.selectTextFieldWordFromPoint(hit.field, point.x, hit.rect);
      else if (!this.pointHitsTextFieldSelection(hit.field, point.x, hit.rect)) this.setTextFieldCursorFromPoint(hit.field, point.x, hit.rect, false);
      this.openTextFieldContextMenu(point, hit.field);
      return true;
    }
    if (hit.type === "settingsNumber") {
      this.focusSettingsNumber(hit.key, hit.rect);
      if (selectTextFirst) this.selectSettingsNumberWordFromPoint(point.x, hit.rect);
      else if (!this.pointHitsSettingsNumberSelection(point.x, hit.rect)) this.setSettingsNumberCursorFromPoint(point.x, hit.rect, false);
      this.openSettingsNumberTextContextMenu(point, hit.key);
      return true;
    }
    if (hit.type === "file") {
      if (this.renamePath && this.renamePath !== hit.path) void this.commitRename();
      this.selectFileTreePath(hit.path);
      this.openFileContextMenu(point, hit.path);
      return true;
    }
    if (hit.type === "folder") {
      if (this.renamePath && this.renamePath !== hit.path) void this.commitRename();
      this.selectFileTreePath(hit.path);
      this.openFolderContextMenu(point, hit.path);
      return true;
    }
    if (hit.type === "filesRoot") {
      if (this.renamePath) void this.commitRename();
      this.openRootContextMenu(point);
      return true;
    }
    if (hit.type === "settingsRoot") {
      this.openSettingsRootContextMenu(point);
      return true;
    }
    if (hit.type === "chatRoot") {
      this.openChatRootContextMenu(point);
      return true;
    }
    if (hit.type === "chatBubble") {
      this.openChatBubbleContextMenu(point, hit.messageId);
      return true;
    }
    if (hit.type === "tab" || hit.type === "tabClose") {
      this.openTabContextMenu(point, hit.groupId, hit.docId);
      return true;
    }
    if (hit.type === "tabBar") {
      this.openTabBarContextMenu(point, hit.groupId);
      return true;
    }
    if (hit.type === "tabOverflow") {
      this.openTabOverflowMenu(hit.groupId, hit.rect);
      return true;
    }
    if (hit.type === "editorGutter") {
      this.openGutterContextMenu(point, hit.groupId, hit.docId);
      return true;
    }
    if (hit.type === "editor") {
      const group = this.groupById(hit.groupId);
      const docId = group.activeDocId;
      const doc = docId ? this.docs.get(docId) : undefined;
      if (!doc) {
        this.closeContextMenu();
        return false;
      }
      this.activeGroupId = group.id;
      this.activeDocId = doc.id;
      group.activeDocId = doc.id;
      this.selectActiveDocumentInFileTree();
      if (selectTextFirst) this.selectEditorWordFromPoint(doc, group.editorRect, point);
      else if (!this.pointHitsSelection(doc, group.editorRect, point)) doc.setSelection(this.positionFromPoint(point.x, point.y));
      this.openEditorContextMenu(point, group, doc);
      this.focusEditor();
      return true;
    }
    return false;
  }

  private updateContextMenuHover(hit: HitItem | undefined): void {
    const next = hit?.type === "contextMenu" && hit.enabled ? hit.command : null;
    if (this.contextMenuHover === next) return;
    this.contextMenuHover = next;
    if (this.contextMenu) this.scheduleDraw();
  }

  private updateModalHover(hit: HitItem | undefined): void {
    const next = hit?.type === "modalButton" && hit.enabled ? hit.action : null;
    if (this.modalHover === next) return;
    this.modalHover = next;
    if (this.modal) this.scheduleDraw();
  }

  private closeContextMenu(): void {
    if (!this.contextMenu) return;
    this.contextMenu = null;
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private closeContextMenuForScroll(): void {
    if (!this.contextMenu) return;
    this.contextMenu = null;
    this.contextMenuHover = null;
  }

  private closeContextMenuForTextInput(): void {
    if (!this.contextMenu) return;
    this.contextMenu = null;
    this.contextMenuHover = null;
  }

  private openEditorContextMenu(point: Point, group: EditorGroup, doc: TextDocument): void {
    const selected = doc.hasSelection();
    const editable = !doc.readOnly;
    this.contextMenu = this.makeContextMenu(point, { type: "editor", groupId: group.id, docId: doc.id }, [
      { command: "cut", label: "Cut", enabled: selected && editable },
      { command: "copy", label: "Copy", enabled: selected },
      { command: "paste", label: "Paste", enabled: editable },
      ...this.mobileSystemClipboardEntries(selected, editable),
      ...this.undoRedoContextEntries(doc.canUndo() && editable, doc.canRedo() && editable)
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openGutterContextMenu(point: Point, groupId: string, docId: string): void {
    this.contextMenu = this.makeContextMenu(point, { type: "gutter", groupId, docId }, [
      { command: "toggleLineNumbers", label: this.settings.showLineNumbers ? "Hide Line Numbers" : "Show Line Numbers", enabled: true }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openTabContextMenu(point: Point, groupId: string, docId: string): void {
    const group = this.groupById(groupId);
    const entries: ContextMenuSeed[] = [
      { command: "save", label: "Save", enabled: !this.isSettingsTab(docId) && !this.docs.get(docId)?.readOnly },
      { separator: true },
      { command: "close", label: "Close", enabled: true },
      { command: "closeOthers", label: "Close Others", enabled: group.tabs.some((id) => id !== docId) },
      { separator: true },
      this.isSettingsTab(docId)
        ? { command: "resetSettings", label: "Reset Settings", enabled: true }
        : { command: "findInFile", label: "Find in File", enabled: true }
    ];
    this.contextMenu = this.makeContextMenu(point, { type: "tab", groupId, docId }, entries);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openTabBarContextMenu(point: Point, groupId: string): void {
    const group = this.groupById(groupId);
    this.contextMenu = this.makeContextMenu(point, { type: "tabBar", groupId }, [
      { command: "newFile", label: "New File", enabled: true },
      { command: "uploadFile", label: "Upload File", enabled: true },
      { command: "closeAll", label: "Close All", enabled: group.tabs.length > 0 }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openTabOverflowMenu(groupId: string, buttonRect: Rect): void {
    const group = this.groupById(groupId);
    const tabRect = { x: group.frameRect.x, y: group.frameRect.y, w: group.frameRect.w, h: this.ui(32) };
    const layout = this.tabLayoutForGroup(group, tabRect);
    const visibleStart = layout.scroll;
    const visibleEnd = layout.scroll + layout.stripRect.w;
    const hidden = layout.items.filter((item) => item.start < visibleStart || item.end > visibleEnd);
    const entries: ContextMenuSeed[] = (hidden.length ? hidden : layout.items).map((item) => ({
      command: tabOverflowCommand(item.docId),
      label: item.label,
      enabled: true
    }));
    const width = Math.min(this.ui(320), Math.max(this.ui(190), ...entries.map((entry) => "separator" in entry ? 0 : this.renderer.measureText(entry.label, "ui") + this.ui(28))));
    this.contextMenu = this.makeContextMenu(
      { x: buttonRect.x, y: buttonRect.y + buttonRect.h },
      { type: "tabOverflow", groupId },
      entries,
      { x: buttonRect.x + buttonRect.w - width, y: buttonRect.y + buttonRect.h, w: width }
    );
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openFileContextMenu(point: Point, path: string): void {
    this.contextMenu = this.makeContextMenu(point, { type: "file", path }, [
      { command: "rename", label: "Rename", enabled: true },
      { command: "duplicate", label: "Duplicate", enabled: true },
      { command: "delete", label: "Delete", enabled: true }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openFolderContextMenu(point: Point, path: string): void {
    this.contextMenu = this.makeContextMenu(point, { type: "folder", path }, [
      { command: "rename", label: "Rename", enabled: true },
      { command: "delete", label: "Delete", enabled: true },
      { command: "createFile", label: "Create File", enabled: true },
      { command: "createFolder", label: "Create Folder", enabled: true },
      { command: "uploadFile", label: "Upload File", enabled: true }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openRootContextMenu(point: Point): void {
    this.contextMenu = this.makeContextMenu(point, { type: "root", path: "/" }, [
      { command: "createFile", label: "Create File", enabled: true },
      { command: "createFolder", label: "Create Folder", enabled: true },
      { command: "uploadFile", label: "Upload File", enabled: true }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openSettingsRootContextMenu(point: Point): void {
    this.contextMenu = this.makeContextMenu(point, { type: "settingsRoot" }, [
      { command: "resetSettings", label: "Reset Settings", enabled: true }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openChatRootContextMenu(point: Point): void {
    this.contextMenu = this.makeContextMenu(point, { type: "chatRoot" }, [
      { command: "exportChat", label: "Export Chat", enabled: this.chat.visibleMessages().length > 0 },
      { command: "debugChat", label: "Debug Chat", enabled: this.chat.visibleMessages().length > 0 },
      { command: "clearChat", label: "Clear Chat", enabled: this.chat.messages.length > 0 && !this.chat.running },
      { command: "compactChat", label: "Compact", enabled: this.chat.messages.length > 0 && !this.chat.running }
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openChatBubbleContextMenu(point: Point, messageId: string): void {
    const hasBubble = Boolean(this.chatDisplayMessages().find((msg) => msg.id === messageId));
    const hasChat = this.chatDisplayMessages().length > 0;
    const entries: ContextMenuSeed[] = [
      { command: "copyBubble", label: "Copy Bubble", enabled: hasBubble },
      { command: "copyChat", label: "Copy Chat", enabled: hasChat },
      { command: "clearChat", label: "Clear Chat", enabled: this.chat.messages.length > 0 && !this.chat.running }
    ];
    if (isMobileWebKit()) {
      entries.push(
        { separator: true },
        { command: "systemCopyBubble", label: "System Copy Bubble", enabled: hasBubble },
        { command: "systemCopyChat", label: "System Copy Chat", enabled: hasChat }
      );
    }
    this.contextMenu = this.makeContextMenu(point, { type: "chatBubble", messageId }, entries, { w: this.ui(isMobileWebKit() ? 188 : 144) });
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openRenameTextContextMenu(point: Point, path: string): void {
    const selected = this.renameBuffer.hasSelection();
    this.contextMenu = this.makeContextMenu(point, { type: "rename", path }, [
      { command: "cut", label: "Cut", enabled: selected },
      { command: "copy", label: "Copy", enabled: selected },
      { command: "paste", label: "Paste", enabled: true },
      ...this.mobileSystemClipboardEntries(selected),
      ...this.undoRedoContextEntries(this.renameBuffer.canUndo(), this.renameBuffer.canRedo())
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openSearchTextContextMenu(point: Point): void {
    const selected = this.searchBuffer.hasSelection();
    this.contextMenu = this.makeContextMenu(point, { type: "search" }, [
      { command: "cut", label: "Cut", enabled: selected },
      { command: "copy", label: "Copy", enabled: selected },
      { command: "paste", label: "Paste", enabled: true },
      ...this.mobileSystemClipboardEntries(selected),
      ...this.undoRedoContextEntries(this.searchBuffer.canUndo(), this.searchBuffer.canRedo())
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openChatInputContextMenu(point: Point): void {
    const selected = this.chatDraft.hasSelection();
    this.contextMenu = this.makeContextMenu(point, { type: "chatInput" }, [
      { command: "cut", label: "Cut", enabled: selected },
      { command: "copy", label: "Copy", enabled: selected },
      { command: "paste", label: "Paste", enabled: true },
      ...this.mobileSystemClipboardEntries(selected),
      ...this.undoRedoContextEntries(this.chatDraft.canUndo(), this.chatDraft.canRedo())
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openTextFieldContextMenu(point: Point, field: TextFieldKey): void {
    const buffer = this.bufferForTextField(field);
    const selected = buffer.hasSelection();
    const scope: ContextMenuScope = field === "search" ? { type: "search" } : { type: "textField", field };
    this.contextMenu = this.makeContextMenu(point, scope, [
      { command: "cut", label: "Cut", enabled: selected },
      { command: "copy", label: "Copy", enabled: selected },
      { command: "paste", label: "Paste", enabled: true },
      ...this.mobileSystemClipboardEntries(selected),
      ...this.undoRedoContextEntries(buffer.canUndo(), buffer.canRedo())
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private openSettingsNumberTextContextMenu(point: Point, key: SettingNumberKey): void {
    const selected = this.settingsNumberBuffer.hasSelection();
    this.contextMenu = this.makeContextMenu(point, { type: "settingsNumber", key }, [
      { command: "cut", label: "Cut", enabled: selected },
      { command: "copy", label: "Copy", enabled: selected },
      { command: "paste", label: "Paste", enabled: true },
      ...this.mobileSystemClipboardEntries(selected),
      ...this.undoRedoContextEntries(this.settingsNumberBuffer.canUndo(), this.settingsNumberBuffer.canRedo())
    ]);
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private mobileSystemClipboardEntries(selected: boolean, pasteEnabled = true): ContextMenuSeed[] {
    return isMobileWebKit()
      ? [
          { separator: true },
          { command: "systemCopy", label: "System Copy", enabled: selected },
          { command: "systemPaste", label: "System Paste", enabled: pasteEnabled }
        ]
      : [];
  }

  private undoRedoContextEntries(canUndo: boolean, canRedo: boolean): ContextMenuSeed[] {
    if (!canUndo && !canRedo) return [];
    const entries: ContextMenuSeed[] = [{ separator: true }];
    if (canUndo) entries.push({ command: "undo", label: "Undo", enabled: true });
    if (canRedo) entries.push({ command: "redo", label: "Redo", enabled: true });
    return entries;
  }

  private makeContextMenu(point: Point, scope: ContextMenuScope, entries: ContextMenuSeed[], layout: Partial<Pick<Rect, "x" | "y" | "w">> = {}): ContextMenuState {
    const vp = this.viewport.get();
    const pad = this.ui(CONTEXT_MENU_PAD);
    const width = layout.w ?? this.ui(CONTEXT_MENU_WIDTH);
    const rowH = this.ui(CONTEXT_MENU_ROW_H);
    const separatorH = this.ui(CONTEXT_MENU_SEPARATOR_H);
    const menuH = pad * 2 + entries.reduce((sum, entry) => sum + ("separator" in entry ? separatorH : rowH), 0);
    const x = clamp(layout.x ?? point.x, 0, Math.max(0, vp.cssWidth - width - 1));
    const y = clamp(layout.y ?? point.y, 0, Math.max(0, vp.cssHeight - menuH - 1));
    const rect = { x, y, w: width, h: menuH };
    const items: ContextMenuEntry[] = [];
    let rowY = y + pad;
    for (const entry of entries) {
      if ("separator" in entry) {
        items.push({ kind: "separator", rect: { x: x + pad + this.ui(8), y: rowY + Math.floor(separatorH / 2), w: width - pad * 2 - this.ui(16), h: 1 } });
        rowY += separatorH;
      } else {
        items.push({
          kind: "item",
          ...entry,
          rect: { x: x + pad, y: rowY, w: width - pad * 2, h: rowH }
        });
        rowY += rowH;
      }
    }
    return { scope, rect, items };
  }

  private openModal(modal: ModalState): void {
    this.contextMenu = null;
    this.contextMenuHover = null;
    this.revokeDownloadReadyModal();
    this.modal = modal;
    this.modalHover = null;
    this.input.blur();
    this.scheduleDraw();
  }

  private closeModal(): void {
    if (!this.modal) return;
    this.revokeDownloadReadyModal();
    this.modal = null;
    this.modalHover = null;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.scheduleDraw();
  }

  private async openDirtyCloseModal(doc: TextDocument): Promise<void> {
    const label = this.documentLabel(doc);
    const savePath = doc.path ? undefined : await this.savePathForUntitledDocument(doc);
    this.openModal({
      kind: "dirtyClose",
      title: "Save before closing?",
      message: `${label} has unsaved changes.`,
      detail: savePath ? `Save will create ${savePath} in the root folder.` : "Save your changes before closing this tab?",
      docId: doc.id,
      savePath,
      defaultAction: "save",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("save", "Save", "primary"),
        modalButton("discard", "Don't Save", "secondary"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private async openDirtyDownloadModal(doc: TextDocument): Promise<void> {
    const label = this.documentLabel(doc);
    const savePath = doc.path ? undefined : await this.savePathForUntitledDocument(doc);
    this.openModal({
      kind: "dirtyDownload",
      title: "Save before downloading?",
      message: `${label} has unsaved changes.`,
      detail: savePath
        ? `Save will create ${savePath} in the root folder. Choose Don't Save to omit this memory-only file from the zip.`
        : "Saved files are included in the zip. Choose Don't Save to export the last saved version.",
      docId: doc.id,
      savePath,
      defaultAction: "save",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("save", "Save", "primary"),
        modalButton("discard", "Don't Save", "secondary"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private openZipProgressModal(message: string, detail: string, progress: number): void {
    this.openModal({
      kind: "zipProgress",
      title: "Preparing download",
      message,
      detail,
      progress,
      defaultAction: "cancel",
      cancelAction: "cancel",
      pending: true,
      buttons: []
    });
  }

  private openCompactingModal(): void {
    this.openModal({
      kind: "compactProgress",
      title: "Compacting conversation",
      message: "Summarizing the chat history.",
      detail: "The editor will continue when compaction is done.",
      defaultAction: "cancel",
      cancelAction: "cancel",
      pending: true,
      buttons: []
    });
  }

  private closeCompactingModal(): void {
    if (this.modal?.kind !== "compactProgress") return;
    this.modal = null;
    this.modalHover = null;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.scheduleDraw();
  }

  private openDownloadReadyModal(url: string, filename: string, fileCount: number, byteLength: number): void {
    this.openModal({
      kind: "downloadReady",
      title: "Download ready",
      message: `${filename} is ready.`,
      detail: `${fileCount} file${fileCount === 1 ? "" : "s"} • ${formatBytes(byteLength)}`,
      url,
      filename,
      defaultAction: "download",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("download", "Download", "primary"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private openToolCallLimitModal(limit: number, used: number): Promise<ToolCallLimitDecision> {
    return new Promise((resolve) => {
      this.openModal({
        kind: "toolCallLimit",
        title: "Max tool calls reached",
        message: `This turn has used ${used} tool call${used === 1 ? "" : "s"}.`,
        detail: `The per-turn limit is ${limit}. Choose whether this turn can keep using tools.`,
        limit,
        used,
        resolve,
        defaultAction: "allowMore",
        cancelAction: "stopToolCalls",
        pending: false,
        buttons: [
          modalButton("allowMore", `Allow ${limit} more`, "primary"),
          modalButton("allowAll", "Allow all", "secondary"),
          modalButton("stopToolCalls", "Stop tool calls", "danger")
        ]
      });
    });
  }

  private openDuplicateToolCallModal(call: DuplicateToolCallInfo): Promise<DuplicateToolCallDecision> {
    return new Promise((resolve) => {
      this.openModal({
        kind: "duplicateToolCall",
        title: "Duplicate tool call detected",
        message: `The assistant requested ${call.name} with the same arguments twice in a row.`,
        detail: `Arguments: ${formatToolArgsForModal(call.args)}`,
        call,
        resolve,
        defaultAction: "breakDuplicateTool",
        cancelAction: "breakDuplicateTool",
        pending: false,
        buttons: [
          modalButton("allowDuplicateTool", "Allow", "primary"),
          modalButton("breakDuplicateTool", "Break", "danger")
        ]
      });
    });
  }

  private openDeleteFolderModal(path: string, itemCount: number): void {
    this.openModal({
      kind: "deleteFolder",
      title: "Delete non-empty folder?",
      message: `Delete ${path} and all contents?`,
      detail: `${itemCount} item${itemCount === 1 ? "" : "s"} will be deleted. Open files inside this folder will be closed.`,
      path,
      defaultAction: "delete",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("delete", "Delete", "danger"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private openClearFileSystemModal(): void {
    this.openModal({
      kind: "clearFileSystem",
      title: "Clear file system?",
      message: "Delete every file and folder in this workspace?",
      detail: "This closes all open tabs and removes the IndexedDB file tree. This cannot be undone.",
      defaultAction: "delete",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("delete", "Clear", "danger"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private openClearChatModal(): void {
    this.openModal({
      kind: "clearChat",
      title: "Clear chat?",
      message: "Remove every message from the current chat?",
      detail: "This clears the visible conversation and the agent context for future turns.",
      defaultAction: "clearChat",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("clearChat", "Clear", "danger"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private openZipImportModal(file: File): void {
    this.openModal({
      kind: "zipImport",
      title: "Import workspace zip?",
      message: `${file.name} is a zip file.`,
      detail: "Replace clears the current project before importing. Append adds the zip contents to the current project.",
      file,
      defaultAction: "append",
      cancelAction: "cancel",
      pending: false,
      buttons: [
        modalButton("replace", "Replace", "danger"),
        modalButton("append", "Append", "primary"),
        modalButton("cancel", "Cancel", "secondary")
      ]
    });
  }

  private async runModalAction(action: ModalAction): Promise<void> {
    const modal = this.modal;
    const button = modal?.buttons.find((candidate) => candidate.action === action);
    if (!modal || !button?.enabled || modal.pending) return;
    if (modal.kind === "downloadReady" && action === "download") {
      this.startBrowserDownload(modal);
      return;
    }
    if (modal.kind === "toolCallLimit") {
      this.runToolCallLimitModalAction(modal, action);
      return;
    }
    if (modal.kind === "duplicateToolCall") {
      this.runDuplicateToolCallModalAction(modal, action);
      return;
    }
    if (modal.kind === "dirtyDownload" && action === "cancel") {
      modal.pending = true;
      this.scheduleDraw();
      try {
        await this.runDirtyDownloadModalAction(modal, "discard");
      } catch (error) {
        if (this.modal !== modal) throw error;
        modal.pending = false;
        this.statusText = error instanceof Error ? error.message : "Operation failed";
        this.scheduleDraw();
      }
      return;
    }
    if (action === "cancel") {
      this.pendingCloseQueue = [];
      this.pendingDownloadDirtyQueue = [];
      this.downloadInProgress = false;
      this.statusText = "Canceled";
      this.closeModal();
      return;
    }

    modal.pending = true;
    this.scheduleDraw();
    try {
      if (modal.kind === "dirtyClose") await this.runDirtyCloseModalAction(modal, action);
      else if (modal.kind === "dirtyDownload") await this.runDirtyDownloadModalAction(modal, action);
      else if (modal.kind === "deleteFolder") await this.runDeleteFolderModalAction(modal, action);
      else if (modal.kind === "clearFileSystem") await this.runClearFileSystemModalAction(modal, action);
      else if (modal.kind === "clearChat") await this.runClearChatModalAction(modal, action);
      else if (modal.kind === "zipImport") await this.runZipImportModalAction(modal, action);
    } catch (error) {
      if (this.modal !== modal) throw error;
      modal.pending = false;
      this.statusText = error instanceof Error ? error.message : "Operation failed";
      this.scheduleDraw();
    }
  }

  private runToolCallLimitModalAction(modal: Extract<ModalState, { kind: "toolCallLimit" }>, action: ModalAction): void {
    const decision: ToolCallLimitDecision = action === "allowAll" ? "allowAll" : action === "allowMore" ? "allowMore" : "stop";
    modal.resolve(decision);
    this.statusText = decision === "allowAll"
      ? "Tool calls unlimited for this turn"
      : decision === "allowMore"
        ? `Allowed ${modal.limit} more tool calls`
        : "Tool calls stopped";
    this.closeModal();
  }

  private runDuplicateToolCallModalAction(modal: Extract<ModalState, { kind: "duplicateToolCall" }>, action: ModalAction): void {
    const decision: DuplicateToolCallDecision = action === "allowDuplicateTool" ? "allow" : "break";
    modal.resolve(decision);
    this.statusText = decision === "allow"
      ? `Allowed duplicate ${modal.call.name}`
      : `Broke duplicate ${modal.call.name}`;
    this.closeModal();
  }

  private async runDirtyCloseModalAction(modal: Extract<ModalState, { kind: "dirtyClose" }>, action: ModalAction): Promise<void> {
    const doc = this.docs.get(modal.docId);
    if (!doc) {
      this.closeModal();
      return;
    }
    if (action !== "save" && action !== "discard") return;
    if (action === "save") await this.saveDocument(doc, modal.savePath);
    const path = doc.path;
    const label = path ?? this.documentLabel(doc);
    this.modal = null;
    this.modalHover = null;
    this.closeTab(doc.id);
    if (action === "discard") {
      if (path) this.docs.removePath(path);
      else this.forgetUntitledDocument(doc.id);
    }
    this.statusText = action === "save" ? `Saved and closed ${path ?? label}` : `Closed ${label} without saving`;
    if (this.pendingCloseQueue.length > 0) {
      await this.closeNextPendingTab();
      return;
    }
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.scheduleDraw();
  }

  private async runDirtyDownloadModalAction(modal: Extract<ModalState, { kind: "dirtyDownload" }>, action: ModalAction): Promise<void> {
    const doc = this.docs.get(modal.docId);
    if (!doc) {
      this.modal = null;
      this.modalHover = null;
      await this.openNextDownloadDirtyModal();
      return;
    }
    if (action !== "save" && action !== "discard") return;
    if (action === "save") await this.saveDocument(doc, modal.savePath);
    this.modal = null;
    this.modalHover = null;
    this.statusText = action === "save" ? `Saved ${doc.path ?? this.documentLabel(doc)}` : `Skipped ${doc.path ?? this.documentLabel(doc)}`;
    await this.openNextDownloadDirtyModal();
  }

  private async runDeleteFolderModalAction(modal: Extract<ModalState, { kind: "deleteFolder" }>, action: ModalAction): Promise<void> {
    if (action !== "delete") return;
    await this.deleteFolderNow(modal.path);
    if (this.modal === modal) {
      this.modal = null;
      this.modalHover = null;
      if (this.activeDoc()) this.focusEditor();
      else this.input.blur();
      this.scheduleDraw();
    }
  }

  private async runClearFileSystemModalAction(modal: Extract<ModalState, { kind: "clearFileSystem" }>, action: ModalAction): Promise<void> {
    if (action !== "delete") return;
    await this.clearFileSystemNow();
    if (this.modal === modal) {
      this.modal = null;
      this.modalHover = null;
      this.input.blur();
      this.scheduleDraw();
    }
  }

  private async runClearChatModalAction(modal: Extract<ModalState, { kind: "clearChat" }>, action: ModalAction): Promise<void> {
    if (action !== "clearChat" || this.chat.running) return;
    await this.clearChatNow();
    if (this.modal === modal) {
      this.modal = null;
      this.modalHover = null;
      if (this.activeDoc()) this.focusEditor();
      else this.input.blur();
      this.scheduleDraw();
    }
  }

  private async runZipImportModalAction(modal: Extract<ModalState, { kind: "zipImport" }>, action: ModalAction): Promise<void> {
    if (action !== "replace" && action !== "append") return;
    const mode = action;
    const file = modal.file;
    this.modal = null;
    this.modalHover = null;
    await this.importWorkspaceZip(file, mode);
  }

  private startRename(path: string, rect?: Rect): void {
    this.closeContextMenu();
    this.renamePath = normalizePath(path);
    this.selectFileTreePath(this.renamePath);
    const name = basename(path);
    const selectedEnd = fileStemSelectionEnd(name);
    this.renameBuffer.text = name;
    this.renameBuffer.anchor = 0;
    this.renameBuffer.cursor = selectedEnd;
    this.renameBuffer.scrollX = 0;
    this.renameBuffer.clearUndoHistory();
    this.statusText = `Renaming ${path}`;
    this.draw();
    this.focusRename(rect ?? this.renameInputRect() ?? undefined);
    this.scheduleDraw();
  }

  private primeRenameKeyboardForTouch(): void {
    if (!isIOSDevice() && !this.isMobileContextMode()) return;
    this.beginTouchKeyboardStabilization();
    this.input.focusEditor(this.renameTarget(), this.renameInputRect() ?? { x: this.ui(56), y: this.ui(40), w: Math.max(this.ui(80), this.sidebarWidth - this.ui(20)), h: this.ui(24) });
    this.resetCaretBlink();
  }

  private focusRename(rect?: Rect): void {
    this.input.focusEditor(this.renameTarget(), rect ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 24 });
    this.resetCaretBlink();
    this.requestFocusedInputReveal();
  }

  private cancelRename(): void {
    if (!this.renamePath) return;
    this.renamePath = null;
    this.renameSelecting = false;
    this.renameBuffer.text = "";
    this.renameBuffer.cursor = 0;
    this.renameBuffer.anchor = 0;
    this.renameBuffer.scrollX = 0;
    this.statusText = "Rename canceled";
    this.focusEditor();
    this.scheduleDraw();
  }

  private async commitRename(): Promise<boolean> {
    const oldPath = this.renamePath;
    if (!oldPath) return false;
    const name = this.renameBuffer.text.trim();
    if (!isValidFileName(name)) {
      this.statusText = invalidFileNameCharacterRanges(this.renameBuffer.text).length ? "File name contains invalid characters" : "File name is not valid";
      this.focusRename();
      return false;
    }
    const newPath = joinPath(dirname(oldPath), name);
    if (newPath === oldPath) {
      this.renamePath = null;
      this.renameSelecting = false;
      this.focusEditor();
      this.scheduleDraw();
      return true;
    }
    if (await this.vfs.stat(newPath)) {
      this.statusText = `File exists: ${newPath}`;
      this.focusRename();
      return false;
    }
    const node = await this.vfs.stat(oldPath);
    await this.vfs.rename(oldPath, newPath);
    if (node?.kind === "dir") {
      for (const doc of this.docs.all()) {
        if (!doc.path || !isSameOrDescendant(doc.path, oldPath)) continue;
        const nextPath = doc.path === oldPath ? newPath : joinPath(newPath, doc.path.slice(oldPath.length + 1));
        this.docs.renamePath(doc.path, nextPath);
      }
      this.remapFolderExpansion(oldPath, newPath);
    } else {
      this.docs.renamePath(oldPath, newPath);
    }
    this.remapFileTreeSelection(oldPath, newPath);
    this.renamePath = null;
    this.renameSelecting = false;
    await this.refreshFiles();
    this.syncOpenTabs();
    this.statusText = `Renamed ${oldPath} to ${newPath}`;
    this.focusEditor();
    this.contextMenuHover = null;
    this.scheduleDraw();
    return true;
  }

  private setRenameCursorFromPoint(x: number, rect: Rect, extend: boolean): void {
    const offset = x - (rect.x + this.ui(5)) + this.renameBuffer.scrollX;
    const col = this.columnFromTextOffset(this.renameBuffer.text, offset, "ui");
    this.renameBuffer.cursor = col;
    if (!extend) this.renameBuffer.anchor = col;
    this.revealMiniBufferCaret(this.renameBuffer, rect, this.ui(5));
    this.resetCaretBlink();
  }

  private selectRenameWordFromPoint(x: number, rect: Rect): void {
    const offset = x - (rect.x + this.ui(5)) + this.renameBuffer.scrollX;
    const text = this.renameBuffer.text;
    if (!text) return;
    const col = this.columnFromTextOffset(text, offset, "ui");
    let index = clamp(col, 0, Math.max(0, text.length - 1));
    if (!isWordChar(text.charAt(index)) && col > 0 && isWordChar(text.charAt(col - 1))) index = col - 1;
    let start = index;
    let end = index + 1;
    if (isWordChar(text.charAt(index))) {
      while (start > 0 && isWordChar(text.charAt(start - 1))) start--;
      while (end < text.length && isWordChar(text.charAt(end))) end++;
    }
    this.renameBuffer.anchor = start;
    this.renameBuffer.cursor = end;
    this.revealMiniBufferCaret(this.renameBuffer, rect, this.ui(5));
    this.resetCaretBlink();
  }

  private pointHitsRenameSelection(x: number, rect: Rect): boolean {
    if (!this.renameBuffer.hasSelection()) return false;
    const start = Math.min(this.renameBuffer.anchor, this.renameBuffer.cursor);
    const end = Math.max(this.renameBuffer.anchor, this.renameBuffer.cursor);
    const textX = rect.x + this.ui(5) - this.renameBuffer.scrollX;
    const startX = textX + this.renderer.measureText(this.renameBuffer.text.slice(0, start), "ui");
    const endX = textX + this.renderer.measureText(this.renameBuffer.text.slice(0, end), "ui");
    return x >= startX && x <= Math.max(startX + 2, endX);
  }

  private renameInputRect(): Rect | null {
    return this.hits.find((hit): hit is Extract<HitItem, { type: "fileRenameInput" }> => hit.type === "fileRenameInput")?.rect ?? null;
  }

  private setSearchCursorFromPoint(x: number, rect: Rect, extend: boolean): void {
    const offset = x - (rect.x + this.ui(8)) + this.searchBuffer.scrollX;
    const col = this.columnFromTextOffset(this.searchBuffer.text, offset, "ui");
    this.searchBuffer.cursor = col;
    if (!extend) this.searchBuffer.anchor = col;
    this.revealMiniBufferCaret(this.searchBuffer, rect, this.ui(8));
    this.resetCaretBlink();
  }

  private selectSearchWordFromPoint(x: number, rect: Rect): void {
    const offset = x - (rect.x + this.ui(8)) + this.searchBuffer.scrollX;
    const text = this.searchBuffer.text;
    if (!text) return;
    const col = this.columnFromTextOffset(text, offset, "ui");
    let index = clamp(col, 0, Math.max(0, text.length - 1));
    if (!isWordChar(text.charAt(index)) && col > 0 && isWordChar(text.charAt(col - 1))) index = col - 1;
    let start = index;
    let end = index + 1;
    if (isWordChar(text.charAt(index))) {
      while (start > 0 && isWordChar(text.charAt(start - 1))) start--;
      while (end < text.length && isWordChar(text.charAt(end))) end++;
    }
    this.searchBuffer.anchor = start;
    this.searchBuffer.cursor = end;
    this.revealMiniBufferCaret(this.searchBuffer, rect, this.ui(8));
    this.resetCaretBlink();
  }

  private pointHitsSearchSelection(x: number, rect: Rect): boolean {
    if (!this.searchBuffer.hasSelection()) return false;
    const start = Math.min(this.searchBuffer.anchor, this.searchBuffer.cursor);
    const end = Math.max(this.searchBuffer.anchor, this.searchBuffer.cursor);
    const textX = rect.x + this.ui(8) - this.searchBuffer.scrollX;
    const startX = textX + this.renderer.measureText(this.searchBuffer.text.slice(0, start), "ui");
    const endX = textX + this.renderer.measureText(this.searchBuffer.text.slice(0, end), "ui");
    return x >= startX && x <= Math.max(startX + 2, endX);
  }

  private searchInputRect(): Rect | null {
    return this.textFieldRect("search") ?? this.hits.find((hit): hit is Extract<HitItem, { type: "searchInput" }> => hit.type === "searchInput")?.rect ?? null;
  }

  private setChatInputCursorFromPoint(point: Point, rect: Rect, extend: boolean): void {
    const doc = this.chatDraft;
    const position = this.chatInputPositionFromPoint(point, rect);
    doc.setSelection(extend ? doc.selection.anchor : position, position);
    this.ensureChatInputCaretVisible(rect);
    this.resetCaretBlink();
  }

  private selectChatInputWordFromPoint(point: Point, rect: Rect): void {
    const doc = this.chatDraft;
    const position = this.chatInputPositionFromPoint(point, rect);
    const lineIndex = position.line;
    const line = doc.lines[lineIndex] ?? "";
    if (!line) {
      doc.setSelection({ line: lineIndex, col: 0 });
      this.resetCaretBlink();
      return;
    }
    const col = position.col;
    let index = clamp(col, 0, Math.max(0, line.length - 1));
    if (!isWordChar(line.charAt(index)) && col > 0 && isWordChar(line.charAt(col - 1))) index = col - 1;
    let start = index;
    let end = index + 1;
    if (isWordChar(line.charAt(index))) {
      while (start > 0 && isWordChar(line.charAt(start - 1))) start--;
      while (end < line.length && isWordChar(line.charAt(end))) end++;
    }
    doc.setSelection({ line: lineIndex, col: start }, { line: lineIndex, col: end });
    this.ensureChatInputCaretVisible(rect);
    this.resetCaretBlink();
  }

  private pointHitsChatInputSelection(point: Point, rect: Rect): boolean {
    const doc = this.chatDraft;
    if (!doc.hasSelection()) return false;
    const metrics = this.chatInputMetrics(rect);
    const content = metrics.content;
    const lineH = this.renderer.lineHeight("ui");
    const visualIndex = clamp(Math.floor((point.y - content.y + this.chatInputScrollY) / lineH), 0, metrics.visualLines.length - 1);
    const visualLine = metrics.visualLines[visualIndex]!;
    const line = visualLine.line;
    const ordered = doc.getOrderedSelection();
    if (line < ordered.start.line || line > ordered.end.line) return false;
    const text = doc.lines[line] ?? "";
    const start = Math.max(visualLine.start, ordered.start.line === line ? ordered.start.col : 0);
    const end = Math.min(visualLine.end, ordered.end.line === line ? ordered.end.col : text.length);
    if (end <= start) return false;
    const sx = content.x + this.renderer.measureText(text.slice(visualLine.start, start), "ui");
    const ex = content.x + this.renderer.measureText(text.slice(visualLine.start, end), "ui");
    return point.x >= sx && point.x <= Math.max(sx + 2, ex);
  }

  private ensureChatInputCaretVisible(rect: Rect): void {
    const metrics = this.chatInputMetrics(rect);
    const content = metrics.content;
    const lineH = this.renderer.lineHeight("ui");
    const visual = this.chatInputVisualPositionForDocPosition(this.chatDraft.selection.head, metrics.visualLines);
    const caretTop = visual.index * lineH;
    const caretBottom = caretTop + lineH;
    const margin = Math.min(lineH, Math.max(0, content.h / 3));
    let scroll = this.chatInputScrollY;
    if (caretTop < scroll + margin) scroll = caretTop - margin;
    else if (caretBottom > scroll + content.h - margin) scroll = caretBottom - content.h + margin;
    this.chatInputScrollY = clamp(scroll, 0, Math.max(0, metrics.contentHeight - metrics.viewport.h));
  }

  private chatInputCaretRect(input: Rect): Rect {
    return this.chatInputPositionRect(input, this.chatDraft.selection.head);
  }

  private chatInputPositionRect(input: Rect, pos: Position): Rect {
    const metrics = this.chatInputMetrics(input);
    const content = metrics.content;
    const lineH = this.renderer.lineHeight("ui");
    const clamped = this.chatDraft.clampPosition(pos);
    const line = this.chatDraft.lines[clamped.line] ?? "";
    const visual = this.chatInputVisualPositionForDocPosition(clamped, metrics.visualLines);
    const x = content.x + this.renderer.measureText(line.slice(visual.line.start, clamped.col), "ui");
    const y = content.y + visual.index * lineH - this.chatInputScrollY + this.ui(2);
    return { x, y, w: 1.5, h: lineH };
  }

  private chatInputPositionFromPoint(point: Point, rect: Rect): Position {
    const metrics = this.chatInputMetrics(rect);
    const content = metrics.content;
    const lineH = this.renderer.lineHeight("ui");
    const visualIndex = clamp(Math.floor((point.y - content.y + this.chatInputScrollY) / lineH), 0, metrics.visualLines.length - 1);
    const visualLine = metrics.visualLines[visualIndex]!;
    const col = visualLine.start + this.columnFromTextOffset(visualLine.text, point.x - content.x, "ui");
    return this.chatDraft.clampPosition({ line: visualLine.line, col: clamp(col, visualLine.start, visualLine.end) });
  }

  private bufferForTextField(field: TextFieldKey): MiniBuffer {
    if (field === "search") return this.searchBuffer;
    if (field === "projectReplace") return this.projectReplaceBuffer;
    if (isSettingTextField(field)) return this.settingsTextBuffers[field];
    const findState = this.activeFindState();
    if (field === "find") return findState?.findBuffer ?? this.inactiveFindBuffer;
    return findState?.replaceBuffer ?? this.inactiveFindReplaceBuffer;
  }

  private afterTextFieldChanged(field: TextFieldKey): void {
    if (isSettingTextField(field)) {
      if (field === "aiBaseUrl" || field === "aiApiKey") this.markAiEndpointEdited();
      this.scheduleDraw();
      return;
    }
    if (field === "search") {
      void this.runSearch();
      return;
    }
    if (field === "find") {
      this.selectDocumentFindMatch(1, true);
    }
  }

  private markAiEndpointEdited(): void {
    if (this.aiConnectionStatus.state === "idle" && !this.aiEndpointFieldState) return;
    this.aiConnectionStatus = { state: "idle", message: "Server settings changed. Check server again." };
    this.aiEndpointFieldState = null;
  }

  private syncSettingsTextBufferFromConfig(field: SettingTextKey): void {
    const config = loadAiEndpointConfig();
    const buffer = this.settingsTextBuffers[field];
    if (field === "aiBaseUrl") buffer.text = config.apiBaseUrl;
    else if (field === "aiApiKey") buffer.text = config.apiKey;
    else if (field === "aiModel") buffer.text = config.model;
    else buffer.text = config.maxContextTokens ? String(config.maxContextTokens) : "";
    buffer.cursor = buffer.text.length;
    buffer.anchor = buffer.cursor;
    buffer.scrollX = 0;
    buffer.clearUndoHistory();
  }

  private setTextFieldCursorFromPoint(field: TextFieldKey, x: number, rect: Rect, extend: boolean): void {
    const buffer = this.bufferForTextField(field);
    const offset = x - (rect.x + this.ui(8)) + buffer.scrollX;
    const col = this.columnFromTextOffset(buffer.text, offset, "ui");
    buffer.cursor = col;
    if (!extend) buffer.anchor = col;
    this.revealMiniBufferCaret(buffer, rect, this.ui(8));
    this.resetCaretBlink();
  }

  private selectTextFieldWordFromPoint(field: TextFieldKey, x: number, rect: Rect): void {
    const buffer = this.bufferForTextField(field);
    const offset = x - (rect.x + this.ui(8)) + buffer.scrollX;
    const text = buffer.text;
    if (!text) return;
    const col = this.columnFromTextOffset(text, offset, "ui");
    let index = clamp(col, 0, Math.max(0, text.length - 1));
    if (!isWordChar(text.charAt(index)) && col > 0 && isWordChar(text.charAt(col - 1))) index = col - 1;
    let start = index;
    let end = index + 1;
    if (isWordChar(text.charAt(index))) {
      while (start > 0 && isWordChar(text.charAt(start - 1))) start--;
      while (end < text.length && isWordChar(text.charAt(end))) end++;
    }
    buffer.anchor = start;
    buffer.cursor = end;
    this.revealMiniBufferCaret(buffer, rect, this.ui(8));
    this.resetCaretBlink();
  }

  private pointHitsTextFieldSelection(field: TextFieldKey, x: number, rect: Rect): boolean {
    const buffer = this.bufferForTextField(field);
    if (!buffer.hasSelection()) return false;
    const start = Math.min(buffer.anchor, buffer.cursor);
    const end = Math.max(buffer.anchor, buffer.cursor);
    const textX = rect.x + this.ui(8) - buffer.scrollX;
    const startX = textX + this.renderer.measureText(buffer.text.slice(0, start), "ui");
    const endX = textX + this.renderer.measureText(buffer.text.slice(0, end), "ui");
    return x >= startX && x <= Math.max(startX + 2, endX);
  }

  private textFieldRect(field: TextFieldKey): Rect | null {
    return this.hits.find((hit): hit is Extract<HitItem, { type: "textField" }> => hit.type === "textField" && hit.field === field)?.rect ?? null;
  }

  private bufferForTextSelectionHandleTarget(target: Exclude<TextSelectionHandleTarget, { type: "chatInput" }>): MiniBuffer {
    if (target.type === "rename") return this.renameBuffer;
    if (target.type === "settingsNumber") return this.settingsNumberBuffer;
    return this.bufferForTextField(target.field);
  }

  private focusTextSelectionHandleTarget(target: TextSelectionHandleTarget, rect: Rect): void {
    if (target.type === "rename") {
      this.focusRename(rect);
    } else if (target.type === "settingsNumber") {
      this.focusSettingsNumber(target.key, rect);
    } else if (target.type === "chatInput") {
      this.focusMiniTarget("chat", rect, true);
    } else {
      this.focusTextField(target.field, rect);
    }
  }

  private textSelectionHandlePadX(target: Exclude<TextSelectionHandleTarget, { type: "chatInput" }>): number {
    return target.type === "rename" ? this.ui(5) : this.ui(8);
  }

  private textSelectionTargetLabel(target: TextSelectionHandleTarget): string {
    if (target.type === "rename") return target.path;
    if (target.type === "textField") return target.field;
    if (target.type === "settingsNumber") return target.key;
    return "chatInput";
  }

  private isTextSelectionHandleTargetActive(target: TextSelectionHandleTarget): boolean {
    if (target.type === "rename") return this.renamePath === target.path;
    if (target.type === "settingsNumber") return this.activeSettingsNumber === target.key;
    if (target.type === "chatInput") return this.input.activeTarget?.kind === "chat";
    return this.input.activeTarget?.kind === target.field;
  }

  private miniBufferColumnFromPoint(buffer: MiniBuffer, input: Rect, padX: number, x: number): number {
    const offset = x - (input.x + padX) + buffer.scrollX;
    return this.columnFromTextOffset(buffer.text, offset, "ui");
  }

  private miniBufferContentRect(input: Rect, padX: number): Rect {
    return { x: input.x + padX, y: input.y, w: Math.max(1, input.w - padX * 2), h: input.h };
  }

  private clampMiniBufferScroll(buffer: MiniBuffer, input: Rect, padX: number): void {
    const content = this.miniBufferContentRect(input, padX);
    const maxScroll = Math.max(0, this.renderer.measureText(buffer.text, "ui") - content.w);
    buffer.scrollX = clamp(buffer.scrollX, 0, maxScroll);
  }

  private revealMiniBufferCaret(buffer: MiniBuffer, input: Rect, padX: number): void {
    const content = this.miniBufferContentRect(input, padX);
    const caretX = this.renderer.measureText(buffer.text.slice(0, buffer.cursor), "ui");
    const maxScroll = Math.max(0, this.renderer.measureText(buffer.text, "ui") - content.w);
    const margin = Math.min(this.ui(24), Math.max(0, content.w / 3));
    let scroll = buffer.scrollX;
    if (caretX < scroll + margin) scroll = caretX - margin;
    else if (caretX > scroll + content.w - margin) scroll = caretX - content.w + margin;
    buffer.scrollX = clamp(scroll, 0, maxScroll);
  }

  private isTextFieldCaretVisible(field: TextFieldKey): boolean {
    return this.input.activeTarget?.kind === field && (this.input.composing || this.isCaretBlinkOn());
  }

  private toggleFolder(path: string): void {
    if (this.expandedFolders.has(path)) this.expandedFolders.delete(path);
    else this.expandedFolders.add(path);
    this.statusText = `${this.expandedFolders.has(path) ? "Expanded" : "Collapsed"} ${path}`;
    this.scheduleDraw();
  }

  private syncFileTreeFolders(): void {
    const next = new Set<string>();
    for (const node of this.treeNodes) {
      if (node.kind !== "dir") continue;
      const path = normalizePath(node.path);
      next.add(path);
      if (!this.knownFolders.has(path)) this.expandedFolders.add(path);
    }
    for (const path of [...this.expandedFolders]) {
      if (!next.has(path)) this.expandedFolders.delete(path);
    }
    this.knownFolders.clear();
    for (const path of next) this.knownFolders.add(path);
  }

  private fileTreeSelectedPath(): string | null {
    const activePath = this.activeDoc()?.path;
    return this.selectedFileTreePath ?? (activePath && !this.isAiSpecialPath(activePath) ? activePath : null);
  }

  private selectFileTreePath(path: string | null): void {
    const next = path ? normalizePath(path) : null;
    if (this.selectedFileTreePath === next) return;
    this.selectedFileTreePath = next;
    this.scheduleDraw();
  }

  private selectActiveDocumentInFileTree(): void {
    const path = this.activeDoc()?.path;
    if (path && !this.isAiSpecialPath(path)) this.selectFileTreePath(path);
  }

  private syncFileTreeSelection(): void {
    const paths = new Set(this.treeNodes.map((node) => normalizePath(node.path)));
    if (this.selectedFileTreePath && !paths.has(this.selectedFileTreePath)) this.selectedFileTreePath = null;
    if (this.hoveredFileTreePath && !paths.has(this.hoveredFileTreePath)) this.hoveredFileTreePath = null;
  }

  private remapFileTreeSelection(oldPath: string, newPath: string): void {
    this.selectedFileTreePath = remapSelectedTreePath(this.selectedFileTreePath, oldPath, newPath);
    this.hoveredFileTreePath = remapSelectedTreePath(this.hoveredFileTreePath, oldPath, newPath);
  }

  private clearFileTreeSelectionUnder(path: string): void {
    if (this.selectedFileTreePath && isSameOrDescendant(this.selectedFileTreePath, path)) this.selectedFileTreePath = null;
    if (this.hoveredFileTreePath && isSameOrDescendant(this.hoveredFileTreePath, path)) this.hoveredFileTreePath = null;
  }

  private remapFolderExpansion(oldPath: string, newPath: string): void {
    const remapped = new Map<string, string>();
    for (const path of this.expandedFolders) {
      if (isSameOrDescendant(path, oldPath)) remapped.set(path, path === oldPath ? newPath : joinPath(newPath, path.slice(oldPath.length + 1)));
    }
    for (const [oldFolder, newFolder] of remapped) {
      this.expandedFolders.delete(oldFolder);
      this.expandedFolders.add(newFolder);
    }
  }

  private removeFolderExpansion(path: string): void {
    for (const folder of [...this.expandedFolders]) {
      if (isSameOrDescendant(folder, path)) this.expandedFolders.delete(folder);
    }
    this.knownFolders.delete(path);
  }

  private fileTreeEntries(): FileTreeEntry[] {
    const root: Extract<FileTreeEntry, { type: "dir" }> = { type: "dir", path: "/", name: "", children: [] };
    const dirs = new Map<string, Extract<FileTreeEntry, { type: "dir" }>>([["/", root]]);
    for (const node of this.treeNodes) {
      const path = normalizePath(node.path);
      const parts = path.split("/").filter(Boolean);
      let parent = root;
      let dirPath = "";
      const dirDepth = node.kind === "dir" ? parts.length : parts.length - 1;
      for (let i = 0; i < dirDepth; i++) {
        dirPath = `${dirPath}/${parts[i]}`;
        let dir = dirs.get(dirPath);
        if (!dir) {
          dir = { type: "dir", path: dirPath, name: parts[i]!, children: [] };
          dirs.set(dirPath, dir);
          parent.children.push(dir);
        }
        parent = dir;
      }
      if (node.kind === "file") parent.children.push({ type: "file", path, name: parts[parts.length - 1] ?? path });
    }
    sortFileTree(root.children);
    return root.children;
  }

  private async saveDocument(doc: TextDocument, path = doc.path): Promise<string> {
    if (this.isAiSpecialDoc(doc)) {
      this.saveAiSpecialDocument(doc);
      return doc.path ?? this.documentLabel(doc);
    }
    if (doc.readOnly) {
      doc.markSaved();
      this.statusText = "File type not supported";
      this.scheduleDraw();
      return doc.path ?? this.documentLabel(doc);
    }
    const wasUntitled = !doc.path;
    const target = path ?? await this.savePathForUntitledDocument(doc);
    if (doc.path) await this.docs.save(doc);
    else await this.docs.saveAs(doc, target);
    if (wasUntitled) {
      this.untitledLabels.delete(doc.id);
      this.untitledPreferredNames.delete(doc.id);
      await this.refreshFiles();
    }
    this.scheduleDraw();
    return doc.path ?? target;
  }

  private saveAiSpecialDocument(doc: TextDocument): boolean {
    if (!doc.path) return false;
    const path = normalizePath(doc.path);
    const text = doc.getText();
    try {
      if (path === AI_SETTINGS_DOC_PATH) {
        const parsed = JSON.parse(text) as Partial<ReturnType<typeof loadAiEndpointConfig>>;
        saveAiEndpointConfig(parsed);
      } else if (path === AI_SYSTEM_PROMPT_DOC_PATH) {
        saveAiSystemPrompt(text);
      } else if (path === AI_TAG_TOOL_PROMPT_DOC_PATH) {
        saveAiTagToolPrompt(text);
      } else if (path === AI_HARMONY_TOOL_PROMPT_DOC_PATH) {
        saveAiHarmonyToolPrompt(text);
      } else if (path === AI_COMPACT_PROMPT_DOC_PATH) {
        saveAiCompactPrompt(text);
      } else {
        return false;
      }
      doc.markSaved();
      this.statusText = `Saved ${this.aiSpecialLabel(path)}`;
      this.scheduleDraw();
      return true;
    } catch (error) {
      this.statusText = `AI settings JSON is invalid: ${error instanceof Error ? error.message : String(error)}`;
      this.scheduleDraw();
      return false;
    }
  }

  private afterDocumentMutated(doc: TextDocument | undefined): void {
    if (!doc) return;
    this.scheduleDraw();
  }

  private async savePathForUntitledDocument(doc: TextDocument): Promise<string> {
    const preferred = this.untitledPreferredNames.get(doc.id);
    if (preferred && isValidFileName(preferred)) {
      const candidate = joinPath("/", preferred);
      if (!await this.vfs.stat(candidate)) return candidate;
    }
    return this.nextCreatedPath("/", "file");
  }

  private forgetUntitledDocument(docId: string): void {
    this.untitledLabels.delete(docId);
    this.untitledPreferredNames.delete(docId);
    this.clearDocumentCaches(docId);
    this.docs.remove(docId);
  }

  private async requestCloseTab(docId: string): Promise<void> {
    if (this.isSettingsTab(docId)) {
      this.closeTab(docId);
      return;
    }
    const doc = this.docs.get(docId);
    if (!doc) return;
    if (doc.dirty) {
      await this.openDirtyCloseModal(doc);
      return;
    }
    this.closeTab(docId);
    if (!doc.path) this.forgetUntitledDocument(doc.id);
  }

  private async requestCloseTabs(docIds: string[]): Promise<void> {
    this.pendingCloseQueue = [...new Set(docIds)];
    await this.closeNextPendingTab();
  }

  private async closeNextPendingTab(): Promise<void> {
    while (this.pendingCloseQueue.length > 0) {
      const docId = this.pendingCloseQueue.shift()!;
      if (this.isSettingsTab(docId)) {
        if (this.groupContaining(docId)) this.closeTab(docId);
        continue;
      }
      const doc = this.docs.get(docId);
      if (!doc || !this.groupContaining(docId)) continue;
      if (doc.dirty) {
        await this.openDirtyCloseModal(doc);
        return;
      }
      this.closeTab(docId);
      if (!doc.path) this.forgetUntitledDocument(doc.id);
    }
  }

  private async requestWorkspaceDownload(): Promise<void> {
    if (this.downloadInProgress || this.modal) return;
    if (this.renamePath && !await this.commitRename()) return;
    this.pendingDownloadDirtyQueue = this.docs.all().filter((doc) => doc.dirty && !this.isAiSpecialDoc(doc)).map((doc) => doc.id);
    if (this.pendingDownloadDirtyQueue.length > 0) {
      await this.openNextDownloadDirtyModal();
      return;
    }
    await this.prepareWorkspaceDownload();
  }

  private async openNextDownloadDirtyModal(): Promise<void> {
    while (this.pendingDownloadDirtyQueue.length > 0) {
      const docId = this.pendingDownloadDirtyQueue.shift()!;
      const doc = this.docs.get(docId);
      if (!doc?.dirty) continue;
      await this.openDirtyDownloadModal(doc);
      return;
    }
    await this.prepareWorkspaceDownload();
  }

  private async prepareWorkspaceDownload(): Promise<void> {
    if (this.downloadInProgress) return;
    this.downloadInProgress = true;
    this.statusText = "Preparing download";
    this.openZipProgressModal("Reading workspace files...", "Starting", 0);
    await nextFrame();
    try {
      const zip = new JSZip();
      const entries = await this.collectZipEntries("/");
      const files = entries.filter((entry) => entry.node.kind === "file");
      let readCount = 0;
      for (const entry of entries) {
        if (entry.node.kind === "dir") {
          zip.folder(entry.zipPath);
          continue;
        }
        readCount++;
        this.updateZipProgress("Reading workspace files...", `${readCount} of ${files.length}: ${entry.zipPath}`, files.length ? readCount / files.length * 0.72 : 0.72);
        const data = await this.vfs.readFile(entry.node.path);
        zip.file(entry.zipPath, data, { binary: true, date: new Date(entry.node.mtime) });
        if (readCount === files.length || readCount % 8 === 0) await nextFrame();
      }
      this.updateZipProgress("Compressing workspace...", "0%", 0.74);
      let lastProgressUpdate = 0;
      const blob = await zip.generateAsync({
        type: "blob",
        compression: "DEFLATE",
        compressionOptions: { level: 6 },
        platform: "UNIX"
      }, (metadata) => {
        const now = performance.now();
        if (metadata.percent < 100 && now - lastProgressUpdate < 80) return;
        lastProgressUpdate = now;
        this.updateZipProgress("Compressing workspace...", `${Math.round(metadata.percent)}%`, 0.74 + metadata.percent / 100 * 0.26);
      });
      const filename = `workspace-${downloadTimestamp()}.zip`;
      const url = URL.createObjectURL(blob);
      this.downloadInProgress = false;
      this.statusText = "Download ready";
      this.openDownloadReadyModal(url, filename, files.length, blob.size);
    } catch (error) {
      this.downloadInProgress = false;
      this.pendingDownloadDirtyQueue = [];
      this.closeModal();
      this.statusText = error instanceof Error ? error.message : "Could not prepare download";
      this.scheduleDraw();
    }
  }

  private async collectZipEntries(path: string): Promise<Array<{ node: VfsNode; zipPath: string }>> {
    const entries: Array<{ node: VfsNode; zipPath: string }> = [];
    const children = await this.vfs.listDir(path);
    for (const node of children) {
      if (node.path === "/" || node.path.startsWith("/.slug-")) continue;
      const zipPath = node.path.slice(1);
      entries.push({ node, zipPath: node.kind === "dir" ? `${zipPath}/` : zipPath });
      if (node.kind === "dir") entries.push(...await this.collectZipEntries(node.path));
    }
    return entries;
  }

  private updateZipProgress(message: string, detail: string, progress: number): void {
    const modal = this.modal;
    if (modal?.kind === "zipProgress") {
      modal.message = message;
      modal.detail = detail;
      modal.progress = clamp(progress, 0, 1);
    }
    this.statusText = `${message} ${Math.round(clamp(progress, 0, 1) * 100)}%`;
    this.scheduleDraw();
  }

  private async importWorkspaceZip(file: File, mode: "replace" | "append"): Promise<void> {
    try {
      this.statusText = "Reading workspace zip...";
      this.scheduleDraw();
      await nextFrame();
      const entries = await this.loadZipWorkspaceEntries(file);
      if (mode === "replace") {
        this.statusText = "Replacing workspace...";
        this.scheduleDraw();
        await this.clearWorkspaceContents();
        this.resetEditorSession();
      } else {
        this.statusText = "Importing workspace...";
        this.scheduleDraw();
      }
      const result = await this.writeZipEntriesToWorkspace(entries);
      await this.refreshFiles();
      if (mode === "replace") this.input.blur();
      this.statusText = `${mode === "replace" ? "Replaced" : "Imported"} ${result.files} file${result.files === 1 ? "" : "s"} from ${file.name}`;
      this.scheduleDraw();
    } catch (error) {
      this.statusText = error instanceof Error ? error.message : "Could not import zip";
      this.scheduleDraw();
    }
  }

  private async clearWorkspaceContents(): Promise<void> {
    await this.vfs.remove("/", { recursive: true });
    await this.vfs.mkdir("/");
    this.expandedFolders.clear();
    this.knownFolders.clear();
  }

  private resetEditorSession(): void {
    this.clearPersistedEditorSession();
    this.docs.clear();
    const group = makeGroup("group-main");
    this.groups = [group];
    this.dockRoot = { type: "leaf", group };
    this.activeGroupId = group.id;
    this.activeDocId = null;
    this.openTabs = [];
    this.scrollStates.clear();
    this.tabScrollStates.clear();
    this.pendingTabRevealIds.clear();
    this.documentWidthCache.clear();
    this.lineWidthCache.clear();
    this.highlightCache.clear();
    this.findStates.clear();
    this.untitledLabels.clear();
    this.untitledPreferredNames.clear();
    this.filesScrollY = 0;
    this.searchScrollY = 0;
    this.pendingCloseQueue = [];
    this.pendingDownloadDirtyQueue = [];
  }

  private async loadZipWorkspaceEntries(file: File): Promise<ZipWorkspaceEntry[]> {
    const zip = await JSZip.loadAsync(await file.arrayBuffer());
    return Object.values(zip.files)
      .map((entry) => ({ entry, path: pathForZipEntry(entry.name) }))
      .filter((item): item is ZipWorkspaceEntry => Boolean(item.path));
  }

  private async writeZipEntriesToWorkspace(entries: ZipWorkspaceEntry[]): Promise<{ files: number; dirs: number; bytes: number }> {
    let files = 0;
    let dirs = 0;
    let bytes = 0;
    for (const { entry, path } of entries) {
      if (entry.dir) {
        await this.vfs.mkdir(path);
        dirs++;
        continue;
      }
      const data = await entry.async("uint8array");
      await this.vfs.writeFile(path, data, guessMime(path));
      files++;
      bytes += data.byteLength;
      if (files % 8 === 0) {
        this.statusText = `Imported ${files} file${files === 1 ? "" : "s"}...`;
        this.scheduleDraw();
        await nextFrame();
      }
    }
    return { files, dirs, bytes };
  }

  private startBrowserDownload(modal: Extract<ModalState, { kind: "downloadReady" }>): void {
    const anchor = document.createElement("a");
    anchor.href = modal.url;
    anchor.download = modal.filename;
    anchor.style.display = "none";
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    const url = modal.url;
    this.modal = null;
    this.modalHover = null;
    this.statusText = `Downloaded ${modal.filename}`;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    this.scheduleDraw();
  }

  private exportChatToDisk(): void {
    const filename = `chat-${downloadTimestamp()}.jsonl`;
    const blob = new Blob([this.chat.exportJsonl()], { type: "application/x-ndjson;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = filename;
    anchor.style.display = "none";
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    this.statusText = `Exported ${filename}`;
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
    this.scheduleDraw();
  }

  private debugChatToUntitled(): void {
    const text = this.chat.debugApiJsonl(this.aiRuntimeSettings(), this.settings.aiInsertEditorContext ? this.editorContextBundle() : null);
    const filename = `chat-debug-${downloadTimestamp()}.jsonl`;
    this.openUntitledDocument(this.activeGroupId, {
      label: "Debug Chat",
      text,
      preferredName: filename,
      dirty: true
    });
    this.statusText = "Opened chat debug JSONL";
    this.scheduleDraw();
  }

  private chatTranscriptText(messages = this.chatDisplayMessages()): string {
    return messages
      .map((msg) => {
        const label = msg.name ? `${this.chatRoleLabel(msg.role)}: ${msg.name}` : this.chatRoleLabel(msg.role);
        return `${label}\n${msg.text}`;
      })
      .join("\n\n");
  }

  private revokeDownloadReadyModal(): void {
    if (this.modal?.kind === "downloadReady") URL.revokeObjectURL(this.modal.url);
  }

  private closeTab(docId: string): void {
    const group = this.groupContaining(docId);
    if (!group) return;
    const label = this.tabLabel(docId);
    const index = group.tabs.indexOf(docId);
    group.tabs.splice(index, 1);
    this.findStates.delete(docId);
    if (group.activeDocId === docId) {
      group.activeDocId = group.tabs[index] ?? group.tabs[index - 1] ?? null;
    }
    this.pruneDockTree();
    if (this.activeDocId === docId) {
      const nextGroup = this.groups.find((item) => item.activeDocId) ?? this.groups[0]!;
      this.activeGroupId = nextGroup.id;
      this.activeDocId = nextGroup.activeDocId;
      if (this.activeDoc()) this.focusEditor();
      else this.input.blur();
    }
    this.syncOpenTabs();
    this.statusText = `Closed ${label}`;
    this.scheduleDraw();
  }

  private startTabDrag(docId: string, sourceGroupId: string, pointer: Point): void {
    const source = this.groupById(sourceGroupId);
    const sourceIndex = source.tabs.indexOf(docId);
    if (sourceIndex < 0) return;
    this.tabDrag = {
      docId,
      sourceGroupId,
      sourceIndex,
      restoreRoot: cloneDockNode(this.dockRoot),
      restoreActiveGroupId: this.activeGroupId,
      restoreActiveDocId: this.activeDocId,
      pointer: { ...pointer }
    };
    this.removeDocFromGroups(docId);
    const nextGroup = this.groups.find((group) => group.activeDocId) ?? this.groups[0]!;
    this.activeGroupId = nextGroup.id;
    this.activeDocId = nextGroup.activeDocId;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.syncOpenTabs();
    this.statusText = `Moving ${this.tabLabel(docId)}`;
    this.draw();
  }

  private clampSidebarWidth(width: number): number {
    const vp = this.viewport.get();
    const activityW = this.ui(48);
    const min = Math.min(this.ui(220), Math.max(this.ui(160), vp.cssWidth - activityW - this.ui(180)));
    const max = Math.max(min, Math.min(this.ui(560), vp.cssWidth - activityW - this.ui(180)));
    const next = clamp(width, min, max);
    this.lastSidebarWidth = next;
    return next;
  }

  private scrollForDoc(docId: string): EditorScrollState {
    let state = this.scrollStates.get(docId);
    if (!state) {
      state = { x: 0, y: 0 };
      this.scrollStates.set(docId, state);
    }
    return state;
  }

  private clearDocumentCaches(docId: string): void {
    this.documentWidthCache.delete(docId);
    for (const key of [...this.lineWidthCache.keys()]) {
      if (key.startsWith(`${docId}:`)) this.lineWidthCache.delete(key);
    }
    for (const key of [...this.highlightCache.keys()]) {
      if (key.startsWith(`${docId}:`)) this.highlightCache.delete(key);
    }
  }

  private editorGroupAt(x: number, y: number): EditorGroup | undefined {
    return this.groups.find((group) => rectContains(group.editorRect, x, y));
  }

  private maxScrollY(doc: TextDocument, rect: Rect): number {
    return Math.max(0, this.documentContentHeight(doc) - this.editorContentRect(doc, rect).h);
  }

  private maxScrollX(doc: TextDocument, rect: Rect): number {
    const contentRect = this.editorContentRect(doc, rect);
    return Math.max(0, this.documentContentWidth(doc) - this.visibleTextWidth(doc, contentRect));
  }

  private editorContentRect(doc: TextDocument, rect: Rect): Rect {
    return this.editorContentRectForOverflow(rect, this.editorOverflow(doc, rect));
  }

  private editorContentRectForOverflow(rect: Rect, overflow: EditorOverflow): Rect {
    const scrollbarSize = this.editorScrollbarSize();
    return {
      x: rect.x,
      y: rect.y,
      w: Math.max(1, rect.w - (overflow.vertical ? scrollbarSize : 0)),
      h: Math.max(1, rect.h - (overflow.horizontal ? scrollbarSize : 0))
    };
  }

  private editorScrollbarSize(): number {
    return this.ui(EDITOR_SCROLLBAR_SIZE);
  }

  private editorOverflow(doc: TextDocument, rect: Rect): EditorOverflow {
    let overflow: EditorOverflow = { vertical: false, horizontal: false };
    for (let i = 0; i < 4; i++) {
      const contentRect = this.editorContentRectForOverflow(rect, overflow);
      const next = {
        vertical: this.documentContentHeight(doc) > contentRect.h,
        horizontal: this.documentContentWidth(doc) > this.visibleTextWidth(doc, contentRect)
      };
      if (next.vertical === overflow.vertical && next.horizontal === overflow.horizontal) return next;
      overflow = next;
    }
    return overflow;
  }

  private gutterWidthForDoc(doc: TextDocument): number {
    if (!this.settings.showLineNumbers) return 0;
    const digits = Math.max(EDITOR_GUTTER_MIN_DIGITS, String(Math.max(1, doc.lineCount())).length);
    return Math.ceil(this.renderer.measureText("9".repeat(digits), "gutter") + EDITOR_GUTTER_PAD_LEFT + EDITOR_GUTTER_PAD_RIGHT);
  }

  private editorTextX(doc: TextDocument, contentRect: Rect): number {
    return contentRect.x + this.gutterWidthForDoc(doc) + EDITOR_TEXT_PAD_X;
  }

  private visibleTextWidth(doc: TextDocument, contentRect: Rect): number {
    return Math.max(1, contentRect.w - this.gutterWidthForDoc(doc) - EDITOR_TEXT_PAD_X * 2);
  }

  private documentContentHeight(doc: TextDocument): number {
    return doc.lineCount() * this.renderer.lineHeight("code");
  }

  private documentContentWidth(doc: TextDocument): number {
    const layoutKey = this.codeLayoutKey();
    const cached = this.documentWidthCache.get(doc.id);
    if (cached && cached.revision === doc.revision && cached.layoutKey === layoutKey) return cached.width;
    let maxLineWidth = 0;
    for (let lineIndex = 0; lineIndex < doc.lines.length; lineIndex++) {
      maxLineWidth = Math.max(maxLineWidth, this.lineWidthForDocLine(doc, lineIndex, layoutKey));
    }
    const width = maxLineWidth + EDITOR_TEXT_TRAILING_PAD_X;
    this.documentWidthCache.set(doc.id, { revision: doc.revision, layoutKey, width });
    return width;
  }

  private lineWidthForDocLine(doc: TextDocument, lineIndex: number, layoutKey = this.codeLayoutKey()): number {
    const text = doc.lines[lineIndex] ?? "";
    const key = `${doc.id}:${lineIndex}`;
    const hasNewlineMarker = this.settings.showWhitespace && lineIndex < doc.lineCount() - 1;
    const cacheText = hasNewlineMarker ? `${text}\n` : text;
    const cached = this.lineWidthCache.get(key);
    if (cached && cached.layoutKey === layoutKey && cached.text === cacheText) return cached.width;
    const newlineMarkerWidth = hasNewlineMarker ? this.renderer.measureText("\\n", "code") : 0;
    const width = this.measureCodeText(text) + newlineMarkerWidth;
    this.lineWidthCache.set(key, { layoutKey, text: cacheText, width });
    if (this.lineWidthCache.size > 20000) {
      const first = this.lineWidthCache.keys().next().value;
      if (first) this.lineWidthCache.delete(first);
    }
    return width;
  }

  private codeLayoutKey(): string {
    return `${this.settings.fontSize}:${this.settings.monospacedFont ? 1 : 0}:${this.codeTabSpaces()}:${this.settings.useTabStops ? 1 : 0}:${this.settings.showWhitespace ? 1 : 0}`;
  }

  private codeTabSpaces(): number {
    return clamp(Math.trunc(this.settings.tabSpaces), 1, 32);
  }

  private editorIndentString(): string {
    return this.settings.useTabStops ? "\t" : " ".repeat(this.codeTabSpaces());
  }

  private codeTabWidthPx(): number {
    return Math.max(1, this.renderer.measureText(" ", "code") * this.codeTabSpaces());
  }

  private codeAdvanceForText(text: string, startOffset = 0): number {
    let offset = startOffset;
    for (const char of text) offset += this.codeAdvanceForChar(char, offset);
    return offset - startOffset;
  }

  private codeAdvanceForChar(char: string, currentOffset: number): number {
    if (char !== "\t") return this.renderer.measureText(char, "code");
    const tabWidth = this.codeTabWidthPx();
    if (!this.settings.useTabStops) return tabWidth;
    return Math.max(1, Math.ceil((currentOffset + 0.0001) / tabWidth) * tabWidth - currentOffset);
  }

  private measureCodeText(text: string): number {
    return this.codeAdvanceForText(text, 0);
  }

  private measureCodePrefix(text: string, col: number): number {
    return this.measureCodeText(text.slice(0, col));
  }

  private drawVisibleCodeText(text: string, baseX: number, y: number, color: Color, startOffset: number, visibleStart: number, visibleEnd: number): { endOffset: number; clippedRight: boolean } {
    let offset = startOffset;
    let run = "";
    let runStartOffset = offset;
    const flush = () => {
      if (!run) return;
      this.renderer.text(run, baseX + runStartOffset, y, color, "code");
      run = "";
    };
    for (const char of text) {
      const advance = this.codeAdvanceForChar(char, offset);
      const nextOffset = offset + advance;
      if (nextOffset > visibleStart && offset < visibleEnd && char !== "\t") {
        if (!run) runStartOffset = offset;
        run += char;
      } else {
        flush();
      }
      offset = nextOffset;
      if (offset > visibleEnd) {
        flush();
        return { endOffset: offset, clippedRight: true };
      }
    }
    flush();
    return { endOffset: offset, clippedRight: false };
  }

  private drawWhitespaceForLine(text: string, lineIndex: number, lineCount: number, baseX: number, y: number, lineH: number, visibleStart: number, visibleEnd: number): void {
    if (!this.settings.showWhitespace) return;
    const color = this.whitespaceMarkerColor();
    let offset = 0;
    for (const char of text) {
      const advance = this.codeAdvanceForChar(char, offset);
      const nextOffset = offset + advance;
      if (nextOffset > visibleStart && offset < visibleEnd) {
        if (char === " ") this.drawSpaceMarker(baseX + offset, baseX + nextOffset, y, lineH, color);
        else if (char === "\t") this.drawTabMarker(baseX + offset, baseX + nextOffset, y, lineH, color);
      }
      offset = nextOffset;
      if (offset > visibleEnd) break;
    }
    if (lineIndex < lineCount - 1) {
      const markerWidth = this.renderer.measureText("\\n", "code");
      if (offset + markerWidth > visibleStart && offset < visibleEnd) this.renderer.text("\\n", baseX + offset, y + this.whitespaceNewlineYOffset(), color, "code");
    }
  }

  private drawSpaceMarker(startX: number, endX: number, y: number, lineH: number, color: Color): void {
    const dotSize = Math.max(1.25, Math.min(2.25, this.renderer.lineHeight("code") * 0.11));
    const cx = (startX + endX) * 0.5;
    const cy = y + lineH * 0.66;
    this.renderer.rect({ x: cx - dotSize * 0.5, y: cy - dotSize * 0.5, w: dotSize, h: dotSize }, color);
  }

  private whitespaceNewlineYOffset(): number {
    return this.ui(4);
  }

  private drawTabMarker(startX: number, endX: number, y: number, lineH: number, color: Color): void {
    const pad = Math.max(2, Math.min(6, this.renderer.monoAdvance("code") * 0.22));
    const x0 = startX + pad;
    const x1 = endX - pad;
    if (x1 - x0 < 4) return;
    const lineWidth = Math.max(1, Math.min(1.5, this.renderer.lineHeight("code") * 0.08));
    const midY = y + lineH * 0.56;
    const head = Math.min(6, Math.max(3, (x1 - x0) * 0.32));
    this.renderer.line({ x: x0, y: midY }, { x: x1, y: midY }, lineWidth, color);
    this.renderer.line({ x: x1, y: midY }, { x: x1 - head, y: midY - head * 0.55 }, lineWidth, color);
    this.renderer.line({ x: x1, y: midY }, { x: x1 - head, y: midY + head * 0.55 }, lineWidth, color);
  }

  private whitespaceMarkerColor(): Color {
    return [theme.textDim[0], theme.textDim[1], theme.textDim[2], this.settings.theme === "light" ? 0.56 : 0.46];
  }

  private tokensForLine(doc: TextDocument, lineIndex: number): Token[] {
    const text = doc.lines[lineIndex] ?? "";
    const key = `${doc.id}:${lineIndex}`;
    const cached = this.highlightCache.get(key);
    if (cached && cached.syntaxId === doc.syntaxId && cached.text === text) return cached.tokens;
    const tokens = this.highlighter.tokenizeLine(text, doc.syntaxId);
    this.highlightCache.set(key, { syntaxId: doc.syntaxId, text, tokens });
    if (this.highlightCache.size > 5000) {
      const first = this.highlightCache.keys().next().value;
      if (first) this.highlightCache.delete(first);
    }
    return tokens;
  }

  private normalizedWheelDelta(value: number, mode: number, rect: Rect): number {
    if (mode === WheelEvent.DOM_DELTA_LINE) return value * this.renderer.lineHeight("code");
    if (mode === WheelEvent.DOM_DELTA_PAGE) return value * rect.h;
    return value;
  }

  private tabRectForGroup(group: EditorGroup): Rect {
    return { x: group.frameRect.x, y: group.frameRect.y, w: group.frameRect.w, h: this.ui(32) };
  }

  private tabGroupAtPoint(point: Point): EditorGroup | undefined {
    return this.groups.find((group) => rectContains(this.tabRectForGroup(group), point.x, point.y));
  }

  private setTabGroupScroll(group: EditorGroup, value: number, layout = this.tabLayoutForGroup(group, this.tabRectForGroup(group))): boolean {
    const current = this.tabScrollStates.get(group.id) ?? 0;
    const next = clamp(value, 0, layout.maxScroll);
    if (Math.abs(next - current) < 0.5) return false;
    this.tabScrollStates.set(group.id, next);
    return true;
  }

  private scrollTabGroupFromWheel(group: EditorGroup, event: WheelEvent, point: Point): boolean {
    const layout = this.tabLayoutForGroup(group, this.tabRectForGroup(group));
    if (layout.maxScroll <= 0 || !rectContains(layout.stripRect, point.x, point.y)) return false;
    const primaryDelta = Math.abs(event.deltaX) > 0 ? event.deltaX : event.deltaY;
    const delta = this.normalizedWheelDelta(primaryDelta, event.deltaMode, layout.stripRect);
    this.setTabGroupScroll(group, layout.scroll + delta, layout);
    this.scheduleDraw();
    return true;
  }

  private sidebarScrollRegionForPoint(point: Point): { panel: SidebarScrollPanel; viewport: Rect } | null {
    if (this.sidebarWidth <= 0) return null;
    const vp = this.viewport.get();
    const sidebarRect = { x: this.ui(48), y: 0, w: this.sidebarWidth, h: Math.max(0, vp.cssHeight - this.ui(24)) };
    if (!rectContains(sidebarRect, point.x, point.y)) return null;
    const body = this.sidebarPanelBodyRect(sidebarRect);
    if (this.sidebarMode === "files") return rectContains(body, point.x, point.y) ? { panel: "files", viewport: body } : null;
    if (this.sidebarMode === "settings") return rectContains(body, point.x, point.y) ? { panel: "settings", viewport: body } : null;
    if (this.sidebarMode !== "search") return null;
    const viewport = this.searchResultsViewport(body);
    return rectContains(viewport, point.x, point.y) ? { panel: "search", viewport } : null;
  }

  private chatScrollRegionForPoint(point: Point): { panel: ChatScrollbarPanel; viewport: Rect } | null {
    if (this.sidebarWidth <= 0 || this.sidebarMode !== "chat") return null;
    const hit = this.hitAt(point.x, point.y);
    if (hit?.type === "chatTranscript") return { panel: "chatTranscript", viewport: hit.rect };
    if (hit?.type === "chatInput") return { panel: "chatInput", viewport: hit.rect };
    if (hit?.type === "chatScrollbar") return { panel: hit.panel, viewport: hit.viewportRect };
    return null;
  }

  private sidebarPanelBodyRect(rect: Rect): Rect {
    const headerH = this.ui(PANEL_HEADER_H);
    return { x: rect.x, y: rect.y + headerH, w: rect.w, h: Math.max(0, rect.h - headerH) };
  }

  private searchResultsViewport(body: Rect): Rect {
    const controlsH = this.ui(8) + this.ui(28) + this.ui(14) + (this.searchReplaceExpanded ? this.ui(42) : 0);
    return { x: body.x, y: body.y + controlsH, w: body.w, h: Math.max(0, body.h - controlsH) };
  }

  private chatPanelScrollY(panel: ChatScrollbarPanel): number {
    return panel === "chatInput" ? this.chatInputScrollY : this.chatScrollY;
  }

  private setChatPanelScrollY(panel: ChatScrollbarPanel, value: number, viewport: Rect): void {
    const next = clamp(value, 0, this.maxChatScrollY(panel, viewport));
    if (panel === "chatInput") this.chatInputScrollY = next;
    else this.chatScrollY = next;
  }

  private maxChatScrollY(panel: ChatScrollbarPanel, viewport: Rect): number {
    const contentHeight = panel === "chatInput" ? this.chatInputMetrics(viewport).contentHeight : this.chatTranscriptContentHeight(Math.max(1, viewport.w - this.ui(12)));
    return Math.max(0, contentHeight - viewport.h);
  }

  private fileTreeVisibleRowCount(entries = this.fileTreeEntries()): number {
    let count = 0;
    for (const entry of entries) {
      count++;
      if (entry.type === "dir" && this.expandedFolders.has(entry.path)) count += this.fileTreeVisibleRowCount(entry.children);
    }
    return count;
  }

  private fileTreeContentHeight(): number {
    const rowH = this.ui(22);
    const rowGap = this.ui(2);
    return this.ui(16) + this.fileTreeVisibleRowCount() * (rowH + rowGap);
  }

  private searchResultsContentHeight(): number {
    return this.searchResults.length * this.ui(42);
  }

  private maxSidebarScrollY(panel: SidebarScrollPanel, viewport: Rect): number {
    const contentHeight = panel === "files" ? this.fileTreeContentHeight() : panel === "settings" ? this.settingsContentHeight() : this.searchResultsContentHeight();
    return Math.max(0, contentHeight - viewport.h);
  }

  private sidebarScrollY(panel: SidebarScrollPanel): number {
    return panel === "files" ? this.filesScrollY : panel === "settings" ? this.settingsScrollY : this.searchScrollY;
  }

  private setSidebarScrollY(panel: SidebarScrollPanel, value: number, viewport: Rect): void {
    const next = clamp(value, 0, this.maxSidebarScrollY(panel, viewport));
    if (panel === "files") this.filesScrollY = next;
    else if (panel === "settings") this.settingsScrollY = next;
    else this.searchScrollY = next;
  }

  private scrollSidebarPanel(panel: SidebarScrollPanel, deltaY: number, viewport: Rect): void {
    this.setSidebarScrollY(panel, this.sidebarScrollY(panel) + deltaY, viewport);
    this.scheduleDraw();
  }

  private settingsViewportHeight(rect: Rect): number {
    return Math.max(1, rect.h);
  }

  private settingsContentHeight(): number {
    let y = this.ui(8);
    y += this.ui(30);
    if (this.settingsExpanded.has("visual")) y += this.ui(34) * 6;
    y += this.ui(6);
    y += this.ui(30);
    if (this.settingsExpanded.has("interface")) y += this.ui(34) * 4;
    y += this.ui(6);
    y += this.ui(30);
    if (this.settingsExpanded.has("ai")) y += this.ui(54) * 2 + this.ui(46) + this.ui(34) * 13;
    y += this.ui(6);
    y += this.ui(30);
    if (this.settingsExpanded.has("danger")) y += this.ui(34) * 2;
    return y + this.ui(32);
  }

  private maxSettingsScrollY(rect: Rect): number {
    return Math.max(0, this.settingsContentHeight() - this.settingsViewportHeight(rect));
  }

  private clampScrollForDoc(doc: TextDocument, rect: Rect): EditorScrollState {
    const scroll = this.scrollForDoc(doc.id);
    scroll.y = clamp(scroll.y, 0, this.maxScrollY(doc, rect));
    scroll.x = clamp(scroll.x, 0, this.maxScrollX(doc, rect));
    return scroll;
  }

  private ensureCaretVisible(doc: TextDocument, rect: Rect): void {
    if (rect.w <= 0 || rect.h <= 0) return;
    const scroll = this.scrollForDoc(doc.id);
    const contentRect = this.editorContentRect(doc, rect);
    const lineH = this.renderer.lineHeight("code");
    const caretTop = doc.selection.head.line * lineH;
    const caretBottom = caretTop + lineH;
    const verticalMargin = Math.min(lineH * 2, Math.max(0, (contentRect.h - lineH) / 2));
    if (caretTop < scroll.y + verticalMargin) {
      scroll.y = caretTop - verticalMargin;
    } else if (caretBottom > scroll.y + contentRect.h - verticalMargin) {
      scroll.y = caretBottom - contentRect.h + verticalMargin;
    }

    const line = doc.lines[doc.selection.head.line] ?? "";
    const caretX = this.measureCodePrefix(line, doc.selection.head.col);
    const visibleTextWidth = this.visibleTextWidth(doc, contentRect);
    const horizontalMargin = Math.min(48, Math.max(0, (visibleTextWidth - 2) / 3));
    if (caretX < scroll.x + horizontalMargin) {
      scroll.x = caretX - horizontalMargin;
    } else if (caretX + 2 > scroll.x + visibleTextWidth - horizontalMargin) {
      scroll.x = caretX + 2 - visibleTextWidth + horizontalMargin;
    }

    scroll.y = clamp(scroll.y, 0, this.maxScrollY(doc, rect));
    scroll.x = clamp(scroll.x, 0, this.maxScrollX(doc, rect));
  }

  private startScrollbarDrag(hit: Extract<HitItem, { type: "editorScrollbar" }>, point: Point): void {
    const group = this.groupById(hit.groupId);
    const doc = this.docs.get(hit.docId);
    if (!doc) return;
    this.hoveredScrollbar = { axis: hit.axis, groupId: hit.groupId, docId: hit.docId, overThumb: rectContains(hit.thumbRect, point.x, point.y) };
    if (!rectContains(hit.thumbRect, point.x, point.y)) this.scrollDocumentFromScrollbarPoint(doc, group.editorRect, hit.axis, hit.trackRect, hit.thumbRect, point);
    const scroll = this.scrollForDoc(hit.docId);
    this.scrollbarDrag = {
      axis: hit.axis,
      groupId: hit.groupId,
      docId: hit.docId,
      startPoint: hit.axis === "vertical" ? point.y : point.x,
      startScroll: hit.axis === "vertical" ? scroll.y : scroll.x,
      trackRect: { ...hit.trackRect },
      thumbRect: { ...hit.thumbRect }
    };
    this.canvas.style.cursor = "";
    this.scheduleDraw();
  }

  private dragScrollbar(point: Point): void {
    const drag = this.scrollbarDrag;
    if (!drag) return;
    const group = this.groupById(drag.groupId);
    const doc = this.docs.get(drag.docId);
    if (!doc) return;
    const maxScroll = drag.axis === "vertical" ? this.maxScrollY(doc, group.editorRect) : this.maxScrollX(doc, group.editorRect);
    const thumbTravel = Math.max(1, drag.axis === "vertical" ? drag.trackRect.h - drag.thumbRect.h : drag.trackRect.w - drag.thumbRect.w);
    const currentPoint = drag.axis === "vertical" ? point.y : point.x;
    const delta = ((currentPoint - drag.startPoint) / thumbTravel) * maxScroll;
    const scroll = this.scrollForDoc(doc.id);
    if (drag.axis === "vertical") scroll.y = clamp(drag.startScroll + delta, 0, maxScroll);
    else scroll.x = clamp(drag.startScroll + delta, 0, maxScroll);
    this.persistEditorSession();
    this.scheduleDraw();
  }

  private scrollDocumentFromScrollbarPoint(doc: TextDocument, editorRect: Rect, axis: ScrollbarAxis, trackRect: Rect, thumbRect: Rect, point: Point): void {
    const maxScroll = axis === "vertical" ? this.maxScrollY(doc, editorRect) : this.maxScrollX(doc, editorRect);
    if (maxScroll <= 0) return;
    const scroll = this.scrollForDoc(doc.id);
    if (axis === "vertical") {
      const thumbTravel = Math.max(1, trackRect.h - thumbRect.h);
      const thumbTop = clamp(point.y - thumbRect.h / 2, trackRect.y, trackRect.y + thumbTravel);
      scroll.y = ((thumbTop - trackRect.y) / thumbTravel) * maxScroll;
      return;
    }
    const thumbTravel = Math.max(1, trackRect.w - thumbRect.w);
    const thumbLeft = clamp(point.x - thumbRect.w / 2, trackRect.x, trackRect.x + thumbTravel);
    scroll.x = ((thumbLeft - trackRect.x) / thumbTravel) * maxScroll;
  }

  private startSettingsScrollbarDrag(hit: Extract<HitItem, { type: "settingsScrollbar" }>, point: Point): void {
    this.hoveredSettingsScrollbar = { overThumb: rectContains(hit.thumbRect, point.x, point.y) };
    if (!rectContains(hit.thumbRect, point.x, point.y)) this.scrollSettingsFromScrollbarPoint(hit.viewportRect, hit.trackRect, hit.thumbRect, point);
    this.settingsScrollbarDrag = {
      startPoint: point.y,
      startScroll: this.settingsScrollY,
      viewportRect: { ...hit.viewportRect },
      trackRect: { ...hit.trackRect },
      thumbRect: { ...hit.thumbRect }
    };
    this.canvas.style.cursor = "";
    this.scheduleDraw();
  }

  private dragSettingsScrollbar(point: Point): void {
    const drag = this.settingsScrollbarDrag;
    if (!drag) return;
    const maxScroll = this.maxSettingsScrollY(drag.viewportRect);
    const thumbTravel = Math.max(1, drag.trackRect.h - drag.thumbRect.h);
    const delta = ((point.y - drag.startPoint) / thumbTravel) * maxScroll;
    this.settingsScrollY = clamp(drag.startScroll + delta, 0, maxScroll);
    this.scheduleDraw();
  }

  private scrollSettingsFromScrollbarPoint(editorRect: Rect, trackRect: Rect, thumbRect: Rect, point: Point): void {
    const maxScroll = this.maxSettingsScrollY(editorRect);
    if (maxScroll <= 0) return;
    const thumbTravel = Math.max(1, trackRect.h - thumbRect.h);
    const thumbTop = clamp(point.y - thumbRect.h / 2, trackRect.y, trackRect.y + thumbTravel);
    this.settingsScrollY = ((thumbTop - trackRect.y) / thumbTravel) * maxScroll;
  }

  private startSidebarScrollbarDrag(hit: Extract<HitItem, { type: "sidebarScrollbar" }>, point: Point): void {
    this.hoveredSidebarScrollbar = { panel: hit.panel, overThumb: rectContains(hit.thumbRect, point.x, point.y) };
    if (!rectContains(hit.thumbRect, point.x, point.y)) this.scrollSidebarFromScrollbarPoint(hit.panel, hit.viewportRect, hit.contentHeight, hit.trackRect, hit.thumbRect, point);
    this.sidebarScrollbarDrag = {
      panel: hit.panel,
      startPoint: point.y,
      startScroll: this.sidebarScrollY(hit.panel),
      trackRect: { ...hit.trackRect },
      thumbRect: { ...hit.thumbRect },
      viewportRect: { ...hit.viewportRect },
      contentHeight: hit.contentHeight
    };
    this.canvas.style.cursor = "";
    this.scheduleDraw();
  }

  private dragSidebarScrollbar(point: Point): void {
    const drag = this.sidebarScrollbarDrag;
    if (!drag) return;
    const maxScroll = Math.max(0, drag.contentHeight - drag.viewportRect.h);
    const thumbTravel = Math.max(1, drag.trackRect.h - drag.thumbRect.h);
    const delta = ((point.y - drag.startPoint) / thumbTravel) * maxScroll;
    this.setSidebarScrollY(drag.panel, drag.startScroll + delta, drag.viewportRect);
    this.scheduleDraw();
  }

  private scrollSidebarFromScrollbarPoint(panel: SidebarScrollPanel, viewport: Rect, contentHeight: number, trackRect: Rect, thumbRect: Rect, point: Point): void {
    const maxScroll = Math.max(0, contentHeight - viewport.h);
    if (maxScroll <= 0) return;
    const thumbTravel = Math.max(1, trackRect.h - thumbRect.h);
    const thumbTop = clamp(point.y - thumbRect.h / 2, trackRect.y, trackRect.y + thumbTravel);
    this.setSidebarScrollY(panel, ((thumbTop - trackRect.y) / thumbTravel) * maxScroll, viewport);
  }

  private startChatScrollbarDrag(hit: Extract<HitItem, { type: "chatScrollbar" }>, point: Point): void {
    this.hoveredChatScrollbar = { panel: hit.panel, overThumb: rectContains(hit.thumbRect, point.x, point.y) };
    if (!rectContains(hit.thumbRect, point.x, point.y)) this.scrollChatFromScrollbarPoint(hit.panel, hit.viewportRect, hit.contentHeight, hit.trackRect, hit.thumbRect, point);
    this.chatScrollbarDrag = {
      panel: hit.panel,
      startPoint: point.y,
      startScroll: this.chatPanelScrollY(hit.panel),
      trackRect: { ...hit.trackRect },
      thumbRect: { ...hit.thumbRect },
      viewportRect: { ...hit.viewportRect },
      contentHeight: hit.contentHeight
    };
    this.canvas.style.cursor = "";
    this.scheduleDraw();
  }

  private dragChatScrollbar(point: Point): void {
    const drag = this.chatScrollbarDrag;
    if (!drag) return;
    const maxScroll = Math.max(0, drag.contentHeight - drag.viewportRect.h);
    const thumbTravel = Math.max(1, drag.trackRect.h - drag.thumbRect.h);
    const delta = ((point.y - drag.startPoint) / thumbTravel) * maxScroll;
    this.setChatPanelScrollY(drag.panel, drag.startScroll + delta, drag.viewportRect);
    this.scheduleDraw();
  }

  private scrollChatFromScrollbarPoint(panel: ChatScrollbarPanel, viewport: Rect, contentHeight: number, trackRect: Rect, thumbRect: Rect, point: Point): void {
    const maxScroll = Math.max(0, contentHeight - viewport.h);
    if (maxScroll <= 0) return;
    const thumbTravel = Math.max(1, trackRect.h - thumbRect.h);
    const thumbTop = clamp(point.y - thumbRect.h / 2, trackRect.y, trackRect.y + thumbTravel);
    this.setChatPanelScrollY(panel, ((thumbTop - trackRect.y) / thumbTravel) * maxScroll, viewport);
  }

  private startDockResize(hit: Extract<HitItem, { type: "dockResize" }>, point: Point): void {
    const split = findDockSplitNode(this.dockRoot, hit.splitId);
    if (!split) return;
    const weights = normalizeSplitWeights(split);
    this.dockResize = {
      splitId: hit.splitId,
      index: hit.index,
      direction: hit.direction,
      startPoint: hit.direction === "row" ? point.x : point.y,
      startWeights: [...weights],
      splitRect: { ...hit.splitRect }
    };
    this.canvas.style.cursor = hit.direction === "row" ? "col-resize" : "row-resize";
    this.statusText = "Resizing dock";
  }

  private resizeDockSplit(point: Point): void {
    const resize = this.dockResize;
    if (!resize) return;
    const split = findDockSplitNode(this.dockRoot, resize.splitId);
    if (!split || split.direction !== resize.direction || resize.index < 0 || resize.index >= split.children.length - 1) return;
    const axisSize = Math.max(1, (resize.direction === "row" ? resize.splitRect.w : resize.splitRect.h) - DOCK_SPLITTER_GAP * (split.children.length - 1));
    const weights = normalizeWeightsForCount(resize.startWeights, split.children.length);
    const totalWeight = weights.reduce((sum, weight) => sum + weight, 0) || 1;
    const pxPerWeight = axisSize / totalWeight;
    const deltaPx = (resize.direction === "row" ? point.x : point.y) - resize.startPoint;
    const deltaWeight = deltaPx / pxPerWeight;
    const first = weights[resize.index]!;
    const second = weights[resize.index + 1]!;
    const pairWeight = first + second;
    const pairPx = pairWeight * pxPerWeight;
    const minPx = Math.max(0.5, Math.min(DOCK_MIN_PANEL_SIZE, pairPx / 2 - 1));
    const minWeight = Math.max(0.001, minPx / pxPerWeight);
    const nextFirst = clamp(first + deltaWeight, minWeight, pairWeight - minWeight);
    weights[resize.index] = nextFirst;
    weights[resize.index + 1] = pairWeight - nextFirst;
    split.weights = weights;
    this.statusText = `${resize.direction === "row" ? "Width" : "Height"} ${Math.round(nextFirst * pxPerWeight)}px`;
    this.scheduleDraw();
  }

  private updateDockPreview(point: { x: number; y: number }): void {
    if (!this.tabDrag) return;
    if (this.updateTabInsertionPreview(point)) {
      this.dockPreview = null;
      this.canvas.style.cursor = "";
      this.scheduleDraw();
      return;
    }
    this.tabInsertionPreview = null;
    this.lastTabDragPoint = null;
    this.stopTabDragAutoscroll();
    const preview = this.resolveDockPreview(point);
    this.dockPreview = preview;
    this.scheduleDraw();
    this.canvas.style.cursor = preview ? "" : "not-allowed";
  }

  private updateTabInsertionPreview(point: Point): boolean {
    const group = this.tabGroupAtPoint(point);
    if (!group) return false;
    this.lastTabDragPoint = { ...point };
    let layout = this.tabLayoutForGroup(group, this.tabRectForGroup(group));
    if (this.scrollTabGroupDuringDrag(group, layout, point)) {
      this.scheduleTabDragAutoscroll();
      layout = this.tabLayoutForGroup(group, this.tabRectForGroup(group));
    } else {
      this.stopTabDragAutoscroll();
    }
    const index = this.tabInsertionIndexForLayout(layout, point.x);
    this.tabInsertionPreview = { groupId: group.id, index, rect: this.tabInsertionLineRect(layout, index) };
    return true;
  }

  private scheduleTabDragAutoscroll(): void {
    if (this.tabDragAutoscrollTimer) return;
    this.tabDragAutoscrollTimer = window.setTimeout(() => {
      this.tabDragAutoscrollTimer = 0;
      if (!this.tabDrag || !this.lastTabDragPoint) return;
      if (this.updateTabInsertionPreview(this.lastTabDragPoint)) this.scheduleDraw();
    }, 45);
  }

  private stopTabDragAutoscroll(): void {
    if (!this.tabDragAutoscrollTimer) return;
    window.clearTimeout(this.tabDragAutoscrollTimer);
    this.tabDragAutoscrollTimer = 0;
  }

  private scrollTabGroupDuringDrag(group: EditorGroup, layout: TabLayout, point: Point): boolean {
    if (layout.maxScroll <= 0 || !rectContains(layout.stripRect, point.x, point.y)) return false;
    const edge = Math.min(this.ui(TAB_AUTOSCROLL_EDGE_W), layout.stripRect.w / 3);
    const leftAmount = layout.stripRect.x + edge - point.x;
    const rightAmount = point.x - (layout.stripRect.x + layout.stripRect.w - edge);
    const step = this.ui(26);
    if (leftAmount > 0) return this.setTabGroupScroll(group, layout.scroll - step * clamp(leftAmount / edge, 0.25, 1), layout);
    if (rightAmount > 0) return this.setTabGroupScroll(group, layout.scroll + step * clamp(rightAmount / edge, 0.25, 1), layout);
    return false;
  }

  private tabInsertionIndexForLayout(layout: TabLayout, x: number): number {
    const contentX = clamp(x - layout.stripRect.x + layout.scroll, 0, Math.max(0, layout.totalWidth));
    for (let i = 0; i < layout.items.length; i++) {
      const item = layout.items[i]!;
      if (contentX < item.start + item.width / 2) return i;
    }
    return layout.items.length;
  }

  private tabInsertionLineRect(layout: TabLayout, index: number): Rect {
    const gap = this.ui(TAB_GAP);
    const previous = layout.items[index - 1];
    const next = layout.items[index];
    const contentX = next ? next.start : previous ? previous.end + gap : 0;
    const x = clamp(layout.stripRect.x + contentX - layout.scroll, layout.stripRect.x + 1, layout.stripRect.x + layout.stripRect.w - 1);
    return {
      x: x - this.ui(1),
      y: layout.stripRect.y + this.ui(3),
      w: Math.max(2, this.ui(2)),
      h: Math.max(4, layout.stripRect.h - this.ui(6))
    };
  }

  private resolveDockPreview(point: { x: number; y: number }): DockPreview | null {
    const targets = this.allDockTargets();
    const centerTarget = targets.find((item) => item.zone === "center" && pointInPolygon(point, item.polygon));
    const target = centerTarget ?? targets.find((item) => item.zone !== "center" && pointInPolygon(point, item.polygon));
    return target ? { groupId: target.groupId, zone: target.zone, rect: target.previewRect, polygon: target.polygon } : null;
  }

  private applyTabDrop(): void {
    const drag = this.tabDrag;
    const preview = this.dockPreview;
    if (!drag) return;
    if (this.tabInsertionPreview) {
      this.dropDraggedTabIntoGroup(drag.docId, this.tabInsertionPreview.groupId, this.tabInsertionPreview.index);
      return;
    }
    if (!preview) {
      this.restoreDraggedTab();
      return;
    }
    if (preview.zone === "center") {
      const group = this.groups.find((item) => item.id === preview.groupId);
      if (!group) {
        this.restoreDraggedTab();
        return;
      }
      if (!group.tabs.includes(drag.docId)) group.tabs.push(drag.docId);
      group.activeDocId = drag.docId;
      this.activeGroupId = group.id;
      this.activeDocId = drag.docId;
      this.selectActiveDocumentInFileTree();
      this.syncOpenTabs();
      return;
    }
    const target = this.groups.find((group) => group.id === preview.groupId);
    if (!target) {
      this.restoreDraggedTab();
      return;
    }
    const direction = preview.zone === "left" || preview.zone === "right" ? "row" : "column";
    const group = makeGroup(`group-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`);
    group.tabs.push(drag.docId);
    group.activeDocId = drag.docId;
    const draggedNode: DockNode = { type: "leaf", group };
    const targetNode: DockNode = { type: "leaf", group: target };
    const replacement = makeDockSplit(direction, preview.zone === "left" || preview.zone === "top" ? [draggedNode, targetNode] : [targetNode, draggedNode]);
    this.dockRoot = replaceLeafNode(this.dockRoot, target.id, replacement) ?? this.dockRoot;
    this.pruneDockTree();
    this.activeGroupId = group.id;
    this.activeDocId = drag.docId;
    this.selectActiveDocumentInFileTree();
    this.syncOpenTabs();
    this.statusText = `Docked ${preview.zone}`;
  }

  private dropDraggedTabIntoGroup(docId: string, groupId: string, index: number): void {
    const group = this.groups.find((item) => item.id === groupId);
    if (!group) {
      this.restoreDraggedTab();
      return;
    }
    const existing = group.tabs.indexOf(docId);
    if (existing >= 0) group.tabs.splice(existing, 1);
    const target = clamp(index, 0, group.tabs.length);
    group.tabs.splice(target, 0, docId);
    group.activeDocId = docId;
    this.activeGroupId = group.id;
    this.activeDocId = docId;
    this.selectActiveDocumentInFileTree();
    this.syncOpenTabs();
    this.revealTabInGroup(group, docId);
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.statusText = "Moved tab";
  }

  private restoreDraggedTab(): void {
    const drag = this.tabDrag;
    if (!drag) return;
    this.dockRoot = cloneDockNode(drag.restoreRoot);
    this.groups = collectDockGroups(this.dockRoot);
    this.activeGroupId = drag.restoreActiveGroupId;
    this.activeDocId = drag.restoreActiveDocId;
    this.syncOpenTabs();
    this.statusText = `Move canceled`;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
  }

  private removeDocFromGroups(docId: string, prune = true): void {
    for (const group of this.groups) {
      const index = group.tabs.indexOf(docId);
      if (index < 0) continue;
      group.tabs.splice(index, 1);
      if (group.activeDocId === docId) group.activeDocId = group.tabs[index] ?? group.tabs[index - 1] ?? null;
    }
    if (prune) this.pruneDockTree();
  }

  private activeGroup(): EditorGroup {
    return this.groupById(this.activeGroupId);
  }

  private groupById(id: string): EditorGroup {
    return this.groups.find((group) => group.id === id) ?? this.groups[0]!;
  }

  private groupContaining(docId: string): EditorGroup | undefined {
    return this.groups.find((group) => group.tabs.includes(docId));
  }

  private activateTabInGroup(group: EditorGroup, docId: string, focus = true): void {
    group.activeDocId = docId;
    this.activeGroupId = group.id;
    this.activeDocId = docId;
    this.revealTabInGroup(group, docId);
    this.selectActiveDocumentInFileTree();
    if (focus && this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.persistEditorSession();
  }

  private syncOpenTabs(persist = true): void {
    this.groups = collectDockGroups(this.dockRoot);
    this.openTabs = this.groups.flatMap((group) => group.tabs);
    if (persist) this.persistEditorSession();
  }

  private persistEditorSession(): void {
    try {
      this.clearLegacyPersistedEditorSessions();
      if (!this.settings.rememberOpenFiles) {
        localStorage.removeItem(SESSION_STORAGE_KEY);
        return;
      }
      const session = this.makePersistedSession();
      if (!session) {
        localStorage.removeItem(SESSION_STORAGE_KEY);
        return;
      }
      localStorage.setItem(SESSION_STORAGE_KEY, JSON.stringify(session));
    } catch {
      // Session restore is a convenience; editing should not fail if storage is unavailable.
    }
  }

  private clearPersistedEditorSession(): void {
    try {
      localStorage.removeItem(SESSION_STORAGE_KEY);
      this.clearLegacyPersistedEditorSessions();
    } catch {
      // Ignore storage failures.
    }
  }

  private clearLegacyPersistedEditorSessions(): void {
    for (const key of Object.keys(localStorage)) {
      if (key.startsWith(`${SESSION_STORAGE_KEY}:`)) localStorage.removeItem(key);
    }
  }

  private makePersistedSession(): PersistedSession | null {
    const dockRoot = persistDockNode(this.dockRoot, (docId) => {
      const doc = this.docs.get(docId);
      return doc?.path && !this.isAiSpecialPath(doc.path) ? normalizePath(doc.path) : null;
    });
    if (!dockRoot || persistedDockPathCount(dockRoot) === 0) return null;
    const scrollStates: Record<string, EditorScrollState> = {};
    for (const doc of this.docs.all()) {
      if (!doc.path || this.isAiSpecialPath(doc.path) || !this.groupContaining(doc.id)) continue;
      const scroll = this.scrollStates.get(doc.id);
      if (scroll) scrollStates[normalizePath(doc.path)] = { x: scroll.x, y: scroll.y };
    }
    const activeDoc = this.activeDoc();
    return {
      version: 1,
      activePath: activeDoc?.path ? normalizePath(activeDoc.path) : null,
      activeGroupId: this.activeGroupId,
      sidebarMode: this.sidebarMode,
      sidebarWidth: this.sidebarWidth,
      lastSidebarWidth: this.lastSidebarWidth,
      dockRoot,
      scrollStates
    };
  }

  private async restoreEditorSession(): Promise<void> {
    if (!this.settings.rememberOpenFiles) {
      this.clearPersistedEditorSession();
      return;
    }
    let session: PersistedSession | null = null;
    try {
      this.clearLegacyPersistedEditorSessions();
      const raw = localStorage.getItem(SESSION_STORAGE_KEY);
      session = raw ? normalizePersistedSession(JSON.parse(raw)) : null;
    } catch {
      session = null;
    }
    if (!session) return;
    const paths = [...new Set(persistedDockPaths(session.dockRoot))];
    const pathToDocId = new Map<string, string>();
    for (const path of paths) {
      const node = await this.vfs.stat(path);
      if (!node || node.kind !== "file") continue;
      const doc = await this.docs.open(path);
      pathToDocId.set(path, doc.id);
    }
    const restoredRoot = restorePersistedDockNode(session.dockRoot, pathToDocId);
    if (!restoredRoot || restoredDockTabCount(restoredRoot) === 0) {
      this.clearPersistedEditorSession();
      return;
    }
    this.dockRoot = restoredRoot;
    this.groups = collectDockGroups(this.dockRoot);
    this.activeGroupId = this.groups.find((group) => group.id === session.activeGroupId)?.id ?? this.groups[0]!.id;
    const activeDocId = session.activePath ? pathToDocId.get(session.activePath) ?? null : null;
    if (activeDocId) {
      const group = this.groupContaining(activeDocId);
      if (group) {
        group.activeDocId = activeDocId;
        this.activeGroupId = group.id;
        this.activeDocId = activeDocId;
      }
    }
    if (!this.activeDocId) {
      const group = this.groups.find((item) => item.activeDocId) ?? this.groups[0]!;
      this.activeGroupId = group.id;
      this.activeDocId = group.activeDocId;
    }
    this.sidebarMode = session.sidebarMode;
    this.sidebarWidth = Math.max(0, session.sidebarWidth);
    this.lastSidebarWidth = Math.max(0, session.lastSidebarWidth || this.lastSidebarWidth);
    this.scrollStates.clear();
    this.tabScrollStates.clear();
    this.pendingTabRevealIds.clear();
    for (const [path, scroll] of Object.entries(session.scrollStates ?? {})) {
      const docId = pathToDocId.get(path);
      if (docId) this.scrollStates.set(docId, { x: Math.max(0, scroll.x), y: Math.max(0, scroll.y) });
    }
    this.syncOpenTabs(false);
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.statusText = "Restored workspace";
    this.persistEditorSession();
  }

  private blockReadOnlyEdit(doc: TextDocument | undefined): boolean {
    if (!doc?.readOnly) return false;
    this.statusText = "File type not supported";
    this.scheduleDraw();
    return true;
  }

  private pruneDockTree(): void {
    this.dockRoot = pruneDockNode(this.dockRoot) ?? { type: "leaf", group: makeGroup("group-main") };
    this.groups = collectDockGroups(this.dockRoot);
    if (!this.groups.find((group) => group.id === this.activeGroupId)) this.activeGroupId = this.groups[0]!.id;
    if (this.activeDocId && !this.groupContaining(this.activeDocId)) {
      const group = this.activeGroup();
      this.activeDocId = group.activeDocId;
    }
  }

  private editorTarget(): TextInputTarget {
    return {
      kind: "editor",
      getSelectedText: () => this.activeDoc()?.selectedText() ?? "",
      replaceSelection: (text) => {
        this.closeContextMenuForTextInput();
        const doc = this.activeDoc();
        if (this.blockReadOnlyEdit(doc)) return;
        doc?.replaceSelection(text);
        this.afterDocumentMutated(doc);
        this.revealEditorCaret();
      },
      deleteSelectionOrBackward: (unit = "char") => {
        this.closeContextMenuForTextInput();
        const doc = this.activeDoc();
        if (this.blockReadOnlyEdit(doc)) return;
        doc?.deleteBackward(unit);
        this.afterDocumentMutated(doc);
        this.revealEditorCaret();
      },
      deleteForward: (unit = "char") => {
        this.closeContextMenuForTextInput();
        const doc = this.activeDoc();
        if (this.blockReadOnlyEdit(doc)) return;
        doc?.deleteForward(unit);
        this.afterDocumentMutated(doc);
        this.revealEditorCaret();
      },
      moveCursor: (command, extend) => {
        this.activeDoc()?.move(command, extend);
        this.revealEditorCaret();
      },
      runShortcut: (command) => this.runEditorShortcut(command),
      onCompositionPreview: () => this.resetCaretBlink(),
      onCompositionCommit: (text) => {
        this.closeContextMenuForTextInput();
        const doc = this.activeDoc();
        if (text && !this.blockReadOnlyEdit(doc)) {
          doc?.replaceSelection(text, "composition");
          this.afterDocumentMutated(doc);
        }
        this.revealEditorCaret();
      }
    };
  }

  private miniTarget(kind: "search" | "chat"): TextInputTarget {
    if (kind === "chat") return this.chatInputTarget();
    const buffer = this.searchBuffer;
    return {
      kind,
      getSelectedText: () => buffer.selectedText(),
      replaceSelection: (text) => {
        this.closeContextMenuForTextInput();
        buffer.replaceSelection(text.replaceAll("\n", " "));
        if (kind === "search") void this.runSearch();
        this.resetCaretBlink();
      },
      deleteSelectionOrBackward: () => {
        this.closeContextMenuForTextInput();
        buffer.deleteBackward();
        if (kind === "search") void this.runSearch();
        this.resetCaretBlink();
      },
      deleteForward: () => {
        this.closeContextMenuForTextInput();
        buffer.deleteForward();
        if (kind === "search") void this.runSearch();
        this.resetCaretBlink();
      },
      moveCursor: (command, extend) => {
        buffer.move(command, extend);
        this.resetCaretBlink();
      },
      runShortcut: (command) => {
        if (command === "Enter" && kind === "search") {
          void this.runSearch();
          this.resetCaretBlink();
          return true;
        }
        if (command === "Mod+A") {
          buffer.selectAll();
          this.resetCaretBlink();
          return true;
        }
        return this.runGlobalShortcut(command);
      },
      onCompositionPreview: () => this.resetCaretBlink(),
      onCompositionCommit: (text) => {
        this.closeContextMenuForTextInput();
        buffer.replaceSelection(text);
        if (kind === "search") void this.runSearch();
        this.resetCaretBlink();
      }
    };
  }

  private chatInputTarget(): TextInputTarget {
    const doc = this.chatDraft;
    return {
      kind: "chat",
      getSelectedText: () => doc.selectedText(),
      replaceSelection: (text) => {
        this.closeContextMenuForTextInput();
        doc.replaceSelection(text);
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.resetCaretBlink();
      },
      deleteSelectionOrBackward: (unit) => {
        this.closeContextMenuForTextInput();
        doc.deleteBackward(unit);
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.resetCaretBlink();
      },
      deleteForward: (unit) => {
        this.closeContextMenuForTextInput();
        doc.deleteForward(unit);
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.resetCaretBlink();
      },
      moveCursor: (command, extend) => {
        doc.move(command, extend);
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.resetCaretBlink();
      },
      runShortcut: (command) => {
        if (command === "Enter") {
          void this.sendChat();
          return true;
        }
        if (command === "Shift+Enter") {
          this.closeContextMenuForTextInput();
          doc.replaceSelection("\n");
          this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
          this.resetCaretBlink();
          return true;
        }
        if (command === "Mod+Enter") {
          void this.sendChat();
          return true;
        }
        if (command === "Mod+A") {
          doc.selectAll();
          this.resetCaretBlink();
          return true;
        }
        return this.runGlobalShortcut(command);
      },
      onCompositionPreview: () => this.resetCaretBlink(),
      onCompositionCommit: (text) => {
        this.closeContextMenuForTextInput();
        doc.replaceSelection(text);
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.resetCaretBlink();
      }
    };
  }

  private textFieldTarget(field: TextFieldKey): TextInputTarget {
    const buffer = this.bufferForTextField(field);
    return {
      kind: field,
      getSelectedText: () => buffer.selectedText(),
      replaceSelection: (text) => {
        this.closeContextMenuForTextInput();
        buffer.replaceSelection(this.sanitizeTextFieldInput(field, text));
        this.afterTextFieldChanged(field);
        this.resetCaretBlink();
      },
      deleteSelectionOrBackward: () => {
        this.closeContextMenuForTextInput();
        buffer.deleteBackward();
        this.afterTextFieldChanged(field);
        this.resetCaretBlink();
      },
      deleteForward: () => {
        this.closeContextMenuForTextInput();
        buffer.deleteForward();
        this.afterTextFieldChanged(field);
        this.resetCaretBlink();
      },
      moveCursor: (command, extend) => {
        buffer.move(command, extend);
        this.resetCaretBlink();
      },
      runShortcut: (command) => {
        if (command === "Enter") {
          if (isSettingTextField(field)) this.commitSettingsTextInput();
          else if (field === "find") this.selectDocumentFindMatch(1);
          else if (field === "findReplace") this.replaceCurrentFindMatch();
          else if (field === "projectReplace") void this.replaceAllInWorkspace();
          else void this.runSearch();
          this.resetCaretBlink();
          return true;
        }
        if (command === "Escape") {
          if (isSettingTextField(field)) this.cancelSettingsTextInput();
          else if (field === "find" || field === "findReplace") this.closeFindWidget();
          else this.focusEditor();
          return true;
        }
        if (command === "Mod+A") {
          buffer.selectAll();
          this.resetCaretBlink();
          return true;
        }
        return this.runGlobalShortcut(command);
      },
      onCompositionPreview: () => this.resetCaretBlink(),
      onCompositionCommit: (text) => {
        this.closeContextMenuForTextInput();
        buffer.replaceSelection(this.sanitizeTextFieldInput(field, text));
        this.afterTextFieldChanged(field);
        this.resetCaretBlink();
      }
    };
  }

  private sanitizeTextFieldInput(field: TextFieldKey, text: string): string {
    const singleLine = sanitizeSingleLineInput(text);
    return field === "aiMaxContextTokens" ? singleLine.replace(/\D+/g, "") : singleLine;
  }

  private renameTarget(): TextInputTarget {
    const buffer = this.renameBuffer;
    return {
      kind: "command",
      getSelectedText: () => buffer.selectedText(),
      replaceSelection: (text) => {
        this.closeContextMenuForTextInput();
        buffer.replaceSelection(text.replaceAll("\r\n", " ").replaceAll("\r", " ").replaceAll("\n", " "));
        this.resetCaretBlink();
      },
      deleteSelectionOrBackward: () => {
        this.closeContextMenuForTextInput();
        buffer.deleteBackward();
        this.resetCaretBlink();
      },
      deleteForward: () => {
        this.closeContextMenuForTextInput();
        buffer.deleteForward();
        this.resetCaretBlink();
      },
      moveCursor: (command, extend) => {
        buffer.move(command, extend);
        this.resetCaretBlink();
      },
      runShortcut: (command) => {
        if (command === "Enter") {
          void this.commitRename();
          return true;
        }
        if (command === "Escape") {
          this.cancelRename();
          return true;
        }
        if (command === "Mod+A") {
          buffer.selectAll();
          this.resetCaretBlink();
          return true;
        }
        return false;
      },
      onCompositionPreview: () => this.resetCaretBlink(),
      onCompositionCommit: (text) => {
        this.closeContextMenuForTextInput();
        buffer.replaceSelection(text.replaceAll("\r\n", " ").replaceAll("\r", " ").replaceAll("\n", " "));
        this.resetCaretBlink();
      }
    };
  }

  private runEditorShortcut(command: string): boolean {
    const doc = this.activeDoc();
    if (!doc) return false;
    if (this.runGlobalShortcut(command)) return true;
    if (command === "Mod+A") {
      doc.selectAll();
      this.resetCaretBlink();
      return true;
    }
    if (command === "Mod+C") {
      const text = doc.selectedText();
      if (!text) return false;
      this.copyTextToClipboard(text);
      return true;
    }
    if (doc.readOnly) {
      this.statusText = "File type not supported";
      this.scheduleDraw();
      return true;
    }
    if (command === "Tab") {
      doc.indentSelectedLines(this.editorIndentString());
      this.afterDocumentMutated(doc);
      this.revealEditorCaret();
      return true;
    }
    if (command === "Shift+Tab") {
      doc.unindentSelectedLines(this.codeTabSpaces());
      this.afterDocumentMutated(doc);
      this.revealEditorCaret();
      return true;
    }
    if (command === "Mod+Z") {
      doc.undo();
      this.afterDocumentMutated(doc);
      this.revealEditorCaret();
      return true;
    }
    if (command === "Mod+Shift+Z" || command === "Mod+Y") {
      doc.redo();
      this.afterDocumentMutated(doc);
      this.revealEditorCaret();
      return true;
    }
    if (command === "Mod+X") {
      const text = doc.selectedText();
      if (!text) return false;
      this.copyTextToClipboard(text);
      doc.replaceSelection("", "cut");
      this.afterDocumentMutated(doc);
      this.revealEditorCaret();
      return true;
    }
    return false;
  }

  private async runContextMenuCommand(command: ContextMenuCommand): Promise<void> {
    const menu = this.contextMenu;
    const item = menu?.items.find((candidate): candidate is ContextMenuItem => isContextMenuItem(candidate) && candidate.command === command);
    if (!menu || !item?.enabled) return;
    this.contextMenu = null;
    this.contextMenuHover = null;
    if (menu.scope.type === "file") {
      await this.runFileContextMenuCommand(menu.scope.path, command);
      this.scheduleDraw();
      return;
    }
    if (menu.scope.type === "folder") {
      await this.runFolderContextMenuCommand(menu.scope.path, command);
      this.scheduleDraw();
      return;
    }
    if (menu.scope.type === "root") {
      await this.runRootContextMenuCommand(command);
      this.scheduleDraw();
      return;
    }
    if (menu.scope.type === "tab") {
      await this.runTabContextMenuCommand(menu.scope.groupId, menu.scope.docId, command);
      return;
    }
    if (menu.scope.type === "tabBar") {
      await this.runTabBarContextMenuCommand(menu.scope.groupId, command);
      return;
    }
    if (menu.scope.type === "tabOverflow") {
      this.runTabOverflowContextMenuCommand(menu.scope.groupId, command);
      return;
    }
    if (menu.scope.type === "highlightDropdown") {
      this.runHighlightDropdownCommand(menu.scope.groupId, menu.scope.docId, command);
      return;
    }
    if (menu.scope.type === "gutter") {
      this.runGutterContextMenuCommand(menu.scope.groupId, menu.scope.docId, command);
      return;
    }
    if (menu.scope.type === "settingsRoot") {
      if (command === "resetSettings") this.resetSettings();
      this.closeContextMenu();
      return;
    }
    if (menu.scope.type === "chatRoot") {
      await this.runChatRootContextMenuCommand(command);
      return;
    }
    if (menu.scope.type === "chatBubble") {
      await this.runChatBubbleContextMenuCommand(menu.scope.messageId, command);
      return;
    }
    if (menu.scope.type === "settingsDropdown") {
      this.runSettingsDropdownCommand(menu.scope.key, command);
      return;
    }
    if (menu.scope.type === "settingsNumber") {
      await this.runSettingsNumberContextMenuCommand(menu.scope.key, command);
      return;
    }
    if (menu.scope.type === "rename") {
      await this.runRenameContextMenuCommand(command);
      return;
    }
    if (menu.scope.type === "search") {
      await this.runSearchContextMenuCommand(command);
      return;
    }
    if (menu.scope.type === "chatInput") {
      await this.runChatInputContextMenuCommand(command);
      return;
    }
    if (menu.scope.type === "textField") {
      await this.runTextFieldContextMenuCommand(menu.scope.field, command);
      return;
    }
    if (!isEditorContextMenuCommand(command)) return;
    const group = this.groupById(menu.scope.groupId);
    const doc = this.docs.get(menu.scope.docId);
    if (!doc) {
      this.closeContextMenu();
      return;
    }
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    group.activeDocId = doc.id;
    this.selectActiveDocumentInFileTree();

    if (command === "undo" || command === "redo") {
      if (doc.readOnly) {
        this.statusText = "File type not supported";
        this.scheduleDraw();
        return;
      }
      if (command === "undo" && doc.canUndo()) {
        doc.undo();
        this.afterDocumentMutated(doc);
        this.revealEditorCaret();
        this.statusText = "Undid edit";
      } else if (command === "redo" && doc.canRedo()) {
        doc.redo();
        this.afterDocumentMutated(doc);
        this.revealEditorCaret();
        this.statusText = "Redid edit";
      } else {
        this.focusEditor();
      }
      this.scheduleDraw();
      return;
    }

    if (command === "systemCopy") {
      const text = doc.selectedText();
      if (text) this.openSystemCopyDialog(text, () => this.focusEditor());
      else this.statusText = "No selection";
      this.scheduleDraw();
      return;
    }
    if (command === "systemPaste") {
      if (doc.readOnly) {
        this.statusText = "File type not supported";
        this.scheduleDraw();
        return;
      }
      this.openSystemPasteDialog((text) => {
        doc.replaceSelection(text.replaceAll("\r\n", "\n").replaceAll("\r", "\n"), "paste");
        this.afterDocumentMutated(doc);
        this.statusText = "Pasted";
        this.revealEditorCaret();
        this.scheduleDraw();
      }, () => this.focusEditor());
      return;
    }

    let changedDocument = false;
    if (command === "copy" || command === "cut") {
      const text = doc.selectedText();
      if (!text) {
        this.statusText = "No selection";
      } else {
        this.copyTextToClipboard(text);
        if (command === "cut") {
          if (doc.readOnly) {
            this.statusText = "File type not supported";
            this.scheduleDraw();
            return;
          }
          doc.replaceSelection("", "cut");
          this.afterDocumentMutated(doc);
          this.statusText = "Cut selection";
          changedDocument = true;
        } else {
          this.statusText = "Copied selection";
        }
      }
    } else {
      this.focusEditor();
      const text = await this.readTextFromClipboard();
      if (text === null) {
        this.statusText = "Clipboard paste unavailable";
      } else if (!text) {
        this.statusText = "Clipboard empty";
      } else if (doc.readOnly) {
        this.statusText = "File type not supported";
      } else {
        doc.replaceSelection(text.replaceAll("\r\n", "\n").replaceAll("\r", "\n"), "paste");
        this.afterDocumentMutated(doc);
        this.statusText = "Pasted";
        changedDocument = true;
      }
    }

    if (changedDocument) this.revealEditorCaret();
    else this.focusEditor();
    this.scheduleDraw();
  }

  private async runRenameContextMenuCommand(command: ContextMenuCommand): Promise<void> {
    if (!isEditorContextMenuCommand(command)) return;
    if (command === "undo" || command === "redo") {
      if (command === "undo" && this.renameBuffer.canUndo()) {
        this.renameBuffer.undo();
        this.statusText = "Undid edit";
      } else if (command === "redo" && this.renameBuffer.canRedo()) {
        this.renameBuffer.redo();
        this.statusText = "Redid edit";
      }
      this.focusRename(this.renameInputRect() ?? undefined);
      this.resetCaretBlink();
      return;
    }
    if (command === "systemCopy") {
      const text = this.renameBuffer.selectedText();
      if (text) this.openSystemCopyDialog(text, () => this.focusRename(this.renameInputRect() ?? undefined));
      else this.statusText = "No selection";
      this.scheduleDraw();
      return;
    }
    if (command === "systemPaste") {
      this.openSystemPasteDialog((text) => {
        this.renameBuffer.replaceSelection(sanitizeSingleLineInput(text));
        this.statusText = "Pasted";
        this.focusRename(this.renameInputRect() ?? undefined);
        this.resetCaretBlink();
      }, () => this.focusRename(this.renameInputRect() ?? undefined));
      return;
    }
    if (command === "copy" || command === "cut") {
      const text = this.renameBuffer.selectedText();
      if (!text) {
        this.statusText = "No selection";
      } else {
        this.copyTextToClipboard(text);
        if (command === "cut") {
          this.renameBuffer.replaceSelection("");
          this.statusText = "Cut file name text";
        } else {
          this.statusText = "Copied file name text";
        }
      }
    } else {
      this.focusRename(this.renameInputRect() ?? undefined);
      const text = await this.readTextFromClipboard();
      if (text === null) {
        this.statusText = "Clipboard paste unavailable";
      } else if (!text) {
        this.statusText = "Clipboard empty";
      } else {
        this.renameBuffer.replaceSelection(sanitizeSingleLineInput(text));
        this.statusText = "Pasted";
      }
    }
    this.focusRename(this.renameInputRect() ?? undefined);
    this.resetCaretBlink();
  }

  private async runSearchContextMenuCommand(command: ContextMenuCommand): Promise<void> {
    if (!isEditorContextMenuCommand(command)) return;
    if (command === "undo" || command === "redo") {
      if (command === "undo" && this.searchBuffer.canUndo()) {
        this.searchBuffer.undo();
        this.statusText = "Undid edit";
      } else if (command === "redo" && this.searchBuffer.canRedo()) {
        this.searchBuffer.redo();
        this.statusText = "Redid edit";
      }
      void this.runSearch();
      this.focusMiniTarget("search", this.searchInputRect() ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 });
      this.resetCaretBlink();
      return;
    }
    if (command === "systemCopy") {
      const text = this.searchBuffer.selectedText();
      if (text) this.openSystemCopyDialog(text, () => this.focusMiniTarget("search", this.searchInputRect() ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 }));
      else this.statusText = "No selection";
      this.scheduleDraw();
      return;
    }
    if (command === "systemPaste") {
      this.openSystemPasteDialog((text) => {
        this.searchBuffer.replaceSelection(sanitizeSingleLineInput(text));
        void this.runSearch();
        this.statusText = "Pasted";
        this.focusMiniTarget("search", this.searchInputRect() ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 });
      }, () => this.focusMiniTarget("search", this.searchInputRect() ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 }));
      return;
    }
    if (command === "copy" || command === "cut") {
      const text = this.searchBuffer.selectedText();
      if (!text) {
        this.statusText = "No selection";
      } else {
        this.copyTextToClipboard(text);
        if (command === "cut") {
          this.searchBuffer.replaceSelection("");
          void this.runSearch();
          this.statusText = "Cut search text";
        } else {
          this.statusText = "Copied search text";
        }
      }
    } else {
      this.focusMiniTarget("search", this.searchInputRect() ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 });
      const text = await this.readTextFromClipboard();
      if (text === null) {
        this.statusText = "Clipboard paste unavailable";
      } else if (!text) {
        this.statusText = "Clipboard empty";
      } else {
        this.searchBuffer.replaceSelection(sanitizeSingleLineInput(text));
        void this.runSearch();
        this.statusText = "Pasted";
      }
    }
    this.focusMiniTarget("search", this.searchInputRect() ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 });
  }

  private async runChatInputContextMenuCommand(command: ContextMenuCommand): Promise<void> {
    if (!isEditorContextMenuCommand(command)) return;
    const restore = () => this.focusMiniTarget("chat", this.chatInputRectForFocus());
    if (command === "undo" || command === "redo") {
      if (command === "undo" && this.chatDraft.canUndo()) {
        this.chatDraft.undo();
        this.statusText = "Undid edit";
      } else if (command === "redo" && this.chatDraft.canRedo()) {
        this.chatDraft.redo();
        this.statusText = "Redid edit";
      }
      this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
      restore();
      this.resetCaretBlink();
      return;
    }
    if (command === "systemCopy") {
      const text = this.chatDraft.selectedText();
      if (text) this.openSystemCopyDialog(text, restore);
      else this.statusText = "No selection";
      this.scheduleDraw();
      return;
    }
    if (command === "systemPaste") {
      this.openSystemPasteDialog((text) => {
        this.chatDraft.replaceSelection(text.replaceAll("\r\n", "\n").replaceAll("\r", "\n"));
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.statusText = "Pasted";
        restore();
      }, restore);
      return;
    }
    if (command === "copy" || command === "cut") {
      const text = this.chatDraft.selectedText();
      if (!text) {
        this.statusText = "No selection";
      } else {
        this.copyTextToClipboard(text);
        if (command === "cut") {
          this.chatDraft.replaceSelection("");
          this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
          this.statusText = "Cut chat text";
        } else {
          this.statusText = "Copied chat text";
        }
      }
    } else {
      restore();
      const text = await this.readTextFromClipboard();
      if (text === null) {
        this.statusText = "Clipboard paste unavailable";
      } else if (!text) {
        this.statusText = "Clipboard empty";
      } else {
        this.chatDraft.replaceSelection(text.replaceAll("\r\n", "\n").replaceAll("\r", "\n"));
        this.ensureChatInputCaretVisible(this.chatInputRectForFocus());
        this.statusText = "Pasted";
      }
    }
    restore();
    this.resetCaretBlink();
  }

  private async runChatRootContextMenuCommand(command: ContextMenuCommand): Promise<void> {
    if (command === "exportChat") {
      this.exportChatToDisk();
      return;
    }
    if (command === "debugChat") {
      this.debugChatToUntitled();
      return;
    }
    if (command === "clearChat") {
      if (this.chat.running) return;
      this.openClearChatModal();
      return;
    }
    if (command === "compactChat") {
      if (this.chat.running) return;
      this.statusText = "Compacting chat";
      const result = await this.chat.compact(this.aiRuntimeSettings(), {
        onUpdate: () => this.scheduleDraw(),
        onCompactStart: () => this.openCompactingModal(),
        onCompactEnd: () => this.closeCompactingModal()
      });
      this.chatScrollY = Number.MAX_SAFE_INTEGER;
      this.statusText = result.output;
      this.scheduleDraw();
    }
  }

  private async runChatBubbleContextMenuCommand(messageId: string, command: ContextMenuCommand): Promise<void> {
    const message = this.chatDisplayMessages().find((msg) => msg.id === messageId);
    const chatText = this.chatTranscriptText();
    const restore = () => this.input.blur();
    if (command === "copyBubble") {
      if (!message) {
        this.statusText = "Chat bubble not found";
      } else {
        this.copyTextToClipboard(message.text);
        this.statusText = "Copied chat bubble";
      }
      this.scheduleDraw();
      return;
    }
    if (command === "copyChat") {
      if (!chatText) {
        this.statusText = "Chat empty";
      } else {
        this.copyTextToClipboard(chatText);
        this.statusText = "Copied chat";
      }
      this.scheduleDraw();
      return;
    }
    if (command === "systemCopyBubble") {
      if (message) this.openSystemCopyDialog(message.text, restore);
      else this.statusText = "Chat bubble not found";
      this.scheduleDraw();
      return;
    }
    if (command === "systemCopyChat") {
      if (chatText) this.openSystemCopyDialog(chatText, restore);
      else this.statusText = "Chat empty";
      this.scheduleDraw();
      return;
    }
    if (command === "clearChat") {
      if (!this.chat.running) this.openClearChatModal();
      return;
    }
  }

  private async runTextFieldContextMenuCommand(field: TextFieldKey, command: ContextMenuCommand): Promise<void> {
    if (field === "search") {
      await this.runSearchContextMenuCommand(command);
      return;
    }
    if (!isEditorContextMenuCommand(command)) return;
    const buffer = this.bufferForTextField(field);
    const fallback = { x: this.ui(56), y: this.ui(40), w: Math.max(this.ui(80), this.sidebarWidth - this.ui(20)), h: this.ui(28) };
    const restore = () => this.focusTextField(field, this.textFieldRect(field) ?? fallback);
    if (command === "undo" || command === "redo") {
      if (command === "undo" && buffer.canUndo()) {
        buffer.undo();
        this.statusText = "Undid edit";
      } else if (command === "redo" && buffer.canRedo()) {
        buffer.redo();
        this.statusText = "Redid edit";
      }
      this.afterTextFieldChanged(field);
      restore();
      this.resetCaretBlink();
      return;
    }
    if (command === "systemCopy") {
      const text = buffer.selectedText();
      if (text) this.openSystemCopyDialog(text, restore);
      else this.statusText = "No selection";
      this.scheduleDraw();
      return;
    }
    if (command === "systemPaste") {
      this.openSystemPasteDialog((text) => {
        buffer.replaceSelection(this.sanitizeTextFieldInput(field, text));
        this.afterTextFieldChanged(field);
        this.statusText = "Pasted";
        restore();
        this.resetCaretBlink();
      }, restore);
      return;
    }
    if (command === "copy" || command === "cut") {
      const text = buffer.selectedText();
      if (!text) {
        this.statusText = "No selection";
      } else {
        this.copyTextToClipboard(text);
        if (command === "cut") {
          buffer.replaceSelection("");
          this.afterTextFieldChanged(field);
          this.statusText = "Cut text";
        } else {
          this.statusText = "Copied text";
        }
      }
    } else {
      restore();
      const text = await this.readTextFromClipboard();
      if (text === null) {
        this.statusText = "Clipboard paste unavailable";
      } else if (!text) {
        this.statusText = "Clipboard empty";
      } else {
        buffer.replaceSelection(this.sanitizeTextFieldInput(field, text));
        this.afterTextFieldChanged(field);
        this.statusText = "Pasted";
      }
    }
    restore();
    this.resetCaretBlink();
  }

  private async runTabContextMenuCommand(groupId: string, docId: string, command: ContextMenuCommand): Promise<void> {
    if (!isTabContextMenuCommand(command)) return;
    const group = this.groupById(groupId);
    if (!group.tabs.includes(docId)) return;
    if (command === "save") {
      if (this.isSettingsTab(docId)) return;
      const doc = this.docs.get(docId);
      if (!doc) return;
      if (doc.readOnly) {
        this.statusText = "File type not supported";
        this.scheduleDraw();
        return;
      }
      await this.saveDocument(doc);
      this.statusText = `Saved ${doc.path}`;
      this.scheduleDraw();
      return;
    }
    if (command === "findInFile") {
      if (this.isSettingsTab(docId)) return;
      group.activeDocId = docId;
      this.activeGroupId = group.id;
      this.activeDocId = docId;
      this.selectActiveDocumentInFileTree();
      this.openFindWidget();
      return;
    }
    if (command === "resetSettings") {
      if (!this.isSettingsTab(docId)) return;
      group.activeDocId = docId;
      this.activeGroupId = group.id;
      this.activeDocId = docId;
      this.resetSettings();
      return;
    }
    if (command === "close") {
      await this.requestCloseTab(docId);
      return;
    }

    group.activeDocId = docId;
    this.activeGroupId = group.id;
    this.activeDocId = docId;
    this.selectActiveDocumentInFileTree();
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    const others = group.tabs.filter((id) => id !== docId);
    await this.requestCloseTabs(others);
  }

  private async runTabBarContextMenuCommand(groupId: string, command: ContextMenuCommand): Promise<void> {
    if (!isTabBarContextMenuCommand(command)) return;
    const group = this.groupById(groupId);
    if (command === "newFile") {
      this.openUntitledDocument(group.id);
      return;
    }
    if (command === "uploadFile") {
      this.requestFileUpload("/");
      return;
    }
    await this.requestCloseTabs(group.tabs);
  }

  private runTabOverflowContextMenuCommand(groupId: string, command: ContextMenuCommand): void {
    const docId = tabOverflowCommandDocId(command);
    if (!docId) return;
    const group = this.groupById(groupId);
    if (!group.tabs.includes(docId)) return;
    this.activateTabInGroup(group, docId);
    this.statusText = `Opened ${this.tabLabel(docId)}`;
    this.scheduleDraw();
  }

  private openHighlightDropdown(hit: Extract<HitItem, { type: "statusHighlight" }>): void {
    const doc = this.docs.get(hit.docId);
    if (!doc) return;
    const entries = HIGHLIGHT_OPTIONS.map((option) => ({
      command: highlightCommand(option.id),
      label: `${doc.syntaxId === option.id ? "✔️ " : ""}${option.label}`,
      enabled: true
    }));
    const pad = this.ui(CONTEXT_MENU_PAD);
    const menuH = pad * 2 + entries.length * this.ui(CONTEXT_MENU_ROW_H);
    const menuW = Math.max(this.ui(150), ...entries.map((entry) => this.renderer.measureText(entry.label, "ui") + this.ui(34)));
    const menuX = hit.rect.x + hit.rect.w - menuW;
    this.contextMenu = this.makeContextMenu(
      { x: menuX, y: hit.rect.y - menuH },
      { type: "highlightDropdown", groupId: hit.groupId, docId: hit.docId },
      entries,
      { x: menuX, y: hit.rect.y - menuH, w: menuW }
    );
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private runHighlightDropdownCommand(groupId: string, docId: string, command: ContextMenuCommand): void {
    const syntaxId = highlightCommandSyntaxId(command);
    if (!syntaxId) return;
    const doc = this.docs.get(docId);
    const group = this.groupById(groupId);
    if (!doc || !group.tabs.includes(doc.id)) return;
    doc.syntaxId = syntaxId;
    group.activeDocId = doc.id;
    this.activeGroupId = group.id;
    this.activeDocId = doc.id;
    this.selectActiveDocumentInFileTree();
    this.statusText = `Highlight ${this.highlightLabel(syntaxId)}`;
    this.contextMenu = null;
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private runGutterContextMenuCommand(groupId: string, docId: string, command: ContextMenuCommand): void {
    if (command !== "toggleLineNumbers") return;
    const group = this.groupById(groupId);
    const doc = this.docs.get(docId);
    if (doc && group.tabs.includes(doc.id)) {
      this.activeGroupId = group.id;
      this.activeDocId = doc.id;
      group.activeDocId = doc.id;
      this.selectActiveDocumentInFileTree();
    }
    this.settings.showLineNumbers = !this.settings.showLineNumbers;
    this.statusText = this.settings.showLineNumbers ? "Line numbers shown" : "Line numbers hidden";
    this.saveAndApplySettings();
  }

  private openSettingsDropdown(rect: Rect, key: SettingDropdownKey): void {
    const currentConfig = loadAiEndpointConfig();
    let entries: ContextMenuSeed[];
    if (key === "theme") {
      entries = [
        { command: "themeDark", label: "Dark", enabled: true },
        { command: "themeLight", label: "Light", enabled: true }
      ];
    } else if (key === "aiToolCallFormat") {
      entries = [
        { command: "aiToolFormatNone", label: "None", enabled: true },
        { command: "aiToolFormatTag", label: "Tag", enabled: true },
        { command: "aiToolFormatHarmony", label: "Harmony", enabled: true }
      ];
    } else if (key === "aiModel") {
      const models = [...this.aiModels];
      if (currentConfig.model && !models.some((model) => model.id === currentConfig.model)) models.unshift({ id: currentConfig.model, contextLength: currentConfig.maxContextTokens });
      entries = models.length
        ? models.map((model) => ({
            command: aiModelCommand(model.id),
            label: `${model.id}${model.contextLength ? ` (${Math.round(model.contextLength / 1000)}k)` : ""}`,
            enabled: true
          }))
        : [{ command: aiModelCommand(""), label: "No models probed", enabled: false }];
    } else {
      entries = [
        { command: "aiProviderLocal", label: "Local", enabled: true },
        { command: "aiProviderOpenAI", label: "OpenAI", enabled: true }
      ];
    }
    const menuW = key === "aiModel"
      ? Math.min(
          Math.max(rect.w, ...entries.map((entry) => "separator" in entry ? 0 : this.renderer.measureText(entry.label, "ui") + this.ui(34))),
          Math.max(rect.w, this.viewport.get().cssWidth - this.ui(24))
        )
      : rect.w;
    this.contextMenu = this.makeContextMenu({ x: rect.x, y: rect.y + rect.h }, { type: "settingsDropdown", key }, entries, { x: rect.x, y: rect.y + rect.h, w: menuW });
    this.contextMenuHover = null;
    this.scheduleDraw();
  }

  private runSettingsDropdownCommand(key: SettingDropdownKey, command: ContextMenuCommand): void {
    if (!isSettingContextMenuCommand(command)) return;
    if (key === "theme") {
      if (command === "themeDark") this.settings.theme = "dark";
      else if (command === "themeLight") this.settings.theme = "light";
    } else if (key === "aiProvider") {
      if (command === "aiProviderLocal") this.settings.aiProvider = "local";
      else if (command === "aiProviderOpenAI") this.settings.aiProvider = "openai";
    } else if (key === "aiToolCallFormat") {
      if (command === "aiToolFormatNone") this.settings.aiToolCallFormat = "none";
      else if (command === "aiToolFormatTag") this.settings.aiToolCallFormat = "tag";
      else if (command === "aiToolFormatHarmony") this.settings.aiToolCallFormat = "harmony";
    } else if (key === "aiModel") {
      const modelId = aiModelCommandValue(command);
      if (modelId !== null) {
        const selected = this.aiModels.find((model) => model.id === modelId);
        const config = loadAiEndpointConfig();
        const detectedContextTokens = selected?.contextLength || resolveAiContextTokens({ ...config, model: modelId, maxContextTokens: 0 });
        saveAiEndpointConfig({
          ...config,
          model: modelId,
          maxContextTokens: detectedContextTokens || config.maxContextTokens
        });
        this.statusText = modelId ? `AI model ${modelId}` : "AI model unchanged";
      }
    }
    this.saveAndApplySettings();
  }

  private async runSettingsNumberContextMenuCommand(key: SettingNumberKey, command: ContextMenuCommand): Promise<void> {
    if (!isEditorContextMenuCommand(command)) return;
    const restore = () => this.focusSettingsNumber(key, this.settingsNumberInputRect(key) ?? { x: 56, y: 40, w: Math.max(80, this.sidebarWidth - 20), h: 28 });
    if (command === "undo" || command === "redo") {
      if (command === "undo" && this.settingsNumberBuffer.canUndo()) {
        this.settingsNumberBuffer.undo();
        this.statusText = "Undid edit";
      } else if (command === "redo" && this.settingsNumberBuffer.canRedo()) {
        this.settingsNumberBuffer.redo();
        this.statusText = "Redid edit";
      }
      this.applySettingsNumberFromBuffer();
      restore();
      this.resetCaretBlink();
      return;
    }
    if (command === "systemCopy") {
      const text = this.settingsNumberBuffer.selectedText();
      if (text) this.openSystemCopyDialog(text, restore);
      else this.statusText = "No selection";
      this.scheduleDraw();
      return;
    }
    if (command === "systemPaste") {
      this.openSystemPasteDialog((text) => {
        this.settingsNumberBuffer.replaceSelection(text.replace(/\D+/g, ""));
        this.applySettingsNumberFromBuffer();
        this.statusText = "Pasted";
        restore();
        this.resetCaretBlink();
      }, restore);
      return;
    }
    if (command === "copy" || command === "cut") {
      const text = this.settingsNumberBuffer.selectedText();
      if (!text) {
        this.statusText = "No selection";
      } else {
        this.copyTextToClipboard(text);
        if (command === "cut") {
          this.settingsNumberBuffer.replaceSelection("");
          this.applySettingsNumberFromBuffer();
          this.statusText = "Cut setting value";
        } else {
          this.statusText = "Copied setting value";
        }
      }
    } else {
      restore();
      const text = await this.readTextFromClipboard();
      if (text === null) {
        this.statusText = "Clipboard paste unavailable";
      } else if (!text) {
        this.statusText = "Clipboard empty";
      } else {
        this.settingsNumberBuffer.replaceSelection(text.replace(/\D+/g, ""));
        this.applySettingsNumberFromBuffer();
        this.statusText = "Pasted";
      }
    }
    restore();
    this.resetCaretBlink();
  }

  private toggleSettingsHeader(id: SettingHeaderId): void {
    if (this.settingsExpanded.has(id)) this.settingsExpanded.delete(id);
    else this.settingsExpanded.add(id);
    this.scheduleDraw();
  }

  private toggleSettingsCheckbox(key: SettingCheckboxKey): void {
    this.settings[key] = !this.settings[key];
    if (key === "aiModelManual") this.syncSettingsTextBufferFromConfig("aiModel");
    this.saveAndApplySettings();
  }

  private focusSettingsNumber(key: SettingNumberKey, rect: Rect): void {
    const wasActive = this.activeSettingsNumber === key;
    this.activeSettingsNumber = key;
    if (!wasActive) {
      this.settingsNumberBuffer.text = String(this.settings[key]);
      this.settingsNumberBuffer.cursor = this.settingsNumberBuffer.text.length;
      this.settingsNumberBuffer.anchor = this.settingsNumberBuffer.cursor;
      this.settingsNumberBuffer.scrollX = 0;
      this.settingsNumberBuffer.clearUndoHistory();
    }
    this.input.focusEditor(this.settingsNumberTarget(), rect);
    this.resetCaretBlink();
    this.requestFocusedInputReveal();
  }

  private settingsNumberTarget(): TextInputTarget {
    return {
      kind: "command",
      getSelectedText: () => this.settingsNumberBuffer.selectedText(),
      replaceSelection: (text) => {
        this.closeContextMenuForTextInput();
        this.settingsNumberBuffer.replaceSelection(text.replace(/\D+/g, ""));
        this.applySettingsNumberFromBuffer();
        this.resetCaretBlink();
      },
      deleteSelectionOrBackward: () => {
        this.closeContextMenuForTextInput();
        this.settingsNumberBuffer.deleteBackward();
        this.applySettingsNumberFromBuffer();
        this.resetCaretBlink();
      },
      deleteForward: () => {
        this.closeContextMenuForTextInput();
        this.settingsNumberBuffer.deleteForward();
        this.applySettingsNumberFromBuffer();
        this.resetCaretBlink();
      },
      moveCursor: (command, extend) => {
        this.settingsNumberBuffer.move(command, extend);
        this.resetCaretBlink();
      },
      runShortcut: (command) => {
        if (command === "Enter") {
          this.commitSettingsNumberInput();
          return true;
        }
        if (command === "Escape") {
          this.cancelSettingsNumberInput();
          return true;
        }
        if (command === "Mod+A") {
          this.settingsNumberBuffer.selectAll();
          this.resetCaretBlink();
          return true;
        }
        return false;
      },
      onCompositionPreview: () => this.resetCaretBlink(),
      onCompositionCommit: (text) => {
        this.closeContextMenuForTextInput();
        this.settingsNumberBuffer.replaceSelection(text.replace(/\D+/g, ""));
        this.applySettingsNumberFromBuffer();
        this.resetCaretBlink();
      }
    };
  }

  private applySettingsNumberFromBuffer(): void {
    const key = this.activeSettingsNumber;
    if (!key) return;
    const value = Number.parseInt(this.settingsNumberBuffer.text, 10);
    if (!Number.isFinite(value)) return;
    if (key === "fontSize") this.settings[key] = Math.max(1, value);
    else if (key === "tabSpaces") this.settings[key] = clamp(Math.trunc(value), 1, 32);
    else if (key === "aiMaxToolCalls") this.settings[key] = clamp(Math.trunc(value), 1, 200);
    else if (key === "aiCompactFreePercent") this.settings[key] = clamp(Math.trunc(value), 1, 95);
    else this.settings[key] = clamp(Math.trunc(value), 1, 400);
    this.saveAndApplySettings();
  }

  private commitSettingsNumberInput(blur = true): void {
    const key = this.activeSettingsNumber;
    if (!key) return;
    this.applySettingsNumberFromBuffer();
    this.settingsNumberBuffer.text = String(this.settings[key]);
    this.settingsNumberBuffer.cursor = this.settingsNumberBuffer.text.length;
    this.settingsNumberBuffer.anchor = this.settingsNumberBuffer.cursor;
    this.settingsNumberBuffer.scrollX = 0;
    this.settingsNumberBuffer.clearUndoHistory();
    this.activeSettingsNumber = null;
    this.settingsNumberSelecting = false;
    if (blur) this.input.blur();
    this.scheduleDraw();
  }

  private cancelSettingsNumberInput(): void {
    this.activeSettingsNumber = null;
    this.settingsNumberSelecting = false;
    this.input.blur();
    this.scheduleDraw();
  }

  private commitSettingsTextInput(blur = true): void {
    const key = this.activeSettingsText;
    if (!key) return;
    this.applySettingsTextFromBuffer(key);
    this.activeSettingsText = null;
    this.textFieldSelecting = null;
    if (blur) this.input.blur();
    this.scheduleDraw();
  }

  private cancelSettingsTextInput(): void {
    const key = this.activeSettingsText;
    if (key) this.syncSettingsTextBufferFromConfig(key);
    this.activeSettingsText = null;
    this.textFieldSelecting = null;
    this.input.blur();
    this.scheduleDraw();
  }

  private applySettingsTextFromBuffer(key: SettingTextKey): void {
    const config = loadAiEndpointConfig();
    const buffer = this.settingsTextBuffers[key];
    if (key === "aiBaseUrl") {
      const next = saveAiEndpointConfig({ ...config, apiBaseUrl: buffer.text });
      this.aiModels = [];
      buffer.text = next.apiBaseUrl;
      this.markAiEndpointEdited();
      this.statusText = "AI base URL updated";
    } else if (key === "aiApiKey") {
      saveAiEndpointConfig({ ...config, apiKey: buffer.text.trim() });
      buffer.text = buffer.text.trim();
      this.markAiEndpointEdited();
      this.statusText = "AI API key updated";
    } else if (key === "aiModel") {
      const model = buffer.text.trim();
      saveAiEndpointConfig({ ...config, model });
      buffer.text = model;
      this.statusText = model ? `AI model ${model}` : "AI model cleared";
    } else {
      const value = Number.parseInt(buffer.text, 10);
      const maxContextTokens = Number.isFinite(value) ? Math.max(0, Math.trunc(value)) : 0;
      saveAiEndpointConfig({ ...config, maxContextTokens });
      buffer.text = maxContextTokens ? String(maxContextTokens) : "";
      this.statusText = maxContextTokens ? "AI max context tokens updated" : "AI max context tokens set to auto-detect";
    }
    buffer.cursor = buffer.text.length;
    buffer.anchor = buffer.cursor;
    buffer.scrollX = 0;
  }

  private setSettingsNumberCursorFromPoint(x: number, rect: Rect, extend: boolean): void {
    const offset = x - (rect.x + this.ui(8)) + this.settingsNumberBuffer.scrollX;
    const col = this.columnFromTextOffset(this.settingsNumberBuffer.text, offset, "ui");
    this.settingsNumberBuffer.cursor = col;
    if (!extend) this.settingsNumberBuffer.anchor = col;
    this.revealMiniBufferCaret(this.settingsNumberBuffer, rect, this.ui(8));
    this.resetCaretBlink();
  }

  private selectSettingsNumberWordFromPoint(_x: number, _rect: Rect): void {
    this.settingsNumberBuffer.selectAll();
    this.resetCaretBlink();
  }

  private pointHitsSettingsNumberSelection(x: number, rect: Rect): boolean {
    if (!this.settingsNumberBuffer.hasSelection()) return false;
    if (x >= rect.x && x <= rect.x + rect.w) return true;
    const start = Math.min(this.settingsNumberBuffer.anchor, this.settingsNumberBuffer.cursor);
    const end = Math.max(this.settingsNumberBuffer.anchor, this.settingsNumberBuffer.cursor);
    const textX = rect.x + this.ui(8) - this.settingsNumberBuffer.scrollX;
    const startX = textX + this.renderer.measureText(this.settingsNumberBuffer.text.slice(0, start), "ui");
    const endX = textX + this.renderer.measureText(this.settingsNumberBuffer.text.slice(0, end), "ui");
    return x >= startX && x <= Math.max(startX + 2, endX);
  }

  private settingsNumberInputRect(key = this.activeSettingsNumber): Rect | null {
    return this.hits.find((hit): hit is Extract<HitItem, { type: "settingsNumber" }> => hit.type === "settingsNumber" && hit.key === key)?.rect ?? null;
  }

  private isSettingsNumberCaretVisible(key: SettingNumberKey): boolean {
    return this.activeSettingsNumber === key && (this.input.composing || this.isCaretBlinkOn());
  }

  private async runSettingsButton(action: SettingButtonAction): Promise<void> {
    if (action === "resetAll") {
      this.resetSettings();
      return;
    }
    if (action === "editSystemPrompt") {
      this.openSystemPromptDocument();
      return;
    }
    if (action === "editTagToolPrompt") {
      this.openTagToolPromptDocument();
      return;
    }
    if (action === "editHarmonyToolPrompt") {
      this.openHarmonyToolPromptDocument();
      return;
    }
    if (action === "editCompactPrompt") {
      this.openCompactPromptDocument();
      return;
    }
    if (action === "checkAiServer") {
      await this.checkAiServer();
      return;
    }
    if (action === "probeLmStudioModels") {
      await this.probeLmStudioModels();
      return;
    }
    if (action === "probeLmStudioMaxTokens") {
      await this.probeLmStudioMaxTokens();
      return;
    }
    this.openClearFileSystemModal();
  }

  private async checkAiServer(): Promise<void> {
    this.setAiConnectionStatus("checking", "Checking AI server...", null);
    const result = await checkOpenAICompatibleServer(loadAiEndpointConfig());
    this.applyAiServerCheckResult(result);
  }

  private async probeLmStudioModels(): Promise<void> {
    this.setAiConnectionStatus("checking", "Probing LM Studio models...", null);
    const config = loadAiEndpointConfig();
    const result = await checkOpenAICompatibleServer(config);
    if (!result.ok) {
      this.applyAiServerCheckResult(result);
      return;
    }
    this.aiModels = result.models;
    if (result.models.length > 0) {
      let selected = config.model ? result.models.find((model) => model.id === config.model) : undefined;
      if (!selected && result.models.length === 1) selected = result.models[0];
      if (selected) {
        saveAiEndpointConfig({
          ...config,
          model: selected.id,
          maxContextTokens: selected.contextLength || config.maxContextTokens
        });
      }
    }
    this.setAiConnectionStatus("ok", `Found ${result.models.length} model${result.models.length === 1 ? "" : "s"} at ${result.baseUrl}.`, "ok", result.baseUrl);
  }

  private async probeLmStudioMaxTokens(): Promise<void> {
    this.setAiConnectionStatus("checking", "Probing LM Studio max tokens...", null);
    const config = loadAiEndpointConfig();
    if (!config.model) {
      this.setAiConnectionStatus("error", "Pick a model first.");
      return;
    }
    const result = await checkOpenAICompatibleServer(config);
    if (!result.ok) {
      this.applyAiServerCheckResult(result);
      return;
    }
    if (result.models.length > 0) this.aiModels = result.models;
    const match = result.models.find((model) => model.id === config.model);
    const maxContextTokens = match?.contextLength || resolveAiContextTokens({ ...config, maxContextTokens: 0 });
    if (!maxContextTokens) {
      this.setAiConnectionStatus("ok", `Connected to ${result.baseUrl}, but no max context tokens were reported for ${config.model}.`, "ok", result.baseUrl);
      return;
    }
    saveAiEndpointConfig({ ...config, maxContextTokens });
    this.syncSettingsTextBufferFromConfig("aiMaxContextTokens");
    this.setAiConnectionStatus("ok", `Max context: ${maxContextTokens} tokens for ${config.model}.`, "ok", result.baseUrl);
  }

  private applyAiServerCheckResult(result: AiServerCheckResult): void {
    if (result.ok) {
      this.aiModels = result.models;
      this.setAiConnectionStatus("ok", result.message, "ok", result.baseUrl);
      return;
    }
    this.setAiConnectionStatus("error", result.message, "error", result.baseUrl);
  }

  private setAiConnectionStatus(state: AiConnectionStatus["state"], message: string, endpointFieldState?: AiEndpointFieldState, baseUrl?: string): void {
    this.aiConnectionStatus = {
      state,
      message,
      baseUrl,
      checkedAt: state === "idle" || state === "checking" ? undefined : Date.now()
    };
    if (endpointFieldState !== undefined) this.aiEndpointFieldState = endpointFieldState;
    this.statusText = message;
    this.scheduleDraw();
  }

  private resetSettings(): void {
    this.settings = { ...DEFAULT_SETTINGS };
    this.aiModels = [];
    this.aiConnectionStatus = { state: "idle", message: "" };
    this.aiEndpointFieldState = null;
    saveAiEndpointConfig(DEFAULT_AI_ENDPOINT_CONFIG);
    resetAiPromptStorage();
    this.syncAllSettingsTextBuffersFromConfig();
    this.reloadOpenAiSpecialDocuments();
    this.settingsScrollY = 0;
    this.resetSettingsExpansion();
    this.saveAndApplySettings();
    this.statusText = "Settings reset";
  }

  private syncAllSettingsTextBuffersFromConfig(): void {
    this.syncSettingsTextBufferFromConfig("aiBaseUrl");
    this.syncSettingsTextBufferFromConfig("aiApiKey");
    this.syncSettingsTextBufferFromConfig("aiModel");
    this.syncSettingsTextBufferFromConfig("aiMaxContextTokens");
  }

  private reloadOpenAiSpecialDocuments(): void {
    this.replaceOpenAiDocument(AI_SETTINGS_DOC_PATH, JSON.stringify(loadAiEndpointConfig(), null, 2));
    this.replaceOpenAiDocument(AI_SYSTEM_PROMPT_DOC_PATH, loadAiSystemPrompt());
    this.replaceOpenAiDocument(AI_TAG_TOOL_PROMPT_DOC_PATH, loadAiTagToolPrompt());
    this.replaceOpenAiDocument(AI_HARMONY_TOOL_PROMPT_DOC_PATH, loadAiHarmonyToolPrompt());
    this.replaceOpenAiDocument(AI_COMPACT_PROMPT_DOC_PATH, loadAiCompactPrompt());
  }

  private replaceOpenAiDocument(path: string, text: string): void {
    const doc = this.docs.getByPath(path);
    if (!doc) return;
    doc.selectAll();
    doc.replaceSelection(text, "virtual");
    doc.setSelection({ line: 0, col: 0 });
    doc.markSaved();
  }

  private resetSettingsExpansion(): void {
    this.settingsExpanded.clear();
    this.settingsExpanded.add("visual");
    this.settingsExpanded.add("interface");
    this.settingsExpanded.add("ai");
  }

  private async clearFileSystemNow(): Promise<void> {
    await this.vfs.resetToEmpty();
    this.clearPersistedEditorSession();
    this.docs.clear();
    const group = makeGroup("group-main");
    this.groups = [group];
    this.dockRoot = { type: "leaf", group };
    this.activeGroupId = group.id;
    this.activeDocId = null;
    this.openTabs = [];
    this.scrollStates.clear();
    this.tabScrollStates.clear();
    this.pendingTabRevealIds.clear();
    this.documentWidthCache.clear();
    this.lineWidthCache.clear();
    this.highlightCache.clear();
    this.findStates.clear();
    this.untitledLabels.clear();
    this.untitledPreferredNames.clear();
    this.selectedFileTreePath = null;
    this.expandedFolders.clear();
    this.knownFolders.clear();
    this.filesScrollY = 0;
    this.searchScrollY = 0;
    this.pendingCloseQueue = [];
    this.pendingDownloadDirtyQueue = [];
    await this.refreshFiles();
    this.input.blur();
    this.statusText = "File system cleared";
  }

  private async clearChatNow(): Promise<void> {
    this.chat.clear();
    await this.chat.persist();
    this.chatScrollY = 0;
    this.chatInputScrollY = 0;
    this.statusText = "Chat cleared";
  }

  private copyTextToClipboard(text: string): void {
    this.localClipboard = text;
    void copyText(text);
  }

  private async readTextFromClipboard(): Promise<string | null> {
    if (isMobileWebKit()) return this.localClipboard;
    const text = await readClipboardText();
    if (text === null) return this.localClipboard || null;
    if (text) this.localClipboard = text;
    return text || this.localClipboard;
  }

  private openSystemCopyDialog(text: string, restoreFocus: () => void): void {
    this.localClipboard = text;
    this.openSystemClipboardDialog({
      title: "System Copy",
      message: "Direct clipboard access on iOS is not available from this WebGL editor. Use the text field below with the system text menu to copy.",
      value: text,
      okLabel: "OK",
      selectText: true,
      restoreFocus,
      onOk: (value) => {
        this.localClipboard = value;
        this.statusText = "System copy text shown";
        this.scheduleDraw();
      }
    });
  }

  private openSystemPasteDialog(onPaste: (text: string) => void, restoreFocus: () => void): void {
    this.openSystemClipboardDialog({
      title: "System Paste",
      message: "Direct clipboard access on iOS is not available from this WebGL editor. Paste into the text field below, then tap OK.",
      value: "",
      okLabel: "OK",
      selectText: false,
      restoreFocus,
      onOk: (value) => {
        if (!value) {
          this.statusText = "Clipboard empty";
          this.scheduleDraw();
          return;
        }
        this.localClipboard = value;
        onPaste(value);
      }
    });
  }

  private openSystemClipboardDialog(options: {
    title: string;
    message: string;
    value: string;
    okLabel: string;
    selectText: boolean;
    restoreFocus: () => void;
    onOk: (value: string) => void;
  }): void {
    this.closeSystemClipboardDialog();
    this.closeSystemFileUploadDialog();
    this.viewport.setVisualViewportCanvasResizeEnabled(false);
    this.input.blur();
    const overlay = document.createElement("div");
    overlay.className = "system-clipboard-overlay";
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-modal", "true");
    this.applySystemClipboardTheme(overlay);

    const dialog = document.createElement("div");
    dialog.className = "system-clipboard-dialog";
    const title = document.createElement("h2");
    title.textContent = options.title;
    const message = document.createElement("p");
    message.textContent = options.message;
    const textarea = document.createElement("textarea");
    textarea.className = "system-clipboard-field";
    textarea.value = options.value;
    textarea.autocapitalize = "off";
    textarea.autocomplete = "off";
    textarea.spellcheck = false;
    textarea.setAttribute("autocorrect", "off");

    const actions = document.createElement("div");
    actions.className = "system-clipboard-actions";
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "system-clipboard-button secondary";
    cancel.textContent = "Cancel";
    const ok = document.createElement("button");
    ok.type = "button";
    ok.className = "system-clipboard-button primary";
    ok.textContent = options.okLabel;
    actions.append(cancel, ok);
    dialog.append(title, message, textarea, actions);
    overlay.append(dialog);
    document.body.append(overlay);
    this.systemClipboardOverlay = overlay;
    this.systemClipboardViewportCleanup = this.installSystemClipboardViewportSync(overlay);

    let closed = false;
    const close = () => {
      if (closed) return;
      closed = true;
      this.closeSystemClipboardDialog();
      options.restoreFocus();
      this.scheduleDraw();
    };
    cancel.addEventListener("click", close);
    ok.addEventListener("click", () => {
      const value = textarea.value;
      close();
      options.onOk(value);
    });
    overlay.addEventListener("pointerdown", (event) => {
      if (event.target === overlay) close();
    });
    overlay.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      event.preventDefault();
      close();
    });

    window.setTimeout(() => {
      textarea.focus({ preventScroll: true });
      if (options.selectText) textarea.select();
    });
  }

  private applySystemClipboardTheme(overlay: HTMLElement): void {
    overlay.style.setProperty("--system-clipboard-overlay-bg", colorToCss(this.settings.theme === "light" ? theme.text : theme.background, 0.48));
    overlay.style.setProperty("--system-clipboard-panel", colorToCss(theme.panel2, 0.99));
    overlay.style.setProperty("--system-clipboard-field-bg", colorToCss(theme.background));
    overlay.style.setProperty("--system-clipboard-divider", colorToCss(theme.divider));
    overlay.style.setProperty("--system-clipboard-text", colorToCss(theme.text));
    overlay.style.setProperty("--system-clipboard-text-dim", colorToCss(theme.textDim));
    overlay.style.setProperty("--system-clipboard-accent", colorToCss(theme.accent));
    overlay.style.setProperty("--system-clipboard-secondary", colorToCss(theme.activityActive));
    overlay.style.setProperty("--system-clipboard-button-text", colorToCss(this.settings.theme === "light" ? theme.panel2 : theme.text));
  }

  private installSystemClipboardViewportSync(overlay: HTMLElement): () => void {
    const sync = () => {
      const vv = window.visualViewport;
      const left = vv?.offsetLeft ?? 0;
      const top = vv?.offsetTop ?? 0;
      const width = vv?.width ?? window.innerWidth;
      const height = vv?.height ?? window.innerHeight;
      overlay.style.left = `${left}px`;
      overlay.style.top = `${top}px`;
      overlay.style.width = `${Math.max(1, width)}px`;
      overlay.style.height = `${Math.max(1, height)}px`;
      overlay.style.setProperty("--system-clipboard-width", `${Math.max(1, width)}px`);
      overlay.style.setProperty("--system-clipboard-height", `${Math.max(1, height)}px`);
    };
    sync();
    window.addEventListener("resize", sync);
    window.visualViewport?.addEventListener("resize", sync);
    window.visualViewport?.addEventListener("scroll", sync);
    return () => {
      window.removeEventListener("resize", sync);
      window.visualViewport?.removeEventListener("resize", sync);
      window.visualViewport?.removeEventListener("scroll", sync);
      this.viewport.setVisualViewportCanvasResizeEnabled(true);
    };
  }

  private closeSystemClipboardDialog(): void {
    this.systemClipboardOverlay?.remove();
    this.systemClipboardOverlay = null;
    this.systemClipboardViewportCleanup?.();
    this.systemClipboardViewportCleanup = null;
  }

  private async runFileContextMenuCommand(path: string, command: ContextMenuCommand): Promise<void> {
    if (!isFileContextMenuCommand(command)) return;
    if (command === "rename") {
      this.startRename(path);
      return;
    }
    if (command === "duplicate") {
      await this.duplicateFile(path);
      return;
    }
    await this.deleteFile(path);
  }

  private async runFolderContextMenuCommand(path: string, command: ContextMenuCommand): Promise<void> {
    if (!isFolderContextMenuCommand(command)) return;
    if (command === "rename") {
      this.startRename(path);
      return;
    }
    if (command === "delete") {
      await this.requestDeleteFolder(path);
      return;
    }
    if (command === "createFile") {
      this.primeRenameKeyboardForTouch();
      await this.createFileInFolder(path);
      return;
    }
    if (command === "createFolder") {
      this.primeRenameKeyboardForTouch();
      await this.createFolderInFolder(path);
      return;
    }
    this.requestFileUpload(path);
  }

  private async runRootContextMenuCommand(command: ContextMenuCommand): Promise<void> {
    if (command === "createFile") {
      this.primeRenameKeyboardForTouch();
      await this.createFileInFolder("/");
    } else if (command === "createFolder") {
      this.primeRenameKeyboardForTouch();
      await this.createFolderInFolder("/");
    } else if (command === "uploadFile") {
      this.requestFileUpload("/");
    }
  }

  private async duplicateFile(path: string): Promise<void> {
    const source = normalizePath(path);
    const node = await this.vfs.stat(source);
    if (!node || node.kind !== "file") {
      this.statusText = `File not found: ${source}`;
      return;
    }
    const copyPath = await this.nextDuplicatePath(source);
    const data = await this.vfs.readFile(source);
    await this.vfs.writeFile(copyPath, data, node.mime ?? "application/octet-stream");
    await this.refreshFiles();
    this.statusText = `Duplicated ${copyPath}`;
    this.scheduleDraw();
  }

  private async deleteFile(path: string): Promise<void> {
    const target = normalizePath(path);
    if (this.renamePath === target) this.cancelRename();
    this.clearFileTreeSelectionUnder(target);
    const doc = this.docs.getByPath(target);
    if (doc) this.closeTab(doc.id);
    await this.vfs.remove(target);
    this.docs.removePath(target);
    await this.refreshFiles();
    this.syncOpenTabs();
    this.statusText = `Deleted ${target}`;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.scheduleDraw();
  }

  private async requestDeleteFolder(path: string): Promise<void> {
    const target = normalizePath(path);
    if (target === "/") return;
    const node = await this.vfs.stat(target);
    if (!node || node.kind !== "dir") {
      this.statusText = `Folder not found: ${target}`;
      this.scheduleDraw();
      return;
    }
    const children = await this.vfs.listDir(target);
    if (children.length > 0) {
      this.openDeleteFolderModal(target, children.length);
      return;
    }
    await this.deleteFolderNow(target);
  }

  private async deleteFolderNow(path: string): Promise<void> {
    const target = normalizePath(path);
    if (target === "/") return;
    if (this.renamePath && isSameOrDescendant(this.renamePath, target)) this.cancelRename();
    this.clearFileTreeSelectionUnder(target);
    const docs = this.docs.all().filter((doc) => doc.path && isSameOrDescendant(doc.path, target));
    for (const doc of docs) {
      this.closeTab(doc.id);
      if (doc.path) this.docs.removePath(doc.path);
    }
    await this.vfs.remove(target, { recursive: true });
    this.removeFolderExpansion(target);
    await this.refreshFiles();
    this.syncOpenTabs();
    this.statusText = `Deleted ${target}`;
    if (this.activeDoc()) this.focusEditor();
    else this.input.blur();
    this.scheduleDraw();
  }

  private async createFileInFolder(folderPath: string): Promise<void> {
    const parent = normalizePath(folderPath);
    const path = await this.nextCreatedPath(parent, "file");
    await this.vfs.writeFile(path, "", "text/plain");
    this.expandedFolders.add(parent);
    await this.refreshFiles();
    this.statusText = `Created ${path}`;
    this.selectFileTreePath(path);
    this.startRename(path);
  }

  private async createFolderInFolder(folderPath: string): Promise<void> {
    const parent = normalizePath(folderPath);
    const path = await this.nextCreatedPath(parent, "folder");
    await this.vfs.mkdir(path);
    this.expandedFolders.add(parent);
    this.expandedFolders.add(path);
    await this.refreshFiles();
    this.statusText = `Created ${path}`;
    this.selectFileTreePath(path);
    this.startRename(path);
  }

  private requestFileUpload(folderPath: string): void {
    this.uploadTargetFolder = normalizePath(folderPath);
    if (isIOSDevice()) {
      this.openSystemFileUploadDialog(this.uploadTargetFolder);
      return;
    }
    const input = this.ensureUploadInput();
    input.value = "";
    input.click();
  }

  private openSystemFileUploadDialog(folderPath: string): void {
    this.closeSystemFileUploadDialog();
    this.closeSystemClipboardDialog();
    this.viewport.setVisualViewportCanvasResizeEnabled(false);
    this.input.blur();
    const targetFolder = normalizePath(folderPath);
    const overlay = document.createElement("div");
    overlay.className = "system-clipboard-overlay system-file-upload-overlay";
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-modal", "true");
    this.applySystemClipboardTheme(overlay);

    const dialog = document.createElement("div");
    dialog.className = "system-clipboard-dialog system-file-upload-dialog";
    const title = document.createElement("h2");
    title.textContent = "Upload File";
    const message = document.createElement("p");
    message.textContent = targetFolder === "/"
      ? "Choose one or more files to upload to the workspace root, then tap OK."
      : `Choose one or more files to upload to ${targetFolder}, then tap OK.`;
    const input = document.createElement("input");
    input.className = "system-file-upload-field";
    input.type = "file";
    input.multiple = true;
    const status = document.createElement("p");
    status.className = "system-file-upload-status";
    status.textContent = "No files selected";

    const actions = document.createElement("div");
    actions.className = "system-clipboard-actions";
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "system-clipboard-button secondary";
    cancel.textContent = "Cancel";
    const ok = document.createElement("button");
    ok.type = "button";
    ok.className = "system-clipboard-button primary";
    ok.textContent = "OK";
    ok.disabled = true;
    actions.append(cancel, ok);
    dialog.append(title, message, input, status, actions);
    overlay.append(dialog);
    document.body.append(overlay);
    this.systemFileUploadOverlay = overlay;
    this.systemFileUploadViewportCleanup = this.installSystemFileUploadViewportSync(overlay);

    const close = () => {
      this.closeSystemFileUploadDialog();
      this.scheduleDraw();
    };
    input.addEventListener("change", () => {
      const count = input.files?.length ?? 0;
      ok.disabled = count === 0;
      status.textContent = count === 0 ? "No files selected" : count === 1 ? input.files![0]!.name : `${count} files selected`;
    });
    cancel.addEventListener("click", close);
    ok.addEventListener("click", () => {
      const files = input.files ? Array.from(input.files) : [];
      if (files.length === 0) {
        status.textContent = "Choose at least one file before tapping OK.";
        ok.disabled = true;
        return;
      }
      close();
      void this.uploadFilesToFolder(files, targetFolder);
    });
    overlay.addEventListener("pointerdown", (event) => {
      if (event.target === overlay) event.preventDefault();
    });
    overlay.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      event.preventDefault();
      close();
    });
  }

  private closeSystemFileUploadDialog(): void {
    this.systemFileUploadOverlay?.remove();
    this.systemFileUploadOverlay = null;
    this.systemFileUploadViewportCleanup?.();
    this.systemFileUploadViewportCleanup = null;
  }

  private installSystemFileUploadViewportSync(overlay: HTMLElement): () => void {
    const cleanup = this.installSystemClipboardViewportSync(overlay);
    return () => cleanup();
  }

  private async uploadFilesToFolder(files: File[], folderPath: string): Promise<void> {
    const parent = normalizePath(folderPath);
    const written: string[] = [];
    for (const file of files) {
      const target = await this.nextUploadPath(parent, file.name);
      await this.vfs.writeFile(target, new Uint8Array(await file.arrayBuffer()), file.type || guessMime(target));
      written.push(target);
    }
    if (written.length === 0) return;
    this.expandedFolders.add(parent);
    await this.refreshFiles();
    this.statusText = written.length === 1 ? `Uploaded ${written[0]}` : `Uploaded ${written.length} files`;
    this.scheduleDraw();
  }

  private async nextUploadPath(folderPath: string, fileName: string): Promise<string> {
    const name = sanitizeUploadedFileName(fileName);
    const candidate = joinPath(folderPath, name);
    if (!await this.vfs.stat(candidate)) return candidate;
    const dot = name.lastIndexOf(".");
    const hasExtension = dot > 0;
    const stem = hasExtension ? name.slice(0, dot) : name;
    const ext = hasExtension ? name.slice(dot) : "";
    for (let index = 2; index < 1000; index++) {
      const next = joinPath(folderPath, `${stem} ${index}${ext}`);
      if (!await this.vfs.stat(next)) return next;
    }
    return joinPath(folderPath, `${stem}-${shortHexName()}${ext}`);
  }

  private async nextCreatedPath(folderPath: string, kind: "file" | "folder"): Promise<string> {
    for (let attempt = 0; attempt < 20; attempt++) {
      const name = kind === "file" ? `${shortHexName()}.txt` : shortHexName();
      const candidate = joinPath(folderPath, name);
      if (!await this.vfs.stat(candidate)) return candidate;
    }
    return joinPath(folderPath, `${Date.now().toString(36)}${kind === "file" ? ".txt" : ""}`);
  }

  private async nextDuplicatePath(path: string): Promise<string> {
    const dir = dirname(path);
    const name = basename(path);
    const dot = name.lastIndexOf(".");
    const hasExtension = dot > 0;
    const stem = hasExtension ? name.slice(0, dot) : name;
    const ext = hasExtension ? name.slice(dot) : "";
    for (let index = 1; index < 1000; index++) {
      const suffix = index === 1 ? " copy" : ` copy ${index}`;
      const candidate = joinPath(dir, `${stem}${suffix}${ext}`);
      if (!await this.vfs.stat(candidate)) return candidate;
    }
    return joinPath(dir, `${stem} copy ${Date.now().toString(36)}${ext}`);
  }

  private runGlobalShortcut(command: string): boolean {
    if (command === "Mod+F") {
      this.openFindWidget();
      return true;
    }
    if (command === "Mod+S") {
      const doc = this.activeDoc();
      if (doc?.readOnly) {
        this.statusText = "File type not supported";
        this.scheduleDraw();
        return true;
      }
      if (doc) void this.saveDocument(doc).then((path) => {
        this.statusText = `Saved ${path}`;
        this.scheduleDraw();
      });
      return true;
    }
    if (command === "Mod+Shift+F") {
      this.sidebarMode = "search";
      if (this.sidebarWidth === 0) this.sidebarWidth = this.lastSidebarWidth || 280;
      this.focusMiniTarget("search", { x: this.ui(56), y: this.ui(48), w: this.ui(220), h: this.ui(24) });
      return true;
    }
    if (command === "Mod+B") {
      if (this.sidebarWidth > 0) {
        this.lastSidebarWidth = this.sidebarWidth;
        this.sidebarWidth = 0;
      } else {
        this.sidebarWidth = this.lastSidebarWidth || 280;
      }
      this.scheduleDraw();
      return true;
    }
    if (command === "Mod+`") {
      this.sidebarMode = "chat";
      if (this.sidebarWidth === 0) this.sidebarWidth = this.lastSidebarWidth || 280;
      this.focusMiniTarget("chat", this.chatInputRectForFocus());
      return true;
    }
    return false;
  }

  private async runSearch(): Promise<void> {
    const query = this.searchBuffer.text.trim();
    if (!query) {
      this.searchResults = [];
      this.searchScrollY = 0;
      this.scheduleDraw();
      return;
    }
    const files = await this.vfs.listAllFiles();
    const results: Array<{ path: string; line: number; text: string }> = [];
    for (const file of files) {
      if (file.encoding === "binary" || file.path.startsWith("/.slug-") || isUnsupportedFilePath(file.path)) continue;
      const text = this.docs.getByPath(file.path)?.getText() ?? await this.vfs.readText(file.path);
      const lines = text.split("\n");
      for (let i = 0; i < lines.length; i++) {
        if (lines[i]!.toLowerCase().includes(query.toLowerCase())) {
          results.push({ path: file.path, line: i, text: lines[i]! });
          if (results.length >= 200) break;
        }
      }
      if (results.length >= 200) break;
    }
    this.searchResults = results;
    this.searchScrollY = 0;
    this.statusText = `${results.length} results`;
    this.scheduleDraw();
  }

  private async replaceAllInWorkspace(): Promise<void> {
    const query = this.searchBuffer.text;
    if (!query) {
      this.statusText = "No search query";
      this.scheduleDraw();
      return;
    }
    const replacement = this.projectReplaceBuffer.text;
    const files = await this.vfs.listAllFiles();
    const targetGroup = this.activeGroup();
    let fileCount = 0;
    let replacementCount = 0;
    let firstChangedDocId: string | null = null;
    for (const file of files) {
      if (file.encoding === "binary" || file.path.startsWith("/.slug-") || isUnsupportedFilePath(file.path)) continue;
      const openDoc = this.docs.getByPath(file.path);
      if (openDoc?.readOnly) continue;
      const sourceText = openDoc?.getText() ?? await this.vfs.readText(file.path);
      const replaced = replaceAllPlainText(sourceText, query, replacement);
      if (replaced.text === sourceText) continue;
      const doc = openDoc ?? await this.docs.open(file.path);
      fileCount++;
      replacementCount += replaced.count;
      doc.selectAll();
      doc.replaceSelection(replaced.text, "replaceAll");
      if (!this.groupContaining(doc.id)) {
        targetGroup.tabs.push(doc.id);
        if (!targetGroup.activeDocId) {
          targetGroup.activeDocId = doc.id;
          this.activeGroupId = targetGroup.id;
          this.activeDocId = doc.id;
        }
      }
      firstChangedDocId ??= doc.id;
    }
    if (!this.activeDocId && firstChangedDocId) {
      targetGroup.activeDocId = firstChangedDocId;
      this.activeGroupId = targetGroup.id;
      this.activeDocId = firstChangedDocId;
    }
    this.syncOpenTabs();
    await this.runSearch();
    this.statusText = `Replaced ${replacementCount} match${replacementCount === 1 ? "" : "es"} in ${fileCount} unsaved file${fileCount === 1 ? "" : "s"}`;
    this.scheduleDraw();
  }

  private openFindWidget(): void {
    const doc = this.activeDoc();
    if (!doc) return;
    const state = this.findStateForDoc(doc.id);
    if (!state) return;
    state.open = true;
    const selection = doc.selectedText();
    if (selection && !selection.includes("\n")) {
      state.findBuffer.text = selection;
      state.findBuffer.cursor = selection.length;
      state.findBuffer.anchor = state.findBuffer.cursor;
    }
    const rect = this.textFieldRect("find") ?? this.findFieldFallbackRect();
    this.focusTextField("find", rect);
    if (state.findBuffer.text) this.selectDocumentFindMatch(1, true);
    this.scheduleDraw();
  }

  private closeFindWidget(): void {
    const state = this.activeFindState(false);
    if (state) state.open = false;
    this.textFieldSelecting = null;
    this.focusEditor();
    this.scheduleDraw();
  }

  private selectDocumentFindMatch(direction: 1 | -1, fromCurrent = false): void {
    const doc = this.activeDoc();
    const query = this.activeFindState(false)?.findBuffer.text ?? "";
    const matches = doc ? this.documentFindMatches(doc, query) : [];
    if (!doc || matches.length === 0) {
      this.statusText = query ? "No matches" : "Find";
      this.scheduleDraw();
      return;
    }
    const head = this.offsetForPosition(doc, doc.selection.head);
    const ordered = doc.getOrderedSelection();
    const selectionStart = this.offsetForPosition(doc, ordered.start);
    const selectionEnd = this.offsetForPosition(doc, ordered.end);
    let current = matches.findIndex((match) => match.start === selectionStart && match.end === selectionEnd);
    if (current < 0) {
      current = direction > 0
        ? matches.findIndex((match) => match.start >= head)
        : findLastIndex(matches, (match) => match.end <= head);
      if (current < 0) current = direction > 0 ? 0 : matches.length - 1;
    } else if (!fromCurrent) {
      current = (current + direction + matches.length) % matches.length;
    }
    const match = matches[current]!;
    doc.setSelection(this.positionForOffset(doc, match.start), this.positionForOffset(doc, match.end));
    this.ensureCaretVisible(doc, this.activeEditorRect());
    this.statusText = `${current + 1} of ${matches.length}`;
    this.scheduleDraw();
  }

  private replaceCurrentFindMatch(): void {
    const doc = this.activeDoc();
    const state = this.activeFindState(false);
    const query = state?.findBuffer.text ?? "";
    if (!doc || !query) return;
    if (doc.readOnly) {
      this.statusText = "File type not supported";
      this.scheduleDraw();
      return;
    }
    const selected = doc.selectedText();
    if (!textEqualsFindQuery(selected, query)) {
      this.selectDocumentFindMatch(1);
      return;
    }
    doc.replaceSelection(state?.replaceBuffer.text ?? "", "replace");
    this.selectDocumentFindMatch(1, true);
    this.revealEditorCaret();
  }

  private replaceAllInActiveDocument(): void {
    const doc = this.activeDoc();
    const state = this.activeFindState(false);
    const query = state?.findBuffer.text ?? "";
    if (!doc || !query) return;
    if (doc.readOnly) {
      this.statusText = "File type not supported";
      this.scheduleDraw();
      return;
    }
    const replaced = replaceAllPlainText(doc.getText(), query, state?.replaceBuffer.text ?? "");
    if (replaced.count === 0) {
      this.statusText = "No matches";
      this.scheduleDraw();
      return;
    }
    doc.selectAll();
    doc.replaceSelection(replaced.text, "replaceAll");
    this.statusText = `Replaced ${replaced.count} match${replaced.count === 1 ? "" : "es"}`;
    this.selectDocumentFindMatch(1, true);
    this.revealEditorCaret();
  }

  private documentFindMatches(doc: TextDocument, query: string): Array<{ start: number; end: number }> {
    if (!query) return [];
    const text = doc.getText();
    const haystack = text.toLowerCase();
    const needle = query.toLowerCase();
    const matches: Array<{ start: number; end: number }> = [];
    let index = 0;
    while (index <= haystack.length) {
      const found = haystack.indexOf(needle, index);
      if (found < 0) break;
      matches.push({ start: found, end: found + query.length });
      index = found + Math.max(1, query.length);
    }
    return matches;
  }

  private offsetForPosition(doc: TextDocument, pos: { line: number; col: number }): number {
    let offset = 0;
    for (let line = 0; line < pos.line; line++) offset += doc.lines[line]!.length + 1;
    return offset + pos.col;
  }

  private positionForOffset(doc: TextDocument, offset: number): { line: number; col: number } {
    let remaining = clamp(offset, 0, doc.getText().length);
    for (let line = 0; line < doc.lines.length; line++) {
      const text = doc.lines[line]!;
      if (remaining <= text.length) return { line, col: remaining };
      remaining -= text.length + 1;
    }
    const last = doc.lines.length - 1;
    return { line: last, col: doc.lines[last]!.length };
  }

  private findFieldFallbackRect(): Rect {
    const rect = this.activeEditorRect();
    return { x: rect.x + Math.max(12, rect.w - this.ui(380)), y: rect.y + this.ui(10), w: this.ui(170), h: this.ui(28) };
  }

  private aiRuntimeSettings(): AiRuntimeSettings {
    return {
      maxToolCallsPerTurn: this.settings.aiMaxToolCalls,
      detectDuplicateToolCalls: this.settings.aiDetectDuplicateToolCalls,
      toolCallFormat: this.settings.aiToolCallFormat,
      thinkingFormat: "auto",
      compactFreePercent: this.settings.aiCompactFreePercent
    };
  }

  private editorContextBundle(): ContextBundle {
    const activeDoc = this.activeDoc();
    const activePath = activeDoc?.path && !this.isAiSpecialPath(activeDoc.path) ? normalizePath(activeDoc.path) : undefined;
    const openDocs = this.docs.all().filter((doc) => !this.isAiSpecialDoc(doc));
    const context: ContextBundle = {
      selectedText: activeDoc && !this.isAiSpecialDoc(activeDoc) ? activeDoc.selectedText() : "",
      openPaths: openDocs.map((doc) => doc.path ? normalizePath(doc.path) : this.untitledLabels.get(doc.id) ?? "Untitled"),
      openFileNames: openDocs.map((doc) => doc.path ? basename(doc.path) : this.untitledLabels.get(doc.id) ?? "Untitled"),
      fileTreePaths: this.treeNodes.map((node) => `${normalizePath(node.path)}${node.kind === "dir" ? "/" : ""}`),
      selectedFileTreePath: this.fileTreeSelectedPath() ?? undefined
    };
    if (activePath) context.activePath = activePath;
    return context;
  }

  private async handleAiWorkspaceChange(change: AiWorkspaceChange): Promise<void> {
    if (change.type === "write") {
      const path = normalizePath(change.path);
      this.expandFileTreeAncestors(path);
      let text = change.text;
      const doc = this.docs.getByPath(path);
      if (doc && !doc.readOnly && text === undefined && !isUnsupportedFilePath(path)) {
        try {
          text = await this.vfs.readText(path);
        } catch {
          text = undefined;
        }
      }
      this.syncOpenDocumentFromWorkspace(path, text);
      await this.refreshFiles();
      this.syncOpenTabs();
      this.scheduleDraw();
      return;
    }

    if (change.type === "mkdir") {
      const path = normalizePath(change.path);
      this.expandFileTreeAncestors(path, true);
      await this.refreshFiles();
      this.scheduleDraw();
      return;
    }

    if (change.type === "remove") {
      const path = normalizePath(change.path);
      if (this.renamePath && this.workspaceRemoveAffectsPath(this.renamePath, path, change.recursive)) this.cancelRename();
      this.clearFileTreeSelectionUnder(path);
      const docs = this.docs.all().filter((doc) => doc.path && this.workspaceRemoveAffectsPath(doc.path, path, change.recursive));
      for (const doc of docs) {
        this.closeTab(doc.id);
        if (doc.path) this.docs.removePath(doc.path);
        this.clearDocumentCaches(doc.id);
      }
      if (path === "/" && change.recursive) {
        this.docs.clear();
        this.resetEditorSession();
      }
      this.removeFolderExpansion(path);
      await this.refreshFiles();
      this.syncOpenTabs();
      if (this.activeDoc()) this.focusEditor();
      else this.input.blur();
      this.scheduleDraw();
      return;
    }

    const oldPath = normalizePath(change.oldPath);
    const newPath = normalizePath(change.newPath);
    if (this.renamePath && isSameOrDescendant(this.renamePath, oldPath)) this.cancelRename();
    const node = await this.vfs.stat(newPath);
    if (node?.kind === "dir") {
      for (const doc of this.docs.all()) {
        if (!doc.path || !isSameOrDescendant(doc.path, oldPath)) continue;
        const nextPath = doc.path === oldPath ? newPath : joinPath(newPath, doc.path.slice(oldPath.length + 1));
        const renamed = this.docs.renamePath(doc.path, nextPath);
        if (renamed) this.clearDocumentCaches(renamed.id);
      }
      this.remapFolderExpansion(oldPath, newPath);
    } else {
      const renamed = this.docs.renamePath(oldPath, newPath);
      if (renamed) this.clearDocumentCaches(renamed.id);
    }
    this.remapFileTreeSelection(oldPath, newPath);
    this.expandFileTreeAncestors(newPath, node?.kind === "dir");
    await this.refreshFiles();
    this.syncOpenTabs();
    this.scheduleDraw();
  }

  private syncOpenDocumentFromWorkspace(path: string, text: string | undefined): void {
    const doc = this.docs.getByPath(path);
    if (!doc || doc.readOnly || text === undefined) return;
    if (doc.getText() !== text) {
      doc.selectAll();
      doc.replaceSelection(text, "agent");
    }
    doc.markSaved();
    this.clearDocumentCaches(doc.id);
    const group = this.groupContaining(doc.id);
    if (group) this.ensureCaretVisible(doc, group.editorRect);
  }

  private expandFileTreeAncestors(path: string, includeSelf = false): void {
    let current = includeSelf ? normalizePath(path) : dirname(path);
    while (true) {
      this.expandedFolders.add(current);
      if (current === "/") break;
      current = dirname(current);
    }
  }

  private workspaceRemoveAffectsPath(path: string, removedPath: string, recursive: boolean): boolean {
    const normalizedPath = normalizePath(path);
    const normalizedRemoved = normalizePath(removedPath);
    if (normalizedRemoved === "/") return recursive || normalizedPath === "/";
    return normalizedPath === normalizedRemoved || (recursive && isSameOrDescendant(normalizedPath, normalizedRemoved));
  }

  private async sendChat(): Promise<void> {
    if (this.chat.running) return;
    const text = this.chatDraft.getText().trim();
    if (!text) return;
    this.chatDraft.selectAll();
    this.chatDraft.replaceSelection("");
    this.chatDraft.markSaved();
    this.chatInputScrollY = 0;
    this.chatScrollY = Number.MAX_SAFE_INTEGER;
    this.statusText = "Sending chat turn";
    await this.chat.send(text, this.activeDoc(), this.docs.all(), {
      runtime: this.aiRuntimeSettings(),
      editorContext: this.settings.aiInsertEditorContext ? this.editorContextBundle() : null,
      onUpdate: () => this.scheduleDraw(),
      onCompactStart: () => this.openCompactingModal(),
      onCompactEnd: () => this.closeCompactingModal(),
      onToolCallLimit: (limit, used) => this.openToolCallLimitModal(limit, used),
      onDuplicateToolCall: (call) => this.openDuplicateToolCallModal(call),
      onWorkspaceChange: (change) => this.handleAiWorkspaceChange(change)
    });
    this.chatScrollY = Number.MAX_SAFE_INTEGER;
    const latest = this.chat.visibleMessages().at(-1);
    this.statusText = latest?.role === "system" && latest.text === "Turn canceled." ? "Chat turn canceled" : "Chat turn complete";
    this.scheduleDraw();
  }

  private runChatSendControl(): void {
    if (this.chat.running) {
      this.statusText = "Stopping chat turn";
      this.chat.cancel();
      this.scheduleDraw();
      return;
    }
    void this.sendChat();
  }

  private draw(): void {
    this.viewport.resizeCanvas(this.renderer.gl);
    this.renderer.setViewport(this.viewport.get());
    this.renderer.beginFrame();
    this.hits.length = 0;
    this.settingsViewportRect = null;
    this.focusedSettingsInputRect = null;
    const vp = this.viewport.get();
    const activityW = this.ui(48);
    const statusH = this.ui(24);
    const sidebarW = this.sidebarWidth;
    const mainX = activityW + sidebarW;
    this.renderer.rect({ x: 0, y: 0, w: vp.cssWidth, h: vp.cssHeight }, theme.background);
    this.drawActivityBar({ x: 0, y: 0, w: activityW, h: vp.cssHeight - statusH });
    if (sidebarW > 0) this.drawSidebar({ x: activityW, y: 0, w: sidebarW, h: vp.cssHeight - statusH });
    this.drawEditorArea({ x: mainX, y: 0, w: vp.cssWidth - mainX, h: vp.cssHeight - statusH });
    if (sidebarW > 0) this.drawSidebarSplitter({ x: activityW + sidebarW - this.ui(3), y: 0, w: this.ui(6), h: vp.cssHeight - statusH });
    this.drawStatus({ x: 0, y: vp.cssHeight - statusH, w: vp.cssWidth, h: statusH });
    this.applyPendingFocusedInputReveal();
    if (this.fileDragActive) this.drawFileDropOverlay({ x: 0, y: 0, w: vp.cssWidth, h: vp.cssHeight });
    if (this.contextMenu) this.drawContextMenu();
    if (this.modal) this.drawModal();
    this.renderer.endFrame();
    this.scheduleCaretBlinkFrame();
  }

  private drawFileDropOverlay(rect: Rect): void {
    this.renderer.rect(rect, [theme.accent[0], theme.accent[1], theme.accent[2], 0.18]);
    const inset = this.ui(14);
    this.drawRectOutline({ x: rect.x + inset, y: rect.y + inset, w: rect.w - inset * 2, h: rect.h - inset * 2 }, theme.accent);
  }

  private drawActivityBar(rect: Rect): void {
    this.renderer.rect(rect, theme.activity);
    const items: Array<{ mode: SidebarMode; label: string; y: number }> = [
      { mode: "files", label: "📂", y: rect.y + this.ui(6) },
      { mode: "search", label: "🔍", y: rect.y + this.ui(56) },
      { mode: "chat", label: "💬", y: rect.y + this.ui(106) }
    ];
    for (const item of items) {
      const r = { x: rect.x + this.ui(6), y: item.y, w: rect.w - this.ui(12), h: this.ui(36) };
      const active = this.sidebarWidth > 0 && this.sidebarMode === item.mode;
      if (active) this.renderer.rect(r, theme.activityActive);
      else if (this.hoveredActivityButton === item.mode) this.renderer.rect(r, activityHoverColor());
      this.drawCenteredText(item.label, r, this.buttonTextColor(true, this.hoveredActivityButton === item.mode), "title");
      this.hits.push({ type: "activity", mode: item.mode, rect: r });
    }
    const settingsRect = { x: rect.x + this.ui(6), y: rect.y + rect.h - this.ui(46), w: rect.w - this.ui(12), h: this.ui(36) };
    const downloadRect = { x: rect.x + this.ui(6), y: settingsRect.y - this.ui(46), w: rect.w - this.ui(12), h: this.ui(36) };
    if (this.hoveredActivityButton === "download") this.renderer.rect(downloadRect, activityHoverColor());
    this.drawCenteredText("📥", downloadRect, this.buttonTextColor(!this.downloadInProgress, this.hoveredActivityButton === "download"), "title");
    this.hits.push({ type: "downloadActivity", rect: downloadRect });
    const settingsActive = this.sidebarWidth > 0 && this.sidebarMode === "settings";
    if (settingsActive) this.renderer.rect(settingsRect, theme.activityActive);
    else if (this.hoveredActivityButton === "settings") this.renderer.rect(settingsRect, activityHoverColor());
    this.drawCenteredText("⚙️", settingsRect, this.buttonTextColor(true, this.hoveredActivityButton === "settings"), "title");
    this.hits.push({ type: "settingsActivity", rect: settingsRect });
  }

  private drawSidebar(rect: Rect): void {
    this.renderer.rect(rect, theme.panel);
    if (this.sidebarMode === "files") this.drawFilesPanel(rect);
    else if (this.sidebarMode === "search") this.drawSearchPanel(rect);
    else if (this.sidebarMode === "settings") this.drawSettingsPanel(rect);
    else this.drawChatPanel(rect);
  }

  private drawSidebarSplitter(rect: Rect): void {
    this.renderer.rect({ x: rect.x + this.ui(2), y: rect.y, w: 1, h: rect.h }, this.resizingSidebar ? theme.accent : theme.divider);
    this.hits.push({ type: "sidebarResize", rect });
  }

  private drawPanelHeader(rect: Rect, title: string): Rect {
    const headerH = this.ui(PANEL_HEADER_H);
    const header = { x: rect.x, y: rect.y, w: rect.w, h: headerH };
    this.renderer.rect(header, theme.panel2);
    this.renderer.rect({ x: header.x, y: header.y + header.h - 1, w: header.w, h: 1 }, theme.divider);
    this.renderer.text(title, header.x + this.ui(12), header.y + this.ui(9), theme.textDim, "ui");
    return this.sidebarPanelBodyRect(rect);
  }

  private drawFilesPanel(rect: Rect): void {
    const body = this.drawPanelHeader(rect, "FILES");
    const maxScroll = this.maxSidebarScrollY("files", body);
    this.filesScrollY = clamp(this.filesScrollY, 0, maxScroll);
    const hasScrollbar = maxScroll > 0;
    const contentBody = hasScrollbar ? { ...body, w: Math.max(0, body.w - this.editorScrollbarSize()) } : body;
    this.hits.push({ type: "filesRoot", rect: body });
    this.hits.push({ type: "filesRoot", rect: { x: rect.x, y: rect.y, w: rect.w, h: this.ui(PANEL_HEADER_H) } });
    this.renderer.pushClip(body);
    this.drawFileTreeEntries(this.fileTreeEntries(), contentBody, body.y + this.ui(8) - this.filesScrollY, 0, body);
    this.renderer.popClip();
    if (hasScrollbar) this.drawSidebarScrollbar("files", body, this.fileTreeContentHeight(), this.filesScrollY);
  }

  private drawFileTreeEntries(entries: FileTreeEntry[], body: Rect, y: number, depth: number, clip: Rect): number {
    const indent = this.ui(14);
    const rowH = this.ui(22);
    const rowGap = this.ui(2);
    for (const entry of entries) {
      if (y > clip.y + clip.h) break;
      const row = { x: body.x + this.ui(4), y, w: body.w - this.ui(8), h: rowH };
      const contentX = row.x + this.ui(6) + depth * indent;
      const visibleRow = intersectRect(row, clip);
      if (entry.type === "dir") {
        const expanded = this.expandedFolders.has(entry.path);
        if (visibleRow) {
          const selected = entry.path === this.fileTreeSelectedPath();
          const hovered = entry.path === this.hoveredFileTreePath;
          if (selected) this.renderer.rect(row, theme.panel2);
          else if (hovered) this.renderer.rect(row, this.hoverControlColor(theme.panel));
          const textColor = selected || hovered ? this.buttonTextColor(true, hovered) : theme.text;
          this.renderer.text(expanded ? "v" : ">", contentX, row.y + this.ui(4), selected || hovered ? textColor : theme.textDim, "ui");
          this.hits.push({ type: "folder", path: entry.path, expanded, rect: visibleRow });
          if (entry.path === this.renamePath) {
            this.drawFileRenameRow(entry.path, { x: contentX + this.ui(14), y: row.y, w: Math.max(this.ui(40), body.x + body.w - contentX - this.ui(14)), h: row.h }, clip);
          } else {
            this.drawClippedText(entry.name, { x: contentX + this.ui(14), y: row.y, w: Math.max(0, body.x + body.w - contentX - this.ui(14)), h: row.h }, row.y + this.ui(4), textColor, "ui");
          }
        }
        y += rowH + rowGap;
        if (expanded) y = this.drawFileTreeEntries(entry.children, body, y, depth + 1, clip);
        continue;
      }
      if (visibleRow) {
        const selected = entry.path === this.fileTreeSelectedPath();
        const hovered = entry.path === this.hoveredFileTreePath;
        if (selected) this.renderer.rect(row, theme.panel2);
        else if (hovered) this.renderer.rect(row, this.hoverControlColor(theme.panel));
        const textColor = selected || hovered ? this.buttonTextColor(true, hovered) : theme.text;
        this.hits.push({ type: "file", path: entry.path, rect: visibleRow });
        if (entry.path === this.renamePath) {
          this.drawFileRenameRow(entry.path, { x: contentX - this.ui(4), y: row.y, w: Math.max(this.ui(40), body.x + body.w - contentX), h: row.h }, clip);
        } else {
          this.drawClippedText(entry.name, { x: contentX, y: row.y, w: Math.max(0, body.x + body.w - contentX), h: row.h }, row.y + this.ui(4), textColor, "ui");
        }
      }
      y += rowH + rowGap;
    }
    return y;
  }

  private drawFileRenameRow(path: string, row: Rect, hitClip?: Rect): void {
    const input = { x: row.x + this.ui(4), y: row.y + 1, w: row.w - this.ui(8), h: row.h - 2 };
    const invalidRanges = invalidFileNameCharacterRanges(this.renameBuffer.text);
    const invalid = !isValidFileName(this.renameBuffer.text.trim());
    const border = invalid ? theme.error : theme.accent;
    this.renderer.rect(input, theme.activity);
    this.drawRectOutline(input, border);
    const padX = this.ui(5);
    this.revealMiniBufferCaret(this.renameBuffer, input, padX);
    const content = this.miniBufferContentRect(input, padX);
    const textX = content.x - this.renameBuffer.scrollX;
    const textY = input.y + this.ui(3);
    const selectionStart = Math.min(this.renameBuffer.anchor, this.renameBuffer.cursor);
    const selectionEnd = Math.max(this.renameBuffer.anchor, this.renameBuffer.cursor);
    const beforeSelection = this.renameBuffer.text.slice(0, selectionStart);
    const selected = this.renameBuffer.text.slice(selectionStart, selectionEnd);
    this.renderer.pushClip(content);
    if (selectionEnd > selectionStart) {
      const sx = textX + this.renderer.measureText(beforeSelection, "ui");
      const sw = Math.max(2, this.renderer.measureText(selected, "ui"));
      this.renderer.rect({ x: sx, y: input.y + this.ui(2), w: sw, h: input.h - this.ui(4) }, theme.selection);
    }
    if (this.renameBuffer.text) this.drawTextWithInvalidCharacterHighlights(this.renameBuffer.text, invalidRanges, textX, textY);
    else this.renderer.text("file name", textX, textY, theme.textDim, "ui");
    if (this.isRenameCaretVisible()) {
      const caretX = textX + this.renderer.measureText(this.renameBuffer.text.slice(0, this.renameBuffer.cursor), "ui");
      this.renderer.rect({ x: caretX, y: input.y + this.ui(3), w: 1.5, h: input.h - this.ui(6) }, theme.caret);
    }
    this.renderer.popClip();
    const hitRect = hitClip ? intersectRect(input, hitClip) : input;
    if (hitRect) this.hits.push({ type: "fileRenameInput", path, rect: hitRect });
    this.drawMiniBufferSelectionHandles({ type: "rename", path }, this.renameBuffer, input, padX, hitClip);
  }

  private drawTextWithInvalidCharacterHighlights(text: string, invalidRanges: Array<{ start: number; end: number }>, x: number, y: number): void {
    if (invalidRanges.length === 0) {
      this.renderer.text(text, x, y, theme.text, "ui");
      return;
    }
    let cursor = 0;
    let drawX = x;
    for (const range of invalidRanges) {
      if (range.start > cursor) {
        const chunk = text.slice(cursor, range.start);
        drawX += this.renderer.text(chunk, drawX, y, theme.text, "ui");
      }
      const invalid = text.slice(range.start, range.end);
      drawX += this.renderer.text(invalid, drawX, y, theme.error, "ui");
      cursor = range.end;
    }
    if (cursor < text.length) this.renderer.text(text.slice(cursor), drawX, y, theme.text, "ui");
  }

  private drawSettingsPanel(rect: Rect): void {
    const body = this.drawPanelHeader(rect, "SETTINGS");
    this.hits.push({ type: "settingsRoot", rect: { x: rect.x, y: rect.y, w: rect.w, h: this.ui(PANEL_HEADER_H) } });
    this.drawSettingsContent(body);
  }

  private drawSearchPanel(rect: Rect): void {
    const body = this.drawPanelHeader(rect, "SEARCH");
    const toggle = { x: body.x + this.ui(10), y: body.y + this.ui(8), w: this.ui(28), h: this.ui(28) };
    const refresh = { x: body.x + body.w - this.ui(10) - this.ui(28), y: toggle.y, w: this.ui(28), h: this.ui(28) };
    const input = { x: toggle.x + toggle.w + this.ui(6), y: toggle.y, w: Math.max(this.ui(60), refresh.x - toggle.x - toggle.w - this.ui(12)), h: toggle.h };
    this.drawIconButton(toggle, this.searchReplaceExpanded ? "v" : ">", true, "ui", this.isButtonHovered("searchReplaceToggle"));
    this.hits.push({ type: "searchReplaceToggle", rect: toggle });
    this.drawSearchInput(input);
    this.drawIconButton(refresh, "🔎", true, "ui", this.isButtonHovered("searchRefresh"));
    this.hits.push({ type: "searchRefresh", rect: refresh });
    let y = input.y + this.ui(42);
    if (this.searchReplaceExpanded) {
      const buttonW = this.ui(94);
      const replaceInput = { x: toggle.x, y, w: Math.max(this.ui(60), body.w - this.ui(20) - buttonW - this.ui(8)), h: input.h };
      const button = { x: replaceInput.x + replaceInput.w + this.ui(8), y, w: buttonW, h: input.h };
      this.drawTextFieldInput("projectReplace", replaceInput, "replace");
      this.drawButton(button, "Replace All", Boolean(this.searchBuffer.text), this.isButtonHovered("searchReplaceAll"));
      this.hits.push({ type: "searchReplaceAll", rect: button, enabled: Boolean(this.searchBuffer.text) });
      y += this.ui(42);
    }
    const resultsViewport = this.searchResultsViewport(body);
    const maxScroll = this.maxSidebarScrollY("search", resultsViewport);
    this.searchScrollY = clamp(this.searchScrollY, 0, maxScroll);
    const hasScrollbar = maxScroll > 0;
    const resultsBody = hasScrollbar ? { ...resultsViewport, w: Math.max(0, resultsViewport.w - this.editorScrollbarSize()) } : resultsViewport;
    this.renderer.pushClip(resultsViewport);
    y = resultsViewport.y - this.searchScrollY;
    for (const result of this.searchResults) {
      if (y > resultsViewport.y + resultsViewport.h) break;
      const row = { x: resultsBody.x + this.ui(8), y, w: resultsBody.w - this.ui(16), h: this.ui(38) };
      const visibleRow = intersectRect(row, resultsViewport);
      if (visibleRow) {
        this.drawClippedText(`${result.path}:${result.line + 1}`, { x: row.x + this.ui(4), y: row.y, w: Math.max(0, row.w - this.ui(8)), h: this.ui(18) }, row.y + this.ui(2), theme.accent, "ui");
        this.drawClippedText(result.text.trim().slice(0, 80), { x: row.x + this.ui(4), y: row.y + this.ui(18), w: Math.max(0, row.w - this.ui(8)), h: this.ui(18) }, row.y + this.ui(18), theme.textDim, "ui");
        this.hits.push({ type: "searchResult", path: result.path, line: result.line, rect: visibleRow });
      }
      y += this.ui(42);
    }
    this.renderer.popClip();
    if (hasScrollbar) this.drawSidebarScrollbar("search", resultsViewport, this.searchResultsContentHeight(), this.searchScrollY);
  }

  private drawSearchInput(input: Rect): void {
    this.drawTextFieldInput("search", input, "type to search");
  }

  private drawTextFieldInput(field: TextFieldKey, input: Rect, placeholder: string, pushHit = true): void {
    const buffer = this.bufferForTextField(field);
    const active = this.input.activeTarget?.kind === field;
    const border = this.textFieldBorderColor(field, active);
    this.renderer.rect(input, active ? theme.activity : theme.panel2);
    this.drawRectOutline(input, border);
    const padX = this.ui(8);
    if (active) this.revealMiniBufferCaret(buffer, input, padX);
    else this.clampMiniBufferScroll(buffer, input, padX);
    const content = this.miniBufferContentRect(input, padX);
    const textX = content.x - buffer.scrollX;
    const textY = input.y + this.ui(7);
    const selectionStart = Math.min(buffer.anchor, buffer.cursor);
    const selectionEnd = Math.max(buffer.anchor, buffer.cursor);
    const beforeSelection = buffer.text.slice(0, selectionStart);
    const selected = buffer.text.slice(selectionStart, selectionEnd);
    this.renderer.pushClip(content);
    if (selectionEnd > selectionStart) {
      const sx = textX + this.renderer.measureText(beforeSelection, "ui");
      const sw = Math.max(2, this.renderer.measureText(selected, "ui"));
      this.renderer.rect({ x: sx, y: input.y + this.ui(3), w: sw, h: input.h - this.ui(6) }, theme.selection);
    }
    this.renderer.text(buffer.text || placeholder, textX, textY, buffer.text ? theme.text : theme.textDim, "ui");
    if (this.isTextFieldCaretVisible(field)) {
      const caretX = textX + this.renderer.measureText(buffer.text.slice(0, buffer.cursor), "ui");
      this.renderer.rect({ x: caretX, y: input.y + this.ui(5), w: 1.5, h: input.h - this.ui(10) }, theme.caret);
    }
    this.renderer.popClip();
    if (pushHit) this.hits.push({ type: "textField", field, rect: input });
    this.drawMiniBufferSelectionHandles({ type: "textField", field }, buffer, input, padX);
  }

  private textFieldBorderColor(field: TextFieldKey, active: boolean): Color {
    if (active) return theme.accent;
    if ((field === "aiBaseUrl" || field === "aiApiKey") && this.aiEndpointFieldState) {
      return this.aiEndpointFieldState === "ok" ? theme.accent2 : theme.error;
    }
    return theme.divider;
  }

  private drawIconButton(rect: Rect, label: string, enabled: boolean, font: FontName = "ui", hovered = false): void {
    this.renderer.rect(rect, this.buttonFill(enabled, hovered));
    this.drawRectOutline(rect, theme.divider);
    this.drawCenteredText(label, rect, this.buttonTextColor(enabled, hovered), font);
  }

  private drawButton(rect: Rect, label: string, enabled: boolean, hovered = false): void {
    this.renderer.rect(rect, this.buttonFill(enabled, hovered));
    this.drawRectOutline(rect, enabled ? theme.divider : theme.panel);
    this.drawCenteredText(label, rect, this.buttonTextColor(enabled, hovered), "ui");
  }

  private buttonFill(enabled: boolean, hovered: boolean, base: Color = theme.activityActive): Color {
    if (!enabled) return theme.panel2;
    return hovered ? this.hoverControlColor(base) : base;
  }

  private buttonTextColor(enabled: boolean, hovered: boolean): Color {
    if (!enabled) return theme.textDim;
    if (!hovered) return theme.text;
    return this.settings.theme === "light" ? [0.02, 0.03, 0.04, 1] : [0.98, 0.99, 1, 1];
  }

  private hoverControlColor(base: Color): Color {
    const amount = this.settings.theme === "light" ? -0.07 : 0.08;
    return [
      clamp(base[0] + amount, 0, 1),
      clamp(base[1] + amount, 0, 1),
      clamp(base[2] + amount, 0, 1),
      base[3]
    ];
  }

  private drawChatPanel(rect: Rect): void {
    const body = this.drawPanelHeader(rect, "CHAT");
    this.hits.push({ type: "chatRoot", rect: { x: rect.x, y: rect.y, w: rect.w, h: this.ui(PANEL_HEADER_H) } });
    const layout = this.chatPanelLayoutFromBody(body);
    this.drawChatTranscript(layout.transcript);
    this.drawChatInput(layout.input);
    const label = this.chat.running ? "Stop" : "Send";
    const enabled = this.chat.running || Boolean(this.chatDraft.getText().trim());
    this.drawButton(layout.send, label, enabled, this.isButtonHovered("chatSend"));
    this.hits.push({ type: "chatSend", rect: layout.send, enabled, label });
    this.drawChatShowThinkingControl(layout.showThinking);
  }

  private chatPanelLayoutFromBody(body: Rect): { transcript: Rect; input: Rect; send: Rect; showThinking: Rect } {
    const pad = this.ui(10);
    const gap = this.ui(8);
    const sendH = this.ui(30);
    const inputH = this.chatInputPreferredHeight();
    const rowX = body.x + pad;
    const rowW = Math.max(1, body.w - pad * 2);
    const rowY = body.y + Math.max(pad, body.h - pad - sendH);
    const labelW = this.renderer.measureText("Show thinking", "ui");
    const boxSize = this.ui(12);
    const preferredThinkingW = Math.max(this.ui(104), boxSize + labelW + this.ui(14));
    const thinkingW = Math.min(preferredThinkingW, Math.max(this.ui(74), rowW - this.ui(64) - gap));
    const sendW = Math.max(1, rowW - thinkingW - gap);
    const showThinking = { x: rowX, y: rowY, w: thinkingW, h: sendH };
    const send = { x: showThinking.x + showThinking.w + gap, y: rowY, w: sendW, h: sendH };
    const input = {
      x: body.x + pad,
      y: Math.max(body.y + pad, send.y - gap - inputH),
      w: Math.max(1, body.w - pad * 2),
      h: Math.max(this.ui(48), send.y - gap - Math.max(body.y + pad, send.y - gap - inputH))
    };
    const transcript = {
      x: body.x + pad,
      y: body.y + pad,
      w: Math.max(1, body.w - pad * 2),
      h: Math.max(1, input.y - body.y - pad - gap)
    };
    return { transcript, input, send, showThinking };
  }

  private drawChatShowThinkingControl(rect: Rect): void {
    const hovered = this.isButtonHovered("chatShowThinking");
    if (hovered) this.renderer.rect(rect, this.hoverControlColor(theme.activity));
    const boxSize = this.ui(12);
    const box = { x: rect.x + this.ui(3), y: rect.y + (rect.h - boxSize) / 2, w: boxSize, h: boxSize };
    this.renderer.rect(box, this.settings.showThinking ? theme.activityActive : theme.panel2);
    this.drawRectOutline(box, theme.divider);
    if (this.settings.showThinking) this.drawCenteredText("✔️", box, this.buttonTextColor(true, hovered), "mini");
    const textY = rect.y + (rect.h - this.renderer.lineHeight("ui")) / 2;
    this.drawClippedText("Show thinking", { x: box.x + box.w + this.ui(5), y: rect.y, w: Math.max(0, rect.x + rect.w - box.x - box.w - this.ui(5)), h: rect.h }, textY, hovered ? this.buttonTextColor(true, true) : theme.textDim, "ui");
    this.hits.push({ type: "chatShowThinking", rect });
  }

  private chatInputPreferredHeight(): number {
    return this.renderer.lineHeight("ui") * 4 + this.ui(14);
  }

  private chatInputRectForSidebar(sidebarRect: Rect): Rect {
    return this.chatPanelLayoutFromBody(this.sidebarPanelBodyRect(sidebarRect)).input;
  }

  private chatInputRectForFocus(): Rect {
    const hit = this.hits.find((candidate): candidate is Extract<HitItem, { type: "chatInput" }> => candidate.type === "chatInput");
    if (hit) return hit.rect;
    const vp = this.viewport.get();
    return this.chatInputRectForSidebar({ x: this.ui(48), y: 0, w: Math.max(this.ui(160), this.sidebarWidth || this.lastSidebarWidth), h: vp.cssHeight - this.ui(24) });
  }

  private chatDisplayMessages(): ChatMessage[] {
    const messages = this.chat.visibleMessages();
    return this.settings.showThinking ? messages : messages.filter((msg) => msg.role !== "thinking");
  }

  private drawChatTranscript(viewport: Rect): void {
    const scrollbarSize = this.editorScrollbarSize();
    const messages = this.chatDisplayMessages();
    this.pruneChatLineCache(messages);
    let contentWidth = viewport.w;
    let contentHeight = this.chatTranscriptContentHeight(contentWidth, messages);
    const hasScrollbar = contentHeight > viewport.h;
    if (hasScrollbar) {
      contentWidth = Math.max(1, viewport.w - scrollbarSize);
      contentHeight = this.chatTranscriptContentHeight(contentWidth, messages);
    }
    this.chatScrollY = clamp(this.chatScrollY, 0, Math.max(0, contentHeight - viewport.h));
    const content = { x: viewport.x, y: viewport.y, w: contentWidth, h: viewport.h };
    this.hits.push({ type: "chatTranscript", rect: viewport });
    this.renderer.pushClip(content);
    let y = viewport.y + this.ui(4) - this.chatScrollY;
    const lineH = this.renderer.lineHeight("ui");
    const bubblePad = this.ui(8);
    const gap = this.ui(8);
    const contentBottom = content.y + content.h;
    for (const msg of messages) {
      const lines = this.chatMessageLinesCached(msg, Math.max(1, content.w - bubblePad * 2));
      const bubbleH = this.ui(26) + lines.length * lineH + bubblePad;
      if (y >= contentBottom) break;
      if (y + bubbleH <= content.y) {
        y += bubbleH + gap;
        continue;
      }
      const bubble = { x: content.x + this.ui(2), y, w: Math.max(1, content.w - this.ui(4)), h: bubbleH };
      const visibleBubble = intersectRect(bubble, content);
      if (visibleBubble) {
        this.hits.push({ type: "chatBubble", messageId: msg.id, rect: visibleBubble, viewportRect: viewport });
        const colors = this.chatRoleColors(msg.role, msg.ok);
        this.renderer.rect(visibleBubble, colors.fill);
        this.drawRectOutlineClipped(bubble, content, colors.outline);
        const label = msg.name ? `${this.chatRoleLabel(msg.role)}: ${msg.name}` : this.chatRoleLabel(msg.role);
        const labelY = bubble.y + this.ui(7);
        const stickyHeaderH = lineH + this.ui(10);
        let textClipTop = content.y;
        if (labelY + lineH >= content.y && labelY <= contentBottom) {
          this.renderer.text(label, bubble.x + bubblePad, labelY, colors.label, "ui");
        } else if (bubble.y < content.y && bubble.y + bubble.h > content.y + stickyHeaderH) {
          const header = intersectRect({ x: bubble.x, y: content.y, w: bubble.w, h: stickyHeaderH }, content);
          if (header) {
            this.renderer.rect(header, colors.fill);
            this.renderer.rect({ x: header.x, y: header.y + header.h - 1, w: header.w, h: 1 }, colors.outline);
            this.renderer.text(label, bubble.x + bubblePad, content.y + this.ui(5), colors.label, "ui");
            textClipTop = header.y + header.h;
          }
        }
        const textStartY = bubble.y + this.ui(25);
        const firstLineOffset = (textClipTop - textStartY) / lineH;
        const firstLine = Math.max(0, textClipTop > content.y ? Math.ceil(firstLineOffset) : Math.floor(firstLineOffset));
        const lastLine = Math.min(lines.length, Math.ceil((contentBottom - textStartY) / lineH) + 1);
        for (let i = firstLine; i < lastLine; i++) {
          this.renderer.text(lines[i]!, bubble.x + bubblePad, textStartY + i * lineH, colors.text, "ui");
        }
      }
      y += bubbleH + gap;
    }
    this.renderer.popClip();
    if (hasScrollbar) this.drawChatScrollbar("chatTranscript", viewport, contentHeight, this.chatScrollY);
  }

  private drawChatInput(input: Rect): void {
    const active = this.input.activeTarget?.kind === "chat";
    const metrics = this.chatInputMetrics(input);
    const { content, contentHeight, hasScrollbar, viewport, visualLines } = metrics;
    this.chatInputScrollY = clamp(this.chatInputScrollY, 0, Math.max(0, contentHeight - viewport.h));
    this.renderer.rect(input, active ? theme.activity : theme.panel2);
    this.drawRectOutline(input, active ? theme.accent : theme.divider);
    const lineH = this.renderer.lineHeight("ui");
    const doc = this.chatDraft;
    const selection = doc.getOrderedSelection();
    this.renderer.pushClip(content);
    if (!doc.getText()) {
      this.renderer.text("ask about the workspace", content.x, content.y + this.ui(7), theme.textDim, "ui");
    }
    const firstLine = Math.max(0, Math.floor(this.chatInputScrollY / lineH));
    const visibleLines = Math.ceil(content.h / lineH) + 2;
    for (let i = 0; i < visibleLines; i++) {
      const visualIndex = firstLine + i;
      const visualLine = visualLines[visualIndex];
      if (!visualLine) break;
      const y = content.y + visualIndex * lineH - this.chatInputScrollY + this.ui(4);
      this.drawChatInputSelectionForLine(visualLine, content.x, y, lineH, selection);
      if (visualLine.text) this.renderer.text(visualLine.text, content.x, y, theme.text, "ui");
    }
    if (active && (this.input.composing || this.isCaretBlinkOn())) {
      const caret = this.chatInputCaretRect(input);
      this.renderer.rect(caret, theme.caret);
      if (this.input.composing && this.input.compositionText) this.renderer.text(this.input.compositionText, caret.x + 2, caret.y, theme.warning, "ui");
    }
    this.renderer.popClip();
    this.hits.push({ type: "chatInput", rect: input });
    this.drawChatInputSelectionHandles(input, content);
    if (hasScrollbar) this.drawChatScrollbar("chatInput", input, contentHeight, this.chatInputScrollY);
  }

  private drawChatInputSelectionForLine(visualLine: ChatInputVisualLine, x: number, y: number, lineH: number, selection: { start: { line: number; col: number }; end: { line: number; col: number } }): void {
    const lineIndex = visualLine.line;
    if (selection.start.line > lineIndex || selection.end.line < lineIndex) return;
    const line = this.chatDraft.lines[lineIndex] ?? "";
    const start = Math.max(visualLine.start, selection.start.line === lineIndex ? selection.start.col : 0);
    const end = Math.min(visualLine.end, selection.end.line === lineIndex ? selection.end.col : line.length);
    if (end <= start) return;
    const startX = x + this.renderer.measureText(line.slice(visualLine.start, start), "ui");
    const endX = x + this.renderer.measureText(line.slice(visualLine.start, end), "ui");
    this.renderer.rect({ x: startX, y: y - this.ui(2), w: Math.max(2, endX - startX), h: lineH }, theme.selection);
  }

  private chatInputContentRect(input: Rect): Rect {
    const pad = this.ui(8);
    return { x: input.x + pad, y: input.y + this.ui(3), w: Math.max(1, input.w - pad * 2), h: Math.max(1, input.h - this.ui(6)) };
  }

  private chatInputMetrics(input: Rect): { viewport: Rect; content: Rect; visualLines: ChatInputVisualLine[]; contentHeight: number; hasScrollbar: boolean } {
    const scrollbarSize = this.editorScrollbarSize();
    let viewport = input;
    let content = this.chatInputContentRect(viewport);
    let visualLines = this.chatInputVisualLines(content.w);
    let contentHeight = this.chatInputContentHeightForVisualLines(visualLines);
    const hasScrollbar = contentHeight > input.h;
    if (hasScrollbar) {
      viewport = { ...input, w: Math.max(1, input.w - scrollbarSize) };
      content = this.chatInputContentRect(viewport);
      visualLines = this.chatInputVisualLines(content.w);
      contentHeight = this.chatInputContentHeightForVisualLines(visualLines);
    }
    return { viewport, content, visualLines, contentHeight, hasScrollbar };
  }

  private chatInputVisualLines(width: number): ChatInputVisualLine[] {
    const result: ChatInputVisualLine[] = [];
    for (let line = 0; line < this.chatDraft.lineCount(); line++) {
      result.push(...this.wrapChatInputLine(line, width));
    }
    return result.length ? result : [{ line: 0, start: 0, end: 0, text: "" }];
  }

  private wrapChatInputLine(lineIndex: number, width: number): ChatInputVisualLine[] {
    const text = this.chatDraft.lines[lineIndex] ?? "";
    if (!text) return [{ line: lineIndex, start: 0, end: 0, text: "" }];
    const result: ChatInputVisualLine[] = [];
    const maxWidth = Math.max(1, width);
    let start = 0;
    while (start < text.length) {
      let end = start;
      let x = 0;
      let lastBreak = -1;
      while (end < text.length) {
        const codePoint = text.codePointAt(end) ?? 0;
        const char = String.fromCodePoint(codePoint);
        const next = end + char.length;
        const advance = this.renderer.measureText(char, "ui");
        if (x + advance > maxWidth && end > start) {
          if (lastBreak > start) end = lastBreak;
          break;
        }
        x += advance;
        end = next;
        if (/\s/.test(char)) lastBreak = next;
      }
      if (end <= start) {
        const codePoint = text.codePointAt(start) ?? 0;
        end = start + String.fromCodePoint(codePoint).length;
      }
      result.push({ line: lineIndex, start, end, text: text.slice(start, end) });
      start = end;
    }
    return result;
  }

  private chatInputContentHeightForVisualLines(visualLines: ChatInputVisualLine[]): number {
    return Math.max(1, visualLines.length * this.renderer.lineHeight("ui") + this.ui(8));
  }

  private chatInputContentHeight(): number {
    const input = this.chatInputRectForFocus();
    return this.chatInputMetrics(input).contentHeight;
  }

  private chatInputVisualPositionForDocPosition(pos: Position, visualLines: ChatInputVisualLine[]): { index: number; line: ChatInputVisualLine } {
    const clamped = this.chatDraft.clampPosition(pos);
    let fallbackIndex = 0;
    for (let i = 0; i < visualLines.length; i++) {
      const line = visualLines[i]!;
      if (line.line !== clamped.line) continue;
      fallbackIndex = i;
      if (line.start === line.end && clamped.col === line.start) return { index: i, line };
      if (clamped.col >= line.start && clamped.col < line.end) return { index: i, line };
    }
    for (let i = visualLines.length - 1; i >= 0; i--) {
      const line = visualLines[i]!;
      if (line.line === clamped.line && clamped.col === line.end) return { index: i, line };
    }
    return { index: fallbackIndex, line: visualLines[fallbackIndex] ?? { line: clamped.line, start: 0, end: 0, text: "" } };
  }

  private chatTranscriptContentHeight(width: number, messages = this.chatDisplayMessages()): number {
    const lineH = this.renderer.lineHeight("ui");
    const bubblePad = this.ui(8);
    const gap = this.ui(8);
    let h = this.ui(4);
    for (const msg of messages) {
      const lines = this.chatMessageLinesCached(msg, Math.max(1, width - this.ui(4) - bubblePad * 2));
      h += this.ui(26) + lines.length * lineH + bubblePad + gap;
    }
    return Math.max(1, h);
  }

  private chatMessageLinesCached(msg: ChatMessage, width: number): string[] {
    const widthKey = Math.round(width * 100) / 100;
    const text = msg.text;
    const first = text.length > 0 ? text.charCodeAt(0) : 0;
    const last = text.length > 0 ? text.charCodeAt(text.length - 1) : 0;
    const key = `${widthKey}:${this.settings.uiScale}:${this.renderer.lineHeight("ui")}:${text.length}:${first}:${last}`;
    const cached = this.chatLineCache.get(msg.id);
    if (cached?.key === key) return cached.lines;
    const lines = this.chatMessageLines(text, width);
    this.chatLineCache.set(msg.id, { key, lines });
    return lines;
  }

  private pruneChatLineCache(messages: ChatMessage[]): void {
    if (this.chatLineCache.size <= messages.length + 8) return;
    const ids = new Set(messages.map((msg) => msg.id));
    for (const id of this.chatLineCache.keys()) {
      if (!ids.has(id)) this.chatLineCache.delete(id);
    }
  }

  private chatMessageLines(text: string, width: number): string[] {
    const lines: string[] = [];
    for (const rawLine of text.split("\n")) {
      const wrapped = this.wrapTextForWidth(rawLine || " ", width, "ui");
      lines.push(...wrapped);
    }
    return lines.length ? lines : [""];
  }

  private chatRoleLabel(role: string): string {
    if (role === "tool_call") return "TOOL CALL";
    if (role === "tool_result") return "TOOL RESULT";
    return role.toUpperCase();
  }

  private chatRoleColors(role: string, ok?: boolean): { fill: Color; outline: Color; label: Color; text: Color } {
    if (role === "user") return { fill: [theme.accent[0], theme.accent[1], theme.accent[2], 0.20], outline: [theme.accent[0], theme.accent[1], theme.accent[2], 0.45], label: theme.accent, text: theme.text };
    if (role === "system") {
      if (ok === false) return { fill: [theme.error[0], theme.error[1], theme.error[2], 0.14], outline: [theme.error[0], theme.error[1], theme.error[2], 0.40], label: theme.error, text: theme.text };
      return { fill: [theme.activityActive[0], theme.activityActive[1], theme.activityActive[2], 0.72], outline: theme.divider, label: theme.textDim, text: theme.textDim };
    }
    if (role === "thinking") return { fill: [theme.keyword[0], theme.keyword[1], theme.keyword[2], 0.15], outline: [theme.keyword[0], theme.keyword[1], theme.keyword[2], 0.42], label: theme.keyword, text: theme.textDim };
    if (role === "tool_call") return { fill: [theme.number[0], theme.number[1], theme.number[2], 0.16], outline: [theme.number[0], theme.number[1], theme.number[2], 0.40], label: theme.number, text: theme.text };
    if (role === "tool_result") {
      const accent = ok === false ? theme.error : theme.string;
      return { fill: [accent[0], accent[1], accent[2], 0.14], outline: [accent[0], accent[1], accent[2], 0.40], label: accent, text: theme.text };
    }
    return { fill: theme.panel2, outline: theme.divider, label: theme.textDim, text: theme.text };
  }

  private drawChatScrollbar(panel: ChatScrollbarPanel, viewport: Rect, contentHeight: number, scrollY: number): void {
    const size = this.editorScrollbarSize();
    const trackRect = { x: viewport.x + viewport.w - size, y: viewport.y, w: size, h: viewport.h };
    const active = this.chatScrollbarDrag?.panel === panel;
    const hovered = this.hoveredChatScrollbar?.panel === panel;
    this.renderer.rect(trackRect, hovered || active
      ? [theme.activity[0], theme.activity[1], theme.activity[2], 0.90]
      : [theme.activity[0], theme.activity[1], theme.activity[2], 0.82]);
    const maxScroll = Math.max(0, contentHeight - viewport.h);
    const thumbRect = this.chatScrollbarThumb(viewport, trackRect, contentHeight, scrollY, maxScroll);
    const thumbColor: Color = active
      ? [0.34, 0.41, 0.50, 1]
      : hovered
        ? [0.28, 0.31, 0.36, 1]
        : theme.activityActive;
    this.renderer.rect(thumbRect, thumbColor);
    this.hits.push({ type: "chatScrollbar", panel, rect: trackRect, trackRect, thumbRect, viewportRect: viewport, contentHeight });
  }

  private chatScrollbarThumb(viewport: Rect, trackRect: Rect, contentHeight: number, scrollY: number, maxScroll: number): Rect {
    const thumbH = clamp((viewport.h / Math.max(1, contentHeight)) * trackRect.h, Math.min(trackRect.h, this.ui(EDITOR_SCROLLBAR_THUMB_MIN)), trackRect.h);
    const thumbTravel = Math.max(1, trackRect.h - thumbH);
    return { x: trackRect.x + this.ui(3), y: trackRect.y + (maxScroll > 0 ? (scrollY / maxScroll) * thumbTravel : 0), w: Math.max(this.ui(3), trackRect.w - this.ui(6)), h: thumbH };
  }

  private drawEditorArea(rect: Rect): void {
    this.renderer.rect(rect, theme.background);
    this.editorRect = rect;
    this.layoutDockNode(this.dockRoot, rect);
    if (this.tabDrag) {
      this.drawDockOverlay();
      if (this.tabInsertionPreview) this.drawTabInsertionPreview();
      this.drawDraggedTabGhost();
    }
  }

  private layoutDockNode(node: DockNode, rect: Rect): void {
    if (node.type === "leaf") {
      this.drawEditorGroup(node.group, rect);
      return;
    }
    const gap = DOCK_SPLITTER_GAP;
    const count = Math.max(1, node.children.length);
    const weights = normalizeSplitWeights(node);
    const totalWeight = weights.reduce((sum, weight) => sum + weight, 0) || 1;
    const splitters: Array<{ index: number; divider: Rect; hit: Rect }> = [];
    if (node.direction === "row") {
      const usableWidth = Math.max(1, rect.w - gap * (count - 1));
      let x = rect.x;
      for (let i = 0; i < node.children.length; i++) {
        const w = i === node.children.length - 1 ? Math.max(1, rect.x + rect.w - x) : Math.max(1, usableWidth * weights[i]! / totalWeight);
        this.layoutDockNode(node.children[i]!, { x, y: rect.y, w, h: rect.h });
        if (i < node.children.length - 1) {
          const divider = { x: x + w, y: rect.y, w: gap, h: rect.h };
          splitters.push({
            index: i,
            divider,
            hit: { x: divider.x - (DOCK_SPLITTER_HIT_SIZE - gap) / 2, y: rect.y, w: DOCK_SPLITTER_HIT_SIZE, h: rect.h }
          });
        }
        x += w + gap;
      }
      this.drawDockSplitters(node, rect, splitters);
      return;
    }
    const usableHeight = Math.max(1, rect.h - gap * (count - 1));
    let y = rect.y;
    for (let i = 0; i < node.children.length; i++) {
      const h = i === node.children.length - 1 ? Math.max(1, rect.y + rect.h - y) : Math.max(1, usableHeight * weights[i]! / totalWeight);
      this.layoutDockNode(node.children[i]!, { x: rect.x, y, w: rect.w, h });
      if (i < node.children.length - 1) {
        const divider = { x: rect.x, y: y + h, w: rect.w, h: gap };
        splitters.push({
          index: i,
          divider,
          hit: { x: rect.x, y: divider.y - (DOCK_SPLITTER_HIT_SIZE - gap) / 2, w: rect.w, h: DOCK_SPLITTER_HIT_SIZE }
        });
      }
      y += h + gap;
    }
    this.drawDockSplitters(node, rect, splitters);
  }

  private drawDockSplitters(node: Extract<DockNode, { type: "split" }>, splitRect: Rect, splitters: Array<{ index: number; divider: Rect; hit: Rect }>): void {
    for (const splitter of splitters) {
      const active = this.dockResize?.splitId === node.id && this.dockResize.index === splitter.index;
      this.renderer.rect(splitter.divider, active ? theme.accent : theme.divider);
      this.hits.push({ type: "dockResize", splitId: node.id, index: splitter.index, direction: node.direction, rect: splitter.hit, splitRect: { ...splitRect } });
    }
  }

  private drawEditorGroup(group: EditorGroup, rect: Rect): void {
    group.frameRect = { ...rect };
    const tabH = this.ui(32);
    this.drawTabs(group, { x: rect.x, y: rect.y, w: rect.w, h: tabH });
    group.editorRect = { x: rect.x, y: rect.y + tabH, w: rect.w, h: rect.h - tabH };
    if (this.isSettingsTab(group.activeDocId)) {
      this.drawSettingsView(group);
      return;
    }
    this.hits.push({ type: "editor", groupId: group.id, rect: group.editorRect });
    const doc = group.activeDocId ? this.docs.get(group.activeDocId) : undefined;
    if (!doc) {
      return;
    }
    this.drawDocument(doc, group.editorRect, this.isDocumentCaretVisible(group, doc.id), group.id);
    if (this.findStateForDoc(doc.id, false)?.open && this.isActiveDocumentInGroup(group, doc.id)) this.drawFindWidget(group.editorRect);
  }

  private validTabIds(group: EditorGroup): string[] {
    return group.tabs.filter((docId) => this.isSettingsTab(docId) || Boolean(this.docs.get(docId)));
  }

  private tabWidthForLabel(label: string): number {
    return Math.min(this.ui(TAB_MAX_W), Math.max(this.ui(TAB_MIN_W), this.renderer.measureText(label, "ui") + this.ui(52)));
  }

  private tabLayoutForGroup(group: EditorGroup, rect: Rect): TabLayout {
    const ids = this.validTabIds(group);
    const gap = this.ui(TAB_GAP);
    const items: TabLayoutItem[] = [];
    let cursor = 0;
    for (const docId of ids) {
      const doc = this.docs.get(docId);
      const label = this.tabLabel(docId) + (doc?.dirty ? "*" : "");
      const width = this.tabWidthForLabel(label);
      items.push({ docId, label, width, start: cursor, end: cursor + width });
      cursor += width + gap;
    }
    const totalWidth = items.length ? Math.max(0, cursor - gap) : 0;
    const overflow = totalWidth > rect.w;
    const buttonW = this.ui(TAB_OVERFLOW_BUTTON_W);
    const overflowButtonRect = overflow ? { x: rect.x + rect.w - buttonW, y: rect.y, w: buttonW, h: rect.h } : null;
    const stripRect = overflow ? { x: rect.x, y: rect.y, w: Math.max(0, rect.w - buttonW), h: rect.h } : { ...rect };
    const maxScroll = Math.max(0, totalWidth - stripRect.w);
    const scroll = clamp(this.tabScrollStates.get(group.id) ?? 0, 0, maxScroll);
    this.tabScrollStates.set(group.id, scroll);
    return { items, stripRect, overflowButtonRect, scroll, maxScroll, totalWidth };
  }

  private revealTabInGroup(group: EditorGroup, docId: string): void {
    if (group.frameRect.w <= 0) {
      this.pendingTabRevealIds.add(docId);
      return;
    }
    const layout = this.tabLayoutForGroup(group, { x: group.frameRect.x, y: group.frameRect.y, w: group.frameRect.w, h: this.ui(32) });
    const item = layout.items.find((candidate) => candidate.docId === docId);
    if (!item) return;
    const pad = Math.min(this.ui(16), layout.stripRect.w / 4);
    let scroll = layout.scroll;
    if (item.start < scroll + pad) scroll = item.start - pad;
    else if (item.end > scroll + layout.stripRect.w - pad) scroll = item.end - layout.stripRect.w + pad;
    this.tabScrollStates.set(group.id, clamp(scroll, 0, layout.maxScroll));
    this.pendingTabRevealIds.delete(docId);
  }

  private drawTabs(group: EditorGroup, rect: Rect): void {
    this.renderer.rect(rect, theme.panel);
    let layout = this.tabLayoutForGroup(group, rect);
    if (group.activeDocId && this.pendingTabRevealIds.has(group.activeDocId)) {
      this.revealTabInGroup(group, group.activeDocId);
      layout = this.tabLayoutForGroup(group, rect);
    }
    this.renderer.pushClip(layout.stripRect);
    for (const item of layout.items) {
      const docId = item.docId;
      const doc = this.docs.get(docId);
      const x = layout.stripRect.x + item.start - layout.scroll;
      const tab = { x, y: rect.y, w: item.width, h: rect.h };
      if (tab.x + tab.w <= layout.stripRect.x || tab.x >= layout.stripRect.x + layout.stripRect.w) continue;
      if (docId === group.activeDocId) this.renderer.rect(tab, theme.panel2);
      const closeSize = this.ui(18);
      const close = { x: tab.x + tab.w - this.ui(26), y: tab.y + (tab.h - closeSize) / 2, w: closeSize, h: closeSize };
      this.drawClippedText(item.label, { x: tab.x + this.ui(10), y: tab.y, w: Math.max(0, close.x - tab.x - this.ui(18)), h: tab.h }, rect.y + this.ui(9), theme.text, "ui", "right");
      const closeHovered = this.isButtonHovered("tabClose", group.id, docId);
      const closeBase = docId === group.activeDocId ? theme.activityActive : theme.activity;
      this.renderer.rect(close, closeHovered ? this.hoverControlColor(closeBase) : closeBase);
      this.drawCenteredText("❌", close, this.buttonTextColor(true, closeHovered), "mini");
      const visibleTab = intersectRect(tab, layout.stripRect);
      const visibleClose = intersectRect(close, layout.stripRect);
      if (visibleTab) this.hits.push({ type: "tab", docId, groupId: group.id, rect: visibleTab });
      if (visibleClose) this.hits.push({ type: "tabClose", docId, groupId: group.id, rect: visibleClose });
    }
    this.renderer.popClip();
    if (layout.overflowButtonRect) {
      const active = this.contextMenu?.scope.type === "tabOverflow" && this.contextMenu.scope.groupId === group.id;
      const hovered = this.isButtonHovered("tabOverflow", group.id);
      const base = active ? theme.activityActive : theme.activity;
      this.renderer.rect(layout.overflowButtonRect, hovered ? this.hoverControlColor(base) : base);
      this.drawRectOutline(layout.overflowButtonRect, theme.divider);
      this.drawCenteredText("▾", layout.overflowButtonRect, this.buttonTextColor(true, hovered), "title");
      this.hits.push({ type: "tabOverflow", groupId: group.id, rect: layout.overflowButtonRect });
    } else {
      this.tabScrollStates.set(group.id, 0);
    }
    const last = layout.items.at(-1);
    const blankX = last ? layout.stripRect.x + last.end - layout.scroll + this.ui(TAB_GAP) : layout.stripRect.x;
    if (blankX < layout.stripRect.x + layout.stripRect.w) {
      this.hits.push({ type: "tabBar", groupId: group.id, rect: { x: Math.max(blankX, layout.stripRect.x), y: rect.y, w: layout.stripRect.x + layout.stripRect.w - Math.max(blankX, layout.stripRect.x), h: rect.h } });
    }
  }

  private drawDraggedTabGhost(): void {
    if (!this.tabDrag) return;
    const doc = this.docs.get(this.tabDrag.docId);
    const label = this.tabLabel(this.tabDrag.docId) + (doc?.dirty ? "*" : "");
    const ghost = this.dragGhostRect();
    this.renderer.rect(ghost, [theme.panel2[0], theme.panel2[1], theme.panel2[2], 0.94]);
    this.renderer.rect({ x: ghost.x, y: ghost.y, w: ghost.w, h: 1 }, theme.accent);
    this.renderer.rect({ x: ghost.x, y: ghost.y + ghost.h - 1, w: ghost.w, h: 1 }, theme.accent);
    this.renderer.rect({ x: ghost.x, y: ghost.y, w: 1, h: ghost.h }, theme.accent);
    this.renderer.rect({ x: ghost.x + ghost.w - 1, y: ghost.y, w: 1, h: ghost.h }, theme.accent);
    this.drawClippedText(label, { x: ghost.x + this.ui(10), y: ghost.y, w: Math.max(0, ghost.w - this.ui(20)), h: ghost.h }, ghost.y + this.ui(9), theme.text, "ui", "right");
  }

  private dragGhostRect(): Rect {
    const drag = this.tabDrag;
    if (!drag) return { x: 0, y: 0, w: 0, h: 0 };
    const doc = this.docs.get(drag.docId);
    const label = this.tabLabel(drag.docId) + (doc?.dirty ? "*" : "");
    const width = Math.min(this.ui(240), Math.max(this.ui(128), this.renderer.measureText(label, "ui") + this.ui(52)));
    const vp = this.viewport.get();
    return {
      x: clamp(drag.pointer.x - this.ui(18), 0, Math.max(0, vp.cssWidth - width)),
      y: clamp(drag.pointer.y - this.ui(16), 0, Math.max(0, vp.cssHeight - this.ui(56))),
      w: width,
      h: this.ui(32)
    };
  }

  private drawDockOverlay(): void {
    const targets = this.allDockTargets();
    for (const target of targets) {
      const active = this.dockPreview?.groupId === target.groupId && this.dockPreview.zone === target.zone;
      const fill: Color = active
        ? [theme.accent[0], theme.accent[1], theme.accent[2], 0.34]
        : [theme.accent[0], theme.accent[1], theme.accent[2], 0.13];
      this.renderer.solidPolygon(target.polygon, fill);
    }
    for (const group of this.groups) {
      const center = this.dockTargetShapes(group).find((target) => target.zone === "center");
      if (!center) continue;
      this.renderer.rect(center.previewRect, [theme.background[0], theme.background[1], theme.background[2], 0.28]);
      this.drawDockRectOutline(center.previewRect, theme.accent);
      this.drawDockGuideLines(group, center.previewRect);
    }
  }

  private drawTabInsertionPreview(): void {
    const preview = this.tabInsertionPreview;
    if (!preview) return;
    this.renderer.rect(preview.rect, theme.accent);
  }

  private drawDockGuideLines(group: EditorGroup, center: Rect): void {
    const outer = group.editorRect;
    const color: Color = [theme.accent[0], theme.accent[1], theme.accent[2], 0.78];
    this.drawDockRectOutline(outer, color);
    const lineWidth = Math.max(1, this.ui(1.5));
    this.renderer.line({ x: outer.x, y: outer.y }, { x: center.x, y: center.y }, lineWidth, color);
    this.renderer.line({ x: outer.x + outer.w, y: outer.y }, { x: center.x + center.w, y: center.y }, lineWidth, color);
    this.renderer.line({ x: outer.x + outer.w, y: outer.y + outer.h }, { x: center.x + center.w, y: center.y + center.h }, lineWidth, color);
    this.renderer.line({ x: outer.x, y: outer.y + outer.h }, { x: center.x, y: center.y + center.h }, lineWidth, color);
  }

  private drawDockRectOutline(rect: Rect, color: Color): void {
    const width = Math.max(1, this.ui(1));
    const half = width / 2;
    this.renderer.line({ x: rect.x, y: rect.y + half }, { x: rect.x + rect.w, y: rect.y + half }, width, color);
    this.renderer.line({ x: rect.x, y: rect.y + rect.h - half }, { x: rect.x + rect.w, y: rect.y + rect.h - half }, width, color);
    this.renderer.line({ x: rect.x + half, y: rect.y }, { x: rect.x + half, y: rect.y + rect.h }, width, color);
    this.renderer.line({ x: rect.x + rect.w - half, y: rect.y }, { x: rect.x + rect.w - half, y: rect.y + rect.h }, width, color);
  }

  private drawSettingsView(group: EditorGroup): void {
    const rect = group.editorRect;
    this.renderer.rect(rect, theme.background);
    this.drawSettingsContent(rect);
  }

  private drawSettingsContent(rect: Rect): void {
    const maxScroll = this.maxSettingsScrollY(rect);
    this.settingsScrollY = clamp(this.settingsScrollY, 0, maxScroll);
    const scrollbarSize = this.editorScrollbarSize();
    const scrollViewport = {
      x: rect.x,
      y: rect.y,
      w: Math.max(1, rect.w - (maxScroll > 0 ? scrollbarSize : 0)),
      h: rect.h
    };
    this.settingsViewportRect = scrollViewport;
    this.focusedSettingsInputRect = null;
    const pad = this.ui(10);
    const content = {
      x: scrollViewport.x + pad,
      y: scrollViewport.y + this.ui(8) - this.settingsScrollY,
      w: Math.max(0, scrollViewport.w - pad * 2),
      h: this.settingsViewportHeight(rect)
    };
    this.renderer.pushClip(scrollViewport);
    this.settingsHitClip = scrollViewport;
    let y = content.y;

    y = this.drawSettingsHeader("visual", "Visual", content, y, 0);
    if (this.settingsExpanded.has("visual")) {
      y = this.drawSettingsDropdownRow(content, y, 1, "Theme", this.settings.theme === "dark" ? "Dark" : "Light", "theme");
      y = this.drawSettingsNumberRow(content, y, 1, "Font Size", "fontSize", "px");
      y = this.drawSettingsNumberRow(content, y, 1, "UI Scale", "uiScale", "%");
      y = this.drawSettingsNumberRow(content, y, 1, "Tab Spaces", "tabSpaces", "");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Use Tab Stops", "useTabStops");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Monospaced Font", "monospacedFont");
    }

    y += this.ui(6);
    y = this.drawSettingsHeader("interface", "Interface", content, y, 0);
    if (this.settingsExpanded.has("interface")) {
      y = this.drawSettingsCheckboxRow(content, y, 1, "Rename On Double Click", "renameOnDoubleClick");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Show Line Numbers", "showLineNumbers");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Show White Space", "showWhitespace");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Remember Open Files", "rememberOpenFiles");
    }

    y += this.ui(6);
    y = this.drawSettingsHeader("ai", "AI", content, y, 0);
    if (this.settingsExpanded.has("ai")) {
      const endpointConfig = loadAiEndpointConfig();
      y = this.drawSettingsTextRow(content, y, 1, "API Base URL", "aiBaseUrl", endpointConfig.apiBaseUrl, "http://localhost:1234/v1");
      y = this.drawSettingsTextRow(content, y, 1, "API Key", "aiApiKey", endpointConfig.apiKey, "(optional)");
      y = this.drawSettingsButtonRow(content, y, 1, "Check Server", "checkAiServer", {
        buttonLabel: this.aiConnectionStatus.state === "checking" ? "Checking..." : "Check",
        enabled: this.aiConnectionStatus.state !== "checking"
      });
      y = this.drawSettingsStatusRow(content, y, 1);
      y = this.drawSettingsModelRows(content, y, 1, endpointConfig.model || "Select Model");
      y = this.drawSettingsInlineTextRow(content, y, 1, "Max Context Tokens", "aiMaxContextTokens", endpointConfig.maxContextTokens ? String(endpointConfig.maxContextTokens) : "", "auto-detect");
      y = this.drawSettingsButtonRow(content, y, 1, "Probe LM Studio Max Tokens", "probeLmStudioMaxTokens", { buttonLabel: "Probe" });
      y = this.drawSettingsButtonRow(content, y, 1, "System Prompt", "editSystemPrompt", { buttonLabel: "Edit" });
      y = this.drawSettingsToolPromptRow(content, y, 1);
      y = this.drawSettingsButtonRow(content, y, 1, "Compact Prompt", "editCompactPrompt", { buttonLabel: "Edit" });
      y = this.drawSettingsDropdownRow(content, y, 1, "Tool Call Format", this.aiToolCallFormatLabel(), "aiToolCallFormat");
      y = this.drawSettingsNumberRow(content, y, 1, "Max Tool Calls Per Turn", "aiMaxToolCalls", "");
      y = this.drawSettingsNumberRow(content, y, 1, "Compact Free", "aiCompactFreePercent", "%");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Detect Duplicate Tool Calls", "aiDetectDuplicateToolCalls");
      y = this.drawSettingsCheckboxRow(content, y, 1, "Insert Editor Context", "aiInsertEditorContext");
    }

    y += this.ui(6);
    y = this.drawSettingsHeader("danger", "Danger", content, y, 0);
    if (this.settingsExpanded.has("danger")) {
      y = this.drawSettingsButtonRow(content, y, 1, "Reset Settings", "resetAll", { buttonLabel: "Reset" });
      y = this.drawSettingsButtonRow(content, y, 1, "Clear File System", "clearFileSystem", { danger: true });
    }
    this.settingsHitClip = null;
    this.renderer.popClip();
    if (maxScroll > 0) this.drawSettingsScrollbar(rect, scrollViewport, maxScroll);
  }

  private drawSettingsHeader(id: SettingHeaderId, label: string, content: Rect, y: number, depth: number): number {
    const indent = this.ui(20) * depth;
    const row = { x: content.x + indent, y, w: Math.max(this.ui(120), content.w - indent), h: this.ui(30) };
    this.renderer.rect(row, depth === 0 ? theme.panel : theme.panel2);
    this.renderer.rect({ x: row.x, y: row.y + row.h - 1, w: row.w, h: 1 }, theme.divider);
    this.renderer.text(this.settingsExpanded.has(id) ? "v" : ">", row.x + this.ui(8), row.y + this.ui(8), theme.textDim, "ui");
    this.renderer.text(label, row.x + this.ui(26), row.y + this.ui(8), theme.text, "ui");
    this.pushSettingsHit({ type: "settingsHeader", id, rect: row });
    return y + row.h;
  }

  private drawSettingsRow(content: Rect, y: number, depth: number, label: string): { row: Rect; control: Rect } {
    const indent = this.ui(20) * depth;
    const row = { x: content.x + indent, y, w: Math.max(this.ui(120), content.w - indent), h: this.ui(34) };
    const controlW = Math.min(this.ui(220), Math.max(this.ui(128), row.w * 0.36));
    const control = { x: row.x + row.w - controlW, y: row.y + this.ui(5), w: controlW, h: row.h - this.ui(10) };
    this.drawClippedText(label, { x: row.x + this.ui(8), y: row.y, w: Math.max(0, control.x - row.x - this.ui(16)), h: row.h }, row.y + this.ui(9), theme.textDim, "ui");
    return { row, control };
  }

  private drawSettingsLabelRow(content: Rect, y: number, depth: number, label: string, value: string): number {
    const { row, control } = this.drawSettingsRow(content, y, depth, label);
    this.drawClippedText(value, { x: control.x + this.ui(8), y: control.y, w: Math.max(0, control.w - this.ui(8)), h: control.h }, row.y + this.ui(9), theme.text, "ui", "right");
    return y + row.h;
  }

  private drawSettingsTextRow(content: Rect, y: number, depth: number, label: string, key: SettingTextKey, value: string, placeholder: string): number {
    const indent = this.ui(20) * depth;
    const row = { x: content.x + indent, y, w: Math.max(this.ui(120), content.w - indent), h: this.ui(54) };
    const input = { x: row.x + this.ui(8), y: row.y + this.ui(23), w: Math.max(this.ui(80), row.w - this.ui(16)), h: this.ui(26) };
    if (this.activeSettingsText !== key) {
      const buffer = this.settingsTextBuffers[key];
      buffer.text = value;
      buffer.cursor = Math.min(buffer.cursor, buffer.text.length);
      buffer.anchor = Math.min(buffer.anchor, buffer.text.length);
      this.clampMiniBufferScroll(buffer, input, this.ui(8));
      buffer.clearUndoHistory();
    }
    this.renderer.text(label, row.x + this.ui(8), row.y + this.ui(5), theme.textDim, "ui");
    if (this.activeSettingsText === key) this.focusedSettingsInputRect = input;
    this.drawTextFieldInput(key, input, placeholder, false);
    this.pushSettingsHit({ type: "textField", field: key, rect: input });
    return y + row.h;
  }

  private drawSettingsInlineTextRow(content: Rect, y: number, depth: number, label: string, key: SettingTextKey, value: string, placeholder: string): number {
    const { row, control } = this.drawSettingsRow(content, y, depth, label);
    const active = this.activeSettingsText === key;
    const buffer = this.settingsTextBuffers[key];
    if (!active) {
      buffer.text = value;
      buffer.cursor = Math.min(buffer.cursor, buffer.text.length);
      buffer.anchor = Math.min(buffer.anchor, buffer.text.length);
      this.clampMiniBufferScroll(buffer, control, this.ui(8));
      buffer.clearUndoHistory();
    }
    if (active) this.focusedSettingsInputRect = control;
    this.drawTextFieldInput(key, control, placeholder, false);
    this.pushSettingsHit({ type: "textField", field: key, rect: control });
    return y + row.h;
  }

  private drawSettingsModelRows(content: Rect, y: number, depth: number, value: string): number {
    const indent = this.ui(20) * depth;
    const labelRow = { x: content.x + indent, y, w: Math.max(this.ui(120), content.w - indent), h: this.ui(34) };
    const checkboxLabel = "Manual";
    const boxSize = this.ui(16);
    const checkboxLabelW = this.renderer.measureText(checkboxLabel, "ui");
    const checkboxRect = {
      x: labelRow.x + labelRow.w - checkboxLabelW - boxSize - this.ui(18),
      y: labelRow.y,
      w: checkboxLabelW + boxSize + this.ui(18),
      h: labelRow.h
    };
    const box = { x: checkboxRect.x + this.ui(2), y: labelRow.y + (labelRow.h - boxSize) / 2, w: boxSize, h: boxSize };
    const hoveredCheckbox = this.isButtonHovered("settingsCheckbox", "aiModelManual");
    const checkboxBase = this.settings.aiModelManual ? theme.activityActive : theme.panel2;
    this.drawClippedText("Model", { x: labelRow.x + this.ui(8), y: labelRow.y, w: Math.max(0, checkboxRect.x - labelRow.x - this.ui(16)), h: labelRow.h }, labelRow.y + this.ui(9), theme.textDim, "ui");
    this.renderer.rect(box, hoveredCheckbox ? this.hoverControlColor(checkboxBase) : checkboxBase);
    this.drawRectOutline(box, theme.divider);
    if (this.settings.aiModelManual) this.drawCenteredText("✔️", box, this.buttonTextColor(true, hoveredCheckbox), "ui");
    this.drawClippedText(checkboxLabel, { x: box.x + box.w + this.ui(8), y: labelRow.y, w: checkboxLabelW + this.ui(2), h: labelRow.h }, labelRow.y + this.ui(9), this.buttonTextColor(true, hoveredCheckbox), "ui");
    this.pushSettingsHit({ type: "settingsCheckbox", key: "aiModelManual", rect: checkboxRect });

    const controlRow = { x: labelRow.x, y: labelRow.y + labelRow.h, w: labelRow.w, h: this.ui(34) };
    const controlY = controlRow.y + this.ui(5);
    const controlH = controlRow.h - this.ui(10);
    if (this.settings.aiModelManual) {
      const input = { x: controlRow.x + this.ui(8), y: controlY, w: Math.max(this.ui(80), controlRow.w - this.ui(16)), h: controlH };
      const buffer = this.settingsTextBuffers.aiModel;
      if (this.activeSettingsText !== "aiModel") {
        buffer.text = value === "Select Model" ? "" : value;
        buffer.cursor = Math.min(buffer.cursor, buffer.text.length);
        buffer.anchor = Math.min(buffer.anchor, buffer.text.length);
        this.clampMiniBufferScroll(buffer, input, this.ui(8));
        buffer.clearUndoHistory();
      }
      if (this.activeSettingsText === "aiModel") this.focusedSettingsInputRect = input;
      this.drawTextFieldInput("aiModel", input, "model name", false);
      this.pushSettingsHit({ type: "textField", field: "aiModel", rect: input });
    } else {
      const gap = this.ui(6);
      const buttonW = this.ui(76);
      const button = { x: controlRow.x + controlRow.w - buttonW - this.ui(8), y: controlY, w: buttonW, h: controlH };
      const dropdown = { x: controlRow.x + this.ui(8), y: controlY, w: Math.max(this.ui(80), button.x - controlRow.x - this.ui(8) - gap), h: controlH };
      const hoveredDropdown = this.isButtonHovered("settingsDropdown", "aiModel");
      this.renderer.rect(dropdown, hoveredDropdown ? this.hoverControlColor(theme.panel2) : theme.panel2);
      this.drawRectOutline(dropdown, theme.divider);
      this.drawClippedText(value, { x: dropdown.x + this.ui(8), y: dropdown.y, w: Math.max(0, dropdown.w - this.ui(30)), h: dropdown.h }, dropdown.y + this.ui(6), this.buttonTextColor(true, hoveredDropdown), "ui", "right");
      this.renderer.text("v", dropdown.x + dropdown.w - this.ui(16), dropdown.y + this.ui(6), hoveredDropdown ? this.buttonTextColor(true, true) : theme.textDim, "ui");
      this.pushSettingsHit({ type: "settingsDropdown", key: "aiModel", rect: dropdown });

      const hoveredButton = this.isButtonHovered("settingsButton", "probeLmStudioModels");
      this.renderer.rect(button, hoveredButton ? this.hoverControlColor(theme.activityActive) : theme.activityActive);
      this.drawRectOutline(button, theme.divider);
      this.drawCenteredText("Probe", button, this.buttonTextColor(true, hoveredButton), "ui");
      this.pushSettingsHit({ type: "settingsButton", action: "probeLmStudioModels", rect: button, enabled: true });
    }
    return controlRow.y + controlRow.h;
  }

  private drawSettingsToolPromptRow(content: Rect, y: number, depth: number): number {
    const { row, control } = this.drawSettingsRow(content, y, depth, "Tool Prompt");
    const gap = this.ui(6);
    const tagW = Math.min(Math.max(this.ui(42), this.renderer.measureText("Tag", "ui") + this.ui(22)), Math.max(this.ui(1), control.w - gap - this.ui(76)));
    const harmonyW = Math.max(this.ui(76), control.w - tagW - gap);
    const tag = { x: control.x, y: control.y, w: tagW, h: control.h };
    const harmony = { x: tag.x + tag.w + gap, y: control.y, w: harmonyW, h: control.h };
    const tagHovered = this.isButtonHovered("settingsButton", "editTagToolPrompt");
    const harmonyHovered = this.isButtonHovered("settingsButton", "editHarmonyToolPrompt");
    this.renderer.rect(tag, tagHovered ? this.hoverControlColor(theme.activityActive) : theme.activityActive);
    this.drawRectOutline(tag, theme.divider);
    this.drawCenteredText("Tag", tag, this.buttonTextColor(true, tagHovered), "ui");
    this.renderer.rect(harmony, harmonyHovered ? this.hoverControlColor(theme.activityActive) : theme.activityActive);
    this.drawRectOutline(harmony, theme.divider);
    this.drawCenteredText("Harmony", harmony, this.buttonTextColor(true, harmonyHovered), "ui");
    this.pushSettingsHit({ type: "settingsButton", action: "editTagToolPrompt", rect: tag, enabled: true });
    this.pushSettingsHit({ type: "settingsButton", action: "editHarmonyToolPrompt", rect: harmony, enabled: true });
    return y + row.h;
  }

  private aiToolCallFormatLabel(): string {
    if (this.settings.aiToolCallFormat === "none") return "None";
    if (this.settings.aiToolCallFormat === "harmony") return "Harmony";
    return "Tag";
  }

  private drawSettingsDropdownRow(content: Rect, y: number, depth: number, label: string, value: string, key: SettingDropdownKey): number {
    const { row, control } = this.drawSettingsRow(content, y, depth, label);
    const hovered = this.isButtonHovered("settingsDropdown", key);
    this.renderer.rect(control, hovered ? this.hoverControlColor(theme.panel2) : theme.panel2);
    this.drawRectOutline(control, theme.divider);
    this.drawClippedText(value, { x: control.x + this.ui(8), y: control.y, w: Math.max(0, control.w - this.ui(30)), h: control.h }, control.y + this.ui(6), this.buttonTextColor(true, hovered), "ui", "right");
    this.renderer.text("v", control.x + control.w - this.ui(16), control.y + this.ui(6), hovered ? this.buttonTextColor(true, true) : theme.textDim, "ui");
    this.pushSettingsHit({ type: "settingsDropdown", key, rect: control });
    return y + row.h;
  }

  private drawSettingsNumberRow(content: Rect, y: number, depth: number, label: string, key: SettingNumberKey, unit: string): number {
    const { row, control } = this.drawSettingsRow(content, y, depth, label);
    const unitW = unit ? this.renderer.measureText(unit, "ui") + this.ui(14) : 0;
    const input = { x: control.x, y: control.y, w: Math.max(this.ui(60), control.w - unitW), h: control.h };
    const active = this.activeSettingsNumber === key;
    if (active) this.focusedSettingsInputRect = input;
    this.renderer.rect(input, active ? theme.activity : theme.panel2);
    this.drawRectOutline(input, active ? theme.accent : theme.divider);
    const text = active ? this.settingsNumberBuffer.text : String(this.settings[key]);
    const padX = this.ui(8);
    if (active) this.revealMiniBufferCaret(this.settingsNumberBuffer, input, padX);
    else this.clampMiniBufferScroll(this.settingsNumberBuffer, input, padX);
    const inputContent = this.miniBufferContentRect(input, padX);
    const textX = active ? inputContent.x - this.settingsNumberBuffer.scrollX : inputContent.x;
    const textY = input.y + this.ui(6);
    this.renderer.pushClip(inputContent);
    if (active && this.settingsNumberBuffer.hasSelection()) {
      const selectionStart = Math.min(this.settingsNumberBuffer.anchor, this.settingsNumberBuffer.cursor);
      const selectionEnd = Math.max(this.settingsNumberBuffer.anchor, this.settingsNumberBuffer.cursor);
      const beforeSelection = text.slice(0, selectionStart);
      const selected = text.slice(selectionStart, selectionEnd);
      const sx = textX + this.renderer.measureText(beforeSelection, "ui");
      const sw = Math.max(2, this.renderer.measureText(selected, "ui"));
      this.renderer.rect({ x: sx, y: input.y + this.ui(3), w: sw, h: input.h - this.ui(6) }, theme.selection);
    }
    this.renderer.text(text || "0", textX, textY, text ? theme.text : theme.textDim, "ui");
    if (this.isSettingsNumberCaretVisible(key)) {
      const caretX = textX + this.renderer.measureText(text.slice(0, this.settingsNumberBuffer.cursor), "ui");
      this.renderer.rect({ x: caretX, y: input.y + this.ui(4), w: 1.5, h: input.h - this.ui(8) }, theme.caret);
    }
    this.renderer.popClip();
    if (unit) this.renderer.text(unit, input.x + input.w + this.ui(8), row.y + this.ui(9), theme.textDim, "ui");
    this.pushSettingsHit({ type: "settingsNumber", key, rect: input });
    if (active) this.drawMiniBufferSelectionHandles({ type: "settingsNumber", key }, this.settingsNumberBuffer, input, padX);
    return y + row.h;
  }

  private drawSettingsCheckboxRow(content: Rect, y: number, depth: number, label: string, key: SettingCheckboxKey): number {
    const indent = this.ui(20) * depth;
    const row = { x: content.x + indent, y, w: Math.max(this.ui(120), content.w - indent), h: this.ui(34) };
    const size = this.ui(16);
    const box = { x: row.x + row.w - size - this.ui(8), y: row.y + (row.h - size) / 2, w: size, h: size };
    const hovered = this.isButtonHovered("settingsCheckbox", key);
    const base = this.settings[key] ? theme.activityActive : theme.panel2;
    this.drawClippedText(label, { x: row.x + this.ui(8), y: row.y, w: Math.max(0, box.x - row.x - this.ui(16)), h: row.h }, row.y + this.ui(9), theme.textDim, "ui");
    this.renderer.rect(box, hovered ? this.hoverControlColor(base) : base);
    this.drawRectOutline(box, theme.divider);
    if (this.settings[key]) this.drawCenteredText("✔️", box, this.buttonTextColor(true, hovered), "ui");
    this.pushSettingsHit({ type: "settingsCheckbox", key, rect: row });
    return y + row.h;
  }

  private drawSettingsButtonRow(content: Rect, y: number, depth: number, label: string, action: SettingButtonAction, options: { buttonLabel?: string; danger?: boolean; enabled?: boolean } = {}): number {
    const buttonLabel = options.buttonLabel ?? label;
    const enabled = options.enabled ?? true;
    const { row, control } = this.drawSettingsRow(content, y, depth, label);
    const button = { x: control.x, y: control.y, w: Math.max(this.ui(128), Math.min(this.ui(190), control.w)), h: control.h };
    const hovered = enabled && this.isButtonHovered("settingsButton", action);
    const base = options.danger ? theme.error : theme.activityActive;
    this.renderer.rect(button, enabled ? (hovered ? this.hoverControlColor(base) : base) : theme.panel2);
    this.drawRectOutline(button, options.danger && enabled ? theme.error : theme.divider);
    this.drawCenteredText(buttonLabel, button, this.buttonTextColor(enabled, hovered), "ui");
    this.pushSettingsHit({ type: "settingsButton", action, rect: button, enabled });
    return y + row.h;
  }

  private drawSettingsStatusRow(content: Rect, y: number, depth: number): number {
    const indent = this.ui(20) * depth;
    const row = { x: content.x + indent, y, w: Math.max(this.ui(120), content.w - indent), h: this.ui(46) };
    const box = { x: row.x + this.ui(8), y: row.y + this.ui(3), w: Math.max(0, row.w - this.ui(16)), h: row.h - this.ui(6) };
    const state = this.aiConnectionStatus.state;
    const message = this.aiConnectionStatus.message || "Server not checked.";
    const color = this.aiConnectionStatusColor(state);
    this.renderer.rect(box, [theme.panel2[0], theme.panel2[1], theme.panel2[2], state === "idle" ? 0.58 : 0.86]);
    this.drawRectOutline(box, state === "idle" ? theme.divider : color);
    const lines = this.wrapTextForWidth(message, Math.max(1, box.w - this.ui(16)), "ui").slice(0, 2);
    const lineH = this.renderer.lineHeight("ui");
    for (let i = 0; i < lines.length; i++) {
      this.drawClippedText(lines[i]!, { x: box.x + this.ui(8), y: box.y + this.ui(4) + i * lineH, w: Math.max(0, box.w - this.ui(16)), h: lineH }, box.y + this.ui(5) + i * lineH, state === "idle" ? theme.textDim : color, "ui");
    }
    return y + row.h;
  }

  private aiConnectionStatusColor(state: AiConnectionStatus["state"]): Color {
    if (state === "ok") return theme.accent2;
    if (state === "error") return theme.error;
    if (state === "checking") return theme.warning;
    return theme.textDim;
  }

  private pushSettingsHit(hit: Extract<HitItem, { type: "settingsHeader" | "settingsCheckbox" | "settingsDropdown" | "settingsNumber" | "settingsButton" | "textField" }>): void {
    if (!this.settingsHitClip || rectIntersects(hit.rect, this.settingsHitClip)) this.hits.push(hit);
  }

  private drawSettingsScrollbar(rect: Rect, viewportRect: Rect, maxScroll: number): void {
    const size = this.editorScrollbarSize();
    const trackRect = { x: rect.x + rect.w - size, y: rect.y, w: size, h: rect.h };
    const active = Boolean(this.settingsScrollbarDrag);
    const hovered = Boolean(this.hoveredSettingsScrollbar);
    this.renderer.rect(trackRect, hovered || active
      ? [theme.activity[0], theme.activity[1], theme.activity[2], 0.90]
      : [theme.activity[0], theme.activity[1], theme.activity[2], 0.82]);
    const thumbRect = this.settingsScrollbarThumb(rect, trackRect, this.settingsScrollY, maxScroll);
    const thumbColor: Color = active
      ? [0.34, 0.41, 0.50, 1]
      : hovered
        ? [0.28, 0.31, 0.36, 1]
        : theme.activityActive;
    this.renderer.rect(thumbRect, thumbColor);
    this.hits.push({ type: "settingsScrollbar", rect: trackRect, trackRect, thumbRect, viewportRect });
  }

  private settingsScrollbarThumb(rect: Rect, trackRect: Rect, scrollY: number, maxScroll: number): Rect {
    const contentHeight = this.settingsContentHeight();
    const thumbH = clamp((this.settingsViewportHeight(rect) / contentHeight) * trackRect.h, Math.min(trackRect.h, this.ui(EDITOR_SCROLLBAR_THUMB_MIN)), trackRect.h);
    const thumbTravel = Math.max(1, trackRect.h - thumbH);
    return { x: trackRect.x + this.ui(3), y: trackRect.y + (scrollY / maxScroll) * thumbTravel, w: Math.max(this.ui(3), trackRect.w - this.ui(6)), h: thumbH };
  }

  private drawSidebarScrollbar(panel: SidebarScrollPanel, viewport: Rect, contentHeight: number, scrollY: number): void {
    const size = this.editorScrollbarSize();
    const trackRect = { x: viewport.x + viewport.w - size, y: viewport.y, w: size, h: viewport.h };
    const active = this.sidebarScrollbarDrag?.panel === panel;
    const hovered = this.hoveredSidebarScrollbar?.panel === panel;
    this.renderer.rect(trackRect, hovered || active
      ? [theme.activity[0], theme.activity[1], theme.activity[2], 0.90]
      : [theme.activity[0], theme.activity[1], theme.activity[2], 0.82]);
    const maxScroll = Math.max(0, contentHeight - viewport.h);
    const thumbRect = this.sidebarScrollbarThumb(viewport, trackRect, contentHeight, scrollY, maxScroll);
    const thumbColor: Color = active
      ? [0.34, 0.41, 0.50, 1]
      : hovered
        ? [0.28, 0.31, 0.36, 1]
        : theme.activityActive;
    this.renderer.rect(thumbRect, thumbColor);
    this.hits.push({ type: "sidebarScrollbar", panel, rect: trackRect, trackRect, thumbRect, viewportRect: viewport, contentHeight });
  }

  private sidebarScrollbarThumb(viewport: Rect, trackRect: Rect, contentHeight: number, scrollY: number, maxScroll: number): Rect {
    const thumbH = clamp((viewport.h / Math.max(1, contentHeight)) * trackRect.h, Math.min(trackRect.h, this.ui(EDITOR_SCROLLBAR_THUMB_MIN)), trackRect.h);
    const thumbTravel = Math.max(1, trackRect.h - thumbH);
    return { x: trackRect.x + this.ui(3), y: trackRect.y + (maxScroll > 0 ? (scrollY / maxScroll) * thumbTravel : 0), w: Math.max(this.ui(3), trackRect.w - this.ui(6)), h: thumbH };
  }

  private drawRectOutline(rect: Rect, color: Color): void {
    this.renderer.rect({ x: rect.x, y: rect.y, w: rect.w, h: 1 }, color);
    this.renderer.rect({ x: rect.x, y: rect.y + rect.h - 1, w: rect.w, h: 1 }, color);
    this.renderer.rect({ x: rect.x, y: rect.y, w: 1, h: rect.h }, color);
    this.renderer.rect({ x: rect.x + rect.w - 1, y: rect.y, w: 1, h: rect.h }, color);
  }

  private drawRectOutlineClipped(rect: Rect, clip: Rect, color: Color): void {
    for (const edge of [
      { x: rect.x, y: rect.y, w: rect.w, h: 1 },
      { x: rect.x, y: rect.y + rect.h - 1, w: rect.w, h: 1 },
      { x: rect.x, y: rect.y, w: 1, h: rect.h },
      { x: rect.x + rect.w - 1, y: rect.y, w: 1, h: rect.h }
    ]) {
      const visible = intersectRect(edge, clip);
      if (visible) this.renderer.rect(visible, color);
    }
  }

  private drawFindWidget(editorRect: Rect): void {
    const state = this.activeFindState(false);
    if (!state) return;
    const expanded = state.replaceExpanded;
    const panelW = Math.min(this.ui(560), Math.max(this.ui(360), editorRect.w - this.ui(32)));
    const rowH = this.ui(28);
    const panelH = expanded ? this.ui(78) : this.ui(42);
    const panel = {
      x: editorRect.x + editorRect.w - panelW - this.ui(12),
      y: editorRect.y + this.ui(10),
      w: panelW,
      h: panelH
    };
    this.renderer.rect(panel, [theme.panel2[0], theme.panel2[1], theme.panel2[2], 0.98]);
    this.drawRectOutline(panel, theme.divider);

    const toggle = { x: panel.x + this.ui(8), y: panel.y + this.ui(7), w: rowH, h: rowH };
    const close = { x: panel.x + panel.w - this.ui(36), y: toggle.y, w: rowH, h: rowH };
    const next = { x: close.x - this.ui(34), y: toggle.y, w: rowH, h: rowH };
    const previous = { x: next.x - this.ui(34), y: toggle.y, w: rowH, h: rowH };
    const input = { x: toggle.x + toggle.w + this.ui(6), y: toggle.y, w: Math.max(this.ui(80), previous.x - toggle.x - toggle.w - this.ui(12)), h: rowH };
    const hasQuery = Boolean(state.findBuffer.text);
    this.drawIconButton(toggle, expanded ? "v" : ">", true, "ui", this.isButtonHovered("findToggle"));
    this.hits.push({ type: "findToggle", rect: toggle });
    this.drawTextFieldInput("find", input, "find");
    this.drawIconButton(previous, "🔺", hasQuery, "ui", this.isButtonHovered("findPrevious"));
    this.drawIconButton(next, "🔻", hasQuery, "ui", this.isButtonHovered("findNext"));
    this.drawIconButton(close, "✖", true, "uiSmall", this.isButtonHovered("findClose"));
    this.hits.push({ type: "findPrevious", rect: previous, enabled: hasQuery });
    this.hits.push({ type: "findNext", rect: next, enabled: hasQuery });
    this.hits.push({ type: "findClose", rect: close });

    if (!expanded) return;
    const replaceY = panel.y + this.ui(43);
    const replaceAllW = this.ui(34);
    const replaceW = this.ui(68);
    const replaceAll = { x: panel.x + panel.w - this.ui(8) - replaceAllW, y: replaceY, w: replaceAllW, h: rowH };
    const replace = { x: replaceAll.x - this.ui(8) - replaceW, y: replaceY, w: replaceW, h: rowH };
    const replaceInput = { x: input.x, y: replaceY, w: Math.max(this.ui(80), replace.x - input.x - this.ui(8)), h: rowH };
    this.drawTextFieldInput("findReplace", replaceInput, "replace");
    this.drawButton(replace, "Replace", hasQuery, this.isButtonHovered("findReplace"));
    this.drawButton(replaceAll, "All", hasQuery, this.isButtonHovered("findReplaceAll"));
    this.hits.push({ type: "findReplace", rect: replace, enabled: hasQuery });
    this.hits.push({ type: "findReplaceAll", rect: replaceAll, enabled: hasQuery });
  }

  private drawDocument(doc: TextDocument, rect: Rect, showCaret: boolean, groupId: string): void {
    const scroll = this.clampScrollForDoc(doc, rect);
    const contentRect = this.editorContentRect(doc, rect);
    this.renderer.pushClip(contentRect);
    const gutterW = this.gutterWidthForDoc(doc);
    const gutterRect = { x: contentRect.x, y: contentRect.y, w: gutterW, h: contentRect.h };
    const gutterHitRect = { x: contentRect.x, y: contentRect.y, w: gutterW > 0 ? gutterW : Math.max(1, this.ui(EDITOR_TEXT_PAD_X)), h: contentRect.h };
    const textClipRect = { x: contentRect.x + gutterW, y: contentRect.y, w: Math.max(0, contentRect.w - gutterW), h: contentRect.h };
    const textX = this.editorTextX(doc, contentRect) - scroll.x;
    const lineH = this.renderer.lineHeight("code");
    if (gutterW > 0) this.renderer.rect(gutterRect, theme.panel);
    const firstLine = Math.max(0, Math.floor(scroll.y / lineH));
    const lineCount = Math.ceil(contentRect.h / lineH) + 2;
    const selection = doc.getOrderedSelection();
    for (let i = 0; i < lineCount; i++) {
      const lineIndex = firstLine + i;
      if (lineIndex >= doc.lineCount()) break;
      const y = contentRect.y + i * lineH - (scroll.y % lineH);
      if (lineIndex === doc.selection.head.line) this.renderer.rect({ x: contentRect.x + gutterW, y, w: contentRect.w - gutterW, h: lineH }, theme.lineHighlight);
      if (gutterW > 0) {
        const lineNumber = String(lineIndex + 1);
        this.renderer.pushClip(gutterRect);
        this.renderer.text(lineNumber, contentRect.x + gutterW - EDITOR_GUTTER_PAD_RIGHT - this.renderer.measureText(lineNumber, "gutter"), y + 3, theme.textDim, "gutter");
        this.renderer.popClip();
      }
      this.renderer.pushClip(textClipRect);
      this.drawSelectionForLine(doc, lineIndex, textX, y, lineH, selection);
      let offset = 0;
      const visibleStart = Math.max(0, textClipRect.x - textX);
      const visibleEnd = Math.max(visibleStart, textClipRect.x + textClipRect.w - textX);
      for (const token of this.tokensForLine(doc, lineIndex)) {
        if (offset > visibleEnd) break;
        const result = this.drawVisibleCodeText(token.text, textX, y + 3, tokenColor(token.type), offset, visibleStart, visibleEnd);
        offset = result.endOffset;
        if (result.clippedRight) break;
      }
      this.drawWhitespaceForLine(doc.lines[lineIndex] ?? "", lineIndex, doc.lineCount(), textX, y, lineH, visibleStart, visibleEnd);
      this.renderer.popClip();
    }
    const caret = this.caretRect(doc, rect);
    const drawCaret = showCaret && (this.input.composing || this.isCaretBlinkOn());
    this.renderer.pushClip(textClipRect);
    if (drawCaret) this.renderer.rect(caret, theme.caret);
    if (drawCaret && this.input.composing && this.input.compositionText) {
      this.renderer.text(this.input.compositionText, caret.x + 2, caret.y, theme.warning, "code");
      this.renderer.rect({ x: caret.x + 2, y: caret.y + lineH - 3, w: this.measureCodeText(this.input.compositionText), h: 1 }, theme.warning);
    }
    this.renderer.popClip();
    this.drawMobileSelectionHandles(doc, rect, contentRect);
    this.hits.push({ type: "editorGutter", groupId, docId: doc.id, rect: gutterHitRect });
    this.renderer.popClip();
    this.drawEditorScrollbars(doc, rect);
  }

  private drawEditorScrollbars(doc: TextDocument, rect: Rect): void {
    const overflow = this.editorOverflow(doc, rect);
    if (overflow.vertical) this.drawEditorScrollbar(doc, rect, "vertical", overflow);
    if (overflow.horizontal) this.drawEditorScrollbar(doc, rect, "horizontal", overflow);
    if (overflow.vertical && overflow.horizontal) {
      const size = this.editorScrollbarSize();
      this.renderer.rect({ x: rect.x + rect.w - size, y: rect.y + rect.h - size, w: size, h: size }, [theme.activity[0], theme.activity[1], theme.activity[2], 0.88]);
    }
  }

  private drawEditorScrollbar(doc: TextDocument, rect: Rect, axis: ScrollbarAxis, overflow: EditorOverflow): void {
    const groupId = this.groupContaining(doc.id)?.id ?? this.activeGroupId;
    const contentRect = this.editorContentRectForOverflow(rect, overflow);
    const size = this.editorScrollbarSize();
    const trackRect = axis === "vertical"
      ? { x: contentRect.x + contentRect.w, y: contentRect.y, w: size, h: contentRect.h }
      : { x: contentRect.x, y: contentRect.y + contentRect.h, w: contentRect.w, h: size };
    const active = this.scrollbarDrag?.axis === axis && this.scrollbarDrag.groupId === groupId && this.scrollbarDrag.docId === doc.id;
    const hovered = this.hoveredScrollbar?.axis === axis && this.hoveredScrollbar.groupId === groupId && this.hoveredScrollbar.docId === doc.id;
    this.renderer.rect(trackRect, hovered || active
      ? [theme.activity[0], theme.activity[1], theme.activity[2], 0.90]
      : [theme.activity[0], theme.activity[1], theme.activity[2], 0.82]);
    const maxScroll = axis === "vertical" ? this.maxScrollY(doc, rect) : this.maxScrollX(doc, rect);
    if (maxScroll <= 0) return;
    const scroll = this.clampScrollForDoc(doc, rect);
    const thumbRect = axis === "vertical"
      ? this.verticalScrollbarThumb(doc, rect, trackRect, scroll.y, maxScroll)
      : this.horizontalScrollbarThumb(doc, rect, trackRect, scroll.x, maxScroll);
    const thumbColor: Color = active
      ? [0.34, 0.41, 0.50, 1]
      : hovered
        ? [0.28, 0.31, 0.36, 1]
        : theme.activityActive;
    this.renderer.rect(thumbRect, thumbColor);
    this.hits.push({ type: "editorScrollbar", axis, groupId, docId: doc.id, rect: trackRect, trackRect, thumbRect });
  }

  private verticalScrollbarThumb(doc: TextDocument, rect: Rect, trackRect: Rect, scrollY: number, maxScroll: number): Rect {
    const contentHeight = this.documentContentHeight(doc);
    const thumbH = clamp((this.editorContentRect(doc, rect).h / contentHeight) * trackRect.h, Math.min(trackRect.h, this.ui(EDITOR_SCROLLBAR_THUMB_MIN)), trackRect.h);
    const thumbTravel = Math.max(1, trackRect.h - thumbH);
    return { x: trackRect.x + this.ui(3), y: trackRect.y + (scrollY / maxScroll) * thumbTravel, w: Math.max(this.ui(3), trackRect.w - this.ui(6)), h: thumbH };
  }

  private horizontalScrollbarThumb(doc: TextDocument, rect: Rect, trackRect: Rect, scrollX: number, maxScroll: number): Rect {
    const contentRect = this.editorContentRect(doc, rect);
    const visibleTextWidth = this.visibleTextWidth(doc, contentRect);
    const contentWidth = visibleTextWidth + maxScroll;
    const thumbW = clamp((visibleTextWidth / contentWidth) * trackRect.w, Math.min(trackRect.w, this.ui(EDITOR_SCROLLBAR_THUMB_MIN)), trackRect.w);
    const thumbTravel = Math.max(1, trackRect.w - thumbW);
    return { x: trackRect.x + (scrollX / maxScroll) * thumbTravel, y: trackRect.y + this.ui(3), w: thumbW, h: Math.max(this.ui(3), trackRect.h - this.ui(6)) };
  }

  private drawSelectionForLine(doc: TextDocument, line: number, x: number, y: number, lineH: number, selection: { start: { line: number; col: number }; end: { line: number; col: number } }): void {
    if (!doc.hasSelection() || line < selection.start.line || line > selection.end.line) return;
    const start = line === selection.start.line ? selection.start.col : 0;
    const end = line === selection.end.line ? selection.end.col : doc.lines[line]!.length;
    if (end <= start) return;
    const text = doc.lines[line]!;
    const startX = x + this.measureCodePrefix(text, start);
    const endX = x + this.measureCodePrefix(text, end);
    this.renderer.rect({ x: startX, y, w: Math.max(2, endX - startX), h: lineH }, theme.selection);
  }

  private drawMobileSelectionHandles(doc: TextDocument, editorRect: Rect, contentRect: Rect): void {
    if (!this.isMobileSelectionMode() || !doc.hasSelection() || doc.readOnly) return;
    const group = this.groupContaining(doc.id);
    if (!group || !this.isActiveDocumentInGroup(group, doc.id)) return;
    const ordered = doc.getOrderedSelection();
    this.drawMobileSelectionHandle("start", doc, group.id, editorRect, contentRect, ordered.start);
    this.drawMobileSelectionHandle("end", doc, group.id, editorRect, contentRect, ordered.end);
  }

  private drawMobileSelectionHandle(edge: SelectionHandleEdge, doc: TextDocument, groupId: string, editorRect: Rect, contentRect: Rect, pos: { line: number; col: number }): void {
    const caret = this.positionRectForDoc(doc, editorRect, pos);
    const hit = this.drawMobileSelectionHandleGlyph(caret, contentRect);
    if (!hit) return;
    this.hits.push({ type: "selectionHandle", edge, groupId, docId: doc.id, rect: hit });
  }

  private drawMiniBufferSelectionHandles(target: Exclude<TextSelectionHandleTarget, { type: "chatInput" }>, buffer: MiniBuffer, input: Rect, padX: number, clip?: Rect): void {
    if (!this.isMobileSelectionMode() || !buffer.hasSelection() || !this.isTextSelectionHandleTargetActive(target)) return;
    const start = Math.min(buffer.anchor, buffer.cursor);
    const end = Math.max(buffer.anchor, buffer.cursor);
    const content = this.miniBufferContentRect(input, padX);
    const textX = content.x - buffer.scrollX;
    const y = input.y + this.ui(3);
    const h = Math.max(1, input.h - this.ui(6));
    const handleClip = clip ? intersectRect(clip, { x: content.x, y: input.y, w: content.w, h: input.h }) : { x: content.x, y: input.y, w: content.w, h: input.h };
    if (!handleClip) return;
    const startX = textX + this.renderer.measureText(buffer.text.slice(0, start), "ui");
    const endX = textX + this.renderer.measureText(buffer.text.slice(0, end), "ui");
    this.drawTextSelectionHandle("start", target, input, { x: startX, y, w: 1.5, h }, handleClip);
    this.drawTextSelectionHandle("end", target, input, { x: endX, y, w: 1.5, h }, handleClip);
  }

  private drawChatInputSelectionHandles(input: Rect, contentRect: Rect): void {
    if (!this.isMobileSelectionMode() || !this.chatDraft.hasSelection() || !this.isTextSelectionHandleTargetActive({ type: "chatInput" })) return;
    const ordered = this.chatDraft.getOrderedSelection();
    this.drawTextSelectionHandle("start", { type: "chatInput" }, input, this.chatInputPositionRect(input, ordered.start), contentRect);
    this.drawTextSelectionHandle("end", { type: "chatInput" }, input, this.chatInputPositionRect(input, ordered.end), contentRect);
  }

  private drawTextSelectionHandle(edge: SelectionHandleEdge, target: TextSelectionHandleTarget, inputRect: Rect, caret: Rect, clipRect: Rect): void {
    const hit = this.drawMobileSelectionHandleGlyph(caret, clipRect);
    if (!hit) return;
    const clippedHit = this.settingsHitClip ? intersectRect(hit, this.settingsHitClip) : hit;
    if (!clippedHit) return;
    this.hits.push({ type: "textSelectionHandle", edge, target, inputRect, rect: clippedHit });
  }

  private drawMobileSelectionHandleGlyph(caret: Rect, contentRect: Rect): Rect | null {
    if (caret.y + caret.h < contentRect.y || caret.y > contentRect.y + contentRect.h) return null;
    const color = theme.accent;
    const stemW = Math.max(2, this.ui(2));
    const knob = Math.max(8, this.ui(10));
    const x = clamp(caret.x, contentRect.x, contentRect.x + contentRect.w);
    const visualY = clamp(caret.y, contentRect.y, contentRect.y + contentRect.h);
    const visualH = Math.max(0, Math.min(caret.h, contentRect.y + contentRect.h - visualY));
    this.renderer.rect({ x: x - stemW / 2, y: visualY, w: stemW, h: visualH }, color);
    const cy = clamp(caret.y + caret.h + knob * 0.5, contentRect.y + knob / 2, contentRect.y + contentRect.h - knob / 2);
    this.renderer.solidPolygon(octagonPoints(x, cy, knob / 2), color);
    const hitSize = this.ui(SELECTION_HANDLE_TOUCH_SIZE);
    return { x: x - hitSize / 2, y: cy - hitSize / 2, w: hitSize, h: hitSize };
  }

  private pointHitsSelection(doc: TextDocument, editorRect: Rect, point: Point): boolean {
    if (!doc.hasSelection()) return false;
    const contentRect = this.editorContentRect(doc, editorRect);
    if (!rectContains(contentRect, point.x, point.y)) return false;
    const selection = doc.getOrderedSelection();
    const lineH = this.renderer.lineHeight("code");
    const scroll = this.scrollForDoc(doc.id);
    const line = Math.floor((point.y - contentRect.y + scroll.y) / lineH);
    if (line < selection.start.line || line > selection.end.line || line < 0 || line >= doc.lineCount()) return false;
    const lineY = contentRect.y + line * lineH - scroll.y;
    if (point.y < lineY || point.y > lineY + lineH) return false;
    const text = doc.lines[line] ?? "";
    const start = line === selection.start.line ? selection.start.col : 0;
    const end = line === selection.end.line ? selection.end.col : text.length;
    if (end <= start) return false;
    const x = this.editorTextX(doc, contentRect) - scroll.x;
    const startX = x + this.measureCodePrefix(text, start);
    const endX = x + this.measureCodePrefix(text, end);
    return point.x >= startX && point.x <= Math.max(startX + 2, endX);
  }

  private selectEditorWordFromPoint(doc: TextDocument, editorRect: Rect, point: Point): void {
    const lineH = this.renderer.lineHeight("code");
    const contentRect = this.editorContentRect(doc, editorRect);
    const scroll = this.scrollForDoc(doc.id);
    const line = clamp(Math.floor((point.y - contentRect.y + scroll.y) / lineH), 0, doc.lineCount() - 1);
    const textX = this.editorTextX(doc, contentRect);
    const col = this.columnFromCodeTextOffset(doc.lines[line]!, point.x - textX + scroll.x);
    const range = wordRangeAt(doc.lines[line]!, col);
    doc.setSelection({ line, col: range.start }, { line, col: range.end });
    this.resetCaretBlink();
  }

  private selectEditorLineFromPoint(doc: TextDocument, editorRect: Rect, point: Point): void {
    const lineH = this.renderer.lineHeight("code");
    const contentRect = this.editorContentRect(doc, editorRect);
    const scroll = this.scrollForDoc(doc.id);
    const line = clamp(Math.floor((point.y - contentRect.y + scroll.y) / lineH), 0, doc.lineCount() - 1);
    doc.setSelection({ line, col: 0 }, { line, col: doc.lines[line]!.length });
    this.resetCaretBlink();
  }

  private drawStatus(rect: Rect): void {
    this.renderer.rect(rect, theme.activity);
    const doc = this.activeDoc();
    const pad = this.ui(8);
    const gap = this.ui(10);
    const controlH = Math.min(this.ui(20), Math.max(0, rect.h - this.ui(4)));
    const y = rect.y + (rect.h - controlH) / 2;
    const textY = rect.y + (rect.h - this.renderer.lineHeight("ui")) / 2;
    let x = rect.x + rect.w - pad;

    const syntaxLabel = this.highlightLabel(doc?.syntaxId ?? "plain");
    const highlightLabel = "Highlight";
    const arrow = "▴";
    const arrowW = this.renderer.measureText(arrow, "ui");
    const valueW = this.renderer.measureText(syntaxLabel, "ui") + arrowW + this.ui(18);
    const labelW = this.renderer.measureText(highlightLabel, "ui") + this.ui(6);
    const highlightValue = { x: x - valueW, y, w: valueW, h: controlH };
    const highlightLabelX = highlightValue.x - labelW;
    const highlightHovered = doc ? this.isButtonHovered("statusHighlight", this.activeGroupId, doc.id) : false;
    const highlightActive = this.contextMenu?.scope.type === "highlightDropdown" && this.contextMenu.scope.docId === doc?.id;
    this.renderer.text(highlightLabel, highlightLabelX, textY, doc ? theme.textDim : [theme.textDim[0], theme.textDim[1], theme.textDim[2], 0.55], "ui");
    if (highlightHovered || highlightActive) {
      this.renderer.rect(highlightValue, this.hoverControlColor(highlightActive ? theme.activityActive : theme.activity));
      if (highlightActive) this.drawRectOutline(highlightValue, theme.divider);
    }
    const highlightTextColor = doc ? this.buttonTextColor(true, highlightHovered || highlightActive) : theme.textDim;
    this.renderer.text(syntaxLabel, highlightValue.x + this.ui(5), textY, highlightTextColor, "ui");
    this.renderer.text(arrow, highlightValue.x + highlightValue.w - arrowW - this.ui(5), textY, highlightTextColor, "ui");
    if (doc) this.hits.push({ type: "statusHighlight", groupId: this.activeGroupId, docId: doc.id, rect: highlightValue });
    x = highlightLabelX - gap;

    const checkboxText = "Show whitespace";
    const checkboxTextW = this.renderer.measureText(checkboxText, "ui");
    const boxSize = this.ui(12);
    const checkboxW = boxSize + checkboxTextW + this.ui(11);
    const checkbox = { x: x - checkboxW, y, w: checkboxW, h: controlH };
    const checkboxHovered = this.isButtonHovered("statusWhitespace");
    if (checkboxHovered) this.renderer.rect(checkbox, this.hoverControlColor(theme.activity));
    const box = { x: checkbox.x + this.ui(3), y: rect.y + (rect.h - boxSize) / 2, w: boxSize, h: boxSize };
    this.renderer.rect(box, this.settings.showWhitespace ? theme.activityActive : theme.panel2);
    this.drawRectOutline(box, theme.divider);
    if (this.settings.showWhitespace) this.drawCenteredText("✔️", box, this.buttonTextColor(true, checkboxHovered), "mini");
    this.renderer.text(checkboxText, box.x + box.w + this.ui(5), textY, checkboxHovered ? this.buttonTextColor(true, true) : theme.textDim, "ui");
    this.hits.push({ type: "statusWhitespace", rect: checkbox });
    x = checkbox.x - gap;

    const lineText = this.statusLineColumnText(doc);
    const lineW = this.renderer.measureText(lineText, "ui");
    this.renderer.text(lineText, x - lineW, textY, theme.textDim, "ui");
  }

  private statusLineColumnText(doc: TextDocument | undefined): string {
    return doc ? `Ln ${doc.selection.head.line + 1}, Col ${doc.selection.head.col + 1}` : "Ln -, Col -";
  }

  private highlightLabel(syntaxId: string): string {
    return HIGHLIGHT_OPTIONS.find((option) => option.id === syntaxId)?.label ?? syntaxId;
  }

  private toggleStatusWhitespace(): void {
    this.settings.showWhitespace = !this.settings.showWhitespace;
    this.statusText = this.settings.showWhitespace ? "Show whitespace" : "Hide whitespace";
    this.saveAndApplySettings();
  }

  private toggleChatShowThinking(): void {
    this.settings.showThinking = !this.settings.showThinking;
    this.statusText = this.settings.showThinking ? "Show thinking" : "Hide thinking";
    this.saveAndApplySettings();
  }

  private drawCenteredText(text: string, rect: Rect, color: Color, font: FontName): void {
    const bounds = this.renderer.visualTextBounds(text, font);
    const x = rect.x + rect.w / 2 - (bounds.x + bounds.w / 2);
    const y = rect.y + rect.h / 2 - (bounds.y + bounds.h / 2);
    this.renderer.text(text, x, y, color, font);
  }

  private drawClippedText(text: string, rect: Rect, y: number, color: Color, font: FontName, overflowAlign: "left" | "right" = "left"): void {
    if (rect.w <= 0 || rect.h <= 0 || !text) return;
    const textW = this.renderer.measureText(text, font);
    const x = overflowAlign === "right" && textW > rect.w ? rect.x + rect.w - textW : rect.x;
    this.renderer.pushClip(rect);
    this.renderer.text(text, x, y, color, font);
    this.renderer.popClip();
  }

  private drawContextMenu(): void {
    const menu = this.contextMenu;
    if (!menu) return;
    this.renderer.rect(menu.rect, [theme.activity[0], theme.activity[1], theme.activity[2], 0.98]);
    this.renderer.rect({ x: menu.rect.x, y: menu.rect.y, w: menu.rect.w, h: 1 }, theme.divider);
    this.renderer.rect({ x: menu.rect.x, y: menu.rect.y + menu.rect.h - 1, w: menu.rect.w, h: 1 }, theme.divider);
    this.renderer.rect({ x: menu.rect.x, y: menu.rect.y, w: 1, h: menu.rect.h }, theme.divider);
    this.renderer.rect({ x: menu.rect.x + menu.rect.w - 1, y: menu.rect.y, w: 1, h: menu.rect.h }, theme.divider);
    for (const item of menu.items) {
      if (!isContextMenuItem(item)) {
        this.renderer.rect(item.rect, theme.divider);
        continue;
      }
      if (item.enabled && this.contextMenuHover === item.command) this.renderer.rect(item.rect, theme.activityActive);
      this.drawClippedText(item.label, { x: item.rect.x + this.ui(12), y: item.rect.y, w: Math.max(0, item.rect.w - this.ui(20)), h: item.rect.h }, item.rect.y + this.ui(7), item.enabled ? theme.text : theme.textDim, "ui", "right");
      this.hits.push({ type: "contextMenu", command: item.command, rect: item.rect, enabled: item.enabled });
    }
  }

  private drawModal(): void {
    const modal = this.modal;
    if (!modal) return;
    const vp = this.viewport.get();
    const buttonH = this.ui(MODAL_BUTTON_H);
    const buttonGap = this.ui(MODAL_BUTTON_GAP);
    const dialogW = Math.min(this.ui(MODAL_WIDTH), Math.max(this.ui(260), vp.cssWidth - this.ui(32)));
    const contentW = dialogW - this.ui(40);
    const messageLines = this.wrapTextForWidth(modal.message, contentW, "ui");
    const detailLines = this.wrapTextForWidth(modal.detail, contentW, "ui");
    const lineH = this.ui(18);
    const textH = messageLines.length * lineH + detailLines.length * lineH;
    const progressH = modal.kind === "zipProgress" ? this.ui(26) : 0;
    const dialogH = Math.max(this.ui(168), this.ui(92) + textH + progressH + buttonH);
    const dialog = {
      x: Math.max(this.ui(12), (vp.cssWidth - dialogW) / 2),
      y: Math.max(this.ui(12), (vp.cssHeight - dialogH) / 2),
      w: dialogW,
      h: dialogH
    };

    this.renderer.rect({ x: 0, y: 0, w: vp.cssWidth, h: vp.cssHeight }, [0, 0, 0, 0.48]);
    this.renderer.rect(dialog, [theme.panel2[0], theme.panel2[1], theme.panel2[2], 0.99]);
    this.renderer.rect({ x: dialog.x, y: dialog.y, w: dialog.w, h: 1 }, theme.divider);
    this.renderer.rect({ x: dialog.x, y: dialog.y + dialog.h - 1, w: dialog.w, h: 1 }, theme.divider);
    this.renderer.rect({ x: dialog.x, y: dialog.y, w: 1, h: dialog.h }, theme.divider);
    this.renderer.rect({ x: dialog.x + dialog.w - 1, y: dialog.y, w: 1, h: dialog.h }, theme.divider);
    this.renderer.text(modal.title, dialog.x + this.ui(20), dialog.y + this.ui(18), theme.text, "title");

    let y = dialog.y + this.ui(52);
    for (const line of messageLines) {
      this.renderer.text(line, dialog.x + this.ui(20), y, theme.text, "ui");
      y += lineH;
    }
    y += this.ui(4);
    for (const line of detailLines) {
      this.renderer.text(line, dialog.x + this.ui(20), y, theme.textDim, "ui");
      y += lineH;
    }
    if (modal.kind === "zipProgress") {
      y += this.ui(6);
      const track = { x: dialog.x + this.ui(20), y, w: contentW, h: this.ui(8) };
      this.renderer.rect(track, theme.activity);
      this.renderer.rect({ x: track.x, y: track.y, w: Math.max(1, track.w * clamp(modal.progress, 0, 1)), h: track.h }, theme.accent);
      this.drawRectOutline(track, theme.divider);
      y += this.ui(20);
    }

    const buttonsW = modal.buttons.reduce((sum, button) => sum + this.modalButtonWidth(button), 0) + buttonGap * Math.max(0, modal.buttons.length - 1);
    let x = dialog.x + dialog.w - this.ui(20) - buttonsW;
    const buttonY = dialog.y + dialog.h - buttonH - this.ui(20);
    for (const button of modal.buttons) {
      const w = this.modalButtonWidth(button);
      button.rect = { x, y: buttonY, w, h: buttonH };
      const enabled = button.enabled && !modal.pending;
      const hovered = enabled && this.modalHover === button.action;
      this.renderer.rect(button.rect, this.modalButtonColor(button.variant, hovered, enabled));
      this.drawCenteredText(button.label, button.rect, enabled ? theme.text : theme.textDim, "ui");
      this.hits.push({ type: "modalButton", action: button.action, rect: button.rect, enabled });
      x += w + buttonGap;
    }
  }

  private modalButtonWidth(button: ModalButton): number {
    return Math.max(this.ui(82), this.renderer.measureText(button.label, "ui") + this.ui(24));
  }

  private modalButtonColor(variant: ModalButtonVariant, hovered: boolean, enabled: boolean): Color {
    const base = variant === "danger" ? theme.error : variant === "primary" ? theme.accent : theme.activityActive;
    const alpha = enabled ? 1 : 0.55;
    if (!hovered) return [base[0], base[1], base[2], alpha];
    return [Math.min(1, base[0] + 0.08), Math.min(1, base[1] + 0.08), Math.min(1, base[2] + 0.08), alpha];
  }

  private wrapTextForWidth(text: string, width: number, font: "ui" | "title" | "code"): string[] {
    const words = text.split(/\s+/).filter(Boolean);
    const lines: string[] = [];
    let line = "";
    for (const word of words) {
      if (this.renderer.measureText(word, font) > width) {
        if (line) {
          lines.push(line);
          line = "";
        }
        lines.push(...this.breakWordForWidth(word, width, font));
        continue;
      }
      const next = line ? `${line} ${word}` : word;
      if (!line || this.renderer.measureText(next, font) <= width) {
        line = next;
        continue;
      }
      lines.push(line);
      line = word;
    }
    if (line) lines.push(line);
    return lines.length ? lines : [""];
  }

  private breakWordForWidth(word: string, width: number, font: "ui" | "title" | "code"): string[] {
    const chunks: string[] = [];
    let chunk = "";
    for (const char of word) {
      const next = chunk + char;
      if (chunk && this.renderer.measureText(next, font) > width) {
        chunks.push(chunk);
        chunk = char;
      } else {
        chunk = next;
      }
    }
    if (chunk) chunks.push(chunk);
    return chunks.length ? chunks : [word];
  }

  private caretRect(doc: TextDocument, editorRect = this.activeEditorRect()): Rect {
    return this.positionRectForDoc(doc, editorRect, doc.selection.head);
  }

  private positionRectForDoc(doc: TextDocument, editorRect: Rect, pos: { line: number; col: number }): Rect {
    const contentRect = this.editorContentRect(doc, editorRect);
    const lineH = this.renderer.lineHeight("code");
    const clamped = doc.clampPosition(pos);
    const line = doc.lines[clamped.line] ?? "";
    const prefixWidth = this.measureCodePrefix(line, clamped.col);
    const scroll = this.scrollForDoc(doc.id);
    return {
      x: this.editorTextX(doc, contentRect) + prefixWidth - scroll.x,
      y: contentRect.y + clamped.line * lineH - scroll.y,
      w: 2,
      h: lineH
    };
  }

  private positionFromPoint(x: number, y: number): { line: number; col: number } {
    const doc = this.activeDoc();
    if (!doc) return { line: 0, col: 0 };
    return this.positionFromPointInEditor(doc, this.activeEditorRect(), x, y);
  }

  private positionFromPointInEditor(doc: TextDocument, editorRect: Rect, x: number, y: number): { line: number; col: number } {
    const lineH = this.renderer.lineHeight("code");
    const contentRect = this.editorContentRect(doc, editorRect);
    const scroll = this.scrollForDoc(doc.id);
    const line = clamp(Math.floor((y - contentRect.y + scroll.y) / lineH), 0, doc.lineCount() - 1);
    const textX = this.editorTextX(doc, contentRect);
    const col = this.columnFromCodeTextOffset(doc.lines[line]!, x - textX + scroll.x);
    return { line, col };
  }

  private columnFromTextOffset(text: string, offset: number, font: FontName = "code"): number {
    if (offset <= 0) return 0;
    let x = 0;
    let col = 0;
    for (const char of text) {
      const advance = this.renderer.measureText(char, font);
      if (offset < x + advance / 2) return col;
      x += advance;
      col += char.length;
    }
    return text.length;
  }

  private columnFromCodeTextOffset(text: string, offset: number): number {
    if (offset <= 0) return 0;
    let x = 0;
    let col = 0;
    for (const char of text) {
      const advance = this.codeAdvanceForChar(char, x);
      if (offset < x + advance / 2) return col;
      x += advance;
      col += char.length;
    }
    return text.length;
  }

  private tabHitState(type: "tab" | "tabClose"): Array<{ path: string; rect: Rect }> {
    return this.hits
      .filter((hit): hit is Extract<HitItem, { type: "tab" | "tabClose" }> => hit.type === type)
      .map((hit) => ({ path: this.tabLabel(hit.docId), rect: hit.rect }));
  }

  private activeEditorRect(): Rect {
    return this.activeGroup().editorRect;
  }

  private isActiveDocumentInGroup(group: EditorGroup, docId: string): boolean {
    return group.id === this.activeGroupId && group.activeDocId === docId && this.activeDocId === docId;
  }

  private isDocumentCaretVisible(group: EditorGroup, docId: string): boolean {
    const doc = this.docs.get(docId);
    return !doc?.readOnly && this.input.activeTarget?.kind === "editor" && !this.renamePath && this.isActiveDocumentInGroup(group, docId);
  }

  private hasBlinkingCaretOwner(): boolean {
    const kind = this.input.activeTarget?.kind;
    return Boolean(this.renamePath || this.activeSettingsNumber || kind === "search" || kind === "chat" || kind === "projectReplace" || kind === "find" || kind === "findReplace" || (kind === "editor" && this.activeDocId));
  }

  private isRenameCaretVisible(): boolean {
    return Boolean(this.renamePath && (this.input.composing || this.isCaretBlinkOn()));
  }

  private isSearchCaretVisible(): boolean {
    return this.input.activeTarget?.kind === "search" && (this.input.composing || this.isCaretBlinkOn());
  }

  private allDockTargets(): DockTarget[] {
    return this.groups.flatMap((group) => this.dockTargetShapes(group));
  }

  private dockTargetShapes(group: EditorGroup): DockTarget[] {
    const outer = group.editorRect;
    if (outer.w <= 20 || outer.h <= 20) return [];
    const centerW = outer.w * DOCK_CENTER_TARGET_RATIO;
    const centerH = outer.h * DOCK_CENTER_TARGET_RATIO;
    const center: Rect = {
      x: outer.x + outer.w * DOCK_EDGE_TARGET_RATIO,
      y: outer.y + outer.h * DOCK_EDGE_TARGET_RATIO,
      w: centerW,
      h: centerH
    };
    const leftW = outer.w * DOCK_EDGE_TARGET_RATIO;
    const rightX = outer.x + outer.w * (DOCK_EDGE_TARGET_RATIO + DOCK_CENTER_TARGET_RATIO);
    const rightW = outer.x + outer.w - rightX;
    const topH = outer.h * DOCK_EDGE_TARGET_RATIO;
    const bottomY = outer.y + outer.h * (DOCK_EDGE_TARGET_RATIO + DOCK_CENTER_TARGET_RATIO);
    const bottomH = outer.y + outer.h - bottomY;
    const outerTL = { x: outer.x, y: outer.y };
    const outerTR = { x: outer.x + outer.w, y: outer.y };
    const outerBR = { x: outer.x + outer.w, y: outer.y + outer.h };
    const outerBL = { x: outer.x, y: outer.y + outer.h };
    const centerTL = { x: center.x, y: center.y };
    const centerTR = { x: center.x + center.w, y: center.y };
    const centerBR = { x: center.x + center.w, y: center.y + center.h };
    const centerBL = { x: center.x, y: center.y + center.h };
    return [
      { groupId: group.id, zone: "top", polygon: [outerTL, outerTR, centerTR, centerTL], previewRect: { x: outer.x, y: outer.y, w: outer.w, h: topH } },
      { groupId: group.id, zone: "right", polygon: [centerTR, outerTR, outerBR, centerBR], previewRect: { x: rightX, y: outer.y, w: rightW, h: outer.h } },
      { groupId: group.id, zone: "bottom", polygon: [centerBL, centerBR, outerBR, outerBL], previewRect: { x: outer.x, y: bottomY, w: outer.w, h: bottomH } },
      { groupId: group.id, zone: "left", polygon: [outerTL, centerTL, centerBL, outerBL], previewRect: { x: outer.x, y: outer.y, w: leftW, h: outer.h } },
      { groupId: group.id, zone: "center", polygon: rectPoints(center), previewRect: center }
    ];
  }
}

function tokenColor(type: TokenType) {
  if (type === "normal") return theme.text;
  if (type === "keyword") return theme.keyword;
  if (type === "string") return theme.string;
  if (type === "number") return theme.number;
  if (type === "comment") return theme.comment;
  if (type === "operator") return theme.operator;
  if (type === "function") return theme.function;
  return theme.type;
}

function colorToCss(color: Color, alpha = color[3]): string {
  const r = Math.round(clamp(color[0], 0, 1) * 255);
  const g = Math.round(clamp(color[1], 0, 1) * 255);
  const b = Math.round(clamp(color[2], 0, 1) * 255);
  return `rgb(${r} ${g} ${b} / ${Math.round(clamp(alpha, 0, 1) * 1000) / 10}%)`;
}

function isEditorContextMenuCommand(command: ContextMenuCommand): command is EditorContextMenuCommand {
  return command === "cut" || command === "copy" || command === "paste" || command === "systemCopy" || command === "systemPaste" || command === "undo" || command === "redo";
}

function isTabContextMenuCommand(command: ContextMenuCommand): command is TabContextMenuCommand {
  return command === "save" || command === "findInFile" || command === "close" || command === "closeOthers" || command === "resetSettings";
}

function isTabBarContextMenuCommand(command: ContextMenuCommand): command is TabBarContextMenuCommand {
  return command === "newFile" || command === "uploadFile" || command === "closeAll";
}

function tabOverflowCommand(docId: string): TabOverflowContextMenuCommand {
  return `selectTab:${docId}`;
}

function tabOverflowCommandDocId(command: ContextMenuCommand): string | null {
  return command.startsWith("selectTab:") ? command.slice("selectTab:".length) : null;
}

function highlightCommand(syntaxId: string): HighlightContextMenuCommand {
  return `highlight:${syntaxId}`;
}

function highlightCommandSyntaxId(command: ContextMenuCommand): string | null {
  if (!command.startsWith("highlight:")) return null;
  const syntaxId = command.slice("highlight:".length);
  return HIGHLIGHT_OPTIONS.some((option) => option.id === syntaxId) ? syntaxId : null;
}

function isSettingContextMenuCommand(command: ContextMenuCommand): command is SettingContextMenuCommand {
  return command === "themeDark"
    || command === "themeLight"
    || command === "aiProviderLocal"
    || command === "aiProviderOpenAI"
    || command === "aiToolFormatNone"
    || command === "aiToolFormatTag"
    || command === "aiToolFormatHarmony"
    || command.startsWith("aiModel:");
}

function isSettingTextField(field: TextFieldKey): field is SettingTextKey {
  return field === "aiBaseUrl" || field === "aiApiKey" || field === "aiModel" || field === "aiMaxContextTokens";
}

function aiModelCommand(modelId: string): SettingContextMenuCommand {
  return `aiModel:${encodeURIComponent(modelId)}` as SettingContextMenuCommand;
}

function aiModelCommandValue(command: ContextMenuCommand): string | null {
  if (!command.startsWith("aiModel:")) return null;
  try {
    return decodeURIComponent(command.slice("aiModel:".length));
  } catch {
    return command.slice("aiModel:".length);
  }
}

function isContextMenuItem(entry: ContextMenuEntry): entry is ContextMenuItem {
  return entry.kind === "item";
}

function cloneSelectionState(selection: Selection): Selection {
  return {
    anchor: { line: selection.anchor.line, col: selection.anchor.col },
    head: { line: selection.head.line, col: selection.head.col }
  };
}

function isMobileWebKit(): boolean {
  return (navigator.maxTouchPoints > 0 || window.matchMedia("(pointer: coarse)").matches) && /AppleWebKit/i.test(navigator.userAgent);
}

function isIOSDevice(): boolean {
  return /iPad|iPhone|iPod/i.test(navigator.userAgent) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
}

function isFileContextMenuCommand(command: ContextMenuCommand): command is FileContextMenuCommand {
  return command === "rename" || command === "duplicate" || command === "delete";
}

function isFolderContextMenuCommand(command: ContextMenuCommand): command is FolderContextMenuCommand {
  return command === "rename" || command === "delete" || command === "createFile" || command === "createFolder" || command === "uploadFile";
}

function loadSettings(): AppSettings {
  try {
    const raw = localStorage.getItem(SETTINGS_STORAGE_KEY);
    if (!raw) return { ...DEFAULT_SETTINGS };
    return normalizeSettings(JSON.parse(raw) as Partial<AppSettings>);
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
}

function normalizeSettings(value: Partial<AppSettings> | null | undefined): AppSettings {
  const fontSize = Number(value?.fontSize);
  const uiScale = Number(value?.uiScale);
  const tabSpaces = Number(value?.tabSpaces);
  const aiMaxToolCalls = Number(value?.aiMaxToolCalls);
  const aiCompactFreePercent = Number(value?.aiCompactFreePercent);
  return {
    theme: value?.theme === "light" ? "light" : "dark",
    fontSize: Number.isFinite(fontSize) ? Math.max(1, fontSize) : DEFAULT_SETTINGS.fontSize,
    uiScale: Number.isFinite(uiScale) ? clamp(Math.trunc(uiScale), 1, 400) : DEFAULT_SETTINGS.uiScale,
    monospacedFont: typeof value?.monospacedFont === "boolean" ? value.monospacedFont : DEFAULT_SETTINGS.monospacedFont,
    tabSpaces: Number.isFinite(tabSpaces) ? clamp(Math.trunc(tabSpaces), 1, 32) : DEFAULT_SETTINGS.tabSpaces,
    useTabStops: typeof value?.useTabStops === "boolean" ? value.useTabStops : DEFAULT_SETTINGS.useTabStops,
    showWhitespace: typeof value?.showWhitespace === "boolean" ? value.showWhitespace : DEFAULT_SETTINGS.showWhitespace,
    showThinking: typeof value?.showThinking === "boolean" ? value.showThinking : DEFAULT_SETTINGS.showThinking,
    renameOnDoubleClick: typeof value?.renameOnDoubleClick === "boolean" ? value.renameOnDoubleClick : DEFAULT_SETTINGS.renameOnDoubleClick,
    showLineNumbers: typeof value?.showLineNumbers === "boolean" ? value.showLineNumbers : DEFAULT_SETTINGS.showLineNumbers,
    rememberOpenFiles: typeof value?.rememberOpenFiles === "boolean" ? value.rememberOpenFiles : DEFAULT_SETTINGS.rememberOpenFiles,
    aiProvider: value?.aiProvider === "local" ? "local" : "openai",
    aiModelManual: typeof value?.aiModelManual === "boolean" ? value.aiModelManual : DEFAULT_SETTINGS.aiModelManual,
    aiMaxToolCalls: Number.isFinite(aiMaxToolCalls) ? clamp(Math.trunc(aiMaxToolCalls), 1, 200) : DEFAULT_SETTINGS.aiMaxToolCalls,
    aiDetectDuplicateToolCalls: typeof value?.aiDetectDuplicateToolCalls === "boolean" ? value.aiDetectDuplicateToolCalls : DEFAULT_SETTINGS.aiDetectDuplicateToolCalls,
    aiToolCallFormat: value?.aiToolCallFormat === "harmony" || value?.aiToolCallFormat === "none" ? value.aiToolCallFormat : "tag",
    aiCompactFreePercent: Number.isFinite(aiCompactFreePercent) ? clamp(Math.trunc(aiCompactFreePercent), 1, 95) : DEFAULT_SETTINGS.aiCompactFreePercent,
    aiInsertEditorContext: typeof value?.aiInsertEditorContext === "boolean" ? value.aiInsertEditorContext : DEFAULT_SETTINGS.aiInsertEditorContext
  };
}

function modalButton(action: ModalAction, label: string, variant: ModalButtonVariant): ModalButton {
  return { action, label, variant, rect: { x: 0, y: 0, w: 0, h: 0 }, enabled: true };
}

function formatToolArgsForModal(args: unknown[]): string {
  let text: string;
  try {
    text = JSON.stringify(args);
  } catch {
    text = String(args);
  }
  return text.length > 220 ? `${text.slice(0, 217)}...` : text;
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 102.4) / 10} KB`;
  return `${Math.round(bytes / 1024 / 102.4) / 10} MB`;
}

function downloadTimestamp(date = new Date()): string {
  const pad = (value: number) => String(value).padStart(2, "0");
  return `${date.getFullYear()}${pad(date.getMonth() + 1)}${pad(date.getDate())}-${pad(date.getHours())}${pad(date.getMinutes())}${pad(date.getSeconds())}`;
}

function nextFrame(): Promise<void> {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}

function dataTransferContainsFiles(dataTransfer: DataTransfer): boolean {
  return Array.from(dataTransfer.types ?? []).includes("Files") || (dataTransfer.files?.length ?? 0) > 0;
}

function isZipFile(file: File): boolean {
  return /\.zip$/i.test(file.name) || file.type === "application/zip" || file.type === "application/x-zip-compressed";
}

function pathForZipEntry(name: string): string | null {
  const normalized = normalizePath(`/${name}`);
  if (normalized === "/" || normalized.startsWith("/.slug-")) return null;
  const parts = normalized.split("/").filter(Boolean);
  if (parts.includes("__MACOSX") || parts.some((part) => part === ".DS_Store")) return null;
  return normalized;
}

function guessMime(path: string): string {
  return path.match(/\.(ts|tsx|js|jsx|json|md|txt|css|html|lua|cpp|c|h|hpp|rs|py|go|java|cs)$/i) ? "text/plain" : "application/octet-stream";
}

function isValidFileName(name: string): boolean {
  return name.length > 0 && name !== "." && name !== ".." && invalidFileNameCharacterRanges(name).length === 0;
}

function invalidFileNameCharacterRanges(name: string): Array<{ start: number; end: number }> {
  const ranges: Array<{ start: number; end: number }> = [];
  for (let index = 0; index < name.length;) {
    const codePoint = name.codePointAt(index);
    if (codePoint === undefined) break;
    const char = String.fromCodePoint(codePoint);
    const end = index + char.length;
    if (isInvalidFileNameCharacter(char, codePoint)) ranges.push({ start: index, end });
    index = end;
  }
  return ranges;
}

function isInvalidFileNameCharacter(char: string, codePoint: number): boolean {
  return codePoint < 32 || codePoint === 127 || /[<>:"/\\|?*]/.test(char);
}

function isWordChar(char: string): boolean {
  return /[A-Za-z0-9_]/.test(char);
}

function wordRangeAt(text: string, col: number): { start: number; end: number } {
  if (!text) return { start: 0, end: 0 };
  let index = clamp(col, 0, Math.max(0, text.length - 1));
  if (!isWordChar(text.charAt(index)) && col > 0 && isWordChar(text.charAt(col - 1))) index = col - 1;
  let start = index;
  let end = index + 1;
  if (isWordChar(text.charAt(index))) {
    while (start > 0 && isWordChar(text.charAt(start - 1))) start--;
    while (end < text.length && isWordChar(text.charAt(end))) end++;
  }
  return { start, end };
}

function findLastIndex<T>(items: T[], predicate: (item: T) => boolean): number {
  for (let index = items.length - 1; index >= 0; index--) {
    if (predicate(items[index]!)) return index;
  }
  return -1;
}

function textEqualsFindQuery(text: string, query: string): boolean {
  return text.length === query.length && text.toLowerCase() === query.toLowerCase();
}

function replaceAllPlainText(text: string, query: string, replacement: string): { text: string; count: number } {
  if (!query) return { text, count: 0 };
  const haystack = text.toLowerCase();
  const needle = query.toLowerCase();
  const parts: string[] = [];
  let count = 0;
  let cursor = 0;
  while (cursor <= text.length) {
    const found = haystack.indexOf(needle, cursor);
    if (found < 0) break;
    parts.push(text.slice(cursor, found), replacement);
    cursor = found + query.length;
    count++;
  }
  if (count === 0) return { text, count };
  parts.push(text.slice(cursor));
  return { text: parts.join(""), count };
}

function sanitizeSingleLineInput(text: string): string {
  return text.replaceAll("\r\n", " ").replaceAll("\r", " ").replaceAll("\n", " ");
}

function sanitizeUploadedFileName(name: string): string {
  const leaf = name.replaceAll("\\", "/").split("/").filter(Boolean).pop() ?? "";
  let sanitized = leaf.trim().replace(new RegExp("[\\x00-\\x1f\\x7f<>:\"/\\\\|?*]", "g"), "_");
  if (!isValidFileName(sanitized)) sanitized = `upload-${shortHexName()}`;
  return sanitized;
}

function fileStemSelectionEnd(name: string): number {
  const dot = name.lastIndexOf(".");
  return dot > 0 ? dot : name.length;
}

function isSameOrDescendant(path: string, root: string): boolean {
  const normalizedPath = normalizePath(path);
  const normalizedRoot = normalizePath(root);
  return normalizedPath === normalizedRoot || normalizedPath.startsWith(`${normalizedRoot}/`);
}

function remapSelectedTreePath(path: string | null, oldPath: string, newPath: string): string | null {
  if (!path || !isSameOrDescendant(path, oldPath)) return path;
  return path === normalizePath(oldPath) ? normalizePath(newPath) : joinPath(newPath, path.slice(normalizePath(oldPath).length + 1));
}

function shortHexName(): string {
  if (crypto.getRandomValues) {
    const bytes = new Uint8Array(4);
    crypto.getRandomValues(bytes);
    return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
  }
  return Math.floor(Math.random() * 0xffffffff).toString(16).padStart(8, "0");
}

function wrapText(text: string, width: number): string[] {
  const lines: string[] = [];
  for (const rawLine of text.split("\n")) {
    let line = rawLine;
    while (line.length > width) {
      lines.push(line.slice(0, width));
      line = line.slice(width);
    }
    lines.push(line);
  }
  return lines;
}

function sortFileTree(entries: FileTreeEntry[]): void {
  entries.sort((a, b) => {
    if (a.type !== b.type) return a.type === "dir" ? -1 : 1;
    return a.name.localeCompare(b.name, undefined, { sensitivity: "base" });
  });
  for (const entry of entries) {
    if (entry.type === "dir") sortFileTree(entry.children);
  }
}

function makeGroup(id: string): EditorGroup {
  return {
    id,
    tabs: [],
    activeDocId: null,
    frameRect: { x: 0, y: 0, w: 0, h: 0 },
    editorRect: { x: 0, y: 32, w: 0, h: 0 }
  };
}

function collectDockGroups(node: DockNode): EditorGroup[] {
  if (node.type === "leaf") return [node.group];
  return node.children.flatMap((child) => collectDockGroups(child));
}

function makeDockSplit(direction: SplitDirection, children: DockNode[], weights: number[] = children.map(() => 1)): DockNode {
  return {
    type: "split",
    id: `split-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`,
    direction,
    children,
    weights: normalizeWeightsForCount(weights, children.length)
  };
}

function findDockSplitNode(node: DockNode, id: string): Extract<DockNode, { type: "split" }> | null {
  if (node.type === "leaf") return null;
  if (node.id === id) return node;
  for (const child of node.children) {
    const found = findDockSplitNode(child, id);
    if (found) return found;
  }
  return null;
}

function normalizeSplitWeights(node: Extract<DockNode, { type: "split" }>): number[] {
  node.weights = normalizeWeightsForCount(node.weights, node.children.length);
  return node.weights;
}

function normalizeWeightsForCount(weights: readonly number[], count: number): number[] {
  const normalized = weights.slice(0, count).map((weight) => Number.isFinite(weight) && weight > 0 ? weight : 1);
  while (normalized.length < count) normalized.push(1);
  return normalized;
}

function cloneDockNode(node: DockNode): DockNode {
  if (node.type === "leaf") {
    return {
      type: "leaf",
      group: {
        id: node.group.id,
        tabs: [...node.group.tabs],
        activeDocId: node.group.activeDocId,
        frameRect: { ...node.group.frameRect },
        editorRect: { ...node.group.editorRect }
      }
    };
  }
  return { type: "split", id: node.id, direction: node.direction, children: node.children.map((child) => cloneDockNode(child)), weights: normalizeWeightsForCount(node.weights, node.children.length) };
}

function replaceLeafNode(node: DockNode, groupId: string, replacement: DockNode): DockNode | null {
  if (node.type === "leaf") return node.group.id === groupId ? replacement : null;
  const children = node.children.map((child) => replaceLeafNode(child, groupId, replacement) ?? child);
  return { ...node, children, weights: normalizeWeightsForCount(node.weights, children.length) };
}

function pruneDockNode(node: DockNode): DockNode | null {
  if (node.type === "leaf") return node.group.tabs.length === 0 ? null : node;
  const sourceWeights = normalizeWeightsForCount(node.weights, node.children.length);
  const children: DockNode[] = [];
  const weights: number[] = [];
  for (let i = 0; i < node.children.length; i++) {
    const child = pruneDockNode(node.children[i]!);
    if (!child) continue;
    children.push(child);
    weights.push(sourceWeights[i]!);
  }
  if (children.length === 0) return null;
  if (children.length === 1) return children[0]!;
  return { ...node, children, weights: normalizeWeightsForCount(weights, children.length) };
}

function persistDockNode(node: DockNode, docPathForId: (docId: string) => string | null): PersistedDockNode | null {
  if (node.type === "leaf") {
    const paths = node.group.tabs.map(docPathForId).filter((path): path is string => Boolean(path));
    if (paths.length === 0) return null;
    const activePath = node.group.activeDocId ? docPathForId(node.group.activeDocId) : null;
    return { type: "leaf", group: { id: node.group.id, paths, activePath: activePath && paths.includes(activePath) ? activePath : paths[0]! } };
  }
  const children: PersistedDockNode[] = [];
  const weights: number[] = [];
  const sourceWeights = normalizeWeightsForCount(node.weights, node.children.length);
  for (let i = 0; i < node.children.length; i++) {
    const child = persistDockNode(node.children[i]!, docPathForId);
    if (!child) continue;
    children.push(child);
    weights.push(sourceWeights[i]!);
  }
  if (children.length === 0) return null;
  if (children.length === 1) return children[0]!;
  return { type: "split", id: node.id, direction: node.direction, children, weights: normalizeWeightsForCount(weights, children.length) };
}

function persistedDockPathCount(node: PersistedDockNode): number {
  if (node.type === "leaf") return node.group.paths.length;
  return node.children.reduce((sum, child) => sum + persistedDockPathCount(child), 0);
}

function persistedDockPaths(node: PersistedDockNode): string[] {
  if (node.type === "leaf") return node.group.paths;
  return node.children.flatMap((child) => persistedDockPaths(child));
}

function restorePersistedDockNode(node: PersistedDockNode, pathToDocId: Map<string, string>): DockNode | null {
  if (node.type === "leaf") {
    const tabs = node.group.paths.map((path) => pathToDocId.get(path)).filter((id): id is string => Boolean(id));
    if (tabs.length === 0) return null;
    const activeDocId = node.group.activePath ? pathToDocId.get(node.group.activePath) ?? null : null;
    return {
      type: "leaf",
      group: {
        id: node.group.id || `group-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
        tabs,
        activeDocId: activeDocId && tabs.includes(activeDocId) ? activeDocId : tabs[0]!,
        frameRect: { x: 0, y: 0, w: 0, h: 0 },
        editorRect: { x: 0, y: 32, w: 0, h: 0 }
      }
    };
  }
  const children: DockNode[] = [];
  const weights: number[] = [];
  const sourceWeights = normalizeWeightsForCount(node.weights, node.children.length);
  for (let i = 0; i < node.children.length; i++) {
    const child = restorePersistedDockNode(node.children[i]!, pathToDocId);
    if (!child) continue;
    children.push(child);
    weights.push(sourceWeights[i]!);
  }
  if (children.length === 0) return null;
  if (children.length === 1) return children[0]!;
  return { type: "split", id: node.id || `split-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 7)}`, direction: node.direction, children, weights: normalizeWeightsForCount(weights, children.length) };
}

function restoredDockTabCount(node: DockNode): number {
  if (node.type === "leaf") return node.group.tabs.length;
  return node.children.reduce((sum, child) => sum + restoredDockTabCount(child), 0);
}

function normalizePersistedSession(value: unknown): PersistedSession | null {
  if (!value || typeof value !== "object") return null;
  const raw = value as Partial<PersistedSession>;
  const dockRoot = normalizePersistedDockNode(raw.dockRoot);
  if (!dockRoot) return null;
  return {
    version: 1,
    activePath: typeof raw.activePath === "string" ? normalizePath(raw.activePath) : null,
    activeGroupId: typeof raw.activeGroupId === "string" ? raw.activeGroupId : null,
    sidebarMode: raw.sidebarMode === "search" || raw.sidebarMode === "chat" || raw.sidebarMode === "settings" ? raw.sidebarMode : "files",
    sidebarWidth: Number.isFinite(raw.sidebarWidth) ? Math.max(0, Number(raw.sidebarWidth)) : 280,
    lastSidebarWidth: Number.isFinite(raw.lastSidebarWidth) ? Math.max(0, Number(raw.lastSidebarWidth)) : 280,
    dockRoot,
    scrollStates: normalizePersistedScrollStates(raw.scrollStates)
  };
}

function normalizePersistedDockNode(value: unknown): PersistedDockNode | null {
  if (!value || typeof value !== "object") return null;
  const raw = value as PersistedDockNode;
  if (raw.type === "leaf") {
    const group = raw.group;
    if (!group || !Array.isArray(group.paths)) return null;
    const paths = [...new Set(group.paths.filter((path): path is string => typeof path === "string").map((path) => normalizePath(path)))];
    if (paths.length === 0) return null;
    const activePath = typeof group.activePath === "string" ? normalizePath(group.activePath) : null;
    return { type: "leaf", group: { id: typeof group.id === "string" ? group.id : "", paths, activePath: activePath && paths.includes(activePath) ? activePath : paths[0]! } };
  }
  if (raw.type !== "split" || (raw.direction !== "row" && raw.direction !== "column") || !Array.isArray(raw.children)) return null;
  const children = raw.children.map((child) => normalizePersistedDockNode(child)).filter((child): child is PersistedDockNode => Boolean(child));
  if (children.length === 0) return null;
  if (children.length === 1) return children[0]!;
  return {
    type: "split",
    id: typeof raw.id === "string" ? raw.id : "",
    direction: raw.direction,
    children,
    weights: normalizeWeightsForCount(Array.isArray(raw.weights) ? raw.weights : [], children.length)
  };
}

function normalizePersistedScrollStates(value: unknown): Record<string, EditorScrollState> {
  if (!value || typeof value !== "object") return {};
  const result: Record<string, EditorScrollState> = {};
  for (const [path, scroll] of Object.entries(value as Record<string, Partial<EditorScrollState>>)) {
    if (!scroll || typeof scroll !== "object") continue;
    result[normalizePath(path)] = {
      x: Number.isFinite(scroll.x) ? Math.max(0, Number(scroll.x)) : 0,
      y: Number.isFinite(scroll.y) ? Math.max(0, Number(scroll.y)) : 0
    };
  }
  return result;
}

function insetRect(rect: Rect, amount: number): Rect {
  const inset = Math.min(amount, rect.w / 4, rect.h / 4);
  return { x: rect.x + inset, y: rect.y + inset, w: Math.max(1, rect.w - inset * 2), h: Math.max(1, rect.h - inset * 2) };
}

function rectIntersects(a: Rect, b: Rect): boolean {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

function intersectRect(a: Rect, b: Rect): Rect | null {
  const x = Math.max(a.x, b.x);
  const y = Math.max(a.y, b.y);
  const right = Math.min(a.x + a.w, b.x + b.w);
  const bottom = Math.min(a.y + a.h, b.y + b.h);
  if (right <= x || bottom <= y) return null;
  return { x, y, w: right - x, h: bottom - y };
}

function activityHoverColor(): Color {
  return [theme.activityActive[0], theme.activityActive[1], theme.activityActive[2], 0.58];
}

function rectPoints(rect: Rect): Point[] {
  return [
    { x: rect.x, y: rect.y },
    { x: rect.x + rect.w, y: rect.y },
    { x: rect.x + rect.w, y: rect.y + rect.h },
    { x: rect.x, y: rect.y + rect.h }
  ];
}

function octagonPoints(cx: number, cy: number, radius: number): Point[] {
  const inset = radius * 0.42;
  return [
    { x: cx - inset, y: cy - radius },
    { x: cx + inset, y: cy - radius },
    { x: cx + radius, y: cy - inset },
    { x: cx + radius, y: cy + inset },
    { x: cx + inset, y: cy + radius },
    { x: cx - inset, y: cy + radius },
    { x: cx - radius, y: cy + inset },
    { x: cx - radius, y: cy - inset }
  ];
}

function pointInPolygon(point: Point, polygon: Point[]): boolean {
  let inside = false;
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
    const a = polygon[i]!;
    const b = polygon[j]!;
    const crosses = (a.y > point.y) !== (b.y > point.y);
    if (crosses) {
      const x = ((b.x - a.x) * (point.y - a.y)) / (b.y - a.y) + a.x;
      if (point.x < x) inside = !inside;
    }
  }
  return inside;
}

async function copyText(text: string): Promise<void> {
  if (!text) return;
  if (navigator.clipboard && window.isSecureContext) try {
    await navigator.clipboard.writeText(text);
    return;
  } catch {
    // Fall through to the editable-element path. iOS Safari is stricter about async clipboard access.
  }
  const area = document.createElement("textarea");
  area.value = text;
  area.readOnly = true;
  area.style.position = "fixed";
  area.style.left = "0";
  area.style.top = "0";
  area.style.width = "2px";
  area.style.height = "24px";
  area.style.opacity = "0.01";
  area.style.zIndex = "10000";
  area.style.pointerEvents = "none";
  area.style.fontSize = "16px";
  document.body.appendChild(area);
  area.focus({ preventScroll: true });
  area.select();
  area.setSelectionRange(0, text.length);
  document.execCommand("copy");
  area.remove();
}

async function readClipboardText(): Promise<string | null> {
  if (!navigator.clipboard || !window.isSecureContext) return null;
  try {
    return await navigator.clipboard.readText();
  } catch {
    return null;
  }
}

export async function importFilesForTests(app: EditorApp, files: File[]): Promise<void> {
  await importFileList(app.vfs, files);
  await app.refreshFiles();
  app.scheduleDraw();
}
