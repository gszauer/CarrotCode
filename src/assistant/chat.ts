import { isUnsupportedFilePath } from "../editor/file_types";
import type { TextDocument } from "../editor/document";
import { uid } from "../shared/types";
import { basename, dirname, joinPath, normalizePath } from "../vfs/path";
import type { Vfs, VfsNode } from "../vfs/types";

export type ChatRole = "system" | "user" | "assistant" | "thinking" | "tool_call" | "tool_result";
export type AiToolCallFormat = "tag" | "harmony" | "none";
export type AiThinkingFormat = "auto" | "tag" | "harmony" | "none";

export type ChatMessage = {
  id: string;
  role: ChatRole;
  text: string;
  at: number;
  ok?: boolean | undefined;
  name?: string | undefined;
  internal?: boolean | undefined;
  displayText?: string | undefined;
  nativeToolCallId?: string | undefined;
  nativeToolArguments?: string | undefined;
};

export type ContextBundle = {
  activePath?: string;
  selectedText: string;
  openPaths: string[];
  fileTreePaths?: string[] | undefined;
  selectedFileTreePath?: string | undefined;
  openFileNames?: string[] | undefined;
};

export type AiEndpointConfig = {
  apiBaseUrl: string;
  apiKey: string;
  model: string;
  temperature: number;
  maxContextTokens: number;
};

export type AiModelInfo = { id: string; contextLength: number };
export type AiServerCheckResult = {
  ok: boolean;
  baseUrl: string;
  message: string;
  models: AiModelInfo[];
};

export type AiRuntimeSettings = {
  maxToolCallsPerTurn: number;
  detectDuplicateToolCalls: boolean;
  toolCallFormat: AiToolCallFormat;
  thinkingFormat: AiThinkingFormat;
  compactFreePercent: number;
};

type CompletionResult = {
  text: string;
  thinking: string;
  toolCalls: ParsedToolCall[];
  usageTotal: number;
  usagePrompt: number;
  usageCompletion: number;
  streamedMessage?: ChatMessage | undefined;
  streamedThinkingMessage?: ChatMessage | undefined;
};

type ParsedToolCall = {
  name: string;
  args: unknown[];
  raw: string;
  nativeId?: string | undefined;
  nativeArguments?: string | undefined;
};

type StreamedToolCallPart = {
  id: string;
  name: string;
  argumentsText: string;
};

type ApiToolCall = { id: string; type: "function"; function: { name: string; arguments: string } };
type ApiMessage =
  | { role: "system" | "user"; content: string }
  | { role: "assistant"; content: string; tool_calls?: ApiToolCall[] | undefined }
  | { role: "tool"; tool_call_id: string; content: string };
type ApiToolDefinition = {
  type: "function";
  function: {
    name: string;
    description: string;
    parameters: {
      type: "object";
      properties: Record<string, { type: string; description?: string | undefined }>;
      required?: string[] | undefined;
    };
  };
};

type ToolResult = {
  ok: boolean;
  output: string;
};

export type ToolCallLimitDecision = "allowMore" | "allowAll" | "stop";
export type DuplicateToolCallDecision = "allow" | "break";
export type DuplicateToolCallInfo = { name: string; args: unknown[]; raw: string };

export type AiWorkspaceChange =
  | { type: "write"; path: string; text?: string | undefined }
  | { type: "mkdir"; path: string }
  | { type: "remove"; path: string; recursive: boolean }
  | { type: "rename"; oldPath: string; newPath: string };

type AiWorkspaceChangeHandler = (change: AiWorkspaceChange) => void | Promise<void>;

type TurnOptions = {
  runtime: AiRuntimeSettings;
  editorContext?: ContextBundle | null | undefined;
  onUpdate?: (() => void) | undefined;
  onCompactStart?: (() => void) | undefined;
  onCompactEnd?: (() => void) | undefined;
  onToolCallLimit?: ((limit: number, used: number) => Promise<ToolCallLimitDecision>) | undefined;
  onDuplicateToolCall?: ((call: DuplicateToolCallInfo) => Promise<DuplicateToolCallDecision>) | undefined;
  onWorkspaceChange?: AiWorkspaceChangeHandler | undefined;
};

type TokenCounterSource = "none" | "probe" | "usage" | "refresh" | "estimate";

export type ChatTokenUsage = {
  calibrated: boolean;
  dirty: boolean;
  basePromptTokens: number;
  promptTokens: number;
  lastPromptTokens: number;
  lastCompletionTokens: number;
  lastTotalTokens: number;
  source: TokenCounterSource;
};

type TokenCounterState = ChatTokenUsage & {
  key: string;
};

type ApiUsage = {
  totalTokens: number;
  promptTokens: number;
  completionTokens: number;
};
type GrepMatcher = { test(line: string): boolean };

const AI_CONFIG_STORAGE_KEY = "slug.aiEndpointConfig";
const AI_SYSTEM_PROMPT_STORAGE_KEY = "slug.aiSystemPrompt";
const AI_COMPACT_PROMPT_STORAGE_KEY = "slug.aiCompactPrompt";
const AI_TAG_TOOL_PROMPT_STORAGE_KEY = "slug.aiTagToolPrompt";
const AI_HARMONY_TOOL_PROMPT_STORAGE_KEY = "slug.aiHarmonyToolPrompt";
const PROBE_USER_MESSAGE = "test";
const PROBE_USER_TOKEN_COUNT = 1;
const PROBE_COMPLETION_TOKEN_COUNT = 1;
const DIRTY_TOKEN_REFRESH_MARGIN_PERCENT = 5;
const ESTIMATED_CHAT_MESSAGE_OVERHEAD_TOKENS = 4;
const COMPACTED_SUMMARY_HEADER = "Summary of compacted conversation";
const EDITOR_CONTEXT_MAX_TREE_ENTRIES = 1000;
const EDITOR_CONTEXT_MAX_SELECTED_TEXT_CHARS = 4000;
const GREP_MAX_MATCHES = 500;
const GREP_MAX_FILE_BYTES = 8 * 1024 * 1024;
const REMOVED_FILE_GREP_TOOL = String.fromCharCode(102, 114, 101, 112, 70, 105, 108, 101);
const AI_SERVER_CHECK_TIMEOUT_MS = 5000;
const LM_STUDIO_NATIVE_PROBE_TIMEOUT_MS = 700;

export const AI_SETTINGS_DOC_PATH = "/.slug-ai-settings.json";
export const AI_SYSTEM_PROMPT_DOC_PATH = "/.slug-system-prompt.md";
export const AI_COMPACT_PROMPT_DOC_PATH = "/.slug-compact-prompt.md";
export const AI_TAG_TOOL_PROMPT_DOC_PATH = "/.slug-tag-tool-prompt.md";
export const AI_HARMONY_TOOL_PROMPT_DOC_PATH = "/.slug-harmony-tool-prompt.md";

export const DEFAULT_AI_ENDPOINT_CONFIG: AiEndpointConfig = {
  apiBaseUrl: "http://localhost:1234/v1",
  apiKey: "",
  model: "",
  temperature: 0.2,
  maxContextTokens: 0
};

export const DEFAULT_AI_RUNTIME_SETTINGS: AiRuntimeSettings = {
  maxToolCallsPerTurn: 50,
  detectDuplicateToolCalls: true,
  toolCallFormat: "tag",
  thinkingFormat: "auto",
  compactFreePercent: 10
};

const NATIVE_TOOL_DEFINITIONS: ApiToolDefinition[] = [
  {
    type: "function",
    function: {
      name: "readFile",
      description: "Read a text file from the virtual workspace.",
      parameters: { type: "object", properties: { path: { type: "string", description: "Workspace path, for example /README.md." } }, required: ["path"] }
    }
  },
  {
    type: "function",
    function: {
      name: "writeFile",
      description: "Create a new text file, or overwrite an existing text file only after readFile has read its current contents.",
      parameters: {
        type: "object",
        properties: {
          path: { type: "string", description: "Workspace path to write." },
          content: { type: "string", description: "Full file contents." }
        },
        required: ["path", "content"]
      }
    }
  },
  {
    type: "function",
    function: {
      name: "editFile",
      description: "Replace an exact string inside a text file. The file must have been read first with readFile.",
      parameters: {
        type: "object",
        properties: {
          path: { type: "string", description: "Workspace path to edit." },
          oldString: { type: "string", description: "Exact text to replace." },
          newString: { type: "string", description: "Replacement text." }
        },
        required: ["path", "oldString", "newString"]
      }
    }
  },
  {
    type: "function",
    function: {
      name: "grep",
      description: "Search all workspace text files for a regular expression.",
      parameters: { type: "object", properties: { pattern: { type: "string", description: "Regular expression pattern." } }, required: ["pattern"] }
    }
  },
  {
    type: "function",
    function: {
      name: "grepFile",
      description: "Search one workspace text file for a regular expression.",
      parameters: {
        type: "object",
        properties: {
          pattern: { type: "string", description: "Regular expression pattern." },
          path: { type: "string", description: "Workspace path to search." }
        },
        required: ["pattern", "path"]
      }
    }
  },
  {
    type: "function",
    function: {
      name: "bash",
      description: "Run a supported emulated shell command.",
      parameters: { type: "object", properties: { command: { type: "string", description: "Shell command to run." } }, required: ["command"] }
    }
  },
  {
    type: "function",
    function: {
      name: "compact",
      description: "Compact the current chat conversation.",
      parameters: { type: "object", properties: {} }
    }
  }
];

const LEGACY_DEFAULT_SYSTEM_PROMPT = `You are an AI coding assistant inside a browser code editor.

You can inspect and edit the virtual workspace using tool calls. Keep responses concise, use tools when you need file contents, and prefer precise edits over broad rewrites.

Available tools:
- readFile(path)
- writeFile(path, content)
- editFile(path, oldString, newString)
- grep(pattern)
- grepFile(pattern, path)
- bash(command)
- compact()

Tag tool-call format:
<tool>readFile("/README.md")</tool>

Harmony-style tool-call format:
<|channel|>commentary to=readFile <|message|>{"path":"/README.md"}<|call|>

After each tool result, continue until the task is done or you need the user.`;

const DEFAULT_SYSTEM_PROMPT = `You are an AI coding assistant inside a browser code editor.

You can inspect and edit the virtual workspace using tool calls when tools are available. Keep responses concise, use tools when you need file contents, and prefer precise edits over broad rewrites.`;

const TOOL_LIST_PROMPT = `Available tools:
- readFile(path)
- writeFile(path, content)
- editFile(path, oldString, newString)
- grep(pattern)
- grepFile(pattern, path)
- bash(command)
- compact()`;

const BASH_EMULATION_PROMPT = `The bash(command) tool is a browser-emulated shell over the virtual workspace, not a real OS shell.
Supported bash commands:
- pwd
- ls [-R] [path]
- cat <file...>
- mkdir <dir...>
- rmdir <dir...>
- rm [-r|-rf|-fr] <path...>
- cp <source> <dest>
- mv <source> <dest>
- touch <file...>
- echo <text...>

Bash limitations:
- Shell operators are not supported: pipes, redirects, command chaining, backgrounding, backticks, and $() substitution are rejected.
- Do not use shell redirects to create files. Use writeFile("/path.txt", "content") instead of echo "content" > path.txt.
- chmod and executable bits are not supported.
- Use grep(pattern) or grepFile(pattern, path) instead of shell grep.
- cp copies files only.
- For sample/demo content, prefer text-friendly extensions such as .txt, .md, .ts, .js, .json, .html, .css, .lua, .py, .c, .cpp, or .h. Avoid fake .png, .pdf, .zip, and other binary-looking files unless the user explicitly asks for them.`;

const DEFAULT_NATIVE_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

${BASH_EMULATION_PROMPT}

Primary tool protocol: use the OpenAI Chat Completions native tool_calls interface. The request already provides the tool schemas, so native tool_calls are the executable tool-call form for this conversation.

Do not write tool calls as plain JSON, tag syntax, Harmony text syntax, markdown, reasoning, analysis, or explanatory text.

Before modifying an existing file with writeFile or editFile, read it first with readFile. Creating a new file does not require readFile.

If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const DEFAULT_TAG_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

${BASH_EMULATION_PROMPT}

Tool calls are executable only when they are emitted in assistant message content. Never put tool calls or tool-call syntax inside reasoning, thinking, analysis, markdown fences, or explanatory text.

Before modifying an existing file with writeFile or editFile, read it first with readFile. Creating a new file does not require readFile.

Use only this tag tool-call format when invoking tools:
<tool>readFile("/README.md")</tool>

If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const DEFAULT_HARMONY_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

${BASH_EMULATION_PROMPT}

Tool calls are executable only when they are emitted in assistant message content/commentary. Never put tool calls or tool-call syntax inside reasoning, thinking, analysis, markdown fences, or explanatory text.

Before modifying an existing file with writeFile or editFile, read it first with readFile. Creating a new file does not require readFile.

Use only this harmony-style tool-call format when invoking tools:
<|channel|>commentary to=writeFile <|message|>{"path":"/notes.txt","content":"hello\\n"}<|call|>

The final token of every harmony tool call must be <|call|>. If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const PRE_BASH_DEFAULT_TAG_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

Tool calls are executable only when they are emitted in assistant message content. Never put tool calls or tool-call syntax inside reasoning, thinking, analysis, markdown fences, or explanatory text.

Before modifying an existing file with writeFile or editFile, read it first with readFile. Creating a new file does not require readFile.

Use only this tag tool-call format when invoking tools:
<tool>readFile("/README.md")</tool>

If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const PRE_BASH_DEFAULT_HARMONY_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

Tool calls are executable only when they are emitted in assistant message content/commentary. Never put tool calls or tool-call syntax inside reasoning, thinking, analysis, markdown fences, or explanatory text.

Before modifying an existing file with writeFile or editFile, read it first with readFile. Creating a new file does not require readFile.

Use only this harmony-style tool-call format when invoking tools:
<|channel|>commentary to=writeFile <|message|>{"path":"/notes.txt","content":"hello\\n"}<|call|>

The final token of every harmony tool call must be <|call|>. If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const LEGACY_DEFAULT_TAG_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

Use only this tag tool-call format when invoking tools:
<tool>readFile("/README.md")</tool>

If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const LEGACY_DEFAULT_HARMONY_TOOL_PROMPT = `${TOOL_LIST_PROMPT}

Use only this harmony-style tool-call format when invoking tools:
<|channel|>commentary to=writeFile <|message|>{"path":"/notes.txt","content":"hello\\n"}<|call|>

The final token of every harmony tool call must be <|call|>. If the user asks to make or create a new file without naming it, choose a short root-level file name and simple starter content, then call writeFile.

After each tool result, continue until the task is done or you need the user.`;

const DEFAULT_COMPACT_PROMPT = `Compact the provided coding-agent conversation aggressively.

Preserve only the user's intent, hard constraints, important decisions, files changed or inspected, current state, errors, test results, and unresolved next steps.

Omit tool-call syntax, repeated output, and low-value chatter. Write concise continuation context.`;

export function defaultSystemPrompt(): string {
  return DEFAULT_SYSTEM_PROMPT;
}

export function loadAiEndpointConfig(): AiEndpointConfig {
  return normalizeAiEndpointConfig(readJsonLocalStorage(AI_CONFIG_STORAGE_KEY));
}

export function saveAiEndpointConfig(config: Partial<AiEndpointConfig>): AiEndpointConfig {
  const normalized = normalizeAiEndpointConfig(config);
  localStorage.setItem(AI_CONFIG_STORAGE_KEY, JSON.stringify(normalized, null, 2));
  return normalized;
}

export function resolveAiContextTokens(config = loadAiEndpointConfig()): number {
  const normalized = normalizeAiEndpointConfig(config);
  return normalized.maxContextTokens || bestKnownContextLength(normalized.model);
}

export function loadAiSystemPrompt(): string {
  const stored = localStorage.getItem(AI_SYSTEM_PROMPT_STORAGE_KEY);
  if (!stored || stored === LEGACY_DEFAULT_SYSTEM_PROMPT) return DEFAULT_SYSTEM_PROMPT;
  return sanitizeSystemPrompt(stored);
}

export function saveAiSystemPrompt(text: string): void {
  localStorage.setItem(AI_SYSTEM_PROMPT_STORAGE_KEY, text);
}

export function loadAiCompactPrompt(): string {
  return localStorage.getItem(AI_COMPACT_PROMPT_STORAGE_KEY) ?? DEFAULT_COMPACT_PROMPT;
}

export function saveAiCompactPrompt(text: string): void {
  localStorage.setItem(AI_COMPACT_PROMPT_STORAGE_KEY, text);
}

export function loadAiTagToolPrompt(): string {
  const stored = localStorage.getItem(AI_TAG_TOOL_PROMPT_STORAGE_KEY);
  return !stored || stored === LEGACY_DEFAULT_TAG_TOOL_PROMPT || stored === PRE_BASH_DEFAULT_TAG_TOOL_PROMPT ? DEFAULT_TAG_TOOL_PROMPT : sanitizeToolPrompt(stored);
}

export function saveAiTagToolPrompt(text: string): void {
  localStorage.setItem(AI_TAG_TOOL_PROMPT_STORAGE_KEY, text);
}

export function loadAiHarmonyToolPrompt(): string {
  const stored = localStorage.getItem(AI_HARMONY_TOOL_PROMPT_STORAGE_KEY);
  return !stored || stored === LEGACY_DEFAULT_HARMONY_TOOL_PROMPT || stored === PRE_BASH_DEFAULT_HARMONY_TOOL_PROMPT ? DEFAULT_HARMONY_TOOL_PROMPT : sanitizeToolPrompt(stored);
}

export function saveAiHarmonyToolPrompt(text: string): void {
  localStorage.setItem(AI_HARMONY_TOOL_PROMPT_STORAGE_KEY, text);
}

export function resetAiPromptStorage(): void {
  localStorage.removeItem(AI_SYSTEM_PROMPT_STORAGE_KEY);
  localStorage.removeItem(AI_COMPACT_PROMPT_STORAGE_KEY);
  localStorage.removeItem(AI_TAG_TOOL_PROMPT_STORAGE_KEY);
  localStorage.removeItem(AI_HARMONY_TOOL_PROMPT_STORAGE_KEY);
}

export async function checkOpenAICompatibleServer(config = loadAiEndpointConfig()): Promise<AiServerCheckResult> {
  const normalized = normalizeAiEndpointConfig(config);
  const requestConfig = withResolvedApiBaseUrl(normalized);
  const headers = authHeaders(normalized);
  const modelsUrl = `${requestConfig.apiBaseUrl}/models`;
  try {
    const response = await fetchWithTimeout(modelsUrl, { headers }, AI_SERVER_CHECK_TIMEOUT_MS);
    if (!response.ok) {
      const detail = await responseErrorDetail(response);
      const suffix = detail ? `: ${detail}` : "";
      return { ok: false, baseUrl: requestConfig.apiBaseUrl, message: `${modelsUrl} returned HTTP ${response.status}${suffix}`, models: [] };
    }
    const data = await response.json() as { data?: unknown[] };
    const models = (Array.isArray(data.data) ? data.data : [])
      .map(modelInfoFromUnknown)
      .filter((model): model is AiModelInfo => Boolean(model?.id));
    const merged = shouldProbeLmStudioNativeModels(requestConfig.apiBaseUrl)
      ? await mergeLmStudioNativeModels(requestConfig, headers, models)
      : sortModelInfo(models);
    return {
      ok: true,
      baseUrl: requestConfig.apiBaseUrl,
      message: `Connected to ${requestConfig.apiBaseUrl}. Found ${merged.length} model${merged.length === 1 ? "" : "s"}.`,
      models: merged
    };
  } catch (error) {
    const detail = error instanceof Error ? error.message : String(error);
    return {
      ok: false,
      baseUrl: requestConfig.apiBaseUrl,
      message: `Could not reach ${modelsUrl}: ${detail}. Check the host, port, server status, and browser/CORS access.`,
      models: []
    };
  }
}

export async function probeOpenAICompatibleModels(config = loadAiEndpointConfig()): Promise<{ models: AiModelInfo[]; error?: string | undefined; baseUrl?: string | undefined }> {
  const result = await checkOpenAICompatibleServer(config);
  return { models: result.models, error: result.ok ? undefined : result.message, baseUrl: result.baseUrl };
}

export class ChatHarness {
  readonly messages: ChatMessage[] = [];

  private abortController: AbortController | null = null;
  private readVersions = new Map<string, string>();
  private tokenCounter: TokenCounterState = {
    key: "",
    calibrated: false,
    dirty: true,
    basePromptTokens: 0,
    promptTokens: 0,
    lastPromptTokens: 0,
    lastCompletionTokens: 0,
    lastTotalTokens: 0,
    source: "none"
  };

  constructor(private readonly vfs: Vfs) {}

  get running(): boolean {
    return this.abortController !== null;
  }

  cancel(): void {
    this.abortController?.abort();
  }

  tokenUsage(): ChatTokenUsage {
    const { key: _key, ...usage } = this.tokenCounter;
    return { ...usage };
  }

  exportJsonl(): string {
    const messages = this.visibleMessages();
    if (messages.length === 0) return "";
    return `${messages.map((msg) => JSON.stringify({
      id: msg.id,
      at: new Date(msg.at).toISOString(),
      role: msg.role,
      name: msg.name,
      ok: msg.ok,
      text: msg.text
    })).join("\n")}\n`;
  }

  debugApiJsonl(runtime: AiRuntimeSettings, editorContext?: ContextBundle | null | undefined): string {
    const normalizedRuntime = normalizeRuntimeSettings(runtime);
    const messages = this.apiMessages(
      normalizedRuntime,
      [],
      shouldUseNativeTools(loadAiEndpointConfig(), normalizedRuntime),
      editorContext ? formatEditorContext(editorContext) : null
    );
    return `${messages.map((msg, index) => JSON.stringify({
      index,
      ...msg
    })).join("\n")}\n`;
  }

  visibleMessages(): ChatMessage[] {
    const visible: ChatMessage[] = [];
    for (const msg of this.messages) {
      if (msg.internal) continue;
      const text = msg.displayText ?? msg.text;
      if (msg.role === "assistant" && !text.trim()) continue;
      visible.push(text === msg.text ? msg : { ...msg, text });
    }
    return visible;
  }

  clear(): void {
    this.messages.splice(0, this.messages.length);
    this.readVersions.clear();
    this.resetTokenCounterState();
  }

  async compact(runtimeSettings: Partial<AiRuntimeSettings>, options: Pick<TurnOptions, "onUpdate" | "onCompactStart" | "onCompactEnd"> = {}): Promise<ToolResult> {
    if (this.running) return { ok: false, output: "Chat is busy." };
    const config = loadAiEndpointConfig();
    const runtime = normalizeRuntimeSettings(runtimeSettings);
    if (!config.model) {
      const output = "No model is configured yet. Use Settings > AI to edit the OpenAI-compatible endpoint settings, or probe LM Studio models.";
      this.push({ role: "system", text: output, ok: false }, options.onUpdate);
      await this.persist();
      return { ok: false, output };
    }
    const readyConfig = await this.ensureContextTokensKnown(config, options.onUpdate);
    if (!readyConfig) {
      const output = "Max context tokens are unknown. Set Settings > AI > Max Context Tokens, or probe LM Studio max tokens before starting the assistant.";
      this.push({ role: "system", text: output }, options.onUpdate);
      await this.persist();
      return { ok: false, output };
    }
    const controller = new AbortController();
    this.abortController = controller;
    try {
      this.resetTokenCounterIfNeeded(readyConfig, runtime);
      return await this.compactConversation(readyConfig, runtime, controller.signal, options);
    } catch (error) {
      const output = `Compaction failed: ${error instanceof Error ? error.message : String(error)}`;
      this.push({ role: "system", text: output }, options.onUpdate);
      return { ok: false, output };
    } finally {
      this.abortController = null;
      await this.persist();
      options.onUpdate?.();
    }
  }

  async send(input: string, activeDoc: TextDocument | undefined, openDocs: TextDocument[], options: TurnOptions): Promise<void> {
    const userInput = input.trim();
    if (!userInput || this.running) return;
    const config = loadAiEndpointConfig();
    const runtime = normalizeRuntimeSettings(options.runtime);
    const context: ContextBundle = {
      selectedText: activeDoc?.selectedText() ?? "",
      openPaths: openDocs.map((doc) => doc.path ?? "(untitled)")
    };
    if (activeDoc?.path) context.activePath = activeDoc.path;
    const editorContext = options.editorContext ? formatEditorContext({ ...context, ...options.editorContext }) : null;

    this.push({ role: "user", text: userInput }, options.onUpdate);
    const controller = new AbortController();
    this.abortController = controller;
    const openByPath = new Map(openDocs.filter((doc) => doc.path).map((doc) => [normalizePath(doc.path!), doc]));
    try {
      if (!config.model) {
        this.push({
          role: "system",
          text: "No model is configured yet. Use Settings > AI to edit the OpenAI-compatible endpoint settings, or probe LM Studio models.",
          ok: false
        }, options.onUpdate);
        return;
      }
      const readyConfig = await this.ensureContextTokensKnown(config, options.onUpdate);
      if (!readyConfig) {
        this.push({
          role: "system",
          text: "Max context tokens are unknown. Set Settings > AI > Max Context Tokens, or probe LM Studio max tokens before starting the assistant."
        }, options.onUpdate);
        return;
      }

      this.resetTokenCounterIfNeeded(readyConfig, runtime);
      const tokenCounterWasCalibrated = this.tokenCounter.calibrated;
      await this.ensureTokenCounterCalibrated(readyConfig, runtime, controller.signal, editorContext);
      if (tokenCounterWasCalibrated) this.markTokenCounterDirtyForLocalMessage(userInput, runtime);

      let toolCalls = 0;
      let allowedToolCalls = runtime.maxToolCallsPerTurn;
      let allowUnlimitedToolCalls = false;
      let stopToolCalls = false;
      let lastFingerprint = "";
      let hiddenToolCallRepairs = 0;
      let pendingRepairPrompt: string | null = null;
      const ensureToolCallsAllowed = async (): Promise<boolean> => {
        if (allowUnlimitedToolCalls || toolCalls < allowedToolCalls) return true;
        const decision = await options.onToolCallLimit?.(runtime.maxToolCallsPerTurn, toolCalls) ?? "stop";
        if (decision === "allowAll") {
          allowUnlimitedToolCalls = true;
          this.push({ role: "system", text: "Max tool calls reached; allowing unlimited tool calls for this turn." }, options.onUpdate);
          return true;
        }
        if (decision === "allowMore") {
          allowedToolCalls += runtime.maxToolCallsPerTurn;
          this.push({ role: "system", text: `Max tool calls reached; allowing ${runtime.maxToolCallsPerTurn} more for this turn.` }, options.onUpdate);
          return true;
        }
        this.push({ role: "system", text: "Max tool calls reached; stopped tool calls for this turn." }, options.onUpdate);
        return false;
      };
      while (!controller.signal.aborted) {
        if (!await ensureToolCallsAllowed()) break;

        const result = await this.complete(readyConfig, runtime, controller.signal, options.onUpdate, pendingRepairPrompt, editorContext);
        pendingRepairPrompt = null;
        const extractedThinking = extractThinkingFromText(result.text, runtime.thinkingFormat);
        const thinking = [result.thinking, extractedThinking.thinking].filter(Boolean).join("\n\n");
        const assistantText = extractedThinking.text;
        if (thinking) {
          if (result.streamedThinkingMessage) {
            result.streamedThinkingMessage.text = thinking;
            options.onUpdate?.();
          } else {
            this.push({ role: "thinking", text: thinking }, options.onUpdate);
          }
        }

        const parsedCalls = runtime.toolCallFormat === "none"
          ? []
          : result.toolCalls.length > 0
            ? result.toolCalls
            : parseTextToolCalls(result.text, runtime.toolCallFormat);
        if (parsedCalls.length === 0) {
          const hiddenToolCall = runtime.toolCallFormat !== "none" && thinkingSuggestsToolCall(thinking, runtime.toolCallFormat);
          if (!assistantText.trim() && hiddenToolCall) {
            this.observeCompletionUsage(result);
            if (!completionResultHasUsage(result) && result.text) this.markTokenCounterDirtyForLocalMessage(result.text, runtime);
            if (hiddenToolCallRepairs >= 2) {
              this.push({ role: "system", ok: false, text: "The model kept writing tool calls inside hidden thinking. Hidden thinking is not executable, so no tool was run." }, options.onUpdate);
              await this.maybeAutoCompactAfterModelResponse(readyConfig, runtime, controller.signal, options, editorContext);
              break;
            }
            const repairPrompt = hiddenToolCallRepairPrompt(runtime.toolCallFormat);
            this.push({ role: "system", ok: false, text: "The model wrote a tool call inside thinking. Hidden thinking is not executable, so no tool was run; asking the model to resend the call as assistant content." }, options.onUpdate);
            pendingRepairPrompt = repairPrompt;
            this.markTokenCounterDirtyForLocalMessage(repairPrompt, runtime);
            hiddenToolCallRepairs++;
            continue;
          }
          const stripTools = runtime.toolCallFormat !== "none";
          if (result.streamedMessage) this.updateAssistantDisplayMessage(result.streamedMessage, assistantText, options.onUpdate, stripTools);
          else this.pushAssistantMessageForDisplay(assistantText || "(empty response)", options.onUpdate, stripTools);
          this.observeCompletionUsage(result);
          if (!completionResultHasUsage(result) && result.text) this.markTokenCounterDirtyForLocalMessage(result.text, runtime);
          if (!result.text) this.markTokenCounterDirtyForLocalMessage("(empty response)", runtime);
          await this.maybeAutoCompactAfterModelResponse(readyConfig, runtime, controller.signal, options, editorContext);
          break;
        }

        const visibleText = assistantText.trim();
        if (visibleText && !result.streamedMessage) {
          this.pushAssistantMessageForDisplay(visibleText, options.onUpdate, true);
        } else if (result.streamedMessage) {
          this.updateAssistantDisplayMessage(result.streamedMessage, visibleText, options.onUpdate, true);
        }
        this.observeCompletionUsage(result);
        if (!completionResultHasUsage(result) && result.text) this.markTokenCounterDirtyForLocalMessage(result.text, runtime);

        for (let callIndex = 0; callIndex < parsedCalls.length; callIndex++) {
          const call = parsedCalls[callIndex]!;
          if (!await ensureToolCallsAllowed()) {
            stopToolCalls = true;
            break;
          }
          const fingerprint = `${call.name}:${JSON.stringify(call.args)}`;
          if (runtime.detectDuplicateToolCalls && fingerprint === lastFingerprint) {
            const decision = await options.onDuplicateToolCall?.({ name: call.name, args: call.args, raw: call.raw }) ?? "break";
            if (decision === "break") {
              const output = "Duplicate tool call detected; ending turn.";
              this.push({ role: "tool_result", name: call.name, ok: false, text: output }, options.onUpdate);
              if (!call.nativeId) {
                const formattedResult = formatToolResult(output, runtime.toolCallFormat);
                this.push({ role: "user", text: formattedResult, internal: true });
                this.markTokenCounterDirtyForLocalMessage(formattedResult, runtime);
              }
              return;
            }
            this.push({ role: "system", text: `Duplicate tool call allowed: ${call.name}` }, options.onUpdate);
            this.markTokenCounterDirtyForLocalMessage(`Duplicate tool call allowed: ${call.name}`, runtime);
          }
          lastFingerprint = fingerprint;
          this.push({
            role: "tool_call",
            name: call.name,
            text: call.raw,
            nativeToolCallId: call.nativeId,
            nativeToolArguments: call.nativeArguments
          }, options.onUpdate);
          const toolResult = await this.runTool(call, openByPath, readyConfig, runtime, controller.signal, {
            onUpdate: options.onUpdate,
            onWorkspaceChange: options.onWorkspaceChange
          });
          this.push({
            role: "tool_result",
            name: call.name,
            ok: toolResult.ok,
            text: toolResult.output,
            nativeToolCallId: call.nativeId
          }, options.onUpdate);
          if (call.nativeId) {
            this.markTokenCounterDirtyForLocalMessage(toolResult.output, runtime);
          } else {
            const formattedResult = formatToolResult(toolResult.output, runtime.toolCallFormat);
            this.messages.push({ id: uid("msg"), role: "user", text: formattedResult, at: Date.now(), ok: toolResult.ok, internal: true });
            this.markTokenCounterDirtyForLocalMessage(formattedResult, runtime);
          }
          toolCalls++;
          if (!toolResult.ok) {
            this.skipRemainingNativeToolCallsAfterFailure(parsedCalls.slice(callIndex + 1), runtime, options.onUpdate);
            break;
          }
        }
        if (stopToolCalls) break;
      }
    } catch (error) {
      if (controller.signal.aborted) {
        this.push({ role: "system", text: "Turn canceled." }, options.onUpdate);
      } else {
        this.push({ role: "system", text: `Request failed: ${error instanceof Error ? error.message : String(error)}` }, options.onUpdate);
      }
    } finally {
      this.abortController = null;
      await this.persist();
      options.onUpdate?.();
    }
  }

  private skipRemainingNativeToolCallsAfterFailure(calls: ParsedToolCall[], runtime: AiRuntimeSettings, onUpdate?: (() => void) | undefined): void {
    for (const call of calls) {
      if (!call.nativeId) continue;
      const output = "Skipped because a previous tool call in the same assistant response failed.";
      this.push({
        role: "tool_call",
        name: call.name,
        text: call.raw,
        nativeToolCallId: call.nativeId,
        nativeToolArguments: call.nativeArguments
      }, onUpdate);
      this.push({
        role: "tool_result",
        name: call.name,
        ok: false,
        text: output,
        nativeToolCallId: call.nativeId
      }, onUpdate);
      this.markTokenCounterDirtyForLocalMessage(output, runtime);
    }
  }

  async persist(): Promise<void> {
    await this.vfs.writeFile("/.slug-chat.json", JSON.stringify(this.messages, null, 2), "application/json");
  }

  private push(seed: Omit<ChatMessage, "id" | "at">, onUpdate?: (() => void) | undefined): ChatMessage {
    const msg = { id: uid("msg"), at: Date.now(), ...seed };
    this.messages.push(msg);
    onUpdate?.();
    return msg;
  }

  private pushAssistantMessageForDisplay(text: string, onUpdate?: (() => void) | undefined, stripTools = true): ChatMessage {
    const displayText = (stripTools ? stripToolCallSyntax(text) : text).trim();
    return this.push({
      role: "assistant",
      text,
      displayText,
      internal: !displayText
    }, onUpdate);
  }

  private updateAssistantDisplayMessage(message: ChatMessage, text: string, onUpdate?: (() => void) | undefined, stripTools = true): void {
    const displayText = (stripTools ? stripToolCallSyntax(text) : text).trim();
    message.text = text;
    message.displayText = displayText;
    message.internal = !displayText;
    onUpdate?.();
  }

  private async complete(config: AiEndpointConfig, runtime: AiRuntimeSettings, signal: AbortSignal, onUpdate?: (() => void) | undefined, extraUserMessage?: string | null, editorContext?: string | null): Promise<CompletionResult> {
    const nativeTools = shouldUseNativeTools(config, runtime);
    const tools = nativeTools ? NATIVE_TOOL_DEFINITIONS : [];
    const body: Record<string, unknown> = {
      model: config.model,
      messages: this.apiMessages(runtime, extraUserMessage ? [{ role: "user", content: extraUserMessage }] : [], nativeTools, editorContext),
      temperature: config.temperature,
      stream: true
    };
    if (tools.length > 0) {
      body.tools = tools;
      body.tool_choice = "auto";
    }
    const response = await fetch(`${resolvedApiBaseUrl(config)}/chat/completions`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        ...authHeaders(config)
      },
      body: JSON.stringify(body),
      signal
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const contentType = response.headers.get("content-type") ?? "";
    if (response.body && contentType.toLowerCase().includes("text/event-stream")) {
      return this.readStreamingCompletion(response, onUpdate);
    }
    const data = await response.json() as {
      choices?: Array<{ message?: { content?: string | null; reasoning_content?: string; reasoning?: string; tool_calls?: unknown[] } }>;
      usage?: { total_tokens?: number; prompt_tokens?: number; completion_tokens?: number };
    };
    return completionResultFromJson(data);
  }

  private async readStreamingCompletion(response: Response, onUpdate?: (() => void) | undefined): Promise<CompletionResult> {
    const reader = response.body!.getReader();
    const decoder = new TextDecoder();
    const streamedToolCalls = new Map<number, StreamedToolCallPart>();
    let buffer = "";
    let text = "";
    let thinking = "";
    let usage = emptyUsage();
    let streamedMessage: ChatMessage | undefined;
    let streamedThinkingMessage: ChatMessage | undefined;
    let done = false;
    while (!done) {
      const { value, done: readerDone } = await reader.read();
      buffer += decoder.decode(value ?? new Uint8Array(), { stream: !readerDone });
      let event: string | null;
      while ((event = takeSseEvent()) !== null) {
        const payload = sseDataPayload(event);
        if (!payload) continue;
        if (payload === "[DONE]") {
          done = true;
          break;
        }
        const chunk = parseJsonObject(payload);
        if (!chunk) continue;
        const nextUsage = usageFromApi((chunk as { usage?: { total_tokens?: number; prompt_tokens?: number; completion_tokens?: number } }).usage);
        if (nextUsage.totalTokens > 0 || nextUsage.promptTokens > 0 || nextUsage.completionTokens > 0) usage = nextUsage;
        const choices = (chunk as { choices?: unknown[] }).choices;
        if (!Array.isArray(choices)) continue;
        for (const choice of choices) {
          if (!choice || typeof choice !== "object") continue;
          const delta = (choice as { delta?: unknown }).delta;
          if (!delta || typeof delta !== "object") continue;
          const content = streamDeltaText(delta, "content");
          const reasoning = streamDeltaText(delta, "reasoning_content") || streamDeltaText(delta, "reasoning");
          if (reasoning) {
            thinking += reasoning;
            if (!streamedThinkingMessage) {
              streamedThinkingMessage = this.push({ role: "thinking", text: reasoning }, onUpdate);
            } else {
              streamedThinkingMessage.text += reasoning;
              onUpdate?.();
            }
          }
          if (content) {
            text += content;
            if (!streamedMessage) {
              streamedMessage = this.push({ role: "assistant", text: content }, onUpdate);
            } else {
              streamedMessage.text += content;
              onUpdate?.();
            }
          }
          appendStreamToolCallDeltas((delta as { tool_calls?: unknown }).tool_calls, streamedToolCalls);
        }
      }
      if (readerDone) break;
    }
    return {
      text,
      thinking,
      toolCalls: parseStreamedToolCalls(streamedToolCalls),
      usageTotal: usage.totalTokens,
      usagePrompt: usage.promptTokens,
      usageCompletion: usage.completionTokens,
      streamedMessage,
      streamedThinkingMessage
    };

    function takeSseEvent(): string | null {
      const lf = buffer.indexOf("\n\n");
      const crlf = buffer.indexOf("\r\n\r\n");
      const indexes = [
        lf >= 0 ? { index: lf, length: 2 } : null,
        crlf >= 0 ? { index: crlf, length: 4 } : null
      ].filter((item): item is { index: number; length: number } => Boolean(item));
      if (indexes.length === 0) return null;
      indexes.sort((a, b) => a.index - b.index);
      const boundary = indexes[0]!;
      const event = buffer.slice(0, boundary.index);
      buffer = buffer.slice(boundary.index + boundary.length);
      return event;
    }
  }

  private apiMessages(runtime: AiRuntimeSettings, extraMessages: Array<{ role: "user" | "assistant"; content: string }> = [], nativeTools = false, editorContext?: string | null): ApiMessage[] {
    const messages: ApiMessage[] = [
      { role: "system", content: composeAiSystemPrompt(runtime.toolCallFormat, nativeTools) }
    ];
    if (editorContext) messages.push({ role: "user", content: editorContext });
    for (const msg of this.messages) {
      if (isHiddenToolRepairMessage(msg)) continue;
      if (msg.role === "system") {
        if (isCompactedSummaryMessage(msg)) messages.push({ role: "user", content: msg.text });
        continue;
      }
      if (msg.role === "assistant") messages.push({ role: "assistant", content: msg.text });
      else if (msg.role === "tool_call") {
        if (nativeTools && msg.nativeToolCallId) {
          messages.push({
            role: "assistant",
            content: "",
            tool_calls: [{
              id: msg.nativeToolCallId,
              type: "function",
              function: { name: msg.name ?? "", arguments: msg.nativeToolArguments ?? "{}" }
            }]
          });
        } else {
          messages.push({ role: "assistant", content: msg.text });
        }
      } else if (msg.role === "tool_result") {
        if (nativeTools && msg.nativeToolCallId) {
          messages.push({ role: "tool", tool_call_id: msg.nativeToolCallId, content: msg.text });
        } else if (msg.nativeToolCallId) {
          messages.push({ role: "user", content: formatToolResult(msg.text, runtime.toolCallFormat) });
        }
      } else if (msg.role === "thinking") continue;
      else messages.push({ role: "user", content: msg.text });
    }
    messages.push(...extraMessages);
    return messages;
  }

  private async maybeAutoCompactAfterModelResponse(config: AiEndpointConfig, runtime: AiRuntimeSettings, signal: AbortSignal, options: TurnOptions, editorContext?: string | null): Promise<void> {
    const maxTokens = resolveAiContextTokens(config);
    if (maxTokens <= 0) return;
    let used = this.tokenCounter.promptTokens || this.estimateCurrentPromptTokens(runtime, editorContext);
    let freePercent = Math.max(0, ((maxTokens - used) / maxTokens) * 100);
    if (this.tokenCounter.dirty && freePercent < runtime.compactFreePercent + DIRTY_TOKEN_REFRESH_MARGIN_PERCENT) {
      used = await this.refreshCurrentPromptTokens(config, runtime, signal, editorContext) || used;
      freePercent = Math.max(0, ((maxTokens - used) / maxTokens) * 100);
    }
    if (freePercent >= runtime.compactFreePercent) return;
    await this.compactConversation(config, runtime, signal, options);
  }

  private async ensureTokenCounterCalibrated(config: AiEndpointConfig, runtime: AiRuntimeSettings, signal: AbortSignal, editorContext?: string | null): Promise<void> {
    if (this.tokenCounter.calibrated) return;
    const nativeTools = shouldUseNativeTools(config, runtime);
    const tools = nativeTools ? NATIVE_TOOL_DEFINITIONS : [];
    const probeMessages = this.apiMessagesWithLatestUserReplaced(runtime, PROBE_USER_MESSAGE, nativeTools, editorContext);
    const usage = await this.probeTokenUsage(config, probeMessages, signal, tools);
    const basePromptTokens = basePromptTokensFromProbeUsage(usage);
    if (basePromptTokens > 0) {
      this.tokenCounter.basePromptTokens = basePromptTokens;
      this.tokenCounter.promptTokens = Math.max(basePromptTokens, this.estimateCurrentPromptTokens(runtime, editorContext));
      this.tokenCounter.calibrated = true;
      this.tokenCounter.dirty = true;
      this.tokenCounter.lastPromptTokens = usage.promptTokens;
      this.tokenCounter.lastCompletionTokens = usage.completionTokens || PROBE_COMPLETION_TOKEN_COUNT;
      this.tokenCounter.lastTotalTokens = usage.totalTokens;
      this.tokenCounter.source = "probe";
      return;
    }
    this.tokenCounter.promptTokens = this.estimateCurrentPromptTokens(runtime, editorContext);
    this.tokenCounter.dirty = true;
    this.tokenCounter.source = "estimate";
  }

  private async refreshCurrentPromptTokens(config: AiEndpointConfig, runtime: AiRuntimeSettings, signal: AbortSignal, editorContext?: string | null): Promise<number> {
    const nativeTools = shouldUseNativeTools(config, runtime);
    const usage = await this.probeTokenUsage(config, this.apiMessages(runtime, [], nativeTools, editorContext), signal, nativeTools ? NATIVE_TOOL_DEFINITIONS : []);
    const promptTokens = promptTokensFromRefreshUsage(usage);
    if (promptTokens <= 0) return 0;
    this.tokenCounter.promptTokens = promptTokens;
    this.tokenCounter.dirty = false;
    this.tokenCounter.lastPromptTokens = usage.promptTokens || promptTokens;
    this.tokenCounter.lastCompletionTokens = usage.completionTokens || PROBE_COMPLETION_TOKEN_COUNT;
    this.tokenCounter.lastTotalTokens = usage.totalTokens;
    this.tokenCounter.source = "refresh";
    return promptTokens;
  }

  private async ensureContextTokensKnown(config: AiEndpointConfig, onUpdate?: (() => void) | undefined): Promise<AiEndpointConfig | null> {
    if (resolveAiContextTokens(config) > 0) return config;
    const result = await probeOpenAICompatibleModels(config);
    const match = result.models.find((model) => model.id === config.model);
    if (!match?.contextLength) return null;
    const updated = saveAiEndpointConfig({ ...config, maxContextTokens: match.contextLength });
    this.push({ role: "system", text: `Detected ${match.contextLength} max context tokens for ${config.model}.` }, onUpdate);
    return updated;
  }

  private async probeTokenUsage(config: AiEndpointConfig, messages: ApiMessage[], signal: AbortSignal, tools: ApiToolDefinition[] = []): Promise<ApiUsage> {
    try {
      const body: Record<string, unknown> = {
        model: config.model,
        max_tokens: 1,
        stream: false,
        messages
      };
      if (tools.length > 0) {
        body.tools = tools;
        body.tool_choice = "auto";
      }
      const response = await fetch(`${resolvedApiBaseUrl(config)}/chat/completions`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          ...authHeaders(config)
        },
        body: JSON.stringify(body),
        signal
      });
      if (!response.ok) return emptyUsage();
      const data = await response.json() as { usage?: { total_tokens?: number; prompt_tokens?: number; completion_tokens?: number } };
      return usageFromApi(data.usage);
    } catch {
      return emptyUsage();
    }
  }

  private apiMessagesWithLatestUserReplaced(runtime: AiRuntimeSettings, replacement: string, nativeTools = false, editorContext?: string | null): ApiMessage[] {
    const messages = this.apiMessages(runtime, [], nativeTools, editorContext).map((msg) => ({ ...msg }));
    for (let i = messages.length - 1; i >= 0; i--) {
      if (messages[i]!.role === "user") {
        messages[i]!.content = replacement;
        return messages;
      }
    }
    messages.push({ role: "user", content: replacement });
    return messages;
  }

  private observeCompletionUsage(result: CompletionResult): void {
    const promptTokens = result.usagePrompt;
    const completionTokens = result.usageCompletion || Math.max(0, result.usageTotal - result.usagePrompt);
    const currentTokens = result.usageTotal || (promptTokens > 0 && completionTokens > 0 ? promptTokens + completionTokens : promptTokens);
    if (currentTokens <= 0) return;
    this.tokenCounter.promptTokens = currentTokens;
    this.tokenCounter.lastPromptTokens = promptTokens;
    this.tokenCounter.lastCompletionTokens = completionTokens;
    this.tokenCounter.lastTotalTokens = result.usageTotal;
    this.tokenCounter.dirty = result.usageTotal <= 0;
    this.tokenCounter.source = "usage";
  }

  private markTokenCounterDirtyForLocalMessage(text: string, runtime: AiRuntimeSettings): void {
    if (this.tokenCounter.promptTokens > 0) {
      this.tokenCounter.promptTokens += estimateMessageTokens(text);
    } else {
      this.tokenCounter.promptTokens = this.estimateCurrentPromptTokens(runtime);
    }
    this.tokenCounter.dirty = true;
    if (this.tokenCounter.source === "none") this.tokenCounter.source = "estimate";
  }

  private estimateCurrentPromptTokens(runtime: AiRuntimeSettings, editorContext?: string | null): number {
    return estimateTokens(this.apiMessages(runtime, [], false, editorContext).map((msg) => msg.content).join("\n"));
  }

  private resetTokenCounterIfNeeded(config: AiEndpointConfig, runtime: AiRuntimeSettings): void {
    const key = tokenCounterKey(config, runtime);
    if (this.tokenCounter.key === key) return;
    this.resetTokenCounterState(key);
  }

  private resetTokenCounterState(key = ""): void {
    this.tokenCounter = {
      key,
      calibrated: false,
      dirty: true,
      basePromptTokens: 0,
      promptTokens: 0,
      lastPromptTokens: 0,
      lastCompletionTokens: 0,
      lastTotalTokens: 0,
      source: "none"
    };
  }

  private async compactConversation(config: AiEndpointConfig, runtime: AiRuntimeSettings, signal: AbortSignal, options: Pick<TurnOptions, "onUpdate" | "onCompactStart" | "onCompactEnd">): Promise<ToolResult> {
    const messages = this.compactionMessages();
    if (messages.length <= 2) return { ok: true, output: "Nothing to compact." };
    options.onCompactStart?.();
    try {
      const response = await fetch(`${resolvedApiBaseUrl(config)}/chat/completions`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          ...authHeaders(config)
        },
        body: JSON.stringify({
          model: config.model,
          temperature: 0,
          max_tokens: 700,
          stream: false,
          messages
        }),
        signal
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json() as { choices?: Array<{ message?: { content?: string | null } }>; usage?: { total_tokens?: number } };
      const summary = data.choices?.[0]?.message?.content?.trim();
      if (!summary) throw new Error("empty summary");
      this.messages.splice(0, this.messages.length, {
        id: uid("msg"),
        role: "system",
        text: `${COMPACTED_SUMMARY_HEADER}\n\n${summary}`,
        at: Date.now()
      });
      this.tokenCounter.promptTokens = this.estimateCurrentPromptTokens(runtime);
      this.tokenCounter.dirty = true;
      this.tokenCounter.source = "estimate";
      options.onUpdate?.();
      return { ok: true, output: "Conversation compacted." };
    } catch (error) {
      const output = `Compaction failed: ${error instanceof Error ? error.message : String(error)}`;
      this.push({ role: "system", text: output }, options.onUpdate);
      return { ok: false, output };
    } finally {
      options.onCompactEnd?.();
    }
  }

  private compactionMessages(): Array<{ role: "system" | "user" | "assistant"; content: string }> {
    const messages: Array<{ role: "system" | "user" | "assistant"; content: string }> = [
      { role: "system", content: loadAiCompactPrompt() }
    ];
    for (const msg of this.messages) {
      if (msg.internal) continue;
      if (msg.role === "system" && !isCompactedSummaryMessage(msg)) continue;
      if (msg.role === "thinking" || msg.role === "tool_call" || msg.role === "tool_result") continue;
      const content = stripToolCallSyntax(msg.text).trim();
      if (content) messages.push({ role: msg.role === "assistant" ? "assistant" : "user", content });
    }
    messages.push({ role: "user", content: "compact / summarize this chat" });
    return messages;
  }

  private async runTool(
    call: ParsedToolCall,
    openByPath: Map<string, TextDocument>,
    config: AiEndpointConfig,
    runtime: AiRuntimeSettings,
    signal: AbortSignal,
    options: Pick<TurnOptions, "onUpdate" | "onWorkspaceChange">
  ): Promise<ToolResult> {
    const name = call.name.replace(/^functions\./, "");
    if (name === "readFile") return this.toolReadFile(call.args);
    if (name === "writeFile") return this.toolWriteFile(call.args, openByPath, options.onWorkspaceChange);
    if (name === "editFile") return this.toolEditFile(call.args, openByPath, options.onWorkspaceChange);
    if (name === "grep") return this.toolGrep(call.args);
    if (name === "grepFile" || name === "grepIn") return this.toolGrepFile(call.args);
    if (name === "bash") return this.toolBash(call.args, openByPath, options.onWorkspaceChange);
    if (name === "compact") return this.compactConversation(config, runtime, signal, { onUpdate: options.onUpdate });
    return { ok: false, output: `Unknown tool: ${call.name}` };
  }

  private async toolReadFile(args: unknown[]): Promise<ToolResult> {
    const path = normalizeToolPath(argString(args, 0, "path"));
    if (!path) return { ok: false, output: "readFile: missing path" };
    if (isUnsupportedFilePath(path)) return { ok: false, output: "File type not supported" };
    try {
      const text = await this.vfs.readText(path);
      await this.markFileObserved(path);
      return { ok: true, output: text };
    } catch {
      return { ok: false, output: `readFile: not found: ${path}` };
    }
  }

  private async toolWriteFile(args: unknown[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const path = normalizeToolPath(argString(args, 0, "path"));
    const content = argString(args, 1, "content");
    if (!path) return { ok: false, output: "writeFile: missing path" };
    if (content === undefined) return { ok: false, output: "writeFile: missing content" };
    const guard = await this.requireFreshReadBeforeExistingWrite(path, "writeFile");
    if (guard) return guard;
    await this.vfs.writeFile(path, content, "text/plain");
    if (onWorkspaceChange) await notifyWorkspaceChange(onWorkspaceChange, { type: "write", path, text: content });
    else syncOpenDocument(openByPath.get(path), content);
    return { ok: true, output: `Wrote ${path}` };
  }

  private async toolEditFile(args: unknown[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const path = normalizeToolPath(argString(args, 0, "path"));
    const oldString = argString(args, 1, "oldString");
    const newString = argString(args, 2, "newString") ?? "";
    if (!path || oldString === undefined) return { ok: false, output: "editFile: usage editFile(path, oldString, newString)" };
    const guard = await this.requireFreshReadBeforeExistingWrite(path, "editFile");
    if (guard) return guard;
    let content: string;
    try {
      content = await this.vfs.readText(path);
    } catch {
      return { ok: false, output: `editFile: not found: ${path}` };
    }
    if (!oldString) return { ok: false, output: "editFile: oldString must not be empty" };
    const first = content.indexOf(oldString);
    if (first < 0) return { ok: false, output: "editFile: oldString not found" };
    if (content.indexOf(oldString, first + oldString.length) >= 0) return { ok: false, output: "editFile: oldString is not unique" };
    const updated = content.slice(0, first) + newString + content.slice(first + oldString.length);
    await this.vfs.writeFile(path, updated, "text/plain");
    if (onWorkspaceChange) await notifyWorkspaceChange(onWorkspaceChange, { type: "write", path, text: updated });
    else syncOpenDocument(openByPath.get(path), updated);
    return { ok: true, output: `Edited ${path}` };
  }

  private async requireFreshReadBeforeExistingWrite(path: string, toolName: string): Promise<ToolResult | null> {
    const node = await this.vfs.stat(path);
    if (!node) return null;
    if (node.kind !== "file") return { ok: false, output: `${toolName}: not a file: ${path}` };
    if (this.readVersions.get(path) === observedFileVersion(node)) return null;
    return { ok: false, output: `${toolName}: call readFile first before modifying existing file: ${path}` };
  }

  private async markFileObserved(path: string): Promise<void> {
    const node = await this.vfs.stat(path);
    if (node?.kind === "file") this.readVersions.set(path, observedFileVersion(node));
    else this.readVersions.delete(path);
  }

  private async toolGrep(args: unknown[]): Promise<ToolResult> {
    const pattern = argString(args, 0, "pattern");
    if (!pattern) return { ok: false, output: "grep: missing pattern" };
    return this.grepIn("/", pattern);
  }

  private async toolGrepFile(args: unknown[]): Promise<ToolResult> {
    let pattern = argString(args, 0, "pattern");
    let path = argString(args, 1, "path");
    if (pattern?.startsWith("/") && path && !path.startsWith("/")) [pattern, path] = [path, pattern];
    if (!pattern || !path) return { ok: false, output: "grepFile: usage grepFile(pattern, path)" };
    return this.grepIn(path, pattern);
  }

  private async grepIn(path: string, pattern: string): Promise<ToolResult> {
    let regex: RegExp;
    try {
      regex = new RegExp(pattern);
    } catch (error) {
      return { ok: false, output: `grep: invalid regex: ${error instanceof Error ? error.message : String(error)}` };
    }
    const matcher = makeGrepMatcher(pattern, regex);
    const matches: string[] = [];
    const stats = { skippedLarge: 0, skippedBinary: 0 };
    const root = normalizeToolPath(path) || "/";
    const node = await this.vfs.stat(root);
    if (!node) return { ok: false, output: `grep: not found: ${root}` };
    if (node.kind === "file") {
      await this.grepFilePath(root, node, matcher, matches, stats);
    } else {
      const files = await this.vfs.listAllFiles();
      for (const file of files) {
        if (matches.length >= GREP_MAX_MATCHES) break;
        if (root !== "/" && !isSameOrDescendantPath(file.path, root)) continue;
        await this.grepFilePath(file.path, file, matcher, matches, stats);
      }
    }

    const notes: string[] = [];
    if (matches.length >= GREP_MAX_MATCHES) notes.push(`[grep truncated at ${GREP_MAX_MATCHES} matches]`);
    if (stats.skippedLarge > 0) notes.push(`[grep skipped ${stats.skippedLarge} file${stats.skippedLarge === 1 ? "" : "s"} larger than ${GREP_MAX_FILE_BYTES / (1024 * 1024)} MiB]`);
    if (stats.skippedBinary > 0) notes.push(`[grep skipped ${stats.skippedBinary} unsupported or binary file${stats.skippedBinary === 1 ? "" : "s"}]`);
    return { ok: true, output: [matches.length ? matches.join("\n") : "(no matches)", ...notes].join("\n") };
  }

  private async grepFilePath(path: string, node: VfsNode, matcher: GrepMatcher, matches: string[], stats: { skippedLarge: number; skippedBinary: number }): Promise<void> {
    if (matches.length >= GREP_MAX_MATCHES || isHiddenSearchPath(path)) return;
    if (isUnsupportedFilePath(path) || node.encoding === "binary") {
      stats.skippedBinary++;
      return;
    }
    if (node.size > GREP_MAX_FILE_BYTES) {
      stats.skippedLarge++;
      return;
    }
    let text = "";
    try {
      text = await this.vfs.readText(path);
    } catch {
      return;
    }
    const lines = text.split("\n");
    for (let i = 0; i < lines.length && matches.length < GREP_MAX_MATCHES; i++) {
      if (!matcher.test(lines[i]!)) continue;
      const line = lines[i]!.length > 200 ? `${lines[i]!.slice(0, 200)}...` : lines[i]!;
      matches.push(`${path}:${i + 1}: ${line}`);
    }
  }

  private async toolBash(args: unknown[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const command = argString(args, 0, "command");
    if (!command) return { ok: false, output: "bash: missing command" };
    if (/[|;><&`]|\$\(/.test(command)) return { ok: false, output: "bash: shell operators are not supported in the browser; use writeFile(path, content) to create or populate files" };
    const argv = tokenizeShell(command);
    if (argv.length === 0) return { ok: true, output: "" };
    const cmd = argv[0]!;
    if (cmd === "pwd") return { ok: true, output: "/\n" };
    if (cmd === "ls") return this.bashLs(argv);
    if (cmd === "cat") return this.bashCat(argv);
    if (cmd === "mkdir") return this.bashMkdir(argv, onWorkspaceChange);
    if (cmd === "rmdir") return this.bashRmdir(argv, onWorkspaceChange);
    if (cmd === "rm") return this.bashRm(argv, openByPath, onWorkspaceChange);
    if (cmd === "cp") return this.bashCp(argv, openByPath, onWorkspaceChange);
    if (cmd === "mv") return this.bashMv(argv, openByPath, onWorkspaceChange);
    if (cmd === "touch") return this.bashTouch(argv, openByPath, onWorkspaceChange);
    if (cmd === "echo") return { ok: true, output: `${argv.slice(1).join(" ")}\n` };
    return { ok: false, output: `bash: unsupported browser command: ${cmd}` };
  }

  private async bashLs(argv: string[]): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set(["-R"]));
    if (parsed.error) return { ok: false, output: `ls: ${parsed.error}` };
    if (parsed.operands.length > 1) return { ok: false, output: "ls: usage ls [-R] [path]" };
    const target = normalizeToolPath(parsed.operands[0] ?? "/") || "/";
    const node = await this.vfs.stat(target);
    if (!node) return { ok: false, output: `ls: ${target}: No such file or directory` };
    if (node.kind === "file") return { ok: true, output: `${basename(target)}\n` };
    if (parsed.flags.has("-R")) return { ok: true, output: await this.recursiveLs(target) };
    const rows = await this.visibleDirRows(target);
    return { ok: true, output: rows.map(formatLsNode).join("\n") + (rows.length ? "\n" : "") };
  }

  private async bashCat(argv: string[]): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set());
    if (parsed.error) return { ok: false, output: `cat: ${parsed.error}` };
    const paths = parsed.operands.map(normalizeToolPath).filter((path): path is string => Boolean(path));
    if (paths.length === 0) return { ok: false, output: "cat: missing file" };
    const out: string[] = [];
    for (const path of paths) {
      try {
        out.push(await this.vfs.readText(path));
      } catch {
        return { ok: false, output: `cat: ${path}: No such file` };
      }
      await this.markFileObserved(path);
    }
    return { ok: true, output: out.join("") };
  }

  private async bashMkdir(argv: string[], onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set(["-p"]));
    if (parsed.error) return { ok: false, output: `mkdir: ${parsed.error}` };
    const dirs = parsed.operands;
    if (dirs.length === 0) return { ok: false, output: "mkdir: missing operand" };
    let created = 0;
    for (const dir of dirs) {
      const path = normalizeToolPath(dir) || "/";
      if (path === "/") continue;
      await this.vfs.mkdir(path);
      await notifyWorkspaceChange(onWorkspaceChange, { type: "mkdir", path });
      created++;
    }
    return { ok: true, output: `Created ${created} director${created === 1 ? "y" : "ies"}` };
  }

  private async bashRmdir(argv: string[], onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set());
    if (parsed.error) return { ok: false, output: `rmdir: ${parsed.error}` };
    const dirs = parsed.operands;
    if (dirs.length === 0) return { ok: false, output: "rmdir: missing operand" };
    let removed = 0;
    for (const dir of dirs) {
      const path = normalizeToolPath(dir) || "/";
      if (path === "/") return { ok: false, output: "rmdir: refusing to remove /" };
      try {
        await this.vfs.remove(path, { recursive: false });
        await notifyWorkspaceChange(onWorkspaceChange, { type: "remove", path, recursive: false });
        removed++;
      } catch (error) {
        return { ok: false, output: `rmdir: ${error instanceof Error ? error.message : String(error)}` };
      }
    }
    return { ok: true, output: `Removed ${removed} director${removed === 1 ? "y" : "ies"}` };
  }

  private async bashRm(argv: string[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set(["-r", "-R", "-f", "-rf", "-fr", "-Rf", "-fR"]));
    if (parsed.error) return { ok: false, output: `rm: ${parsed.error}` };
    const recursive = parsed.flags.has("-r") || parsed.flags.has("-R") || parsed.flags.has("-rf") || parsed.flags.has("-fr") || parsed.flags.has("-Rf") || parsed.flags.has("-fR");
    const force = parsed.flags.has("-f") || parsed.flags.has("-rf") || parsed.flags.has("-fr") || parsed.flags.has("-Rf") || parsed.flags.has("-fR");
    const targets = parsed.operands;
    if (targets.length === 0) return { ok: false, output: "rm: missing operand" };
    const expanded = await this.expandBashPathPatterns(targets);
    if (expanded.length === 0) {
      return { ok: force, output: force ? "rm: removed 0 paths (no matches)" : `rm: cannot remove '${targets[0]}': No such file or directory` };
    }
    let removed = 0;
    for (const path of expanded) {
      if (path === "/") return { ok: false, output: "rm: refusing to remove /" };
      const node = await this.vfs.stat(path);
      if (!node) {
        if (force) continue;
        return { ok: false, output: `rm: cannot remove '${path}': No such file or directory` };
      }
      try {
        await this.vfs.remove(path, { recursive });
      } catch (error) {
        return { ok: false, output: `rm: ${error instanceof Error ? error.message : String(error)}` };
      }
      deleteOpenPathsUnder(openByPath, path, recursive);
      await notifyWorkspaceChange(onWorkspaceChange, { type: "remove", path, recursive });
      removed++;
    }
    return { ok: true, output: `Removed ${removed} path${removed === 1 ? "" : "s"}` };
  }

  private async bashCp(argv: string[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set());
    if (parsed.error) return { ok: false, output: `cp: ${parsed.error}` };
    const [sourceArg, destArg] = parsed.operands;
    const source = normalizeToolPath(sourceArg);
    const dest = normalizeToolPath(destArg);
    if (!source || !dest) return { ok: false, output: "cp: usage cp source dest" };
    const node = await this.vfs.stat(source);
    if (!node || node.kind !== "file") return { ok: false, output: `cp: not a file: ${source}` };
    const guard = await this.requireFreshReadBeforeExistingWrite(dest, "cp");
    if (guard) return guard;
    const data = await this.vfs.readFile(source);
    await this.vfs.writeFile(dest, data, node.mime ?? "text/plain");
    const text = await readWorkspaceTextIfSupported(this.vfs, dest);
    if (onWorkspaceChange) await notifyWorkspaceChange(onWorkspaceChange, { type: "write", path: dest, text });
    else if (text !== undefined) syncOpenDocument(openByPath.get(dest), text);
    return { ok: true, output: `Copied ${source} to ${dest}` };
  }

  private async bashMv(argv: string[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set());
    if (parsed.error) return { ok: false, output: `mv: ${parsed.error}` };
    const [sourceArg, destArg] = parsed.operands;
    const source = normalizeToolPath(sourceArg);
    const dest = normalizeToolPath(destArg);
    if (!source || !dest) return { ok: false, output: "mv: usage mv source dest" };
    const remappedOpenDocs = openPathRemaps(openByPath, source, dest);
    await this.vfs.rename(source, dest);
    await notifyWorkspaceChange(onWorkspaceChange, { type: "rename", oldPath: source, newPath: dest });
    for (const item of remappedOpenDocs) {
      openByPath.delete(item.oldPath);
      if (!onWorkspaceChange) item.doc.path = item.newPath;
      openByPath.set(item.newPath, item.doc);
    }
    return { ok: true, output: `Moved ${source} to ${dest}` };
  }

  private async bashTouch(argv: string[], openByPath: Map<string, TextDocument>, onWorkspaceChange?: AiWorkspaceChangeHandler | undefined): Promise<ToolResult> {
    const parsed = parseBashFlags(argv.slice(1), new Set());
    if (parsed.error) return { ok: false, output: `touch: ${parsed.error}` };
    const targets = parsed.operands;
    if (targets.length === 0) return { ok: false, output: "touch: missing file operand" };
    for (const target of targets) {
      const path = normalizeToolPath(target) || "";
      if (!path) continue;
      const existing = await this.vfs.stat(path);
      if (existing?.kind === "file") {
        const guard = await this.requireFreshReadBeforeExistingWrite(path, "touch");
        if (guard) return guard;
      }
      const text = existing?.kind === "file" ? await this.vfs.readText(path) : "";
      await this.vfs.writeFile(path, text, "text/plain");
      if (onWorkspaceChange) await notifyWorkspaceChange(onWorkspaceChange, { type: "write", path, text });
      else syncOpenDocument(openByPath.get(path), text);
    }
    return { ok: true, output: `Touched ${targets.length} file${targets.length === 1 ? "" : "s"}` };
  }

  private async recursiveLs(path: string): Promise<string> {
    const rows = await this.visibleDirRows(path);
    const out = [`${path}:`, ...rows.map(formatLsNode)];
    for (const row of rows.filter((item) => item.kind === "dir")) {
      out.push("", await this.recursiveLs(row.path));
    }
    return `${out.join("\n")}\n`;
  }

  private async visibleDirRows(path: string): Promise<VfsNode[]> {
    return (await this.vfs.listDir(path)).filter((child) => child.path !== normalizePath(path) && !child.name.startsWith("."));
  }

  private async expandBashPathPatterns(patterns: string[]): Promise<string[]> {
    const expanded: string[] = [];
    for (const pattern of patterns) {
      if (hasShellGlob(pattern)) expanded.push(...await expandSimpleGlob(this.vfs, pattern));
      else expanded.push(normalizeToolPath(pattern));
    }
    return uniquePaths(expanded.filter(Boolean));
  }
}

async function notifyWorkspaceChange(handler: AiWorkspaceChangeHandler | undefined, change: AiWorkspaceChange): Promise<void> {
  if (!handler) return;
  await handler(change);
}

type ParsedBashArgs = { flags: Set<string>; operands: string[]; error?: string | undefined };

function parseBashFlags(args: string[], allowedFlags: Set<string>): ParsedBashArgs {
  const flags = new Set<string>();
  const operands: string[] = [];
  let parsingFlags = true;
  for (const arg of args) {
    if (parsingFlags && arg === "--") {
      parsingFlags = false;
      continue;
    }
    if (parsingFlags && arg.startsWith("-") && arg !== "-") {
      if (!allowedFlags.has(arg)) return { flags, operands, error: `unsupported browser flag: ${arg}` };
      flags.add(arg);
      continue;
    }
    parsingFlags = false;
    operands.push(arg);
  }
  return { flags, operands };
}

function formatLsNode(node: VfsNode): string {
  return `${node.name}${node.kind === "dir" ? "/" : ""}`;
}

function hasShellGlob(pattern: string): boolean {
  return /[*?[]/.test(pattern);
}

async function expandSimpleGlob(vfs: Vfs, pattern: string): Promise<string[]> {
  const normalized = normalizeToolPath(pattern);
  const parent = dirname(normalized);
  const namePattern = basename(normalized);
  if (hasShellGlob(parent)) return [];
  const parentNode = await vfs.stat(parent);
  if (!parentNode || parentNode.kind !== "dir") return [];
  const matcher = globMatcher(namePattern);
  const includeHidden = namePattern.startsWith(".");
  const rows = await vfs.listDir(parent);
  return rows
    .filter((node) => node.path !== parent && (includeHidden || !node.name.startsWith(".")) && matcher.test(node.name))
    .map((node) => node.path)
    .sort((a, b) => a.localeCompare(b));
}

function globMatcher(pattern: string): RegExp {
  let source = "^";
  for (const char of pattern) {
    if (char === "*") source += ".*";
    else if (char === "?") source += ".";
    else source += char.replace(/[\\^$+?.()|{}[\]]/g, "\\$&");
  }
  source += "$";
  return new RegExp(source);
}

function uniquePaths(paths: string[]): string[] {
  return [...new Set(paths.map(normalizePath))].sort((a, b) => b.length - a.length || a.localeCompare(b));
}

async function readWorkspaceTextIfSupported(vfs: Vfs, path: string): Promise<string | undefined> {
  if (isUnsupportedFilePath(path)) return undefined;
  try {
    return await vfs.readText(path);
  } catch {
    return undefined;
  }
}

function openPathRemaps(openByPath: Map<string, TextDocument>, oldPath: string, newPath: string): Array<{ oldPath: string; newPath: string; doc: TextDocument }> {
  const oldNormalized = normalizePath(oldPath);
  const newNormalized = normalizePath(newPath);
  const result: Array<{ oldPath: string; newPath: string; doc: TextDocument }> = [];
  for (const [path, doc] of openByPath) {
    if (!isSameOrDescendantPath(path, oldNormalized)) continue;
    result.push({
      oldPath: path,
      newPath: path === oldNormalized ? newNormalized : joinPath(newNormalized, path.slice(oldNormalized.length + 1)),
      doc
    });
  }
  return result;
}

function deleteOpenPathsUnder(openByPath: Map<string, TextDocument>, path: string, recursive: boolean): void {
  const normalized = normalizePath(path);
  for (const key of [...openByPath.keys()]) {
    if (key === normalized || (recursive && isSameOrDescendantPath(key, normalized))) openByPath.delete(key);
  }
}

function isSameOrDescendantPath(path: string, parent: string): boolean {
  const normalizedPath = normalizePath(path);
  const normalizedParent = normalizePath(parent);
  return normalizedPath === normalizedParent || (normalizedParent !== "/" && normalizedPath.startsWith(`${normalizedParent}/`));
}

function isHiddenSearchPath(path: string): boolean {
  return normalizePath(path).split("/").some((segment) => segment.startsWith("."));
}

function makeGrepMatcher(pattern: string, regex: RegExp): GrepMatcher {
  if (!/[\\^$.*+?()[\]{}|]/.test(pattern)) {
    return { test: (line) => line.includes(pattern) };
  }
  return {
    test: (line) => {
      regex.lastIndex = 0;
      return regex.test(line);
    }
  };
}

function observedFileVersion(node: VfsNode): string {
  return `${node.id}:${node.contentId ?? ""}:${node.size}:${node.mtime}`;
}

function normalizeAiEndpointConfig(value: unknown): AiEndpointConfig {
  const raw = typeof value === "object" && value ? value as Partial<AiEndpointConfig> : {};
  const maxContextTokens = numericSetting(raw.maxContextTokens);
  return {
    apiBaseUrl: typeof raw.apiBaseUrl === "string" ? raw.apiBaseUrl : DEFAULT_AI_ENDPOINT_CONFIG.apiBaseUrl,
    apiKey: typeof raw.apiKey === "string" ? raw.apiKey : "",
    model: typeof raw.model === "string" ? raw.model : "",
    temperature: Number.isFinite(raw.temperature) ? Number(raw.temperature) : DEFAULT_AI_ENDPOINT_CONFIG.temperature,
    maxContextTokens: Number.isFinite(maxContextTokens) ? Math.max(0, Math.trunc(maxContextTokens)) : 0
  };
}

function withResolvedApiBaseUrl(config: AiEndpointConfig): AiEndpointConfig {
  return { ...config, apiBaseUrl: resolvedApiBaseUrl(config) };
}

function resolvedApiBaseUrl(config: Pick<AiEndpointConfig, "apiBaseUrl">): string {
  return normalizeBaseUrl(config.apiBaseUrl);
}

function normalizeRuntimeSettings(value: Partial<AiRuntimeSettings>): AiRuntimeSettings {
  return {
    maxToolCallsPerTurn: Number.isFinite(value.maxToolCallsPerTurn) ? Math.max(1, Math.trunc(Number(value.maxToolCallsPerTurn))) : DEFAULT_AI_RUNTIME_SETTINGS.maxToolCallsPerTurn,
    detectDuplicateToolCalls: typeof value.detectDuplicateToolCalls === "boolean" ? value.detectDuplicateToolCalls : DEFAULT_AI_RUNTIME_SETTINGS.detectDuplicateToolCalls,
    toolCallFormat: value.toolCallFormat === "harmony" || value.toolCallFormat === "none" ? value.toolCallFormat : "tag",
    thinkingFormat: "auto",
    compactFreePercent: Number.isFinite(value.compactFreePercent) ? Math.max(1, Math.min(95, Math.trunc(Number(value.compactFreePercent)))) : DEFAULT_AI_RUNTIME_SETTINGS.compactFreePercent
  };
}

function normalizeBaseUrl(raw: string): string {
  let url = String(raw || "").trim() || DEFAULT_AI_ENDPOINT_CONFIG.apiBaseUrl;
  if (!/^https?:\/\//i.test(url)) url = `http://${url}`;
  url = url.replace(/\/+$/, "");
  try {
    const parsed = new URL(url);
    if (!parsed.port && shouldUseLmStudioDefaultPort(parsed.hostname)) parsed.port = "1234";
    url = parsed.toString().replace(/\/+$/, "");
  } catch {
    // Fall back to the simpler path normalization below.
  }
  if (!/\/(?:api\/)?v\d+$/i.test(url)) url += "/v1";
  return url;
}

function shouldUseLmStudioDefaultPort(hostname: string): boolean {
  const host = hostname.toLowerCase();
  return host === "localhost" || host === "::1" || host.endsWith(".local") || isPrivateIpv4(host);
}

function isPrivateIpv4(host: string): boolean {
  const parts = host.split(".");
  if (parts.length !== 4) return false;
  const octets = parts.map((part) => Number(part));
  if (octets.some((octet) => !Number.isInteger(octet) || octet < 0 || octet > 255)) return false;
  const [a, b] = octets as [number, number, number, number];
  return a === 10
    || a === 127
    || (a === 169 && b === 254)
    || (a === 172 && b >= 16 && b <= 31)
    || (a === 192 && b === 168);
}

function numericSetting(value: unknown): number {
  if (Number.isFinite(value)) return Number(value);
  if (typeof value === "string" && value.trim()) return Number(value.trim());
  return Number.NaN;
}

function authHeaders(config: AiEndpointConfig): Record<string, string> {
  return config.apiKey ? { Authorization: `Bearer ${config.apiKey}` } : {};
}

function readJsonLocalStorage(key: string): unknown {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

async function responseErrorDetail(response: Response): Promise<string> {
  try {
    const text = await response.text();
    return text.trim().replace(/\s+/g, " ").slice(0, 240);
  } catch {
    return "";
  }
}

async function fetchWithTimeout(input: RequestInfo | URL, init: RequestInit, timeoutMs: number): Promise<Response> {
  const controller = new AbortController();
  let timeout = 0;
  const timeoutPromise = new Promise<Response>((_, reject) => {
    timeout = window.setTimeout(() => {
      controller.abort();
      reject(new Error(`Timed out after ${timeoutMs}ms`));
    }, timeoutMs);
  });
  try {
    return await Promise.race([fetch(input, { ...init, signal: controller.signal }), timeoutPromise]);
  } finally {
    window.clearTimeout(timeout);
  }
}

async function mergeLmStudioNativeModels(config: AiEndpointConfig, headers: Record<string, string>, models: AiModelInfo[]): Promise<AiModelInfo[]> {
  const result = [...models];
  const base = config.apiBaseUrl.replace(/\/(?:api\/)?v\d+$/i, "");
  for (const nativeBase of [`${base}/api/v1`, `${base}/api/v0`]) {
    try {
      const response = await fetchWithTimeout(`${nativeBase}/models`, { headers }, LM_STUDIO_NATIVE_PROBE_TIMEOUT_MS);
      if (!response.ok) continue;
      const data = await response.json() as { data?: unknown[] };
      for (const raw of Array.isArray(data.data) ? data.data : []) {
        const model = modelInfoFromUnknown(raw);
        if (!model) continue;
        const existing = result.find((item) => item.id === model.id);
        if (existing) existing.contextLength ||= model.contextLength;
        else result.push(model);
      }
    } catch {
      // Fall through to the next LM Studio native endpoint.
    }
  }
  return sortModelInfo(result);
}

function shouldProbeLmStudioNativeModels(apiBaseUrl: string): boolean {
  try {
    const host = new URL(apiBaseUrl).hostname.toLowerCase();
    return host === "localhost" || host === "127.0.0.1" || host === "::1";
  } catch {
    return false;
  }
}

function sortModelInfo(models: AiModelInfo[]): AiModelInfo[] {
  return [...models].sort((a, b) => a.id.localeCompare(b.id));
}

function modelInfoFromUnknown(raw: unknown): AiModelInfo | null {
  if (!raw || typeof raw !== "object") return null;
  const obj = raw as Record<string, unknown>;
  const id = typeof obj.id === "string" ? obj.id : "";
  if (!id) return null;
  return { id, contextLength: contextLengthFromObject(obj) };
}

function contextLengthFromObject(obj: Record<string, unknown>): number {
  for (const key of ["loaded_context_length", "max_context_length", "context_length", "max_model_len", "context_window", "n_ctx"]) {
    const value = obj[key];
    if (Number.isFinite(value)) return Math.max(0, Math.trunc(Number(value)));
  }
  return 0;
}

function bestKnownContextLength(model: string): number {
  return builtinContextLength(model);
}

function builtinContextLength(model: string): number {
  if (!model) return 0;
  if (/gpt-4o|gpt-5|gpt-4\.1/i.test(model)) return 128000;
  if (/llama-3\.1|qwen|deepseek|mistral-large/i.test(model)) return 131072;
  return 0;
}

function tokenCounterKey(config: AiEndpointConfig, runtime: AiRuntimeSettings): string {
  const nativeTools = shouldUseNativeTools(config, runtime);
  return [
    resolvedApiBaseUrl(config),
    config.model,
    runtime.toolCallFormat,
    runtime.thinkingFormat,
    nativeTools ? "native-tools" : "text-tools",
    composeAiSystemPrompt(runtime.toolCallFormat, nativeTools)
  ].join("\n");
}

function composeAiSystemPrompt(format: AiToolCallFormat, nativeTools = false): string {
  const systemPrompt = loadAiSystemPrompt().trimEnd();
  let toolPrompt = "";
  if (nativeTools) toolPrompt = DEFAULT_NATIVE_TOOL_PROMPT;
  else if (format === "tag") toolPrompt = loadAiTagToolPrompt().trim();
  else if (format === "harmony") toolPrompt = loadAiHarmonyToolPrompt().trim();
  return toolPrompt ? `${systemPrompt}\n\n${toolPrompt}` : systemPrompt;
}

function shouldUseNativeTools(config: Pick<AiEndpointConfig, "model">, runtime: AiRuntimeSettings): boolean {
  if (runtime.toolCallFormat === "none") return false;
  const model = config.model.trim().toLowerCase();
  if (!model) return false;
  return /(?:^|[\/:_-])(?:gpt-oss|gpt-[45][a-z0-9._-]*|o[1345][a-z0-9._-]*)(?:$|[\/:_-])/i.test(model);
}

function sanitizeSystemPrompt(prompt: string): string {
  const trimmed = prompt.trim();
  const toolBlockStart = legacyToolBlockStart(trimmed);
  if (toolBlockStart < 0) return trimmed;
  const cleaned = trimmed.slice(0, toolBlockStart).trim();
  return cleaned || DEFAULT_SYSTEM_PROMPT;
}

function sanitizeToolPrompt(prompt: string): string {
  const removedToolLine = new RegExp(`^\\s*-\\s*${REMOVED_FILE_GREP_TOOL}\\b`);
  return prompt
    .split("\n")
    .filter((line) => !removedToolLine.test(line))
    .join("\n")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}

function legacyToolBlockStart(prompt: string): number {
  const markers = [
    "Available tools:",
    "Tag tool-call format:",
    "Harmony-style tool-call format:",
    "Use only this tag tool-call format",
    "Use only this harmony-style tool-call format"
  ];
  let start = -1;
  for (const marker of markers) {
    const index = prompt.indexOf(marker);
    if (index >= 0 && (start < 0 || index < start)) start = index;
  }
  return start;
}

function isCompactedSummaryMessage(msg: ChatMessage): boolean {
  return msg.role === "system" && msg.text.startsWith(COMPACTED_SUMMARY_HEADER);
}

function isHiddenToolRepairMessage(msg: ChatMessage): boolean {
  return msg.internal === true
    && msg.role === "user"
    && msg.text.startsWith("Your previous response put a tool call inside hidden reasoning/thinking.");
}

function usageFromApi(usage: { total_tokens?: number; prompt_tokens?: number; completion_tokens?: number } | undefined): ApiUsage {
  return {
    totalTokens: Math.max(0, Number(usage?.total_tokens ?? 0)),
    promptTokens: Math.max(0, Number(usage?.prompt_tokens ?? 0)),
    completionTokens: Math.max(0, Number(usage?.completion_tokens ?? 0))
  };
}

function completionResultFromJson(data: {
  choices?: Array<{ message?: { content?: string | null; reasoning_content?: string; reasoning?: string; tool_calls?: unknown[] } }>;
  usage?: { total_tokens?: number; prompt_tokens?: number; completion_tokens?: number };
}): CompletionResult {
  const message = data.choices?.[0]?.message;
  const usage = usageFromApi(data.usage);
  return {
    text: message?.content ?? "",
    thinking: providerReasoningText(message),
    toolCalls: parseNativeToolCalls(message?.tool_calls),
    usageTotal: usage.totalTokens,
    usagePrompt: usage.promptTokens,
    usageCompletion: usage.completionTokens
  };
}

function completionResultHasUsage(result: CompletionResult): boolean {
  return result.usageTotal > 0 || result.usagePrompt > 0 || result.usageCompletion > 0;
}

function sseDataPayload(event: string): string {
  const lines = event.replace(/\r\n/g, "\n").split("\n");
  const data: string[] = [];
  for (const line of lines) {
    if (line.startsWith("data:")) data.push(line.slice(5).trimStart());
  }
  return data.join("\n").trim();
}

function parseJsonObject(text: string): Record<string, unknown> | null {
  try {
    const parsed = JSON.parse(text);
    return parsed && typeof parsed === "object" ? parsed as Record<string, unknown> : null;
  } catch {
    return null;
  }
}

function providerReasoningText(message: { reasoning_content?: string; reasoning?: string } | undefined): string {
  if (typeof message?.reasoning_content === "string") return message.reasoning_content;
  return typeof message?.reasoning === "string" ? message.reasoning : "";
}

function streamDeltaText(delta: object, key: "content" | "reasoning_content" | "reasoning"): string {
  const value = (delta as Record<string, unknown>)[key];
  return typeof value === "string" ? value : "";
}

function emptyUsage(): ApiUsage {
  return { totalTokens: 0, promptTokens: 0, completionTokens: 0 };
}

function basePromptTokensFromProbeUsage(usage: ApiUsage): number {
  if (usage.totalTokens > 0) {
    const completion = usage.completionTokens || PROBE_COMPLETION_TOKEN_COUNT;
    return Math.max(0, usage.totalTokens - completion - PROBE_USER_TOKEN_COUNT);
  }
  if (usage.promptTokens > 0) return Math.max(0, usage.promptTokens - PROBE_USER_TOKEN_COUNT);
  return 0;
}

function promptTokensFromRefreshUsage(usage: ApiUsage): number {
  if (usage.promptTokens > 0) return usage.promptTokens;
  if (usage.totalTokens > 0) return Math.max(0, usage.totalTokens - (usage.completionTokens || PROBE_COMPLETION_TOKEN_COUNT));
  return 0;
}

function parseNativeToolCalls(raw: unknown): ParsedToolCall[] {
  if (!Array.isArray(raw)) return [];
  const result: ParsedToolCall[] = [];
  for (const item of raw) {
    if (!item || typeof item !== "object") continue;
    const record = item as { id?: unknown; function?: { name?: unknown; arguments?: unknown } };
    const fn = record.function;
    const name = typeof fn?.name === "string" ? fn.name : "";
    if (!name) continue;
    const nativeArguments = nativeArgumentsText(fn?.arguments);
    const args = parseJsonArgs(nativeArguments);
    result.push({
      name,
      args,
      raw: nativeToolCallText(name, args),
      nativeId: typeof record.id === "string" && record.id ? record.id : uid("call"),
      nativeArguments
    });
  }
  return result;
}

function appendStreamToolCallDeltas(raw: unknown, parts: Map<number, StreamedToolCallPart>): void {
  if (!Array.isArray(raw)) return;
  for (const item of raw) {
    if (!item || typeof item !== "object") continue;
    const record = item as { id?: unknown; index?: unknown; function?: { name?: unknown; arguments?: unknown } };
    const index = typeof record.index === "number" && Number.isFinite(record.index) ? record.index : parts.size;
    const part = parts.get(index) ?? { id: "", name: "", argumentsText: "" };
    if (typeof record.id === "string" && record.id) part.id = record.id;
    if (typeof record.function?.name === "string") part.name += record.function.name;
    if (typeof record.function?.arguments === "string") part.argumentsText += record.function.arguments;
    parts.set(index, part);
  }
}

function parseStreamedToolCalls(parts: Map<number, StreamedToolCallPart>): ParsedToolCall[] {
  const result: ParsedToolCall[] = [];
  for (const [, part] of [...parts.entries()].sort((a, b) => a[0] - b[0])) {
    if (!part.name) continue;
    const args = parseJsonArgs(part.argumentsText || "{}");
    result.push({
      name: part.name,
      args,
      raw: nativeToolCallText(part.name, args),
      nativeId: part.id || uid("call"),
      nativeArguments: part.argumentsText || "{}"
    });
  }
  return result;
}

function nativeArgumentsText(value: unknown): string {
  if (typeof value === "string") return value || "{}";
  if (value === undefined || value === null) return "{}";
  try {
    return JSON.stringify(value);
  } catch {
    return "{}";
  }
}

function nativeToolCallText(name: string, args: unknown[]): string {
  return `<tool>${name}(${args.map((arg) => JSON.stringify(arg)).join(", ")})</tool>`;
}

function parseTextToolCalls(text: string, format: AiToolCallFormat): ParsedToolCall[] {
  if (format === "none") return [];
  return format === "harmony" ? parseHarmonyToolCalls(text) : parseTagToolCalls(text);
}

function thinkingSuggestsToolCall(text: string, format: AiToolCallFormat): boolean {
  if (!text.trim() || format === "none") return false;
  if (format === "tag") return /<tool\b|<\/tool>|(?:readFile|writeFile|editFile|grep|grepFile|bash|compact)\s*\(/i.test(text);
  return /(?:<\|channel\|>\s*)?commentary\s+to\s*=\s*(?:functions\.)?(?:readFile|writeFile|editFile|grep|grepFile|bash|compact)\b|<\|call\|>|<\|message\|>/i.test(text);
}

function hiddenToolCallRepairPrompt(format: AiToolCallFormat): string {
  if (format === "harmony") {
    return "Your previous response put a tool call inside hidden reasoning/thinking. Hidden reasoning is not executable. If you need a tool, resend only the executable harmony tool call in assistant content/commentary now. Do not explain, do not use markdown, and do not put the call in analysis/reasoning. The format is: <|channel|>commentary to=readFile <|message|>{\"path\":\"/README.md\"}<|call|>";
  }
  return "Your previous response put a tool call inside hidden reasoning/thinking. Hidden reasoning is not executable. If you need a tool, resend only the executable tag tool call in assistant content now. Do not explain, do not use markdown, and do not put the call in thinking. The format is: <tool>readFile(\"/README.md\")</tool>";
}

function parseTagToolCalls(text: string): ParsedToolCall[] {
  const calls: ParsedToolCall[] = [];
  const regex = /<tool>([\s\S]*?)<\/tool>/g;
  for (const match of text.matchAll(regex)) {
    const inner = match[1]?.trim() ?? "";
    const paren = inner.indexOf("(");
    const close = inner.lastIndexOf(")");
    if (paren <= 0 || close < paren) continue;
    const name = inner.slice(0, paren).trim();
    calls.push({ name, args: parseCallArgs(inner.slice(paren + 1, close)), raw: match[0] });
  }
  return calls;
}

function parseHarmonyToolCalls(text: string): ParsedToolCall[] {
  const calls: ParsedToolCall[] = [];
  const regex = /<\|channel\|>\s*commentary(?:\s+to\s*=\s*([^\s<|]+))?[\s\S]*?<\|message\|>([\s\S]*?)(?=<\|call\|>|<\|end\|>|<\|start\|>|<\|channel\|>|$)/g;
  for (const match of text.matchAll(regex)) {
    const name = (match[1] ?? "").replace(/^functions\./, "");
    if (!name) continue;
    const rawArgs = match[2]?.trim() ?? "{}";
    calls.push({ name, args: parseJsonArgs(rawArgs), raw: match[0] });
  }
  return calls;
}

function extractThinkingFromText(text: string, format: AiThinkingFormat): { text: string; thinking: string } {
  if (!text || format === "none") return { text, thinking: "" };
  if (format === "tag") return extractTagThinking(text);
  if (format === "harmony") return extractHarmonyThinking(text);
  const harmony = extractHarmonyThinking(text);
  const tagged = extractTagThinking(harmony.text);
  return {
    text: tagged.text,
    thinking: [harmony.thinking, tagged.thinking].filter(Boolean).join("\n\n")
  };
}

function extractTagThinking(text: string): { text: string; thinking: string } {
  const thinking: string[] = [];
  const stripped = text.replace(/<think(?:ing)?\b[^>]*>([\s\S]*?)<\/think(?:ing)?>/gi, (_match, body: string) => {
    const trimmed = String(body).trim();
    if (trimmed) thinking.push(trimmed);
    return "";
  });
  return { text: stripped.trim(), thinking: thinking.join("\n\n") };
}

function extractHarmonyThinking(text: string): { text: string; thinking: string } {
  const thinking: string[] = [];
  const final: string[] = [];
  let sawHarmonyMessage = false;
  const withoutAnalysisOrFinal = text.replace(/(?:<\|start\|>\s*assistant\s*)?<\|channel\|>\s*(analysis|final)\b[\s\S]*?<\|message\|>([\s\S]*?)(?:<\|end\|>|(?=<\|start\|>|<\|channel\|>|$))/gi, (_match, channel: string, body: string) => {
    sawHarmonyMessage = true;
    const trimmed = String(body).trim();
    if (trimmed) {
      if (String(channel).toLowerCase() === "analysis") thinking.push(trimmed);
      else final.push(trimmed);
    }
    return "";
  });
  if (final.length > 0) return { text: final.join("\n\n"), thinking: thinking.join("\n\n") };
  return {
    text: sawHarmonyMessage ? withoutAnalysisOrFinal.trim() : text,
    thinking: thinking.join("\n\n")
  };
}

function stripToolCallSyntax(text: string): string {
  return text
    .replace(/<tool>[\s\S]*?<\/tool>/g, "")
    .replace(/<\|channel\|>\s*commentary\s+to=[^\s<|]+[\s\S]*?<\|message\|>[\s\S]*?(?:<\|call\|>|<\|end\|>|$)/g, "")
    .trim();
}

function parseJsonArgs(raw: string): unknown[] {
  try {
    const parsed = JSON.parse(raw);
    if (Array.isArray(parsed)) return parsed;
    if (parsed && typeof parsed === "object") return [parsed];
    return [parsed];
  } catch {
    return [raw];
  }
}

function parseCallArgs(src: string): unknown[] {
  const args: unknown[] = [];
  let i = 0;
  const skipWs = () => { while (i < src.length && /\s/.test(src[i]!)) i++; };
  while (i < src.length) {
    skipWs();
    const quote = src[i];
    if (quote === "\"" || quote === "'" || quote === "`") {
      i++;
      let out = "";
      while (i < src.length && src[i] !== quote) {
        if (src[i] === "\\" && i + 1 < src.length) {
          const next = src[++i]!;
          out += next === "n" ? "\n" : next === "t" ? "\t" : next === "r" ? "\r" : next;
          i++;
        } else {
          out += src[i++]!;
        }
      }
      if (src[i] === quote) i++;
      args.push(out);
    } else {
      let token = "";
      while (i < src.length && src[i] !== ",") token += src[i++]!;
      const trimmed = token.trim();
      if (trimmed === "true") args.push(true);
      else if (trimmed === "false") args.push(false);
      else if (trimmed === "null") args.push(null);
      else if (/^-?\d+(?:\.\d+)?$/.test(trimmed)) args.push(Number(trimmed));
      else if (trimmed) args.push(trimmed);
    }
    skipWs();
    if (src[i] === ",") i++;
  }
  return args;
}

function formatToolResult(output: string, format: AiToolCallFormat): string {
  if (format === "harmony") return `<|channel|>commentary <|message|>${output}<|end|>`;
  if (format === "none") return `Tool result:\n${output}`;
  return `<result>${output}</result>`;
}

function formatEditorContext(context: ContextBundle): string {
  const fileTreePaths = uniqueStrings(context.fileTreePaths ?? []);
  const shownTree = fileTreePaths.slice(0, EDITOR_CONTEXT_MAX_TREE_ENTRIES);
  const openNames = uniqueStrings(context.openFileNames ?? context.openPaths.map((path) => path === "(untitled)" ? path : basename(path)));
  const selectedText = truncateContextText(context.selectedText.trim(), EDITOR_CONTEXT_MAX_SELECTED_TEXT_CHARS);
  const lines = [
    "<editor-context>",
    "Current editor state. File contents are not included unless explicitly selected. Use readFile before relying on or modifying existing file contents.",
    "",
    "File tree:",
    ...formatContextList(shownTree),
    ...(fileTreePaths.length > shownTree.length ? [`[file tree truncated: showing ${shownTree.length} of ${fileTreePaths.length} entries]`] : []),
    "",
    "Open files:",
    ...formatContextList(openNames),
    "",
    `Selected in file tree: ${context.selectedFileTreePath || "(none)"}`,
    `Active file: ${context.activePath || "(none)"}`
  ];
  if (selectedText) {
    lines.push("", `Active editor selection${context.activePath ? ` from ${context.activePath}` : ""}:`, selectedText);
  } else {
    lines.push("", "Active editor selection: (none)");
  }
  lines.push("</editor-context>");
  return lines.join("\n");
}

function formatContextList(items: string[]): string[] {
  return items.length ? items.map((item) => `- ${item}`) : ["- (none)"];
}

function uniqueStrings(items: string[]): string[] {
  const result: string[] = [];
  const seen = new Set<string>();
  for (const item of items) {
    const value = item.trim();
    if (!value || seen.has(value)) continue;
    seen.add(value);
    result.push(value);
  }
  return result;
}

function truncateContextText(text: string, maxChars: number): string {
  if (text.length <= maxChars) return text;
  return `${text.slice(0, maxChars)}\n[selection truncated: ${text.length - maxChars} more characters]`;
}

function argString(args: unknown[], index: number, key?: string): string | undefined {
  const direct = args[index];
  if (typeof direct === "string") return direct;
  if (typeof direct === "number" || typeof direct === "boolean") return String(direct);
  const first = args[0];
  if (key && first && typeof first === "object" && !Array.isArray(first)) {
    const value = (first as Record<string, unknown>)[key];
    if (typeof value === "string") return value;
    if (typeof value === "number" || typeof value === "boolean") return String(value);
  }
  return undefined;
}

function normalizeToolPath(path: string | undefined): string {
  if (!path) return "";
  return normalizePath(path.startsWith("/") ? path : `/${path}`);
}

function syncOpenDocument(doc: TextDocument | undefined, text: string): void {
  if (!doc || doc.readOnly) return;
  doc.selectAll();
  doc.replaceSelection(text, "agent");
  doc.markSaved();
}

function tokenizeShell(cmd: string): string[] {
  const tokens: string[] = [];
  let cur = "";
  let single = false;
  let dbl = false;
  for (let i = 0; i < cmd.length; i++) {
    const ch = cmd[i]!;
    if (ch === "'" && !dbl) {
      single = !single;
      continue;
    }
    if (ch === "\"" && !single) {
      dbl = !dbl;
      continue;
    }
    if (ch === "\\" && dbl && i + 1 < cmd.length) {
      cur += cmd[++i]!;
      continue;
    }
    if (/\s/.test(ch) && !single && !dbl) {
      if (cur) tokens.push(cur);
      cur = "";
    } else {
      cur += ch;
    }
  }
  if (cur) tokens.push(cur);
  return tokens;
}

function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4);
}

function estimateMessageTokens(text: string): number {
  return estimateTokens(text) + ESTIMATED_CHAT_MESSAGE_OVERHEAD_TOKENS;
}
